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
#include <signal.h> //segnali

#include "queue.h"
#include "util.h"

#define MAXBUFFER 1000
#define MAXSTRING 100
#define SPAZIO "spazio"
#define NUMEROFILE "numeroFile"
#define SOC "sockName"
#define WORK "numWorkers"
#define MAXBACKLOG 32
#define BYTETOMEGA 1048576

int spazio = 0;
int numeroFile = 0; //informazioni del file config
int numWorkers = 0;
char* SockName = NULL;

//statistiche fine esecuzione
typedef struct _stats 
{
  int FileMaxMemorizzati;
  double spazioMaxOccupato;
  int numSceltaVittime;
  pthread_mutex_t LockStats;
} Statistiche;
Statistiche *s;

fd_set set;         //maschera dei bit
Queue *queueClient; //coda dei client che fanno richieste
Queue *queueFiles;  //coda dei file memorizzati

int spazioOccupato = 0;

int **p;            //array di pipe

static pthread_mutex_t mutexQueueClient = PTHREAD_MUTEX_INITIALIZER;
static pthread_mutex_t mutexQueueFiles = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condQueueClient = PTHREAD_COND_INITIALIZER;

//flag segnali
int flagSigInt;//SIGINT e SIGQUIT hanno il comportamento medersimo
int flagSigHup;//SIGHUP
int* psegnali;//pipe dei segnali


void cleanup() //cancellare il collegamento
{ 
  unlink(SockName);
  free(SockName);
}

void parserFile(char* pathConfig) //parser del file config.txt
{   
  int i;
  char* save;
  char* token;

  char* buffer;
  ec_null((buffer = malloc(sizeof(char) * MAXBUFFER)), "malloc");
  FILE* p;

  ec_null((p = fopen(pathConfig, "r")), "fopen");//Apro il file config in lettura per ottenere i valori di partenza del server

  while(fgets(buffer, MAXBUFFER, p)) //per ogni riga del file
  {
    save = NULL;
    buffer[strlen(buffer) - 1] = '\0';//sto levando il carattere (accapo)
    token = strtok_r(buffer, " ",  &save);
    char** tmp = NULL;
    ec_null((tmp = malloc(sizeof(char*) * 2)), "malloc");
    tmp[0] = NULL;//inizializzazione della variabile del nome dei parametri
    tmp[1] = NULL;//inizializzazione della variabile dell'argomento dei parametri
    i = 0;        //indice dell'array tmp per salvare l'argomento della variabile di interesse
    
    while(token) 
    {
      ec_null((tmp[i] = malloc(sizeof(char) * MAXSTRING)), "malloc");
      strcpy(tmp[i], token); 
      token = strtok_r(NULL, " ", &save);
      i++;
    }

    if(strcmp(tmp[0], SPAZIO) == 0) //controllo se il file è formatatto nel modo corretto 
    {                               //esempio:spazio 100
      if(isNumber(tmp[1],&spazio) != 0) //spazio massimo dedicato
      {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
      } 
    }
    if(strcmp(tmp[0], NUMEROFILE) == 0)//numero massimo di file
    {
      if(isNumber(tmp[1], &numeroFile) != 0) 
      {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
      }
    }
    if(strcmp(tmp[0], SOC) == 0)//nome del socket
    {
      int LenSockName = strlen(tmp[1]);
      ec_null((SockName = malloc(sizeof(char) * (LenSockName+1))), "malloc");
      strcpy(SockName, tmp[1]);
      SockName[LenSockName] ='\0';
    }
    if(strcmp(tmp[0], WORK) == 0)//numero di thread worker
    {
      if(isNumber(tmp[1],&numWorkers) != 0) 
      {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
      }
    }

  free(tmp[0]);
  free(tmp[1]);
  free(tmp);
  }
  if(fclose(p) != 0)//chiudo il file
  { 
    perror("fclose"); 
    exit(EXIT_FAILURE); 
  } 
  free(buffer);// libero l'array tmp, il buffer e chiudo il file
  
  
  if(spazio <= 0 || numeroFile <= 0 || SockName == NULL || numWorkers <= 0)//se ci sono dei parametri che non sono corretti
  {
    fprintf(stderr, "config.txt errato\n");
    exit(EXIT_FAILURE);
  }
  float cambioSpazio = spazio / BYTETOMEGA;

  //stampa di debug
  fprintf(stdout,"\n\n-Spazio massimo: %.2f MB\n", cambioSpazio);
  fprintf(stdout,"-Numero file massimi: %d\n", numeroFile);
  fprintf(stdout,"-Nome socket: %s\n", SockName);
  fprintf(stdout,"-Numero thread Workers: %d\n", numWorkers);
  fprintf(stdout,"\n\n");
}

