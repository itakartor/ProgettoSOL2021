#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <getopt.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/uio.h>

#include <sys/un.h>
#include <ctype.h>
#include <pthread.h>

#include "queue.h"
#include "util.h"
#include "Parser.h"

#define MAXBUFFER 1000
#define MAXSTRING 100
#define MAXPATH 1024
#define SOCKNAME "mysock"
long sockfd;

static int add_to_current_time(long sec, long nsec, struct timespec* res)
{
    // TODO: maybe check its result
    clock_gettime(CLOCK_REALTIME, res);
    res->tv_sec += sec;
    res->tv_nsec += nsec;

    return 0;
}

int set_timespec_from_msec(long msec, struct timespec* req) 
{
  if(msec < 0 || req == NULL)
  {
    errno = EINVAL;
    return -1;
  }

  req->tv_sec = msec / 1000;
  msec = msec % 1000;
  req->tv_nsec = msec * 1000;

  return 0;
}

int openConnection(const char* sockname, int msec, const struct timespec abstime) 
{
    if(sockname == NULL || msec < 0) //argomenti non validi
    { 
        errno = EINVAL;
        return -1;
    }

    struct sockaddr_un serv_addr;
    SYSCALL_EXIT("socket", sockfd, socket(AF_UNIX, SOCK_STREAM, 0), "socket", "");
    memset(&serv_addr, '0', sizeof(serv_addr));

    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path,SOCKNAME, strlen(SOCKNAME)+1);

    // setting waiting time
    struct timespec wait_time;
    // no need to check because msec > 0 and &wait_time != NULL
    set_timespec_from_msec(msec, &wait_time);

    // setting current time
    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);

    // trying to connect
    int err = -1;
    fprintf(stderr, "currtime %ld abstime %ld\n", curr_time.tv_sec, abstime.tv_sec);
    while( (err = connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) == -1
            && curr_time.tv_sec < abstime.tv_sec ){
        //debug("connect didn't succeed, trying again...\n");
        fprintf(stderr, "err4 %d\n", errno);

        if( nanosleep(&wait_time, NULL) == -1){
            sockfd = -1;
            return -1;
        }
        if( clock_gettime(CLOCK_REALTIME, &curr_time) == -1){
            sockfd = -1;
            return -1;
        }
    }

    if(err == -1) {
        //debug("Could not connect to server. :(\n");

        sockfd = -1;
        errno = ETIMEDOUT;
        return -1;
    }

    //debug("Connected! :D\n");

    //socket_path = sockname;
    fprintf(stderr, "mi sono connesso al server!\n");
    return 0;
}

int removeFile(const char* pathname) 
{
  int notused;
  char *buffer = NULL;
  char* towrite = malloc(sizeof(char) * (strlen(pathname) + 1)); //alloco la stringa da scrivere, che sarà del tipo "rfile"
  towrite[0] = 'c';
  for(int i = 1; i <= strlen(pathname); i++)
    towrite[i] = pathname[i - 1];
  fprintf(stderr, "sto scrivendo nel socket %s, nome file originale %s\n", towrite, pathname);
  int n = strlen(towrite) + 1; //terminatore

  SYSCALL_EXIT("writen", notused, writen(sockfd, &n, sizeof(int)), "write", "");
  SYSCALL_EXIT("writen", notused, writen(sockfd, towrite, n * sizeof(char)), "write", "");

  int res;
  SYSCALL_EXIT("readn", notused, readn(sockfd, &res, sizeof(int)), "read", "");
  if(res != 1) { perror("sa_success"); return -1; }
  fprintf(stderr, "File %s cancellato con successo dal server\n", pathname);
  return 0;
}

int EseguiComandoClient(NodoComando *tmp) 
{
  if(tmp->cmd == 'c')//gestisco il comando con c per eliminare il file dal server
  {
    fprintf(stderr, "file da rimuovere %s\n", tmp->name);
    removeFile(tmp->name);
    return 0;
  }

  int notused;
  char *buffer = NULL;
  if(tmp == NULL) return -1; //errore: tmp non può e non deve essere NULL. Abbiamo già controllato che q->len > 0
  char* towrite = malloc(sizeof(char) * (strlen(tmp->name) + 1)); //alloco la stringa da scrivere, che sarà del tipo "rfile"
  //parser semplice da passare alla coda delle richieste
  towrite[0] = tmp->cmd;
  
  for(int i = 1; i <= strlen(tmp->name); i++) //vado a prendere la stringa della richiesta che è stata suddivisa dal parser
    towrite[i] = tmp->name[i - 1];

  //fprintf(stderr, "sto scrivendo nel socket %s\n", towrite);
  int n = strlen(towrite) + 1; //+1 per il terminatore

  SYSCALL_EXIT("writen", notused, writen(sockfd, &n, sizeof(int)), "write", "");          //scrivo nel socket cosa ho estratto dalla coda
  SYSCALL_EXIT("writen", notused, writen(sockfd, towrite, n * sizeof(char)), "write", "");

  if(tmp->cmd == 'W')//gestione del comando W
  {
    buffer = realloc(buffer, n*sizeof(char));
    if (!buffer) { perror("realloc"); fprintf(stderr, "Memoria esaurita....\n"); }
    
    SYSCALL_EXIT("readn", notused, readn(sockfd, &n, sizeof(int)), "read", "");             //leggo una possibile risposta del server se il file che sto per inserire
    SYSCALL_EXIT("readn", notused, readn(sockfd, buffer, n * sizeof(char)), "read", "");    //è gia stato inserito "file ok"-> file da inserire
    buffer[n] = '\0';                                                                       
    if(strcmp(buffer, "file ok") == 0) //
    {
      //fprintf(stderr, "file ok\n");

      FILE * f = fopen (tmp->name, "rb"); //apro il file in lettura e scrittura    

      long length;
      char* bufferFile;
      
      if (f) 
      {//leggo la lunghezza/size del file e il suo contenuto
        fseek (f, 0, SEEK_END);
        length = ftell (f);
        fseek (f, 0, SEEK_SET);
        bufferFile = malloc(length); 
        if(bufferFile)
        {
          fread(bufferFile, 1, length, f); //lettura del file e compiatura nel buffer
        }
        fclose (f);
      }

      SYSCALL_EXIT("writen", notused, writen(sockfd, &length, sizeof(int)), "write", "");             //scrivo nel socket del server il contenuto del file
      SYSCALL_EXIT("writen", notused, writen(sockfd, bufferFile, length * sizeof(char)), "write", "");

      buffer = realloc(buffer, n*sizeof(char));
      if (!buffer) { perror("realloc"); fprintf(stderr, "Memoria esaurita....\n"); }


      SYSCALL_EXIT("readn", notused, readn(sockfd, &n, sizeof(int)), "read", "");                     //leggo la risposta del server dal socket 
      //fprintf(stderr, "e fin qui\n");
      SYSCALL_EXIT("readn", notused, readn(sockfd, buffer, n * sizeof(char)), "read", "");
      buffer[n] = '\0';
      printf("result: %s\n", buffer); //stampo a video cosa ricevo dal server
    }
    else 
    {
      fprintf(stderr, "file già esistente nel server\n");                                              //se il file esiste gia nel server
    }  
  }                                                      
}

