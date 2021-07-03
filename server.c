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

#define MAXBUFFER 1000
#define MAXSTRING 100
#define CONFIGFILE "config.txt"
#define SPAZIO "spazio"
#define NUMEROFILE "numeroFile"
#define SOC "sockName"
#define WORK "numWorkers"
#define MAXBACKLOG 32

int spazio = 0;
int numeroFile = 0; //informazioni del file config
int numWorkers = 0;
char* SockName = NULL;

long connfd;        //fd del socket del client 
fd_set set;         //maschera dei bit
Queue *queueClient; //coda dei client che fanno richieste
Queue *queueFiles; //coda dei file memorizzati

int spazioOccupato = 0;

int **p;            //array di pipe

static pthread_mutex_t mutexQueueClient = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutexQueueFiles = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condQueueClient = PTHREAD_COND_INITIALIZER;

void cleanup() { //cancellare il collegamento
  unlink(SockName);
}

void parserFile(void) {   //parser del file
  int i;
  char* save;
  char* token;

  char* buffer = malloc(sizeof(char) * MAXBUFFER);
  FILE* p;

  if((p = fopen(CONFIGFILE, "r")) == NULL) 
  {
    //gestione dell'errore
    perror("fopen");
  }
  while(fgets(buffer, MAXBUFFER, p)) 
  {
    save = NULL;
    token = strtok_r(buffer, " ",  &save);
    char** tmp = malloc(sizeof(char*) * 2);
    i = 0;        //indice dell'array tmp per salvare l'argomento della variabile di interesse
    while(token) {
      tmp[i] = malloc(sizeof(char) * MAXSTRING);
      strncpy(tmp[i], token, strlen(token) - i); //se è il secondo elemento (i = i) non deve prendere il newline finale,
      token = strtok_r(NULL, " ", &save);
      i++;
    }

    if(strcmp(tmp[0], SPAZIO) == 0) //controllo se il file è formatatto nel modo corretto 
    {                               //esempio:spazio 100
      if(!isNumber(tmp[1],&spazio)) //spazio massimo dedicato
      {
        //fprintf(stderr,"questo è il risultato %d\n",spazio);
      } 
      else 
      {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
        //fprintf(stderr, "ERRORE %s non è un numero\n", tmp[1]);
      }
    }
    if(strcmp(tmp[0], NUMEROFILE) == 0)//numero massimo di file
    {
      //fprintf(stderr,"questo è il risultato dello numero file %d\n", isNumber(tmp[1],&numeroFile));
      if(!isNumber(tmp[1], &numeroFile)) {
      } else {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
      }
    }
    if(strcmp(tmp[0], SOC) == 0)//nome del socket
    {
      SockName = malloc(sizeof(char) * strlen(tmp[1]));
      strncpy(SockName, tmp[1], strlen(tmp[1]));
    }
    if(strcmp(tmp[0], WORK) == 0)//numero di thread worker
    {
      //fprintf(stderr,"questo è il risultato dello worker %d\n", isNumber(tmp[1],&numWorkers));  
      if(!isNumber(tmp[1],&numWorkers)) 
      {}
      else 
      {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
      }
    }

  free(tmp[0]);
  free(tmp[1]);
  free(tmp);
  }
  fclose(p);  
  free(buffer);// libero l'array tmp, il buffer e chiudo il file
  //fprintf(stderr,"abbiamo chiuso il file\n");
  
  if(spazio <= 0 || numeroFile <= 0 || SockName == NULL || numWorkers <= 0) {
    fprintf(stderr, "config.txt errato\n");
    exit(EXIT_FAILURE);
  }
  //stampa di debug
  fprintf(stderr,"spazio: %d\n", spazio);
  fprintf(stderr,"numeroFile: %d\n", numeroFile);
  fprintf(stderr,"SockName: %s\n", SockName);
  fprintf(stderr,"numWorkers: %d\n", numWorkers);
}

