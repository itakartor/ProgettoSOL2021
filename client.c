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
#define MAXLENNUM 10
#define O_CREATE 1
long sockfd;

static int add_to_current_time(long sec, long nsec, struct timespec* res)
{
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
    strncpy(serv_addr.sun_path,sockname, strlen(sockname)+1);

    // setting waiting time
    struct timespec wait_time;
    // no need to check because msec > 0 and &wait_time != NULL
    set_timespec_from_msec(msec, &wait_time);

    // setting current time
    struct timespec curr_time;
    clock_gettime(CLOCK_REALTIME, &curr_time);

    // trying to connect
    int err = -1;
    if(verbose)
      fprintf(stdout, "[Time]: Currtime %ld abstime %ld\n", curr_time.tv_sec, abstime.tv_sec);
    while( (err = connect(sockfd, (struct sockaddr*)&serv_addr, sizeof(serv_addr))) == -1
            && curr_time.tv_sec < abstime.tv_sec )
            {
        fprintf(stderr, "[Errore]: %d Provo a connettermi\n", errno);

        if( nanosleep(&wait_time, NULL) == -1)
        {
            sockfd = -1;
            return -1;
        }
        if( clock_gettime(CLOCK_REALTIME, &curr_time) == -1)
        {
            sockfd = -1;
            return -1;
        }
    }

    if(err == -1) 
    {
        sockfd = -1;
        errno = ETIMEDOUT;
        perror("[Connessione]");
        return -1;
    }

    if(verbose)
      fprintf(stderr, "[Connessione]: Successo\n");
    return 0;
}

int closeConnection(const char* sockname)
{
  if(sockname == NULL)
  {
    errno = EINVAL;
    perror("[Sockname]");
    return -1;
  }
    // wrong socket name
  if(strcmp(sockname, socknameconfig) != 0)
  {
        // this socket is not connected
    errno = ENOTCONN;
    perror("[Strcmp]");
    return -1;
  }

  if( close(sockfd) == -1 )
  {
    sockfd = -1;
    perror("[Close]");
    return -1;
  }
  if(verbose)
    fprintf(stdout, "[Connessione]: Interrotta con Successo\n");

  sockfd = -1;
  return 0;
}

int writeCMD(const char* pathname, char cmd) 
{//parte ricorrente per mandare il comando al server
   if(pathname == NULL)
   {
    errno = EINVAL;
    perror("[Pathname]");
    return -1;
  }
  
  int notused;
  char *buffer = NULL;
  int lenPath = strlen(pathname);
  char* Comando; //alloco la stringa per la stringa comando da mandare al server con terminatore
  ec_null((Comando = malloc(sizeof(char) * (lenPath + 2))), "malloc");//+2 -> +1 per il flag e +1 per il terminatore
  //compatto il comando scompattato dal parser per mandarlo al server
  Comando[0] = cmd;//nella prima posizione c'è sempre il flag del comando per esempio:W
  for(int i = 1; i <= lenPath; i++)
    Comando[i] = pathname[i - 1];
  Comando[lenPath + 1] = '\0';//metto il terminatore
  lenPath += 2; //terminatore

  SYSCALL_EXIT("writen", notused, writen(sockfd, &lenPath, sizeof(int)), "write", ""); //scrivo il comando "semplice" al server
  SYSCALL_EXIT("writen", notused, writen(sockfd, Comando, lenPath * sizeof(char)), "write", "");
  if(verbose)
    fprintf(stdout, "[Invio Comando]: Successo\n"); //debug
  free(Comando);
  return 0;
}

int closeFile(const char* pathname) 
{
  if(pathname == NULL) 
  {
    errno = EINVAL;
    perror("[Pathname]");
    return -1;
  }

  if(writeCMD(pathname, 'z') == -1) 
  {
    errno = EPERM;
    perror("[writeCMD]");
    return -1;
  }
  int risposta, notused;

  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");//leggo la risposta del server che sta "chiudendo il file"
  
  if(risposta == -1) 
  {
    errno = EPERM;
    fprintf(stderr, "[Errore]: Chiusura file %s fallita\n", pathname);
  } 
  else//risposta = 0
  {
    if(verbose)
      fprintf(stdout, "[Chiusura]: File %s chiuso\n", pathname);
  }
  return risposta;
}

