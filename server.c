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

Queue *queueClient; //coda dei client che fanno richieste

static pthread_mutex_t mutexQueueClient = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t condQueueClient = PTHREAD_COND_INITIALIZER;

typedef struct _file {
  char* nome;
  long size;
  char* buffer; //contenuto
  long length; //per fare la read e la write, meglio se memorizzato
} File;


void cleanup() { //cancellare il collegamento
  unlink(SockName);
}

void parser(void) {
  char* a = NULL;
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
    //ora facciamo il parser della singola riga
    //tokenizzo la stringa e vedo il numero di virgole
    save = NULL;
    token = strtok_r(buffer, " ",  &save);
    char** tmp = malloc(sizeof(char*) * 2);
    i = 0;
    while(token) {
      tmp[i] = malloc(sizeof(char) * MAXSTRING);
      strncpy(tmp[i], token, strlen(token) - i); //se è il secondo elemento (i = i) non deve prendere il newline finale,
      token = strtok_r(NULL, " ", &save);
      i++;
    }

    if(strcmp(tmp[0], SPAZIO) == 0) 
    {
      //le due stringhe sono uguali
      if(!isNumber(tmp[1],&spazio)) 
      {
                    //fprintf(stderr,"questo è a %s\n",a);
                    //fprintf(stderr,"questo è il risultato %d\n",spazio);
      } 
      else 
      {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
        //fprintf(stderr, "ERRORE %s non è un numero\n", tmp[1]);
      }
    }
    if(strcmp(tmp[0], NUMEROFILE) == 0) {
      //fprintf(stderr,"questo è il risultato dello numero file %d\n", isNumber(tmp[1],&numeroFile));
      
      if(!isNumber(tmp[1], &numeroFile)) {
      } else {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
      }
    }
    if(strcmp(tmp[0], SOC) == 0) {
      SockName = malloc(sizeof(char) * strlen(tmp[1]));
      strncpy(SockName, tmp[1], strlen(tmp[1]));
      //numeroFile = atoi(tmp[1]);
    }
    if(strcmp(tmp[0], WORK) == 0) {
      //fprintf(stderr,"questo è il risultato dello worker %d\n", isNumber(tmp[1],&numWorkers));
      
      if(!isNumber(tmp[1],&numWorkers)) {
      } else {
        perror("errato config.txt (isNumber)");
        exit(EXIT_FAILURE);
      }
    }

  free(tmp[0]);
  free(tmp[1]);
  free(tmp);
  }
  fclose(p);



  free(buffer);


  //fprintf(stderr,"abbiamo chiuso il file\n");
  fprintf(stderr,"spazio: %d\n", spazio);
  fprintf(stderr,"numeroFile: %d\n", numeroFile);
  fprintf(stderr,"SockName: %s\n", SockName);
  fprintf(stderr,"numWorkers: %d\n", numWorkers);
}

int* threadF(void* arg) { //funzione dei thread worker
  while(1) {
    while(queueClient->len == 0) 
    {
      pthread_cond_wait(&condQueueClient, &mutexQueueClient);
      fprintf(stderr, "sono sveglio!\n");
    }
    pthread_mutex_lock(&mutexQueueClient);
    void* tmp = pop(&queueClient);
    pthread_mutex_unlock(&mutexQueueClient);

    if(tmp == NULL)
      continue;

    long connfd = (long)tmp; // id del client che sta facendo richiesta 

    msg_t str;
    if (readn(connfd, &str.len, sizeof(int))<=0) return -1;
    str.arg = calloc((str.len), sizeof(char));
    if (!str.arg) {
	perror("calloc");
	fprintf(stderr, "Memoria esaurita....\n");
	return -1;
    }
    if (readn(connfd, str.arg, str.len*sizeof(char))<=0) return -1;
    //toup(str.str);
    if (writen(connfd, &str.len, sizeof(int))<=0) { free(str.arg); return -1;}
    if (writen(connfd, str.arg, str.len*sizeof(char))<=0) { free(str.arg); return -1;}
    free(str.arg);

  }
  return 0;
}