static void* threadF(void* arg) //funzione dei thread worker
{
  int* numThread = (int*)arg; 
  while(1) {
    pthread_mutex_lock(&mutexQueueClient);
    while(queueClient->len == 0) 
    {
      fprintf(stderr, "sto dormendo!\n");
      pthread_cond_wait(&condQueueClient, &mutexQueueClient);
      
    }
    fprintf(stderr, "sono sveglio!\n");
    ComandoClient* tmp = pop(&queueClient);
    pthread_mutex_unlock(&mutexQueueClient);

    if(tmp == NULL)
    {
      fprintf(stderr,"è vuoto tmp");
      continue;
    }  

    
    long connfd = tmp->connfd; // id del client che sta facendo richiesta 
    char comando = tmp->comando;
    char* parametro = tmp->parametro;

    fprintf(stderr,"questo è il comando %c\n",tmp->comando);
    fprintf(stderr,"questo è l'argomento %s\n",tmp->parametro);
    fprintf(stderr,"questo è l'ID %ld\n",tmp->connfd);

    char* risposta = malloc(sizeof(char)*MAXSTRING);
    int lenRisposta;
    int notused;

    //POSSIBILI COMANDI PASSATI
    switch(comando) 
    {
      case 'W': 
      { //richiesta di scrittura
        pthread_mutex_lock(&mutexQueueFiles);
        Node* esiste = fileExistsServer(queueFiles, parametro);
        pthread_mutex_unlock(&mutexQueueFiles);

        fileRam* newFile = esiste->data;

          
        //fprintf(stderr, "sto scrivendo\n");
        SYSCALL_EXIT("readn", notused, readn(connfd, &(newFile->length), sizeof(int)), "read", "");//leggo la lunghezza del file 
          
        int cista = 1;
        if(newFile->length > spazio) 
        { //non lo memorizza proprio
          fprintf(stderr, "Il file %s è troppo grande (%ld) e non sta materialmente nel server (capienza massima %d)\n", newFile->nome, newFile->length, spazio);
            //vanno fatte delle FREE
            //vado a scrivere nel socket che il file non ci sta
          cista = 0;
        }

        if (writen(connfd, &cista, sizeof(int))<=0) { perror("c"); }
        if(!cista)
          continue;
        
        while(spazioOccupato + newFile->length > spazio)
        { //deve iniziare ad espellere file
          fprintf(stderr, "il server è pieno (di spazio)\n");
          fileRam *fileramtmptrash = pop(&queueFiles);
          fprintf(stderr, "Sto espellendo il file %s dal server\n", fileramtmptrash->nome);
          spazioOccupato-=fileramtmptrash->length;
            //vanno fatte FREE
        }
        spazioOccupato+=newFile->length;

        newFile->buffer = malloc(sizeof(char) * newFile->length);//contenuto del file da leggere 
        SYSCALL_EXIT("readn", notused, readn(connfd, newFile->buffer, (newFile->length)*sizeof(char)), "read", "");//leggo il contenuto del file
        int risposta = 0;//successo  
        
        fprintf(stderr, "risposta %d\n", risposta);
        printQueueFiles(queueFiles);//debug
          
        if (writen(connfd, &risposta, sizeof(int))<=0) { perror("ERRORE SCRITTURA RISPOSTA SERVER"); } //scrivo nel client il risultato dell'operazione
        
        break;
      }
      case 'c'://remove file dalla coda
      {
        int risposta;
        pthread_mutex_lock(&mutexQueueFiles);//prendo il mutex per vedere se il file esiste e se posso rimuoverlo
        Node* esiste = fileExistsServer(queueFiles, parametro);
        
        if(esiste == NULL)//in caso che non esistesse il file nel server
        {
          pthread_mutex_unlock(&mutexQueueFiles);
          risposta = -1;//errore
          fprintf(stderr, "errore, file %s NON rimosso dal server (non esisteva)\n", parametro);
        } 
        else//il file non esiste nella coda
        {
          fileRam *tmpfileramtrash = esiste->data;
          spazioOccupato-= tmpfileramtrash->length;
          risposta = removeFromQueue(&queueFiles, esiste);
          
          fprintf(stderr, "file %s rimosso con successo dal server\n", parametro);
          pthread_mutex_unlock(&mutexQueueFiles);
          
        }
        printQueueFiles(queueFiles);// debug
        if (writen(connfd, &risposta, sizeof(int))<=0) { perror("ERRORE RISPOSTA SERVER"); }
        break;
      }
      case 'r': 
      {
        fprintf(stderr, "Ho ricevuto un comando di lettura!\n");
        pthread_mutex_lock(&mutexQueueFiles);
        Node* esiste = fileExistsServer(queueFiles, parametro);
        int len; //RISPOSTA che ci dice se è tutto ok o no
        if(esiste != NULL) 
        { //file esistente nel server
          fileRam *filetmp = esiste->data;
          len = filetmp->length;
          char* buf = filetmp->buffer;
          //fprintf(stderr, "sto inserendo %d\n", len);
          if (writen(connfd, &len, sizeof(int))<=0) { perror("ERRORE LUNGHEZZA"); }//leggo il contenuto del file
          if (writen(connfd, buf, len * sizeof(char))<=0) { perror("ERRORE LETTURA CONTENUTO FILE"); }
          fprintf(stderr, "file %s letto con successo dal server\n", parametro);
          pthread_mutex_unlock(&mutexQueueFiles);
        } 
        else
        { //ERRORE: file non trovato nel server
          pthread_mutex_unlock(&mutexQueueFiles);
          len = -1;//risposta
          if (writen(connfd, &len, sizeof(int))<=0) { perror("c"); }
          fprintf(stderr, "errore, file %s NON letto dal server (non esisteva)\n", parametro);
        }
        break;
      }
      case 'R': 
      {
        fprintf(stderr, "Ho ricevuto un comando readNFiles con n = %d\n", atoi(parametro));
        int numDaLeggere;//numero dei file da leggere

        if(atoi(parametro) > queueFiles->len)
          numDaLeggere = queueFiles->len;
        else
          numDaLeggere = atoi(parametro);
        if(numDaLeggere <= 0)
          numDaLeggere = queueFiles->len;
        if (writen(connfd, &numDaLeggere, sizeof(int))<=0) { perror("ERRORE SCRITTURA NUM FILE"); }
        Node* nodetmp = queueFiles->head;
        fileRam *fileramtmp;
        char* buftmp;
        for(int i = 0; i < numDaLeggere; i++)//salvo i nomi dei file in un array per poi leggerli in un secondo momento 
        {
          fileramtmp = nodetmp->data;
          buftmp = malloc(sizeof(char) * strlen(fileramtmp->nome));
          strcpy(buftmp, fileramtmp->nome);
          int buftmplen = strlen(buftmp);
          if (writen(connfd, &buftmplen, sizeof(int))<=0) { perror("c"); }
          if (writen(connfd, buftmp, buftmplen * sizeof(char))<=0) { perror("x"); }
          fprintf(stderr, "sto mandando il file %s al client\n", buftmp);
          free(buftmp);
          nodetmp = nodetmp->next;
        }
        break;
      }
      case 'e': 
      { //openFile
        fprintf(stderr, "ho ricevuto un comando di esistenza di un file %s nel server\n", parametro);
        int risposta;
        pthread_mutex_lock(&mutexQueueFiles);
        Node* esiste = fileExistsServer(queueFiles, parametro);
        
        int flags;
        if (readn(connfd, &flags, sizeof(int))<=0) { fprintf(stderr, "ERRORE LETTURA FLAGS\n"); }
        fprintf(stderr, "codice flags %d\n", flags);
        if(esiste == NULL && flags == 0) //deve aprire il file ma non esiste, errore
          risposta = -1;
        else 
          if(esiste == NULL && flags == 1) 
          { //deve creare e aprire il file (che non esiste)
            if(queueFiles->len + 1 > numeroFile) 
            { //deve iniziare ad espellere file per il numero file
              fprintf(stderr, "il server è pieno (di numero), cancello un file\n");
              fileRam *fileramtmptrash = pop(&queueFiles);
              fprintf(stderr, "Sto espellendo il file %s dal server\n", fileramtmptrash->nome);

              //vanno fatte FREE
            }
            //creo un file vuoto
            fileRam *newfile = malloc(sizeof(fileRam));
            newfile->nome = malloc(sizeof(char) * (strlen(basename(parametro)) + 1));//uso il base name per avere solo il nome del file senza path assoluto
            strcpy(newfile->nome, basename(parametro));
            newfile->nome[strlen(basename(parametro))] = '\0';
            newfile->length = 0;
            newfile->buffer = NULL;
            push(&queueFiles, newfile);
            risposta = 0;//successo
          }
          else 
            if(esiste != NULL && flags == 0) 
            { //deve aprire il file, che esiste già
              risposta = 0;
            }
            else
             if(esiste != NULL && flags == 1) 
             { //deve creare e aprire il file, che esiste già, errore
                risposta = -1;
             }
              pthread_mutex_unlock(&mutexQueueFiles);
              if (writen(connfd, &risposta, sizeof(int))<=0) { perror("c"); }
      }
    }

    FD_SET(connfd, &set);
    write(p[*numThread][1], "vai", 3);

  }
  return NULL;
}