static void* threadF(void* arg) //funzione dei thread worker
{
  int numThread = *(int*)arg;
  fprintf(stdout, "[Thread]: Sono stato creato (numero thread:%d)\n", numThread);//debug
  while(1) 
  {
    Pthread_mutex_lock(&mutexQueueClient);
    while(queueClient->len == 0 && !flagSigInt)//in caso che il thread stia aspettando si sveglia per poi terminare
    {
      fprintf(stdout, "[Thread]: Sto dormendo! (numero thread:%d)\n", numThread);
      if(pthread_cond_wait(&condQueueClient, &mutexQueueClient) != 0) 
      { 
        perror("[Pthread_cond_wait]"); 
        exit(EXIT_FAILURE); 
      }
    }
    fprintf(stderr, "[Thread]: Sono sveglio! (numero thread:%d)\n",numThread);
    
    if(flagSigInt == 1)//se ho ricevuto un segnale ultimamente devo lasciare la coda ed abbandonare
    {
      Pthread_mutex_unlock(&mutexQueueClient);
      fprintf(stdout, "[Segnale]: Sono il thread %d e ho ricevuto un segnale\n", numThread);
      break;
    }
    ComandoClient* tmp = pop(&queueClient);//predo il comando da eseguire
    Pthread_mutex_unlock(&mutexQueueClient);

    if(tmp == NULL)//se la pop avesse avuto dei problemi 
    {
      fprintf(stderr,"[Errore]: è vuoto tmp\n");
      continue;
    }  

    //parametri del comando estratto dalla coda
    long connfd = tmp->connfd; // id del client che sta facendo richiesta 
    char comando = tmp->comando;//flag del comando per esempio: W
    char* parametro = tmp->parametro;//argomento del comando

    //debug
    fprintf(stderr,"\n\nRichiesta ricevuta:\n");
    fprintf(stderr,"[Server]: %c è il comando\n",tmp->comando);
    fprintf(stderr,"[Server]: %s è l'argomento\n",tmp->parametro);
    fprintf(stderr,"[Server]: %ld è l'ID\n\n",tmp->connfd);

    int notused;

    //come vengono gestiti i vari comandi
    switch(comando) 
    {
      case 'W'://devo scrivere un file nel server 
      { 
        fprintf(stdout, "[Comando Scrittura]: %s Ricevuto\n", parametro);
        Pthread_mutex_lock(&mutexQueueFiles);

        Node* esiste = fileExistsServer(queueFiles, parametro);//controllo se il file estite o meno nel server
        Pthread_mutex_unlock(&mutexQueueFiles);
        int cista = 1;//il file ci sta inizialmente
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
          if(newFile->is_opened != connfd)//file è aperto momentaneamente da un altro client quindi non può essere modificato
          {  
            risposta = -1;
          }
          SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", "");//scrivo la risposta
        
          if(risposta != -1)//se non ci sono stati dei problemi
          {
            size_t lentmp;
            SYSCALL_EXIT("readn", notused, readn(connfd, &lentmp, sizeof(size_t)), "read", "");//leggo la lunghezza del contenuto del file 
              
            if(lentmp > spazio || lentmp + newFile->length > spazio) //in caso che stia creando un file troppo grande o stia scrivendo troppe cose sul file per la capienza del server 
            {
              fprintf(stderr, "[Errore]:file %s troppo grande (%ld) (capienza massima %d)\n", newFile->nome, newFile->length + lentmp, spazio);
              
              if(lentmp > spazio)
                if(removeFromQueue(&queueFiles, esiste) != 1)//rimuovo il file dalla coda perchè ho gia inserito il file
                {
                  fprintf(stderr,"[Errore]: Il file %s non è stato rimosso",newFile->nome);
                }
              //libero il file tanto non mi servirà perchè non posso inserirlo 
              free(newFile->nome);
              Pthread_mutex_unlock(&newFile->lock);
              free(newFile);
              cista = 0;
            }
          
            SYSCALL_EXIT("writen", notused, writen(connfd, &cista, sizeof(int)), "write", "");//scrivo al client se il file può essere inserito o meno
            if(cista)
            {
              fileRam *fileramtmptrash;
              Pthread_mutex_lock(&mutexQueueFiles);
              
              Pthread_mutex_lock(&s->LockStats);
              if(spazioOccupato + lentmp > spazio)
                s->numSceltaVittime++;
              Pthread_mutex_unlock(&s->LockStats);
              
              while(spazioOccupato + lentmp > spazio)//Espello file fino a quando non ne ho abbastanza per inserire il nuovo file
              { 
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
                free(fileramtmptrash->nome);
                if(fileramtmptrash->buffer != NULL)
                  free(fileramtmptrash->buffer);
                free(fileramtmptrash);
              }
              //sto aggiungendo il file perchè c'è abbastanza spazio 
              spazioOccupato+=lentmp;//aggiorno lo spazio occupato
              Pthread_mutex_unlock(&mutexQueueFiles);
              
              char* ContenutoFile;//contenuto del file da leggere 
              ec_null((ContenutoFile = malloc(sizeof(char) * lentmp)), "malloc");
              SYSCALL_EXIT("readn", notused, readn(connfd, ContenutoFile, lentmp*sizeof(char)), "read", "");//leggo il contenuto del file
              
              if(newFile->buffer == NULL)//file appena creato, faccio la prima scrittura
              { 
                  newFile->length = lentmp;
                  newFile->buffer = ContenutoFile;
              } 
              else//sto scrivendo in append
              {
                ec_null((newFile->buffer = realloc(newFile->buffer, sizeof(char) * (lentmp + newFile->length))), "realloc");
                for(int i = 0; i < lentmp; i++) //aggiunta in append
                  newFile->buffer[i + newFile->length] = ContenutoFile[i];
                newFile->length+=lentmp;
                free(ContenutoFile);
              }
              
              risposta = 0;//successo  
              
              //aggiorno le statistiche del server
              Pthread_mutex_lock(&s->LockStats);
              if(queueFiles->len > s->FileMaxMemorizzati)
                s->FileMaxMemorizzati = queueFiles->len;
              if(spazioOccupato > s->spazioMaxOccupato)
                s->spazioMaxOccupato = spazioOccupato;
              Pthread_mutex_unlock(&s->LockStats);
              
              //fprintf(stderr, "\n\nSTO STAMPANDO LA CODA\n");
              //printQueueFiles(queueFiles);//debug
              //fprintf(stderr, "\n\n");
                
              SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", ""); //scrivo nel client il risultato dell'operazione
            }
          }
          if(cista != 0)//se il file non è stato inserito devo liberare il mutex del file
            Pthread_mutex_unlock(&newFile->lock);
        }
        break;
      }
      case 'c'://remove file dalla coda
      {
        fprintf(stdout, "[Comando Rimozione]: %s Ricevuto\n", parametro);
        Pthread_mutex_lock(&mutexQueueFiles);//prendo il mutex per vedere se il file esiste e se posso rimuoverlo
        Node* esiste = fileExistsServer(queueFiles, parametro);//controllo se il file esiste, perchè in caso contrario non potrei rimuoverlo
        
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
          else//file esiste ed è aperto, deve essere rimosso
          { 
            spazioOccupato-= tmpfileramtrash->length;
            risposta = removeFromQueue(&queueFiles, esiste);//rimuovo il file
            fprintf(stderr, "[Comando Rimozione]: file rimosso %s Successo \n", parametro);
            
            //liberazione del file trash
            free(tmpfileramtrash->nome);
            if(tmpfileramtrash->buffer != NULL)
              free(tmpfileramtrash->buffer);
            free(tmpfileramtrash); 
          }
          
          Pthread_mutex_unlock(&mutexQueueFiles);
          
        }
        // debug
        fprintf(stdout, "\n\nRISULTATO RIMOZIONE file %s\n", parametro);
        printQueueFiles(queueFiles);
        fprintf(stdout, "\n");
        SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", "");//scrivo il risultato dell'operazione
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
              SYSCALL_EXIT("writen", notused, writen(connfd, &len, sizeof(int)), "write", "");//scrittura lunghezza contenuto file
              SYSCALL_EXIT("writen", notused, writen(connfd, buf, len * sizeof(char)), "write", "");//scrittura contenuto del file
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
        else //il file non è presente nel server
        {
          Pthread_mutex_unlock(&mutexQueueFiles);
          len = -1;//risposta
          SYSCALL_EXIT("writen", notused, writen(connfd, &len, sizeof(int)), "write", "");//risposta al client 
          fprintf(stderr, "[Errore]: %s file non trovato\n", parametro);
        }
        break;
      }
      case 'R': 
      {
        fprintf(stderr, "[Comando N letture]: Numero_File=%d Ricevuto\n\n", atoi(parametro));
        int numDaLeggere;//numero dei file da leggere

        //controllo del vero numero di file da leggere
        if(atoi(parametro) > queueFiles->len)
          numDaLeggere = queueFiles->len;
        else
          numDaLeggere = atoi(parametro);
        if(numDaLeggere <= 0)
          numDaLeggere = queueFiles->len;
        SYSCALL_EXIT("writen", notused, writen(connfd, &numDaLeggere, sizeof(int)), "write", "");
        Node* nodetmp = queueFiles->head;
        fileRam *fileramtmp;
        char* nomeFileTmp; //mi serve salvare i nomi dei file
        for(int i = 0; i < numDaLeggere; i++)//salvo i nomi dei file in un array per poi leggerli in un secondo momento 
        {
          fileramtmp = nodetmp->data;
          int nomeFileTmpLen = strlen(fileramtmp->nome);
          ec_null((nomeFileTmp = malloc(sizeof(char) * (nomeFileTmpLen+1))), "malloc");
          strcpy(nomeFileTmp, fileramtmp->nome);
          nomeFileTmp[nomeFileTmpLen] = '\0' ;

          SYSCALL_EXIT("writen", notused, writen(connfd, &nomeFileTmpLen, sizeof(int)), "write", "");
          SYSCALL_EXIT("writen", notused, writen(connfd, nomeFileTmp, nomeFileTmpLen * sizeof(char)), "write", "");//scrittura nome file
          
          free(nomeFileTmp);
          nodetmp = nodetmp->next;
        }
        break;
      }
      case 'o'://apertura del file 
      { 
        fprintf(stderr, "[Comando Apertura]: %s Ricevuto\n", parametro);
        int risposta;
        Pthread_mutex_lock(&mutexQueueFiles);
        Node* esiste = fileExistsServer(queueFiles, parametro);//verifico se il file esiste 
        
        int flags;
        SYSCALL_EXIT("readn", notused, readn(connfd, &flags, sizeof(int)), "read", "");//lettura dei flag
        
        if(esiste == NULL && flags == 0) //deve aprire il file ma non esiste, errore
          risposta = -1;
        else
        { 
          if(esiste == NULL && flags == 1) //deve creare e aprire il file (che non esiste)
          { 
            if(queueFiles->len + 1 > numeroFile) //deve iniziare ad espellere file per il numero file
            { 
              fprintf(stderr, "[Problema]: Server pieno di numero \n");
              fileRam *fileramtmptrash = pop(&queueFiles);
              fprintf(stderr, "[Problema]: Sto liberando %s\n", fileramtmptrash->nome);
              spazioOccupato-=fileramtmptrash->length;
              //libero il file 
              free(fileramtmptrash->nome);
              free(fileramtmptrash->buffer);
              free(fileramtmptrash);
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
             if(esiste != NULL && flags == 1) //deve creare e aprire il file, che esiste già, errore
             { 
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
      case 'z'://chiusura del file 
      {
        int risposta;
        Pthread_mutex_lock(&mutexQueueFiles);
        Node* esiste = fileExistsServer(queueFiles, parametro);//vedo se il file esiste
        
        if(esiste == NULL)//file non trovato
        { 
          Pthread_mutex_unlock(&mutexQueueFiles);
          risposta = -1;
        } 
        else //file trovato da chiudere
        {
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
          }
          Pthread_mutex_unlock(&fileramtmp->lock);
          Pthread_mutex_unlock(&mutexQueueFiles);
        }
        SYSCALL_EXIT("writen", notused, writen(connfd, &risposta, sizeof(int)), "write", "");
        break;
      }
    }
    FD_SET(connfd, &set);
    SYSCALL_EXIT("writen", notused, writen(p[numThread][1], "vai", 3), "write", "");//scrivo nella pipe del thread per risvegliare la select
    free(tmp->parametro);
    free(tmp);
  }
  fprintf(stderr, "[Thread (%d)]: ho finito \n", numThread); //quando il thread deve morire 
  return NULL;
}

static void* tSegnali(void* arg)//thread per la gestione dei segnali
{
  if(arg == NULL)//argomenti non validi
  {
    errno = EINVAL;
    perror("tSegnali");
    exit(EXIT_FAILURE);
  }
  //riassunto segnali da gestire
  //control + C = segnale 2, SIGINT
  //control + \ = segnale 3, SIGQUIT
  //SIGINT uguale a SIGQUIT
  //kill -1 pid = segnale 1, SIGHUP

  sigset_t *mask = (sigset_t*)arg;
  fprintf(stdout, "[Thread Gestore Segnali]: Sono stato creato\n");
  int idSegnale;
  sigwait(mask, &idSegnale);
  fprintf(stdout, "[Thread Gestore Segnali]: Ho ricevuto il segnale %d\n", idSegnale);
  if(idSegnale == SIGINT || idSegnale == SIGQUIT) //gestione SIGINT e SIGQUIT
  { 
    flagSigInt = 1;
  } 
  else 
  {
    if(idSegnale == SIGHUP) //gestione SIGHUP
    { 
      flagSigHup = 1;
    } 
    else//nessun segnale riconosciuto 
    {
      tSegnali(arg);
      return NULL;
    }
  } 
  int notused;
  SYSCALL_EXIT("writen", notused, writen(psegnali[1], "catturato", 9), "write", "");//scrivo nella pipe dei segnali
  fprintf(stdout, "[tSegnale]: ho ricevuto il segnale sto uscendo\n");
  return NULL;
}

int main(int argc, char* argv[]) 
{
   if(argc != 2)//in caso che non ci sia il parametro del config
   {
      fprintf(stderr, "[Errore Server]: devi passare il path di un file config adeguato\n");
      exit(EXIT_FAILURE); 
   }
  parserFile(argv[1]);      //prendo le informazioni dal file config.txt
  
  
  //inizializzo statistiche del server
  ec_null((s = malloc(sizeof(Statistiche))), "malloc");
  if(pthread_mutex_init(&s->LockStats, NULL) != 0) { perror("pthread_mutex_init"); exit(EXIT_FAILURE); }
  Pthread_mutex_lock(&s->LockStats);
  s->FileMaxMemorizzati = 0;
  s->spazioMaxOccupato = 0;
  s->numSceltaVittime = 0;
  Pthread_mutex_unlock(&s->LockStats);

  
  
  //GESTIONE SEGNALI
  flagSigInt = 0; //inizialmente non ho ricevuto un segnale SIGINT e SIGQUIT
  flagSigHup = 0; //inizialmente non ho ricevuto un segnale SIGHUP
  struct sigaction sahup;
  struct sigaction saquit;
  struct sigaction saint;
  memset(&sahup, 0, sizeof(sigaction));
  memset(&saquit, 0, sizeof(sigaction));
  memset(&saint, 0, sizeof(sigaction));
  sigset_t mask;//maschera dei segnali
  sigemptyset(&mask);//inizializzazione
  //aggiunta dei 3 segnali nella maschera
  sigaddset(&mask, SIGHUP);
  sigaddset(&mask, SIGQUIT);
  sigaddset(&mask, SIGINT);
  if(pthread_sigmask(SIG_SETMASK, &mask, NULL) != 0) { perror("pthread_sigmask"); exit(EXIT_FAILURE); }
  //creazione del thread che gestirà i segnali
  pthread_t tGestoreSegnali;
  if(pthread_create(&tGestoreSegnali, NULL, tSegnali, (void*)&mask) != 0) { perror("pthread_sigmask"); exit(EXIT_FAILURE); }
  //creazione pipe per i segnali
  ec_null((psegnali = (int*)malloc(sizeof(int) * 2)), "malloc");
  ec_meno1((pipe(psegnali)), "pipe");
  
  queueClient = initQueue(); //coda delle richieste dei client
  queueFiles = initQueue();  //coda dei file contenuti nel server

  ec_null((p = (int**)malloc(sizeof(int*) * numWorkers)), "malloc"); //array delle pipe
  int *arrtmp;//array del numero identificativo del thread 
  ec_null((arrtmp = malloc(sizeof(int) * numWorkers)), "malloc"); //array per passare i numeri ai thread worker
  pthread_t *t;
  ec_null((t = malloc(sizeof(pthread_t) * numWorkers)), "malloc");//array dei thread
  for(int i = 0; i < numWorkers; i++) 
  {
    arrtmp[i] = i;
    if(pthread_create(&t[i], NULL, threadF, (void*)&(arrtmp[i])) != 0)//creazione del thread worker
    {
        fprintf(stderr, "[Errore]:pthread_create failed server\n");
        exit(EXIT_FAILURE);
    }

    ec_null((p[i] = (int*)malloc(sizeof(int) * 2)), "malloc");//creazione della pipe associata al thread worker
    ec_meno1((pipe(p[i])),"pipe");
  }

  int listenfd; //codice identificato del listen per accettare le nuove connessioni
  int nattivi = 0;
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
  
  //aggiungo alla set il bit per la pipe dei segnali
  FD_SET(psegnali[0], &set);
  if(psegnali[0] > fdmax)//in caso aggiorno fdmax
    fdmax = psegnali[0];

  
  
  while(!flagSigInt)//appena riceverà il comando terminerà il prima possibile
  {
    
    tmpset = set; // copio il set nella variabile temporanea per la select
    ec_meno1((select(fdmax+1, &tmpset, NULL, NULL, NULL)), "select");

    // cerchiamo di capire da quale fd abbiamo ricevuto una richiesta
    for(int i=0; i <= fdmax; i++) 
    {
      if (FD_ISSET(i, &tmpset)) 
      {
        long connfd;
        if (i == listenfd) // e' una nuova richiesta di connessione
        { 
          SYSCALL_EXIT("accept", connfd, accept(listenfd, (struct sockaddr*)NULL ,NULL), "accept", "");
          FD_SET(connfd, &set);  // aggiungo il descrittore al master set
          nattivi++;
          if(connfd > fdmax)
            fdmax = connfd;  // ricalcolo il massimo
          continue;
        }
        //Ramo ELSE
        //devo controllare se la select si è risvegliata per una pipe 
        //oppure per una nuova richiesta

        connfd = i;         //client gia connesso con una nuova richiesta 

        //gestione segnali
        if(connfd == psegnali[0])//Prima di fare qualsiasi cosa controllo se ho ricevuto un segnale 
        { 
          fprintf(stderr, "[Server]: Ho ricevuto segnale flagSigHup: %d flagSigInt/flagSigQuit: %d nel main\n", flagSigHup, flagSigInt);
          char buftmp[10];
          //leggo dalla pipe 
          SYSCALL_EXIT("readn", notused, readn(connfd, buftmp, 9), "read", ""); //leggo dalla pipe dei segnali
          if(flagSigInt == 1 || nattivi == 0)//se sono finite le richieste o se ho catturato un segnale
          {
            flagSigInt = 1;
            if(pthread_cond_broadcast(&condQueueClient) != 0)//mando un broadcast per avvertire tutti i thread workers 
            { 
              perror("pthread_cond_broadcast"); 
              exit(EXIT_FAILURE); 
            }
          }
          
          if(flagSigHup) 
          {
            FD_CLR(listenfd, &set);//cancello il listenfd dalla set per proibire nuove connessioni da nuovi client
            ec_meno1((close(listenfd)), "close");//chiusura del listenfdS
          }
          continue;
        }

        if(isPipe(numWorkers,connfd,p)) //controllo se è una pipe
        {
          char buftmp[4];
          SYSCALL_EXIT("readn", notused, readn(connfd, buftmp, 3), "read", "");//leggo dal pipe 
          continue;
        }
          
        FD_CLR(connfd, &set);//levo il bit nella set perchè sto gestendo la richiesta di connfd
        //struttura di supporto per la lettura del socket
        msg_t str;
        int err = readn(connfd, &str.len, sizeof(int));//leggo la lunghezza del messaggio nel socket
        
        if(err == 0 || err == -1) //se il socket fosse vuoto
        {
          fprintf(stderr, "[Server]: Client disconnesso\n");
          nattivi--;//cancello una connessione dal contatore delle connessioni attive
          FD_CLR(connfd, &set);//libero il bit sulla maschera per fare spazio e chiudo il socket
          ec_meno1((close(connfd)), "[close]");//chiudo il socket per quel determinato client 
          
          if (connfd == fdmax)//ricalcolo il massimo fdMax
            fdmax = updatemax(set, fdmax);


          if(nattivi > 0)//controllo il numero dei client ancora connessi
          {
            if(flagSigHup)
              fprintf(stderr, "[Problema]: Ci sono connessioni attive N = %d\n", nattivi); //errore in caso che qualche client non termini la propria connessione
          } 
          else 
          {
            if(flagSigHup) 
            {
              fprintf(stderr, "[Server]: Non ci sono connessioni attive\n"); //successo
              flagSigInt = 1; //riutilizzo il flagSigInt visto che ho un comportamento similare 
              if(pthread_cond_broadcast(&condQueueClient) != 0) 
              {
                 perror("[pthread_cond_broadcast]");
                  exit(EXIT_FAILURE); 
              }
            }
          }
          continue;
        }
        if (err<0) { fprintf(stderr, "[Errore]:lettura socket vuoto\n"); }
        
        //se effettivamente ho letto qualcosa nel socket identifico il comando da mettere in coda
        str.len = str.len - sizeof(char);
        //leviamo il primo carattere del comando dal conto della lunghezza
        SYSCALL_EXIT("readn", notused, readn(connfd, &str.comando, sizeof(char)), "read", "");//leggiamo il flag del comando per esempio 'W'
        ec_null((str.arg = calloc((str.len), sizeof(char))), "calloc");
        
        SYSCALL_EXIT("readn", notused, readn(connfd, str.arg, (str.len)*sizeof(char)), "read", "");//leggiamo l'argomento del comando da mettere in coda
        
        //creazione del comando da mettere in coda
        ComandoClient *cmdtmp;
        ec_null((cmdtmp = malloc(sizeof(ComandoClient))), "malloc");
        cmdtmp->comando = str.comando;
        ec_null((cmdtmp->parametro = malloc(sizeof(char) * (strlen(str.arg)+1))), "malloc");
        cmdtmp->connfd = connfd;
        strcpy(cmdtmp->parametro, str.arg);
        cmdtmp->parametro[strlen(str.arg)] = '\0';
        
        free(str.arg);
        Pthread_mutex_lock(&mutexQueueClient);
        ec_meno1((push(&queueClient, cmdtmp)), "push");
        Pthread_mutex_unlock(&mutexQueueClient);
        if(pthread_cond_signal(&condQueueClient) != 0) { perror("pthread_cond_signal"); exit(EXIT_FAILURE); }
      }
    }
  }
  //Quando esco dal while faccio le free mancanti
  //e termino tutti i thread
  //do modo ai thread di terminare
  for(int i = 0; i < numWorkers; i++)
  {  if(pthread_join(t[i], NULL) != 0) 
    { 
      perror("pthread_join_Worker"); 
      exit(EXIT_FAILURE); 
    }
  }
  if(pthread_join(tGestoreSegnali, NULL) != 0) 
  { 
    perror("pthread_join_Segnali"); 
    exit(EXIT_FAILURE); 
  }

  //chiusura connessioni

  //chiudo le pipe dei worker, le cancello dalla set e le libero
  for(int i = 0; i < numWorkers; i++) 
  {
    ec_meno1((close(p[i][0])), "close");
    ec_meno1((close(p[i][1])), "close");
    FD_CLR(p[i][0], &set);
    free(p[i]);
  }
  free(p);
  //chiudo la pipe dei segnali, la cancello dalla set e la libero
  FD_CLR(psegnali[0], &set); 
  ec_meno1((close(psegnali[0])), "close");
  ec_meno1((close(psegnali[1])), "close");
  free(psegnali);

  //chiusura connessioni attive
  for(int i = fdmax; i >= 0; --i) 
  {
    if(FD_ISSET(i, &set)) 
    {
      fprintf(stderr, "[Server]: Chiudo la connesione %d\n", i);
      ec_meno1((close(i)), "close");
    }
  }
  cleanup();

  //stampo le statistiche
  fprintf(stdout, "\n\nStatistiche del server raggiunte:\n");
  fprintf(stdout, "Numero massimo di File caricati sul Server: %d\n", s->FileMaxMemorizzati);
  fprintf(stdout, "Numero massimo di Spazio occupato sul Server: %.2f MB\n", s->spazioMaxOccupato / BYTETOMEGA);
  fprintf(stdout, "Numero di volte che ho scelto le vittime: %d\n", s->numSceltaVittime);
  fprintf(stdout, "\nCoda File Server:\n");
  printQueueFiles(queueFiles);
  fprintf(stdout, "\n");

  free(arrtmp); //free array numero thread
  free(t); //free array di thread
  free(s); //free delle statistiche 

  //free della coda dei file
  fileRam* tmpcodafile;
  do
  {
    tmpcodafile = pop(&queueFiles);
    if(tmpcodafile != NULL)
    {
      free(tmpcodafile->nome);
      free(tmpcodafile->buffer);
      free(tmpcodafile);
    }
  }while(tmpcodafile != NULL);
  free(queueFiles);

  //free della coda dei client (nel caso del SIGINT/SIGQUIT)
  ComandoClient* tmpcodacomandi;
  do
  {
    tmpcodacomandi = pop(&queueClient);
    if(tmpcodafile != NULL)
    {
      free(tmpcodacomandi->parametro);
      free(tmpcodacomandi);
    }
  }while(tmpcodacomandi != NULL);
  free(queueClient);

  //chiusura socket, pipe
}