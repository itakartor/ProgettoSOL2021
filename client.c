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
    fprintf(stderr, "mi sono connesso al server!\n");
    return 0;
}

int closeConnection(const char* sockname){
  if(sockname == NULL){
    errno = EINVAL;
    return -1;
  }
    // wrong socket name
  if(strcmp(sockname, SOCKNAME) != 0){
        // this socket is not connected
    errno = ENOTCONN;
    return -1;
  }

  if( close(sockfd) == -1 ){
    sockfd = -1;
    return -1;
  }

  fprintf(stderr, "Connessione chiusa\n");

  sockfd = -1;
  return 0;
}

int writeCMD(const char* pathname, char cmd) 
{//parte ricorrente per mandare il comando al server
  //manca gestione errore
  int notused;
  char *buffer = NULL;
  char* towrite = malloc(sizeof(char) * (strlen(pathname) + 2)); //alloco la stringa per la stringa comando da mandare al server con terminatore
  towrite[0] = cmd;
  for(int i = 1; i <= strlen(pathname); i++)
    towrite[i] = pathname[i - 1];
  towrite[strlen(pathname) + 1] = '\0';//metto il terminatore
  fprintf(stderr, "sto scrivendo nel socket %s, nome file originale %s\n", towrite, pathname); //debug
  int n = strlen(towrite) + 1; //terminatore

  SYSCALL_EXIT("writen", notused, writen(sockfd, &n, sizeof(int)), "write", ""); //scrivo il comando "semplice" al server
  SYSCALL_EXIT("writen", notused, writen(sockfd, towrite, n * sizeof(char)), "write", "");
}

int openFile(const char* pathname, int flags)//apertura di un file ram
{
  if((flags != 0 && flags != 1) || pathname == NULL) 
  { // i flag passate non sono valide (0 -> open | 1 -> open & create)
    errno = EINVAL;
    return -1;
  }
  writeCMD(pathname, 'e');
  int risposta, notused;
  
  SYSCALL_EXIT("writen", notused, writen(sockfd, &flags, sizeof(int)), "write", "");

  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");
  if(risposta == 0)
    fprintf(stderr, "il file %s è stato creato con successo\n", pathname);
  else
    fprintf(stderr, "il file %s non è stato creato\n", pathname);
  return risposta;
}

int removeFile(const char* pathname) 
{
  writeCMD(pathname,'c');//utilizzo il comando 'c' 
  int risposta, notused;//risposta è il risultato del server per controllare le corrette condizioni di esecuzione
  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");
  if(risposta != 0) { perror("ERRORI RIMOZIONE"); return -1; }//controllo se ho rimosso in modo corretto il file
  fprintf(stderr, "File %s cancellato con successo dal server\n", pathname);//debug 
  
  return 0;//se va tutto bene ritorna 0
}

//non va con il sizet
int readFile(const char* pathname, void** buf, int* size)//leggo il file al pathname e poi metto il contenuto nel buf
{
  //fprintf(stderr, "pathname a questo punto %s\n", pathname);
  writeCMD(pathname, 'r');//utilizzo il comando 'r'
  int notused;
  int n;//intermediario pe il size
  SYSCALL_EXIT("readn", notused, readn(sockfd, &n, sizeof(int)), "read", "");
  *size = n; //ho letto la size del file
  //fprintf(stderr, "la size del file che sto provando ad allocare è %d\n", n);
  if(*size == -1) //se non riesco a trovare il file il server scriverà al client -1 per indicare l'errore
  {
    fprintf(stderr, "file %s non esiste\n", pathname);//debug
    *buf = NULL;//metto il buff a NULL per evitare che provi a scrivere qualcosa nel buf
    *size = 0;
    return -1; //errore o file inesistente
  } 
  else 
  {
    *buf = malloc(sizeof(char) * n);//alloco lo spazio del buffer per il contenuto del file
    SYSCALL_EXIT("readn", notused, readn(sockfd, *buf, n * sizeof(char)), "read", "");//leggo il contenuto del file
    fprintf(stderr, "ho letto il file %s dal server\n", pathname);
    //fprintf(stderr, "ho letto il file %s con contenuto\n %s\n", pathname, (char*)(*buf));
    return 0; //successo
  }
}

