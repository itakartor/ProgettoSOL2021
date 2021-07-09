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
#include <libgen.h> //basename

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

void parserFile(void) //parser del file config.txt
{   
  int i;
  char* save;
  char* token;

  char* buffer;
  ec_null((buffer = malloc(sizeof(char) * MAXBUFFER)), "malloc");
  FILE* p;

  ec_null((p = fopen(CONFIGFILE, "r")), "fopen");

  while(fgets(buffer, MAXBUFFER, p)) 
  {
    save = NULL;
    token = strtok_r(buffer, " ",  &save);
    char** tmp;
    ec_null((tmp = malloc(sizeof(char*) * 2)), "malloc");
    i = 0;        //indice dell'array tmp per salvare l'argomento della variabile di interesse
    
    while(token) 
    {
      ec_null((tmp[i] = malloc(sizeof(char) * MAXSTRING)), "malloc");
      strncpy(tmp[i], token, strlen(token) - i); //l'argomento non deve prendere il terminatore di riga (accapo)
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
      if(!isNumber(tmp[1], &numeroFile)) 
      {}
      else
      {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
      }
    }
    if(strcmp(tmp[0], SOC) == 0)//nome del socket
    {
      ec_null((SockName = malloc(sizeof(char) * strlen(tmp[1]))), "malloc");
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
  if(fclose(p) != 0) 
  { 
    perror("fclose"); 
    exit(EXIT_FAILURE); 
  } 
  free(buffer);// libero l'array tmp, il buffer e chiudo il file
  
  
  if(spazio <= 0 || numeroFile <= 0 || SockName == NULL || numWorkers <= 0) 
  {
    fprintf(stderr, "config.txt errato\n");
    exit(EXIT_FAILURE);
  }
  //stampa di debug
  fprintf(stdout,"\n\nSpazio massimo: %d\n", spazio);
  fprintf(stdout,"Numero file massimi: %d\n", numeroFile);
  fprintf(stdout,"Nome socket: %s\n", SockName);
  fprintf(stdout,"numero thread Workers: %d\n", numWorkers);
  fprintf(stdout,"\n\n");
}

static void* threadF(void* arg) //funzione dei thread worker
{
  int numThread = *(int*)arg;
  fprintf(stdout, "Sono stato creato (numero thread:%d)\n", numThread); 
  while(1) 
  {
    Pthread_mutex_lock(&mutexQueueClient);
    while(queueClient->len == 0) 
    {
      fprintf(stderr, "sto dormendo! (numero thread:%d)\n", numThread);
      if(pthread_cond_wait(&condQueueClient, &mutexQueueClient) != 0) 
      { 
        perror("[Pthread_cond_wait]"); 
        exit(EXIT_FAILURE); 
      }
    }
    fprintf(stderr, "sono sveglio! (numero thread:%d)\n",numThread);
    
    ComandoClient* tmp = pop(&queueClient);//predo il comando da eseguire
    Pthread_mutex_unlock(&mutexQueueClient);

    if(tmp == NULL)
    {
      fprintf(stderr,"è vuoto tmp");
      continue;
    }  

    
    long connfd = tmp->connfd; // id del client che sta facendo richiesta 
    char comando = tmp->comando;
    char* parametro = tmp->parametro;

    fprintf(stderr,"\n\nquesto è il comando %c\n",tmp->comando);
    fprintf(stderr,"questo è l'argomento %s\n",tmp->parametro);
    fprintf(stderr,"questo è l'ID %ld\n\n",tmp->connfd);

    int notused;

    //POSSIBILI COMANDI PASSATI
    switch(comando) 
    {
      case 'W'://devo scrivere un file nel server 
      { 
        fprintf(stdout, "[Comando Scrittura]: %s Ricevuto\n", parametro);
        Pthread_mutex_lock(&mutexQueueFiles);
        //fprintf(stderr, "ho preso la lock\n");
        Node* esiste = fileExistsServer(queueFiles, parametro);
        //fprintf(stderr, "sto per lasciare la lock\n");
        Pthread_mutex_unlock(&mutexQueueFiles);

        int risposta = 0;
        if(esiste == NULL)//errore: ci sono stati errori nella ricerca/creazione file 
        { 
          risposta = -1;
          SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", "");//errore il file non esiste
        } 
        else//file è stato trovato/creato con successo
        { 
          fileRam *newFile = esiste->data;
          Pthread_mutex_lock(&newFile->lock);//alloco il file per evitare che qualcuno lo modifichi
          if(newFile->is_opened != connfd) 
          { //file è aperto momentaneamente da un altro client 
            risposta = -1;
          }
          SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", "");
        
          if(risposta != -1)
          {
            int lentmp;
            //fprintf(stderr, "sto scrivendo\n");
            SYSCALL_EXIT("readn", notused, readn(connfd, &lentmp, sizeof(int)), "read", "");//leggo la lunghezza del file 
              
            int cista = 1;//il file ci sta 
            if(lentmp > spazio || lentmp + newFile->length > spazio) //in caso che stia creando un file troppo grande o stia scrivendo troppe cose sul file per la capienza del server 
            {
              fprintf(stderr, "[Errore]:file %s troppo grande (%ld) (capienza massima %d)\n", newFile->nome, newFile->length, spazio);
                //vado a scrivere nel socket che il file non ci sta
              if(lentmp > spazio)
                removeFromQueue(&queueFiles, esiste);//rimuovo il file dalla coda perchè ho gia inserito il file
              cista = 0;
            }
          
            SYSCALL_EXIT("writen", notused, writen(connfd, &cista, sizeof(int)), "write", "");
            if(cista)
            {
              fileRam *fileramtmptrash;
              Pthread_mutex_lock(&mutexQueueFiles);
              while(spazioOccupato + lentmp > spazio)
              { //deve iniziare ad espellere file
                fprintf(stderr, "[Problema]: Spazio residuo nullo, inizio ad espellere dei file\n");
                fileRam *firstel = returnFirstEl(queueFiles);
                if(newFile != firstel)//devo evitare di rimuovere il file che sto cercando di inserire 
                { 
                  fileramtmptrash = pop(&queueFiles);
                } 
                else //rimuove il secondo elemento visto che il primo è l'elemento che sto inserendo
                { 
                  fileramtmptrash = pop2(&queueFiles);
                }
                fprintf(stderr, "[Problema]: Ho eliminato il file %s dal server\n", fileramtmptrash->nome);
                spazioOccupato-=fileramtmptrash->length;
                  //vanno fatte FREE del thrash
              }
              //sto aggiungendo il file perchè c'è abbastanza spazio 
              spazioOccupato+=lentmp;//aggiorno lo spazio occupato
              Pthread_mutex_unlock(&mutexQueueFiles);
              
              char* buftmp;//contenuto del file da leggere 
              ec_null((buftmp = malloc(sizeof(char) * lentmp)), "malloc");
              SYSCALL_EXIT("readn", notused, readn(connfd, buftmp, lentmp*sizeof(char)), "read", "");//leggo il contenuto del file
              
              if(newFile->buffer == NULL)//file appena creato, faccio la prima scrittura
              { 
                  newFile->length = lentmp;
                  newFile->buffer = buftmp;
              } 
              else
              {
                ec_null((newFile->buffer = realloc(newFile->buffer, sizeof(char) * (lentmp + newFile->length))), "realloc");
                for(int i = 0; i < lentmp; i++)
                  newFile->buffer[i + newFile->length] = buftmp[i];
                newFile->length+=lentmp;
              }
              
              risposta = 0;//successo  
              
              fprintf(stderr, "\n\nSTO STAMPANDO LA CODA\n");
              printQueueFiles(queueFiles);//debug
              fprintf(stderr, "\n\n");
                
              SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", ""); //scrivo nel client il risultato dell'operazione
            }
          }
          Pthread_mutex_unlock(&newFile->lock);
        }
        break;
      }
      case 'c'://remove file dalla coda
      {
        fprintf(stdout, "[Comando Rimozione]: %s Ricevuto\n", parametro);
        Pthread_mutex_lock(&mutexQueueFiles);//prendo il mutex per vedere se il file esiste e se posso rimuoverlo
        Node* esiste = fileExistsServer(queueFiles, parametro);
        
        int risposta;
        if(esiste == NULL)//in caso che non esistesse il file nel server
        {
          Pthread_mutex_unlock(&mutexQueueFiles);
          risposta = -1;//errore
          fprintf(stderr, "[Errore]: file %s inesistente\n", parametro);
        } 
        else//il file esiste nella coda
        {
          fileRam *tmpfileramtrash = esiste->data;
          if(tmpfileramtrash->is_opened != connfd) //file esiste ma non lo posso aprire perchè è gia aperto
          { 
            fprintf(stderr, "[Errore]: file %s gia aperto, non posso rimuoverlo\n", parametro);
            risposta = -1;
          } 
          else
          { //file esiste e lockato, deve essere rimosso
            spazioOccupato-= tmpfileramtrash->length;
            risposta = removeFromQueue(&queueFiles, esiste);
            fprintf(stderr, "[Comando Rimozione]: file rimosso %s Successo \n", parametro);
             
          }
          
          Pthread_mutex_unlock(&mutexQueueFiles);
          
        }
        fprintf(stdout, "\n\nRISULTATO RIMOZIONE file %s\n", parametro);
        printQueueFiles(queueFiles);// debug
        fprintf(stdout, "\n");
        SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", "");
        break;
      }
      case 'r': 
      {
        fprintf(stdout, "[Comando lettura]: %s Ricevuto\n", parametro);
        pthread_mutex_lock(&mutexQueueFiles);
        Node* esiste = fileExistsServer(queueFiles, parametro);
        int len; //RISPOSTA che ci dice se è tutto ok o no
        if(esiste != NULL)//se il file esiste 
        {
           fileRam *filetmp = esiste->data;
           Pthread_mutex_lock(&filetmp->lock);
           Pthread_mutex_unlock(&mutexQueueFiles);
           if(filetmp->is_opened == connfd) //file aperto da quel client
           { 
              len = filetmp->length;
              char* buf = filetmp->buffer;
              //fprintf(stderr, "sto inserendo %d\n", len);
              SYSCALL_EXIT("writen", notused, writen(connfd, &len, sizeof(int)), "write", "");
              SYSCALL_EXIT("writen", notused, writen(connfd, buf, len * sizeof(char)), "write", "");
              fprintf(stderr, "[Comando lettura]: %s Successo\n", parametro);
           }
           else
           {
              len = -1;//errore
              SYSCALL_EXIT("writen", notused, writen(connfd, &len, sizeof(int)), "write", "");
              fprintf(stderr, "[Comando lettura]: %s Errore Apertura\n", parametro);
           }
           Pthread_mutex_unlock(&filetmp->lock);
        } 
        else
        { //il file non è presente nel server
          Pthread_mutex_unlock(&mutexQueueFiles);
          len = -1;//risposta
          SYSCALL_EXIT("writen", notused, writen(connfd, &len, sizeof(int)), "write", "");
          fprintf(stderr, "[Errore]: %s file non trovato\n", parametro);
        }
        break;
      }
      case 'R': 
      {
        fprintf(stderr, "[Comando N letture]: Numero_File=%d Ricevuto\n\n", atoi(parametro));
        int numDaLeggere;//numero dei file da leggere

        if(atoi(parametro) > queueFiles->len)
          numDaLeggere = queueFiles->len;
        else
          numDaLeggere = atoi(parametro);
        if(numDaLeggere <= 0)
          numDaLeggere = queueFiles->len;
        SYSCALL_EXIT("writen", notused, writen(connfd, &numDaLeggere, sizeof(int)), "write", "");
        Node* nodetmp = queueFiles->head;
        fileRam *fileramtmp;
        char* buftmp;
        for(int i = 0; i < numDaLeggere; i++)//salvo i nomi dei file in un array per poi leggerli in un secondo momento 
        {
          fileramtmp = nodetmp->data;
          ec_null((buftmp = malloc(sizeof(char) * strlen(fileramtmp->nome))), "malloc");
          strcpy(buftmp, fileramtmp->nome);
          int buftmplen = strlen(buftmp);
          SYSCALL_EXIT("writen", notused, writen(connfd, &buftmplen, sizeof(int)), "write", "");
          SYSCALL_EXIT("writen", notused, writen(connfd, buftmp, buftmplen * sizeof(char)), "write", "");
          
          free(buftmp);
          nodetmp = nodetmp->next;
        }
        break;
      }
      case 'o': 
      { //openFile
        fprintf(stderr, "[Comando Apertura]: %s Ricevuto\n", parametro);
        int risposta;
        Pthread_mutex_lock(&mutexQueueFiles);
        Node* esiste = fileExistsServer(queueFiles, parametro);
        
        int flags;
        SYSCALL_EXIT("readn", notused, readn(connfd, &flags, sizeof(int)), "read", "");
        
        if(esiste == NULL && flags == 0) //deve aprire il file ma non esiste, errore
          risposta = -1;
        else
        { 
          if(esiste == NULL && flags == 1) 
          { //deve creare e aprire il file (che non esiste)
            if(queueFiles->len + 1 > numeroFile) 
            { //deve iniziare ad espellere file per il numero file
              fprintf(stderr, "[Problema]: Server pieno di numero \n");
              fileRam *fileramtmptrash = pop(&queueFiles);
              fprintf(stderr, "[Problema]: Sto liberando %s\n", fileramtmptrash->nome);

              //vanno fatte FREE
            }
            //creo un file vuoto
            fileRam *newFile;
            int LenName = strlen(basename(parametro));
            ec_null((newFile = malloc(sizeof(fileRam))), "malloc");
            if(pthread_mutex_init(&newFile->lock, NULL) != 0)//inizializzazione lock file con controllo errore
            {
               perror("pthread_mutex_init");
              exit(EXIT_FAILURE); 
            }
            ec_null((newFile->nome = malloc(sizeof(char) * (LenName + 1))), "malloc");
            //uso il base name per avere solo il nome del file senza path assoluto
            //inizializzazione del file
            strcpy(newFile->nome, basename(parametro));
            newFile->nome[LenName] = '\0';
            newFile->length = 0;
            newFile->buffer = NULL;
            newFile->is_opened = connfd;

            ec_meno1((push(&queueFiles, newFile)), "push");
            risposta = 0;//successo
          }
          else
          { 
            if(esiste != NULL && flags == 0)//devo aprire il file  
            { 
              fileRam *fileramtmp = esiste->data;
              if(fileramtmp->is_opened != -1)//il file è gia stato aperto da un altro client 
                risposta = -1;//quindi do errore perchè devo aspettare di trovarlo chiuso
              else//il file è chiuso allora lo posso aprire
              {
                fileramtmp->is_opened = connfd;
                risposta = 0;//successo
              }
            }
            else
            {
             if(esiste != NULL && flags == 1) 
             { //deve creare e aprire il file, che esiste già, errore
                risposta = -2;
             }
            }
          }
          Pthread_mutex_unlock(&mutexQueueFiles);
          SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", "");
          fprintf(stderr, "[Comando Apertura]: %s Successo\n", parametro);
          break;
        }   
      }
      case 'z': 
      { //closeFile
        int risposta;
        Pthread_mutex_lock(&mutexQueueFiles);
        Node* esiste = fileExistsServer(queueFiles, parametro);
        
        if(esiste == NULL)//file non trovato
        { 
          Pthread_mutex_unlock(&mutexQueueFiles);
          risposta = -1;
          //fprintf(stderr, "il file non esiste!!\n");
        } 
        else //file trovato da chiudere
        {  //fprintf(stderr, "sono qui\n");
          fileRam *fileramtmp = esiste->data;
          Pthread_mutex_lock(&fileramtmp->lock);
          if(fileramtmp->is_opened != connfd)//il file è gia aperto da un altro client 
          { 
            risposta = -1;
            fprintf(stderr, "[Errore]: file %s gia aperto\n",parametro);
          } 
          else//chiudo il file
          { 
            fileramtmp->is_opened = -1;
            risposta = 0; //successo
            //fprintf(stderr, "File chiuso con successo\n");
          }
          Pthread_mutex_unlock(&fileramtmp->lock);
          Pthread_mutex_unlock(&mutexQueueFiles);
        }
        SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", "");
        break;
      }
    }
    FD_SET(connfd, &set);
    //fprintf(stderr, "num thread %d, connfd %ld\n", *numThread, connfd);
    SYSCALL_EXIT("writen", notused, writen(p[numThread][1], "vai", 3), "write", "");
  }
    
  return NULL;
}

int main(int argc, char* argv[]) 
{
  
  parserFile();      //prendo le informazioni dal file config.txt
  numWorkers = 2;
  cleanup();    //ripulisco vecchie connessioni 
  atexit(cleanup);
  queueClient = initQueue(); //coda dei file descriptor dei client che provano a connettersi
  queueFiles = initQueue();

  ec_null((p = (int**)malloc(sizeof(int*) * numWorkers)), "malloc"); //array delle pipe
  int *arrtmp;//array del numero identificativo del thread 
  ec_null((arrtmp = malloc(sizeof(int) * numWorkers)), "malloc");
  pthread_t *t;
  ec_null((t = malloc(sizeof(pthread_t) * numWorkers)), "malloc");//array dei thread
  for(int i = 0; i < numWorkers; i++) 
  {
    arrtmp[i] = i;
    if(pthread_create(&t[i], NULL, threadF, (void*)&(arrtmp[i])) != 0)
    {
        fprintf(stderr, "[Errore]:pthread_create failed server\n");
        exit(EXIT_FAILURE);
    }

    ec_null((p[i] = (int*)malloc(sizeof(int) * 2)), "malloc");
    ec_meno1((pipe(p[i])),"pipe");
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
    ec_meno1((select(fdmax+1, &tmpset, NULL, NULL, NULL)), "select");

// cerchiamo di capire da quale fd abbiamo ricevuto una richiesta
    for(int i=0; i <= fdmax; i++) 
    {
      if (FD_ISSET(i, &tmpset)) 
      {
        long connfd;
        if (i == listenfd) 
        { // e' una nuova richiesta di connessione
          SYSCALL_EXIT("accept", connfd, accept(listenfd, (struct sockaddr*)NULL ,NULL), "accept", "");
          //fprintf(stderr,"il server ha accettato la connessione con %ld\n",connfd);
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
          //fprintf(stderr, "è una pipe\n");
          char buftmp[4];
          SYSCALL_EXIT("readn", notused, readn(connfd, buftmp, 3), "read", "");
          //fprintf(stderr,"leggo buf %s dal pipe\n",buftmp);
          continue;
        }
          
        FD_CLR(connfd, &set);//levo il bit nella set perchè sto gestendo la richiesta di connfd
        
        msg_t str;
        int err = readn(connfd, &str.len, sizeof(int));
        
        if(err == 0) //socket vuoto
        {
          fprintf(stderr, "client disconnesso\n");
          
          FD_CLR(connfd, &set);//libero il bit sulla maschera per fare spazio e chiudo il socket
          ec_meno1((close(connfd)), "close");
          
          if (connfd == fdmax)
            fdmax = updatemax(set, fdmax);
          continue;
        }
        if (err<0) { fprintf(stderr, "[Errore]:lettura socket vuoto\n"); }
        
        //se effettivamente ho letto qualcosa nel socket identifico il comando da mettere in coda
        str.len = str.len - sizeof(char);
        //leviamo il primo carattere del comando
        SYSCALL_EXIT("readn", notused, readn(connfd, &str.comando, sizeof(char)), "read", "");
        fprintf(stderr, "stampo il comando %c \n",str.comando);//leggiamo il flag del comando per esempio 'W'
        ec_null((str.arg = calloc((str.len), sizeof(char))), "calloc");
        
        SYSCALL_EXIT("readn", notused, readn(connfd, str.arg, (str.len)*sizeof(char)), "read", "");//leggiamo l'argomento del comando da mettere in coda
        
        ComandoClient *cmdtmp;
        ec_null((cmdtmp = malloc(sizeof(ComandoClient))), "malloc");
        cmdtmp->comando = str.comando;
        ec_null((cmdtmp->parametro = malloc(sizeof(char) * (strlen(str.arg)+1))), "malloc");
        cmdtmp->connfd = connfd;
        strcpy(cmdtmp->parametro, str.arg);
        cmdtmp->parametro[strlen(str.arg)] = '\0';

        Pthread_mutex_lock(&mutexQueueClient);
        ec_meno1((push(&queueClient, cmdtmp)), "push");
        Pthread_mutex_unlock(&mutexQueueClient);
        if(pthread_cond_signal(&condQueueClient) != 0) { perror("pthread_cond_signal"); exit(EXIT_FAILURE); }
      }
    }
  }
}