int openFile(const char* pathname, int flags)//apertura di un file ram
{
  if((flags != 0 && flags != 1) || pathname == NULL) 
  { // i flag passate non sono valide (0 -> open | 1 -> open & create)
    errno = EINVAL;
    perror("[OpenFile]");
    return -1;
  }
  if(writeCMD(pathname, 'o') == -1) 
  {
    errno = EPERM;
    perror("[writeCMD]");
    return -1;
  }
  int risposta, notused;
  
  SYSCALL_EXIT("writen", notused, writen(sockfd, &flags, sizeof(int)), "write", "");//scrivo i flag dell'apertura file

  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");//leggo il risultato dell'apertura
  if(risposta == 0)
  {
    if(verbose)
      fprintf(stdout, "[Apertura]: Il file %s è stato creato con successo\n", pathname);
  }
  else
  {
    if(risposta == -2)
    {
      fprintf(stderr, "[Avviso]: File %s già esistente nel server (Creazione file fallita)\n", pathname);
      risposta = -1;
    }
    else
    {
        errno = EACCES;
        fprintf(stderr, "[Errore]: Il file %s non è stato creato\n", pathname);
    }
  }
  return risposta;
}

int removeFile(const char* pathname) 
{
  if(pathname == NULL) 
  {
    errno = EINVAL;
    perror("[Pathname]");
    return -1;
  }
  if(writeCMD(pathname, 'c') == -1) 
  {
    errno = EPERM;
    perror("[writeCMD]");
    return -1;
  }
  int risposta, notused;//risposta è il risultato del server per controllare le corrette condizioni di esecuzione
  
  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");
  
  //controllo se ho rimosso in modo corretto il file
  if(risposta == -1) 
  { 
    errno = EACCES;
    fprintf(stderr, "[Errore]: Rimozione %s Fallita\n", pathname);
    return -1; 
  }
  else
  {
    if(verbose)
      fprintf(stdout, "[Rimozione]: %s Successo\n", pathname);//debug   
  }
  
  return 0;//se va tutto bene ritorna 0
}

//non va con il sizet
int readFile(const char* pathname, void** buf, int* size)//leggo il file al pathname e poi metto il contenuto nel buf
{
   if(pathname == NULL) 
   {
    errno = EINVAL;
    perror("[Pathname]");
    return -1;
  }
  if(writeCMD(pathname, 'r') == -1) 
  {
    errno = EPERM;
    perror("[writeCMD]");
    return -1;
  }

  int notused;
  int n;//intermediario pe il size
  SYSCALL_EXIT("readn", notused, readn(sockfd, &n, sizeof(int)), "read", "");
  *size = n; //ho letto la size del file
  //fprintf(stderr, "la size del file che sto provando ad allocare è %d\n", n);
  if(*size == -1) //se non riesco a trovare il file il server scriverà al client -1 per indicare l'errore
  {
    errno = EPERM;
    fprintf(stderr, "[Errore]: Lettura %s Fallita\n", pathname);//debug
    *buf = NULL;//metto il buff a NULL per evitare che provi a scrivere qualcosa nel buf
    *size = 0;
    return -1; //errore o file inesistente
  } 
  else 
  {
    ec_null((*buf = malloc(sizeof(char) * n)), "malloc");//alloco lo spazio del buffer per il contenuto del file
    SYSCALL_EXIT("readn", notused, readn(sockfd, *buf, n * sizeof(char)), "read", "");//leggo il contenuto del file
    
    if(verbose)
    fprintf(stdout, "[Lettura file]: %s Successo\n", pathname);//debug
    
    return 0; //successo
  }
}

int writeBufToDisk(const char* pathname, void* buf, int size) 
{
  if(pathname == NULL || buf == NULL || size < 0) 
  {//argomenti non validi 
    errno = EINVAL;
    perror("[writeBufToDisk]");
    return -1;
  }

  FILE *f; //bisogna gestire gli errori 
  ec_null((f = fopen(pathname, "w")), "fopen");
  fwrite(buf, 1, size, f);//va cambiata con la write perchè non posso controllare l'errore
  neq_zero((fclose(f)), "fclose");
  return 0;//successo
}