int main(int argc, char* argv[]) {
  
  parser();
  cleanup();
  atexit(cleanup);
  queueClient = initQueue(); //coda dei file descriptor dei client che provano a connettersi


   pthread_t t;
    if(pthread_create(&t, NULL, threadF, NULL) != 0)
    {
        fprintf(stderr, "pthread_create failed server\n");
    }
  /*pthread_t *t = malloc(sizeof(pthread_t) * numWorkers); //array dei thread
  for(int i = 0; i < numWorkers; i++) {
    pthread_create(&t[i], NULL, threadF, NULL);
    //sleep(1);
  }*/

  int listenfd;

  SYSCALL_EXIT("socket", listenfd, socket(AF_UNIX, SOCK_STREAM, 0), "socket", "");

  struct sockaddr_un serv_addr;
  memset(&serv_addr, '0', sizeof(serv_addr));
  serv_addr.sun_family = AF_UNIX;
  strncpy(serv_addr.sun_path, SockName, strlen(SockName)+1);
//fprintf(stderr, "SONO ARRIVATO QUI\n");
  int notused;
  SYSCALL_EXIT("bind", notused, bind(listenfd, (struct sockaddr*)&serv_addr,sizeof(serv_addr)), "bind", "");
  
  SYSCALL_EXIT("listen", notused, listen(listenfd, MAXBACKLOG), "listen", "");
  

  fd_set set, tmpset;
  // azzero sia il master set che il set temporaneo usato per la select
  FD_ZERO(&set);
  FD_ZERO(&tmpset);

  // aggiungo il listener fd al master set
  FD_SET(listenfd, &set);

  // tengo traccia del file descriptor con id piu' grande
  int fdmax = listenfd;
  
  for(;;) {
// copio il set nella variabile temporanea per la select
    tmpset = set;
    if (select(fdmax+1, &tmpset, NULL, NULL, NULL) == -1) {
      perror("select");
      return -1;
    }

// cerchiamo di capire da quale fd abbiamo ricevuto una richiesta
    for(int i=0; i <= fdmax; i++) {

      if (FD_ISSET(i, &tmpset)) {
        long connfd;
        if (i == listenfd) { // e' una nuova richiesta di connessione
          SYSCALL_EXIT("accept", connfd, accept(listenfd, (struct sockaddr*)NULL ,NULL), "accept", "");
          FD_SET(connfd, &set);  // aggiungo il descrittore al master set
          if(connfd > fdmax)
            fdmax = connfd;  // ricalcolo il massimo
          continue;
        }
        connfd = i; 

        FD_CLR(connfd, &set);//levo il bit nella set perchè sto gestendo la richiesta di connfd
        
        msg_t str;

        if (readn(connfd, &str.len, sizeof(int))<=0) { fprintf(stderr, "ERRORE SULLA LUNGHEZZA LETTURA\n"); }
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
        //-R 2 -> array char* argv[] e int argc
        //-w file1,file3
        //inserisco in coda il comando letto
        ComandoClient *cmdtmp = malloc(sizeof(ComandoClient));
        cmdtmp->comando = str.comando;
        cmdtmp->parametro = malloc(sizeof(char) * strlen(str.arg));
        cmdtmp->connfd = connfd;
        strcpy(cmdtmp->parametro, str.arg);

        pthread_mutex_lock(&mutexQueueClient);
        push(&queueClient, cmdtmp);
        pthread_mutex_unlock(&mutexQueueClient);
        pthread_cond_signal(&condQueueClient); 
        

        // e' una nuova richiesta da un client già connesso
  // eseguo il comando e se c'e' un errore lo tolgo dal master set
        //if (cmd(connfd) < 0) {
        /*if (-1 < 0) {
          close(connfd);
          FD_CLR(connfd, &set);
    // controllo se deve aggiornare il massimo
          if (connfd == fdmax)
            fdmax = updatemax(set, fdmax);
        }*/
        //parser dei comandi + inserimento delle richieste in una coda dove possano accedere 
        //i worker
        //push(&queueClient, &connfd);
        //printf("inserito\n");
      }
    }
  }
}