int appendToFile(const char* pathname, void* buf, int size) 
{
  FILE *f = fopen (pathname, "a"); //bisogna gestire gli errori 
  fwrite(buf, 1, size, f);
  fclose(f);
  return 0;//successo
}

int readNFiles(int n, const char* dirname) //int n è il numero dei file da leggere
{
  //DA FARE: CONTROLLARE SE DIRNAME = NULL, NEL CASO DARE ERRORE
  char* ntmp = malloc(sizeof(char) * 10); //passo il numero come un carattere per riutilizzare il codice
  sprintf(ntmp, "%d", n);//memorizzo il numero in ntmp
  writeCMD(ntmp, 'R');

  int notused;
  SYSCALL_EXIT("readn", notused, readn(sockfd, &n, sizeof(int)), "read", "");//leggo dal socket del server il numero di file da leggere
  int lenpathtmp;                                                            //visto che potrebbero essere meno di quelli dichiarati dal parametro n
  char* buftmp;
  char** arr_buf = malloc(sizeof(char*) * n); //abbiamo messo l'array perchè sennò fa casino con le read della readFile
  for(int i = 0; i < n; i++) 
  { //per ogni file da leggere dal server
    SYSCALL_EXIT("readn", notused, readn(sockfd, &lenpathtmp, sizeof(int)), "read", "");
    buftmp = malloc(sizeof(char) * lenpathtmp);//passaggio in più inutile
    SYSCALL_EXIT("readn", notused, readn(sockfd, buftmp, lenpathtmp * sizeof(char)), "read", "");
    //fprintf(stderr, "leggo il file %s dal server\n", buftmp);
    arr_buf[i] = buftmp;//passaggio in più inutile posso direttamente allocare l'array e fargli salvare il contenuto in esso
  } 
  fprintf(stderr, "\n\n\n STO STAMPANDO L'ARRAY \n\n\n");//debug
  //prima mi ricavo tutti i nomi dei file da leggere e poi li leggo per evitare conflitti
  //nelle scritture/letture dei socket
  for(int i = 0; i < n; i++) 
  {
    fprintf(stderr, "elemento %d: %s\n", i, arr_buf[i]);//debug
    void* buffile;
    int sizebufffile;
    openFile(arr_buf[i], 0);
    readFile(arr_buf[i], &buffile, &sizebufffile);//leggo un singolo file preso dall'array dei file
    char path[1024];//dichiaro un path 
    //fprintf(stderr, "fin qui ci siamo\n");
    snprintf(path, sizeof(path), "%s/%s", dirname, arr_buf[i]);
    fprintf(stderr, "fin qui ci siamo, dirname %s, path %s\n", dirname, path);//debug

    appendToFile(path, buffile, sizebufffile);//infine scrivo in append il contenuto del file
  }
}

int writeFile(const char* pathname) //scrivo un file nel server 
{
  int notused, n;//n è la lunghezza del buffer nel quale salvo le risposte del server 
  writeCMD(pathname, 'W');
  char *buffer = NULL;
  n = strlen(pathname) + 2; //+2 per il comando e il terminatore
  buffer = realloc(buffer, n*sizeof(char));
  if (!buffer) { perror("realloc"); fprintf(stderr, "Memoria esaurita....\n"); }


  
    //fprintf(stderr, "file ok\n");
    FILE * f = fopen (pathname, "rb");
    //questa parte recupera la lunghezza e il contenuto di un file potrei pure metterlo in una funzione
    long length;
    char* bufferFile;
    if (f) //copia il file in un buffer
    {
      fseek (f, 0, SEEK_END);
      length = ftell (f);
      fseek (f, 0, SEEK_SET);
      bufferFile = malloc (length);
      if (bufferFile)
      {
        fread (bufferFile, 1, length, f);
      }
      fclose (f);
    }

    fprintf(stderr, "length file %s: %ld\n", pathname, length);//debug
    SYSCALL_EXIT("writen", notused, writen(sockfd, &length, sizeof(int)), "write", "");
    int cista;//se il file entra nel server per la capienza o il numero dei file
    SYSCALL_EXIT("readn", notused, readn(sockfd, &cista, sizeof(int)), "read", "");//leggo se il file può essere caricato nel server per capienza o nFile
    fprintf(stderr,"cista %d\n",cista);
    
    if(!cista) 
    { //il file non sta nel server materialmente, neanche se si espellessero tutti i file
      fprintf(stderr, "il file %s non sta materialmente nel server\n", pathname);
      //vanno fatte delle FREE
      return -1;//se il file non ci sta bisogna non fare altro se no il server si blocca
    }
    fprintf(stderr, "IL FILE CI STA\n");//debug
    
    SYSCALL_EXIT("writen", notused, writen(sockfd, bufferFile, length * sizeof(char)), "write", "");//scrivo il contenuto del file
    //fprintf(stderr,"dopo la scrittura del buffer\n");
    int risposta;
    SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");
    fprintf(stderr,"result: %d\n", risposta);
}