int readNFiles(int n, const char* dirname) //int n è il numero dei file da leggere
{
  if(dirname == NULL) 
  {
    errno = EINVAL;
    perror("[readNFiles]");
    return -1;
  }
  char* ntmp; //passo il numero come un carattere per riutilizzare il codice
  ec_null((ntmp = malloc(sizeof(char) * MAXLENNUM)), "malloc");
  if(sprintf(ntmp, "%d", n) < 0) 
  {//memorizzo il numero in ntmp come char
    perror("[Snprintf]");
    return -1;
  }
  if(writeCMD(ntmp, 'R') == -1) 
  {
    errno = EPERM;
    perror("[writeCMD]");
    return -1;
  }

  int notused;
  SYSCALL_EXIT("readn", notused, readn(sockfd, &n, sizeof(int)), "read", "");//leggo dal socket del server il numero di file da leggere
  int lenpathtmp;                                                            //visto che potrebbero essere meno di quelli dichiarati dal parametro n
  char* buftmp;
  char** arr_buf; //tengo traccia dei path dei file per poi leggerli in un secondo momento
  ec_null((arr_buf = malloc(sizeof(char*) * n)), "malloc");
  for(int i = 0; i < n; i++) 
  { //per ogni file da leggere dal server
    SYSCALL_EXIT("readn", notused, readn(sockfd, &lenpathtmp, sizeof(int)), "read", "");
    ec_null((buftmp = malloc(sizeof(char) * lenpathtmp)), "malloc");//passaggio in più inutile
    SYSCALL_EXIT("readn", notused, readn(sockfd, buftmp, lenpathtmp * sizeof(char)), "read", "");
    arr_buf[i] = buftmp;//passaggio in più inutile posso direttamente allocare l'array e fargli salvare il contenuto in esso
  } 
  
  //prima mi ricavo tutti i nomi dei file da leggere e poi li leggo per evitare conflitti
  //nelle scritture/letture dei socket
  
  for(int i = 0; i < n; i++) 
  {
    //fprintf(stderr, "elemento %d: %s\n", i, arr_buf[i]);//debug
    void* buffile;
    int sizebufffile;
    if(openFile(arr_buf[i], 0) != -1)//gestione dell'errore
    {
      if(readFile(arr_buf[i], &buffile, &sizebufffile) == -1) 
      {//leggo un singolo file preso dall'array dei file
        return -1;
      }
      char path[MAXPATH];//dichiaro un path 
      
      if(snprintf(path, sizeof(path), "%s/%s", dirname, arr_buf[i]) < 0) 
      { 
        perror("[Snprintf]"); 
        return -1; 
        }
   
      if(writeBufToDisk(path, buffile, sizebufffile) == -1) { return -1; }//infine scrivo in append il contenuto del file
      if(closeFile(arr_buf[i]) == -1) { return -1; }//infine chiudo il file
    }
    else
    {
      errno = EACCES;
      fprintf(stderr, "[Errore]: Apertura %s Fallita\n", arr_buf[i]);
      return -1;
    }
  }
  return 0;
}

int appendToFile(const char* pathname, void* buf, int size) 
{
  if(pathname == NULL) 
  {
    errno = EINVAL;
    perror("[appendToFile]");
    return -1;
  }

  int notused;
  SYSCALL_EXIT("writen", notused, writen(sockfd, &size, sizeof(int)), "write", "");
  
  int cista;
  SYSCALL_EXIT("readn", notused, readn(sockfd, &cista, sizeof(int)), "read", "");
  if(!cista) 
  { //il file non sta nel server materialmente, neanche se si espellessero tutti i file
    fprintf(stderr, "[Problema]: Il file %s è troppo grande per la capienza del server\n", pathname);
      //vanno fatte delle FREE
    return -1;
  }
  SYSCALL_EXIT("writen", notused, writen(sockfd, (char*)buf, size * sizeof(char)), "write", "");

  int risposta;
  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");//lettura per controllare se la scrittura in append è stata eseguita
  if(risposta == -1) 
  {
    errno = EACCES;
    fprintf(stderr, "[Errore]: Scrittura append del file %s\n", pathname);
  } 
  else
  {
    if(verbose)
      fprintf(stdout, "[Scrittura]: File %s scritto correttamente nel server\n", pathname);
  }
  return risposta;
}

