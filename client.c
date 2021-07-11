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

#include <signal.h>//segnali
#include <sys/un.h>
#include <ctype.h>
#include <pthread.h>

//open,write,read
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "queue.h"
#include "util.h"
#include "Parser.h"

#define MAXBUFFER 1000
#define MAXSTRING 100
#define MAXPATH 1024
#define SOCKNAME "mysock"
#define MAXLENNUM 10
#define O_CREATE 1
#define O_OPENED 0
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

int openConnection(const char* sockname, int msec, const struct timespec abstime)//Apre la connesione verso il server
{
    if(sockname == NULL || msec < 0) //argomenti non validi
    { 
        errno = EINVAL;
        perror("[openConnection]");
        return -1;
    }

    struct sockaddr_un serv_addr;
    SYSCALL_EXIT("socket", sockfd, socket(AF_UNIX, SOCK_STREAM, 0), "socket", "");//creo il socket
    memset(&serv_addr, '0', sizeof(serv_addr));//inizializzo il socket

    int LenSockName = strlen(sockname);
    serv_addr.sun_family = AF_UNIX;
    strncpy(serv_addr.sun_path,sockname, LenSockName+1);//copio il nome del socket comune
    serv_addr.sun_path[LenSockName] = '\0';
      
    struct timespec wait_time;// imposto il tempo di attesa
    set_timespec_from_msec(msec, &wait_time);

    struct timespec curr_time;// imposto il tempo corrente
    clock_gettime(CLOCK_REALTIME, &curr_time);

    int err = -1;
    if(verbose)
      fprintf(stdout, "[Tempo connessione]: Currtime %ld abstime %ld\n", curr_time.tv_sec, abstime.tv_sec);// provo a connettermi
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

int closeConnection(const char* sockname)//chiudo la connessione
{
  if(sockname == NULL)//argomenti non validi
  {
    errno = EINVAL;
    perror("[Sockname]");
    return -1;
  }
    
  if(strcmp(sockname, socknameconfig) != 0)//socket errato
  {
    errno = ENOTCONN;
    perror("[Strcmp]");
    return -1;
  }

  if( close(sockfd) == -1 )//chiusura socket
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

int writeCMD(const char* pathname, char cmd) //parte ricorrente per mandare il comando al server
{
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
  lenPath += 2;

  SYSCALL_EXIT("writen", notused, writen(sockfd, &lenPath, sizeof(int)), "write", ""); //scrivo il comando "semplice" al server
  SYSCALL_EXIT("writen", notused, writen(sockfd, Comando, lenPath * sizeof(char)), "write", "");
  if(verbose)
    fprintf(stdout, "[Invio Comando]: %s Successo \n", Comando); //debug
  free(Comando);
  return 0;
}

int closeFile(const char* pathname)//chiusura di un file
{
  if(pathname == NULL)//argomenti non validi
  {
    errno = EINVAL;
    perror("[Pathname]");
    return -1;
  }

  if(writeCMD(pathname, 'z') == -1)//chiusura file
  {
    errno = EPERM;
    perror("[writeCMD]");
    return -1;
  }
  int risposta, notused;
  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");//leggo la risposta del server che sta "chiudendo il file"
  if(risposta == -1)//verifico se la chiusura del file è avvenuta con successo
  {
    errno = EPERM;
    fprintf(stderr, "[Errore]: Chiusura file %s fallita\n", pathname);
  } 
  else//risposta = 0 successo
  {
    if(verbose)
      fprintf(stdout, "[Chiusura]: File %s chiuso\n", pathname);
  }
  return risposta;
}

int openFile(const char* pathname, int flags)//apertura di un file ram
{
  if((flags != O_OPENED && flags != O_CREATE) || pathname == NULL) //se i flag passati non sono validi
  {                                                                //O_OPENED solo apertura
    errno = EINVAL;                                                //O_CREATE creazione e apertura
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
  if(risposta == 0)//verifico se ha aperto il file
  {
    if(verbose)
      fprintf(stdout, "[Apertura]: Il file %s è stato creato/aperto con successo\n", pathname);
  }
  else//errori
  {
    if(risposta == -2)//ho trovato più file che hanno lo stesso nome e non so come comportarmi
    {
      fprintf(stderr, "[Avviso]: File %s già esistente nel server (Creazione file fallita)\n", pathname);
      risposta = -1;
    }
    else//non sono riuscito per varie ragioni a creare il file
    {
        errno = EACCES;
        fprintf(stderr, "[Errore]: Il file %s non è stato creato\n", pathname);
    }
  }
  return risposta;
}

int removeFile(const char* pathname)//vado a rimuovere un file
{
  if(pathname == NULL)//argomenti non validi
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
  
  if(risposta == -1)//controllo se ho rimosso in modo corretto il file
  { 
    errno = EACCES;
    fprintf(stderr, "[Errore]: Rimozione %s Fallita\n", pathname);
    return -1; 
  }
  else//successo
  {
    if(verbose)
      fprintf(stdout, "[Rimozione]: %s Successo\n", pathname);//debug   
  }
  
  return 0;//se va tutto bene ritorna 0
}

int readFile(const char* pathname, void** buf, size_t* size)//leggo il file al pathname e poi metto il contenuto nel buf
{
   if(pathname == NULL)//argomenti non validi
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
  int len;//intermediario pe il size perchè vado a scrivere -1 in n che è un intero invece in size non potrei perchè accetta solo numeri positivi
  SYSCALL_EXIT("readn", notused, readn(sockfd, &len, sizeof(int)), "read", "");
  //ho letto la size del file o la risposta negativa
  if(len == -1) //se non riesco a trovare il file il server scriverà al client -1 per indicare l'errore
  {
    errno = EPERM;
    fprintf(stderr, "[Errore]: Lettura %s Fallita\n", pathname);//debug
    *buf = NULL;//metto il buff a NULL per evitare che provi a scrivere qualcosa nel buf
    *size = 0;
    return -1; //errore o file inesistente
  } 
  else//ho letto un n>0
  {
    *size = len;
    ec_null((*buf = malloc(sizeof(char) * (*size))), "malloc");//alloco lo spazio del buffer per il contenuto del file
    SYSCALL_EXIT("readn", notused, readn(sockfd, *buf, (*size) * sizeof(char)), "read", "");//leggo il contenuto del file
    
    if(verbose)
    fprintf(stdout, "[Lettura file]: %s Successo\n", pathname);//debug
    
    return 0; //successo
  }
}

int writeLocal(const char* pathname, void* buf, int size)//scrive in locale il contenuto del buf 
{
  if(pathname == NULL || buf == NULL || size < 0) //argomenti non validi
  {//argomenti non validi 
    errno = EINVAL;
    perror("[writeLocal]");
    return -1;
  }

  int idFile;//scrittura del file/crezione locale
  ec_meno1((idFile = open(pathname, O_WRONLY | O_APPEND | O_CREAT, 0644)), "open");
  ec_meno1((writen(idFile, buf, size)), "writen");
  ec_meno1((close(idFile)), "close");
  
  return 0;//successo
}

int readNFiles(int NumFile, const char* dirname) //devo leggere NumFile file
{
  if(dirname == NULL) //argomenti non validi
  {
    errno = EINVAL;
    perror("[readNFiles]");
    return -1;
  }
  char* ntmp; //passo il numero come un carattere per riutilizzare il codice
  ec_null((ntmp = malloc(sizeof(char) * MAXLENNUM)), "malloc");
  if(sprintf(ntmp, "%d", NumFile) < 0)//memorizzo il numero in ntmp come char 
  {
    perror("[Snprintf]");
    return -1;
  }
  if(writeCMD(ntmp, 'R') == -1) 
  {
    errno = EPERM;
    perror("[writeCMD]");
    return -1;
  }
  free(ntmp);

  int notused;
  SYSCALL_EXIT("readn", notused, readn(sockfd, &NumFile, sizeof(int)), "read", "");//leggo dal socket del server il numero di file da leggere
  int lenpathtmp;                                                                  //visto che potrebbero essere meno di quelli dichiarati dal parametro NumFile
  char** arr_buf; //tengo traccia dei path dei file per poi leggerli in un secondo momento
  ec_null((arr_buf = malloc(sizeof(char*) * NumFile)), "malloc");
  for(int i = 0; i < NumFile; i++) //per ogni file da leggere dal server
  { 
    SYSCALL_EXIT("readn", notused, readn(sockfd, &lenpathtmp, sizeof(int)), "read", "");
    ec_null((arr_buf[i] = malloc(sizeof(char) * lenpathtmp)), "malloc");//passaggio in più inutile
    SYSCALL_EXIT("readn", notused, readn(sockfd, arr_buf[i], lenpathtmp * sizeof(char)), "read", "");
  } 
  
  //prima mi ricavo tutti i nomi dei file da leggere e poi li leggo per evitare conflitti
  //nelle scritture/letture dei socket
  
  for(int i = 0; i < NumFile; i++)//Ora leggo il contenuto dei file con i nomi ricavati nel arr_buf
  {
    void* buffile;
    size_t sizebufffile;
    if(openFile(arr_buf[i], 0) != -1)//gestione dell'errore
    {
      if(readFile(arr_buf[i], &buffile, &sizebufffile) == -1) //leggo un singolo file preso dall'array dei file
      {
        return -1;//errore della lettura
      }
      char path[MAXPATH];//dichiaro un path 
      
      if(snprintf(path, sizeof(path), "%s/%s", dirname, arr_buf[i]) < 0) 
      { 
        perror("[Snprintf]"); 
        return -1; 
        }
   
      if(writeLocal(path, buffile, sizebufffile) == -1) { return -1; }//infine scrivo in append il contenuto del file
      if(closeFile(arr_buf[i]) == -1) { return -1; }//infine chiudo il file
    }
    else//errore dell'openFile
    {
      errno = EACCES;
      fprintf(stderr, "[Errore]: Apertura %s Fallita\n", arr_buf[i]);
      return -1;
    }
    free(arr_buf[i]);
  }
  free(arr_buf);
  return NumFile;//devo ritornare il numero dei file letti
}

int appendToFile(const char* pathname, void* buf, size_t size) //scrivo in un file in append, il contenuto del buf
{
  if(pathname == NULL) //argomenti non validi
  {
    errno = EINVAL;
    perror("[appendToFile]");
    return -1;
  }

  int notused;
  SYSCALL_EXIT("writen", notused, writen(sockfd, &size, sizeof(size_t)), "write", "");//scrivo la grandezza del file
  
  int cista;
  SYSCALL_EXIT("readn", notused, readn(sockfd, &cista, sizeof(int)), "read", "");//leggo la risposta del server
  if(!cista)//il file non sta nel server materialmente, neanche se si espellessero tutti i file
  { 
    fprintf(stderr, "[Problema]: Il file %s è troppo grande per la capienza del server\n", pathname);
    return -1;
  }
  SYSCALL_EXIT("writen", notused, writen(sockfd, (char*)buf, size * sizeof(char)), "write", "");//scrivo il contenuto del file

  int risposta;
  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");//lettura per controllare se la scrittura in append è stata eseguita
  if(risposta == -1) //errore di scrittura
  {
    errno = EACCES;
    fprintf(stderr, "[Errore]: Scrittura append del file %s\n", pathname);
  } 
  else//successo
  {
    if(verbose)
      fprintf(stdout, "[Scrittura]: File %s scritto correttamente nel server\n", pathname);
  }
  return risposta;
}

int writeFile(const char* pathname) //scrivo un file nel server 
{
  if(pathname == NULL) //argomenti non validi
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
  int notused, lenBuf, risposta;//lenBuf è la lunghezza del buffer nel quale salvo le risposte del server 
  SYSCALL_EXIT("readn", notused, readn(sockfd, &risposta, sizeof(int)), "read", "");//leggo la risposta del server per vedere se il file è stato aperto
  
  if(risposta == -1)//errore di openFile
  {
    fprintf(stderr, "[Errore]: Apertura file %s Fallita\n", pathname);
    return -1;
  } 
  else//se non ci sono problemi durante la creazione del file o l'apertura
  {
    char *buffer = NULL;
    lenBuf = strlen(pathname) + 2; //+2 per il comando e il terminatore
    ec_null((buffer = realloc(buffer, lenBuf * sizeof(char))), "realloc");

    //copiatura del file per trasformarlo in una info volatile
    struct stat info;
    ec_meno1((stat(pathname, &info)), "stat");//creo lo stat
    long length = (long)info.st_size;//recupero la size del file
    int idFile;
    ec_meno1((idFile = open(pathname, O_RDONLY)), "open");//apro fisicamente il file in lettura
    char* bufferFile;
    ec_null((bufferFile = malloc(sizeof(char) * length)), "malloc");
    ec_meno1((readn(idFile, bufferFile, length)), "readn");//tutto il contenuto lo metto nel buffer 
    ec_meno1((close(idFile)), "close");//chiudo il file fisico

    if(appendToFile(pathname, bufferFile, length) == -1)//scrivo in append
    {
      free(bufferFile);
      return -1;//errore di scrittura in append
    }
  free(bufferFile);    
  }
  return 0;//successo
}

int EseguiComandoClient(NodoComando *tmp)//core che gestisce i comandi 
{
  if(tmp == NULL)//argomenti non validi
  {
      errno = EINVAL;
      perror("[EseguiComandoClient]");
      return -1; //errore: tmp non può e non deve essere NULL. 
  }
  if(tmp->cmd == 'c')//gestisco il comando con c per eliminare il file dal server
  {
    if(verbose)
      fprintf(stdout, "[Rimozione File]: %s in corso\n", tmp->name);
    if(openFile(tmp->name, O_OPENED) != -1)//apro il file se gia presente 
    {
      if(removeFile(tmp->name) == -1) //rimovo il file
        return -1;
    }
    else//errore openFile
    {
      return -1;
    }
  }
  else
  { 
    if(tmp->cmd == 'r')//lettura di una serie di file definiti dal pathname
    {
      if(verbose)
        fprintf(stdout, "[Lettura file]: %s in corso\n",tmp->name);//debug
      if(openFile(tmp->name, O_OPENED) != -1)//se l'apertura ha avuto successo
      {
        void* buf;
        size_t sizebuff; //risolvere problema size_t
        if(readFile(tmp->name, &buf, &sizebuff) == -1) //leggo il file
        {
          return -1;//errore di lettura
        }
        else//errore openFIle
        {
          if(savefiledir != NULL && buf != NULL) //se le condizioni sono corrette leggo il file
          {
            char path[MAXPATH];
            if(snprintf(path, sizeof(path), "%s/%s", savefiledir, tmp->name) < 0)//prendo il path
            { 
              perror("snprintf"); 
              return -1; 
            }
            if(writeLocal(path, buf, sizebuff) == -1) return -1;//scrittura file
            if(closeFile(tmp->name) == -1) return -1; //chiusura file
          }
          else // non ho una cartella per salvare il file oppure il buffer è vuoto
          {
            fprintf(stderr,"[Problema]: non ho una cartella dove salvare il file \n");
            return -1; //errore
          }
          free(buf);
        } 
      }
      else//se l'openFile ha un errore
      {
        return -1;
      }
    } 
    else
    {
      if(tmp->cmd == 'R')//lettura di n file
      {
        if(verbose)
          fprintf(stdout, "[Lettura n File]: %d in corso\n", tmp->n);//debug
        if(readNFiles(tmp->n, savefiledir) == -1)
          return -1;
      } 
      else
      { 
        if(tmp->cmd == 'W')//scrittura di un file
        {
          if(verbose)
            fprintf(stdout, "[Scrittura file]: %s in corso\n", tmp->name);//debug
          if(openFile(tmp->name, O_CREATE) != -1)//flag = O_CREATE = 1 devo creare il file e crearlo
          {
            if(writeFile(tmp->name) == -1) //scrittura file
              return -1; 
            if(closeFile(tmp->name) == -1) //chiusura file
              return -1; 
          } 
          else
          {
            if(openFile(tmp->name, O_OPENED) != -1) //flag = 0 è come se non avessi flag quindi devo provare ad aprire il file
            { 
              if(writeFile(tmp->name) == -1) //scrittura file
                return -1; 
              if(closeFile(tmp->name) == -1)//chiusura file
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
{
  if(name == NULL)//se il path name non è corretto
    return -1;
  char buftmp[MAXPATH];
  if (getcwd(buftmp, MAXPATH) == NULL)//prendendo il pathname della directory attuale
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
    if (entry->d_type == DT_DIR)//vedo se il tipo della entry è una cartella
    {
      if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || entry->d_name[0] == '.')
        continue;
        
        if(snprintf(path, sizeof(path), "%s/%s", name, entry->d_name) < 0) //va a scrivere nel path l'entry->d_name che sto visitando
        {
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
          if(pushTesta(q, new) == -1)//devo assolutamente mettere in testa la richiesta per evitare che altre richieste seguenti falliscano per mancanza file
            return -1;
          free(buffer);
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

static void SigPipe()//handler del messaggio di caduta del server
{
  fprintf(stdout, "[Errore]: Il server mi ha disconnesso\n");
}
int main(int argc, char *argv[]) 
{
  //gestione segnale SigPipe per l'interruzione improvvisa
  struct sigaction sapipe;
  memset(&sapipe, 0, sizeof(struct sigaction));
  sapipe.sa_handler = SigPipe;//SIG_IGN
  sigaction(SIGPIPE, &sapipe, NULL);

  Queue *QueueParser = parser(argv,argc); //coda delle operazioni
  struct timespec abstime;

  add_to_current_time(4, 200, &abstime);
  //primo parametro: tempo limite (in secondi)
  //secondo parametro: intervallo di tempo tra due connessioni (in millisecondi)
  
  ec_meno1((openConnection(socknameconfig, 1000, abstime)), "openConnection"); //apertura della connessione
  abstime.tv_sec = timems / 1000;
  abstime.tv_nsec = (timems % 1000) * 1000000;

  while(QueueParser->len > 0) //gestico le richieste della coda delle operazioni
  {
    NodoComando *tmp = pop(&QueueParser); //prelevo un comando
    nanosleep(&abstime, NULL);
     if(verbose)
      fprintf(stdout, "[Lettura Comando]: '%c' - %s in corso\n", tmp->cmd, tmp->name);
    
    if(tmp->cmd == 'w') //fa una richiesta speciale in modo ricorsivo sfruttando il comando W
    { 
      if(tmp->n == 0)
        tmp->n = -1;  //in caso che n sia zero vuol dire che devo leggere tutti i file della directory
      
      if(strcmp(tmp->name, ".") == 0)//se passo come dir "." metto il path reale
      {
        ec_null((tmp->name = malloc(sizeof(char) * MAXPATH)), "malloc");
        ec_null((getcwd(tmp->name, MAXPATH)), "getcwd");
      }
      if(visitaRicorsiva(tmp->name, &(tmp->n), &QueueParser) == -1) 
        return -1;//però metto n=-1 per evitare il caso in cui n si decrementa fino a 0
      free(tmp->name);
    } 
    else//tutti gli altri casi tranne w
    {
      if(EseguiComandoClient(tmp) == -1)
        fprintf(stderr,"[Errore]: comando %c con parametro %s Fallito \n",tmp->cmd,tmp->name);
        
        free(tmp->name);
        free(tmp);
    }
      
  }
  ec_meno1((closeConnection(socknameconfig)), "closeConnection");//chiusura della connessione con il server
  free(QueueParser);
  
  return 0;
}
