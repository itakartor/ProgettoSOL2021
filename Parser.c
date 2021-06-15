#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
// numero delle opzioni che voglio gestire
#define NOPTIONS       4
// limite sulla lunghezza della stringa passata come argomento
#define MY_ARGV_MAX  512

int isNumber(const char* s, long* n) {
  if (s==NULL) return 1;
  if (strlen(s)==0) return 1;
  char* e = NULL;
  errno=0;
  long val = strtol(s, &e, 10);
  if (errno == ERANGE) return 2;    // overflow
  if (e != NULL && *e == (char)0) {
    *n = val;
    return 0;   // successo 
  }
  return 1;   // non e' un numero
}

int arg_W(optarg)
{
        return 0;
} 

int arg_h()
{
        fprintf(stdout, "%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n%s\n",
                  "-h                 : stampa la lista delle opzioni",
                  "-f filename        : specifica il filename del socket a cui deve connettersi il client",
                  "-w dirname[,n=0]   : manda i file dalla cartella 'dirname' al server",
                  "-W file1[,file2]   : lista dei file che devono essere scritti nel server",
                  "-r file1[,file2]   : lista dei file che devono essere letti dal server",
                  "-R [n=0]           : leggi 'n' file presenti sul server",
                  "-d dirname         : specifica la cartella dove scrivere i file letti con -r e -R",
                  "-t time            : tempo in millisecondi per fare due richieste consecutive al server",
                  "-l file1[,file2]   : lista di file a cui acquisire la mutua esclusione",
                  "-u file1[,file2]   : lista di file a cui rilasciare la mutua esclusione",
                  "-c file1[,file2]   : lista di file da eliminare dal server, se presenti",
                  "-p                 : modalità verbosa per nerd"
          );
        return 0;
}

int arg_f(char* optarg, Queue *q) 
{
        //devo inserire il comando nella coda
}

int arg_w(char* optarg, Queue *q)
{
        //compio l'optarg in una variabile temporanea arg
        char* arg = malloc(sizeof(char)*strlen(optarg));
        strncpy(arg,optarg,strlen(optarg));
        //tokenizzo la stringa e vedo il numero di virgole
        char* save = NULL;
        char* token = strtok_r(arg, ",", &save);
        char* dirname = malloc(sizeof(char) * strlen(token)); //dirname = primo token prima della prima virgola
        strncpy(dirname, token, strlen(token));
        int contavirgole = -1;
        char* tmp;      //temporanea, in cui sarà salvato l'ultimo token dopo le virgole
        int num = 0;    //numero opzionale del comando

        while(token) 
        {
                        contavirgole++;
                        tmp = token;
                        token = strtok_r(NULL, ",", &save);
        }
        
        if(contavirgole > 1) //se ho più di due argomenti mi da errore dove al più posso avere -w cartella,5
        {
                fprintf(stderr, "FATAL ERROR: too many arguments\n");
                exit(EXIT_FAILURE);
        }
        else
        {
                if(contavirgole == 1)//controllare se ha due argomenti e se l'ultimo token è un numero
                {
                        
                        //fprintf(stderr, "tmp da vedere %s\n", tmp);
                        if(isNumber(tmp, &num))
                        {
                                fprintf(stderr,"questo è un numero -> %d\n",num);
                        }
                        else 
                        {
                                fprintf(stderr, "FATAL ERROR: number is required\n");
                                exit(EXIT_FAILURE);
                        }
                } 
        } 
            insert(&q, 'w', dirname, num);
            printQueue(q);

            free(arg);
            free(dirname);  
}

int parsel(char* argv,int argc, int p)
{
    int opt;
    while ((opt = getopt(argc,argv, "hf:w:W:Rd:")) != -1) {
        switch(opt) {
        case 'h': 
                arg_h();  //messaggio di help
                break;
        case 'f': 
                arg_f(optarg);  //manda il comando nella coda
                break;
        case 'w': 
                arg_w(optarg);  //tokenizzare la stringa per suddividere le richieste dei vari file -w file1,[file2]
                break;
        case 'W': 
                arg_W(optarg);  //
                break;
        case 'R':
                arg_R();
                break;
        case 'd':
                arg_d();
                break;
        case ':': {
        printf("l'opzione '-%c' richiede un argomento\n", optopt);
        } break;
        case '?': {  // restituito se getopt trova una opzione non riconosciuta
        printf("l'opzione '-%c' non e' gestita\n", optopt);
        } break;
        default:;
        }
    }
    return 0;
}




int main(int argc, char* argv[])
{
    return 0;
}