int writeFile(const char* pathname) //scrivo un file nel server 
{
  if(pathname == NULL) 
  {
    errno = EINVAL;
    perror("[writeFile]");
    return -1;
  }
  
  if(writeCMD(pathname, 'W') == -1) 
  {
    errno = EPERM;
    perror("[writeCMD]");
    return -1;
  }
  int notused, lenBuf, risposta;//n è la lunghezza del buffer nel quale salvo le risposte del server 
  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");
  
  if(risposta == -1) 
  {
    fprintf(stderr, "[Errore]: Apertura file %s Fallita\n", pathname);
    return -1;
  } 
  else//se non ci sono problemi durante la creazione del file o l'apertura
  {
    char *buffer = NULL;
    lenBuf = strlen(pathname) + 2; //+2 per il comando e il terminatore
    ec_null((buffer = realloc(buffer, lenBuf * sizeof(char))), "realloc");

    FILE * f; //va cambiata la fopen e le funzioni con la f
    ec_null((f = fopen(pathname, "rb")), "fopen");
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
       neq_zero((fclose(f)), "fclose");
    }

    if(appendToFile(pathname, bufferFile, length) == -1)
      return -1;
  }
  return 0;//successo
}

int EseguiComandoClient(NodoComando *tmp) 
{
  if(tmp == NULL)
  {
      errno = EINVAL;
      perror("[EseguiComandoClient]");
      return -1; //errore: tmp non può e non deve essere NULL. 
  }
  if(tmp->cmd == 'c')//gestisco il comando con c per eliminare il file dal server
  {
    if(verbose)
      fprintf(stdout, "[Rimozione File]: %s in corso\n", tmp->name);
    if(openFile(tmp->name, 0) != -1)
    {
      if(removeFile(tmp->name) == -1) 
        return -1;
    }
    else//errore openFile
    {
      return -1;
    }
  }
  else
  { 
    if(tmp->cmd == 'r')
    {
      if(verbose)
        fprintf(stdout, "[Lettura file]: %s in corso\n",tmp->name);//debug
      if(openFile(tmp->name, 0) != -1)
      {
        void* buf;
        int sizebuff; //risolvere problema size_t
        if(readFile(tmp->name, &buf, &sizebuff) == -1) 
        {
          return -1;
        }
        else
        {
          if(savefiledir != NULL && buf != NULL) //se le condizioni sono corrette leggo il file
          {
            char path[MAXPATH];
            snprintf(path, sizeof(path), "%s/%s", savefiledir, tmp->name);
            //fprintf(stderr, "sto andando a scrivere il file %s in %s\n", tmp->name, path);
            writeBufToDisk(path, buf, sizebuff);
            closeFile(tmp->name);
          }
          else // non ho una cartella per salvare il file oppure il buffer è vuoto
          {
            return -1; //errore
          }
        } 
      }//se l'openFile ha un errore
      else
      {
        return -1;
      }
    } 
    else//lettura di n file
    {
      if(tmp->cmd == 'R')
      {
        if(verbose)
          fprintf(stdout, "[Lettura n File]: %d in corso\n", tmp->n);//debug
        if(readNFiles(tmp->n, savefiledir) == -1) 
          return -1;
      } 
      else
      { 
        if(tmp->cmd == 'W') 
        {
          if(verbose)
            fprintf(stdout, "[Scrittura file]: %s in corso\n", tmp->name);//debug
          if(openFile(tmp->name, O_CREATE) != -1)//flag = O_CREATE = 1 devo creare il file e crearlo
          {
            if(writeFile(tmp->name) == -1) 
              return -1; 
            if(closeFile(tmp->name) == -1)
              return -1; 
          } 
          else
          {
            if(openFile(tmp->name, 0) != -1) //flag = 0 è come se non avessi flag quindi devo provare ad aprire il file
            { 
              if(writeFile(tmp->name) == -1) 
                return -1; 
              if(closeFile(tmp->name) == -1)
                return -1; 
            }
            else//OpenFile non riuscita
            {
              return -1;
            }
          } 
        }
        else
        {
          fprintf(stderr, "[Errore]: Parametro %c non riconosciuto\n", tmp->cmd);
          return -1;
        }
      }
    }
  }
  return 0;
}