void visitaRicorsiva(char* name, int *n, Queue **q) 
{ //alcuni file hanno problemi 
  //fprintf(stderr, "sto visitando %s\n", name);
  if(name == NULL)//se il path name non è corretto
    return;
  char buftmp[MAXPATH];
  if (getcwd(buftmp, MAXPATH)==NULL)//prendendo il pathname della directory attuale
  { perror("getcwd");  exit(EXIT_FAILURE); }
  
  if (chdir(name) == -1)//cambio la directory corrente
   { perror("chdir visitaRicorsiva"); fprintf(stderr, "errno %d visitando %s\n", errno, name);  exit(EXIT_FAILURE); }
 
  DIR *dir;
  struct dirent *entry;

  if (!(dir = opendir(name)))//se non si riesce ad aprire la dir
      return;

  while ((entry = readdir(dir)) != NULL && (*n != 0))//apro la dir e vedo n != 0 perchè in caso dovrei fermarmi
  {
    char path[1024];
    if (entry->d_type == DT_DIR)//vedo se il tipo della entry è una cartella
    {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || entry->d_name[0] == '.')
        continue;
        
        snprintf(path, sizeof(path), "%s/%s", name, entry->d_name);//va a scrivere nel path l'entry->d_name che sto visitando 
        
        visitaRicorsiva(path, n, q);//riparte la visita ricorsiva nella stessa dir
    } 
    else
    { 
      if (entry->d_type == DT_REG)//per ogni file regolare vado a decrementare n e a caricare il file se n>0 o n==-1 
      {
        if(*n > 0 || *n == -1) 
        {
          char buffer[1024];
          realpath(entry->d_name, buffer); //prendo il path assoluto del file
          //printf("%*s- %s, realpath %s\n", 0, "", entry->d_name, buffer);

          NodoComando *new = malloc(sizeof(NodoComando));//vado a gestire il -w attraverso la chiamata -W
          new->cmd = 'W';
          new->name = malloc(sizeof(char) * strlen(buffer));
          new->n = 0;
          strcpy(new->name, buffer);
          push(q, new);//bisogna ricontrollare il path perchè ha un problema latente 
          //fprintf(stderr, "HO APPENA SCRITTO %s, strlen %ld\n", new->name, strlen(new->name));
          //printQueuee(*q);
        }
        if(*n > 0)//se solo se n>0 lo decremento
          (*n)--;
      }
    }
  }
  closedir(dir);//chiudo la directory
  if (chdir(buftmp) == -1) { perror("chdir");  exit(EXIT_FAILURE); }
}

int main(int argc, char *argv[]) 
{
  Queue *q = parser(argv,argc); //coda delle operazioni
  struct timespec abstime;

  add_to_current_time(2, 0, &abstime);
  //primo parametro: tempo limite (in secondi)
  //secondo parametro: intervallo di tempo tra due connessioni (in millisecondi)

  int x = openConnection(SOCKNAME, 0, abstime); //da vedere se da errore
  fprintf(stderr, "sockfd: %ld, risultato openconnection %d\n", sockfd, x);


  while(q->len > 0) 
  { //finchè ci sono richieste che il parser ha visto
    NodoComando *tmp = pop(&q);
    if(tmp->cmd == 'w') 
    { //fa una richiesta speciale in modo ricorsivo sfruttando il comando W
      if(tmp->n == 0)
        tmp->n = -1;  //in caso che n sia zero vuol dire che devo leggere tutti i file della directory
      visitaRicorsiva(tmp->name, &(tmp->n), &q);//però metto n=-1 per evitare il caso in cui n si decrementa fino a 0
    } 
    else
      EseguiComandoClient(tmp);
  }
  close(sockfd);
  return 0;
}