int main(int argc, char* argv[]) 
{
  int numThread = -1;//numero identificativo del thread 
  parserFile();      //prendo le informazioni dal file config.txt
  numWorkers = 1;
  cleanup();    //ripulisco vecchie connessioni 
  atexit(cleanup);
  queueClient = initQueue(); //coda dei file descriptor dei client che provano a connettersi
  queueFiles = initQueue();

  p = malloc(sizeof(int*) * numWorkers); //array delle pipe
  
  pthread_t *t = malloc(sizeof(pthread_t) * numWorkers); //array dei thread
  for(int i = 0; i < numWorkers; i++) 
  {
    numThread = i;
    if(pthread_create(&t[i], NULL, &threadF, &numThread) != 0)
    {
        fprintf(stderr, "pthread_create failed server\n");
    }

    int pfd[2];
    if(pipe(pfd) == -1) { perror("ERRORE CREAZIONE PIPE"); }
    p[i] = pfd;
    //sleep(1);
  }

  int listenfd; //codice identificato del listen per accettare le nuove connessioni

  SYSCALL_EXIT("socket", listenfd, socket(AF_UNIX, SOCK_STREAM, 0), "socket", "");

  struct sockaddr_un serv_addr;
  memset(&serv_addr, '0', sizeof(serv_addr));
  serv_addr.sun_family = AF_UNIX;
  strncpy(serv_addr.sun_path, SockName, strlen(SockName)+1);

  int notused;
  SYSCALL_EXIT("bind", notused, bind(listenfd, (struct sockaddr*)&serv_addr,sizeof(serv_addr)), "bind", "");
  
  SYSCALL_EXIT("listen", notused, listen(listenfd, MAXBACKLOG), "listen", "");
  

  fd_set tmpset;
  // azzero sia il master set che il set temporaneo usato per la select
  FD_ZERO(&set);
  FD_ZERO(&tmpset);

  // aggiungo il listener fd al master set
  FD_SET(listenfd, &set);

  // tengo traccia del file descriptor con id piu' grande
  int fdmax = listenfd;

  for(int i = 0; i < numWorkers; i++) 
  {
    FD_SET(p[i][0], &set); //p array di pipe, aggiungo tutte le pipe alla set in lettura
    if(p[i][0] > fdmax)
      fdmax = p[i][0];
    fprintf(stderr, "inserisco il connfd della pipe %d\n", p[i][0]);
  }

  
  
  for(;;) 
  {
// copio il set nella variabile temporanea per la select
    tmpset = set;
    if (select(fdmax+1, &tmpset, NULL, NULL, NULL) == -1) 
    {
      perror("select");
      return -1;
    }

// cerchiamo di capire da quale fd abbiamo ricevuto una richiesta
    for(int i=0; i <= fdmax; i++) 
    {
      if (FD_ISSET(i, &tmpset)) 
      {
        if (i == listenfd) 
        { // e' una nuova richiesta di connessione
          SYSCALL_EXIT("accept", connfd, accept(listenfd, (struct sockaddr*)NULL ,NULL), "accept", "");
          fprintf(stderr,"il server ha accettato la connessione con %ld\n",connfd);
          FD_SET(connfd, &set);  // aggiungo il descrittore al master set
          if(connfd > fdmax)
            fdmax = connfd;  // ricalcolo il massimo
          continue;
        }
        //Ramo ELSE
        //devo controllare se la select si è risvegliata per una pipe 
        //oppure per una nuova richiesta

        connfd = i;         //client gia connesso con una nuova richiesta 

        if(isPipe(numWorkers,connfd,p)) //controllo se è una pipe
        {
          fprintf(stderr, "è una pipe\n");
          char* buftmp;
          read(connfd, buftmp, 6);
          continue;
        }
          


        FD_CLR(connfd, &set);//levo il bit nella set perchè sto gestendo la richiesta di connfd
        
        msg_t str;
        int err =readn(connfd, &str.len, sizeof(int));
        if(err == 0) //socket vuoto
        {
          fprintf(stderr, "client disconnesso\n");
          continue;
        }
        if (err<0) { fprintf(stderr, "ERRORE SULLA LUNGHEZZA LETTURA\n"); }
        
        
        str.len = str.len - sizeof(char);
        //togliamo sizeof(char) perchè nella read al comando prima stiamo leggendo già un carattere
        if (readn(connfd, &str.comando, sizeof(char))<=0) { fprintf(stderr, "ERRORE LETTURA COMANDO\n"); }

        str.arg = calloc((str.len), sizeof(char));
        if (!str.arg) 
        {
    	    perror("calloc");
    	    fprintf(stderr, "Memoria esaurita....\n");
          //return -1
        }
        if (readn(connfd, str.arg, (str.len)*sizeof(char))<=0) { fprintf(stderr, "ERRORE LETTURA ARGOMENTO\n"); }

        ComandoClient *cmdtmp = malloc(sizeof(ComandoClient));
        cmdtmp->comando = str.comando;
        cmdtmp->parametro = malloc(sizeof(char) * (strlen(str.arg)+1));
        cmdtmp->connfd = connfd;
        strcpy(cmdtmp->parametro, str.arg);
        cmdtmp->parametro[strlen(str.arg)] = '\0';

        pthread_mutex_lock(&mutexQueueClient);
        push(&queueClient, cmdtmp);
        pthread_mutex_unlock(&mutexQueueClient);
        pthread_cond_signal(&condQueueClient); 
      }
    }
  }
}