int visitaRicorsiva(char* name, int *n, Queue **q)//name è il nome del path e n è il numero dei file 
{ //alcuni file hanno problemi 
  //fprintf(stderr, "sto visitando %s\n", name);
  if(name == NULL)//se il path name non è corretto
    return -1;
  char buftmp[MAXPATH];
  if (getcwd(buftmp, MAXPATH)==NULL)//prendendo il pathname della directory attuale
  { 
    perror("getcwd"); 
    return -1; 
  }
  
  if (chdir(name) == -1)//cambio la directory corrente
  { 
    perror("chdir visitaRicorsiva"); 
    return -1; 
  }
 
  DIR *dir;
  struct dirent *entry;

  if (!(dir = opendir(name)))//se non si riesce ad aprire la dir
    return -1;

  while ((entry = readdir(dir)) != NULL && (*n != 0))//apro la dir e vedo n != 0 perchè in caso dovrei fermarmi
  {
    char path[MAXPATH];
    //fprintf(stderr,"entry visitata %s\n",entry->d_name);
    if (entry->d_type == DT_DIR)//vedo se il tipo della entry è una cartella
    {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || entry->d_name[0] == '.')
        continue;
        
        if(snprintf(path, sizeof(path), "%s/%s", name, entry->d_name) < 0) 
        {//va a scrivere nel path l'entry->d_name che sto visitando
          perror("snprintf"); 
          return -1; 
        } 
        
        if(visitaRicorsiva(path, n, q) == -1)//riparte la visita ricorsiva nella stessa dir
          return -1;
    } 
    else
    { 
      if (entry->d_type == DT_REG)//per ogni file regolare vado a decrementare n e a caricare il file se n>0 o n==-1 
      {
        if(*n > 0 || *n == -1) 
        {
          char *buffer;
          ec_null((buffer = malloc(sizeof(char) * MAXPATH)), "malloc");
          ec_null((realpath(entry->d_name, buffer)), "realpath");//prendo il path assoluto del file per reperire il file
          
          NodoComando *new;//vado a gestire il -w attraverso la chiamata -W
          ec_null((new = malloc(sizeof(NodoComando))), "malloc");
          new->cmd = 'W';
          ec_null((new->name = malloc(sizeof(char) * (strlen(buffer)+1))), "malloc");
          strcpy(new->name, buffer);
          new->name[strlen(buffer)] = '\0';
          new->n = 0;
          if(push(q, new) == -1) 
            return -1;
        }
        if(*n > 0)//se solo se n>0 lo decremento
          (*n)--;
      }
    }
  }
  ec_meno1((closedir(dir)), "closedir");
  ec_meno1((chdir(buftmp)), "chdir");
  return 0;
}

int main(int argc, char *argv[]) 
{
  Queue *QueueParser = parser(argv,argc); //coda delle operazioni
  //fprintf(stderr,"post parser\n");
  struct timespec abstime;

  add_to_current_time(2, 0, &abstime);
  //primo parametro: tempo limite (in secondi)
  //secondo parametro: intervallo di tempo tra due connessioni (in millisecondi)
  
  //fprintf(stderr,"socknameconfig %s\n",socknameconfig);
  ec_meno1((openConnection(socknameconfig, 1000, abstime)), "openConnection"); //da vedere se da errore
  abstime.tv_sec = timems / 1000;
  abstime.tv_nsec = (timems % 1000) * 1000000;

  while(QueueParser->len > 0) 
  { //finchè ci sono richieste che il parser ha visto
    NodoComando *tmp = pop(&QueueParser);
    nanosleep(&abstime, NULL);
     if(verbose)
      fprintf(stdout, "[Lettura Comando]: '%c' - %s in corso\n", tmp->cmd, tmp->name);
    
    if(tmp->cmd == 'w') 
    { //fa una richiesta speciale in modo ricorsivo sfruttando il comando W
      if(tmp->n == 0)
        tmp->n = -1;  //in caso che n sia zero vuol dire che devo leggere tutti i file della directory
      
      if(strcmp(tmp->name, ".") == 0)//se passo come dir "." metto il path reale
      {
        free(tmp->name);
        ec_null((tmp->name = malloc(sizeof(char) * MAXPATH)), "malloc");
        ec_null((getcwd(tmp->name, MAXPATH)), "getcwd");
      }
      if(visitaRicorsiva(tmp->name, &(tmp->n), &QueueParser) == -1) 
        return -1;//però metto n=-1 per evitare il caso in cui n si decrementa fino a 0
    } 
    else
      if(EseguiComandoClient(tmp) == -1)
        return -1;
  }
  if(closeConnection(socknameconfig) == -1) 
    return -1;
  
  return 0;
}