int EseguiComandoClient(NodoComando *tmp) 
{
  if(tmp == NULL)
  {
      perror("tmp NULL");
      return -1; //errore: tmp non può e non deve essere NULL. 
  }
  if(tmp->cmd == 'c')//gestisco il comando con c per eliminare il file dal server
  {
    fprintf(stderr, "file da rimuovere %s\n", tmp->name);
    openFile(tmp->name, 0);
    removeFile(tmp->name);
  }
  else
  { 
    if(tmp->cmd == 'r')
    {
      fprintf(stderr, "sto eseguendo r\n");//debug
      openFile(tmp->name, 0);
      
      void* buf;
      int sizebuff; //col size_t non va
      readFile(tmp->name, &buf, &sizebuff); //manca gestione errore
      if(savefiledir != NULL && buf != NULL) //se le condizioni sono corrette leggo il file
      {
        char path[1024];
        snprintf(path, sizeof(path), "%s/%s", savefiledir, tmp->name);
        //fprintf(stderr, "sto andando a scrivere il file %s in %s\n", tmp->name, path);
        appendToFile(path, buf, sizebuff);
      }
      else // non ho una cartella per salvare il file oppure il buffer è vuoto(però se è vuoto dovrebbe ugualmente fare qualcosa)
      {
          perror("read");
          return -1; //errore
      }
      //fprintf(stderr,"buffer:::%s\n",(char*)buf);
    } 
    else
    {
      if(tmp->cmd == 'R')//lettura di n file
      {
        fprintf(stderr, "comando readNFiles con n = %d\n", tmp->n);//debug
        readNFiles(tmp->n, savefiledir);
      } 
      else
      { 
        if(tmp->cmd == 'W') 
        {
          fprintf(stderr, "comando W con parametro %s\n", tmp->name);//debug
          if(openFile(tmp->name, 1) != -1) //flag: apri e crea
            writeFile(tmp->name);//se non riesce ad aprire il file non lo scrive neanche
        }
      }
    }
  }
  return 0;
}


void visitaRicorsiva(char* name, int *n, Queue **q)//name è il nome del path e n è il numero dei file 
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
    char path[MAXPATH];
    //fprintf(stderr,"entry visitata %s\n",entry->d_name);
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
          char buffer[MAXPATH];
          realpath(entry->d_name, buffer); //prendo il path assoluto del file per reperire il file
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
  abstime.tv_sec = timems / 1000;
  abstime.tv_nsec = (timems % 1000) * 1000000;
  fprintf(stderr, "sockfd: %ld, risultato openconnection %d\n", sockfd, x);


  while(q->len > 0) 
  { //finchè ci sono richieste che il parser ha visto
    NodoComando *tmp = pop(&q);
    nanosleep(&abstime, NULL);
    if(tmp->cmd == 'w') 
    { //fa una richiesta speciale in modo ricorsivo sfruttando il comando W
      if(tmp->n == 0)
        tmp->n = -1;  //in caso che n sia zero vuol dire che devo leggere tutti i file della directory
      
      if(strcmp(tmp->name, ".") == 0)//se passo come dir "." metto il path reale
      {
        free(tmp->name);
        tmp->name = malloc(sizeof(char) * MAXPATH);
        
        if (getcwd(tmp->name, MAXPATH) == NULL) { perror("getcwd");  exit(EXIT_FAILURE); }
      }
      visitaRicorsiva(tmp->name, &(tmp->n), &q);//però metto n=-1 per evitare il caso in cui n si decrementa fino a 0
    } 
    else
      EseguiComandoClient(tmp);
  }
  closeConnection(SOCKNAME);
  return 0;
}
