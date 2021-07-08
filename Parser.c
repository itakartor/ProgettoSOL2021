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

#include "queue.h" //ho incluso la definizione di coda
#include "util.h"
#include "Parser.h"
// limite sulla lunghezza della stringa passata come argomento
#define MY_ARGV_MAX  512

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

int arg_f(char* optarg) 
{
        
        if(seenf == 1)
        {
            fprintf(stderr, "ERRORE: l'opzione -f va specificata una volta sola\n");
            exit(EXIT_FAILURE);
        }
        seenf = 1;
        ec_null((socknameconfig = malloc(sizeof(char) * strlen(optarg))), "malloc");
        strcpy(socknameconfig, optarg);
        //printf("filename %s\n", optarg);
        return 0;
}

int arg_w(char* optarg, Queue **q)
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
                                //fprintf(stderr,"questo è un numero -> %d\n",num);
                        }
                        else 
                        {
                                fprintf(stderr, "FATAL ERROR: number is required\n");
                                exit(EXIT_FAILURE);
                        }
                } 
        } 
            insert(q, 'w', dirname, num);
            //printQueue(q);

            free(arg);
            free(dirname);
            return 0;  
}
int arg_W(char* optarg, Queue** q)
{

        char* arg = malloc(sizeof(char) * strlen(optarg));//copio momentanemente l'argomento preso dalla getopt per evitare di conpromettere le prossime
        strncpy(arg, optarg, strlen(optarg));             //modifiche all'input

        char* save = NULL;
        char* token = strtok_r(arg, ",", &save); // Attenzione: l’argomento stringa viene modificato!
        while(token) 
        {
                fprintf(stderr,"sto inserendo nella coda \n");
                        
                insert(q, 'W', token, 0);//inserisco tanti comandi in coda quanti sono i file del comando -W file1,[file2]
                
                //fprintf(stderr,"sono qui\n");
                token = strtok_r(NULL, ",", &save);
        }
        free(arg);
        //printQueue(*q);
        //fprintf(stderr,"sto uscendo da W\n");       
        return 0;
}
int arg_r(char* optarg, Queue** q)
{
        char* arg = malloc(sizeof(char) * strlen(optarg));
        strncpy(arg, optarg, strlen(optarg));

        char* save = NULL;
        char* token = strtok_r(arg, ",", &save); // Attenzione: l’argomento stringa viene modificato!
        while(token) {
                //printf("%s\n", token);
                insert(q, 'r', token, 0);       //inserisco tanti comandi in coda quanti sono i file del comando -W file1,[file2]
                token = strtok_r(NULL, ",", &save);
            }
        free(arg);
        seenr = 1;
        //printQueue(q);
        //printf("\n\n\n"); 
        return 0;
}

int arg_R(char* argv[],int argc,Queue** q)
{
        //R può avere opzionalmente una opzione, che è messa quindi facoltativa e parsata a parte
        //printf("guardo R\n");
        int nfacoltativo = 0;
        char* nextstring = NULL; //la stringa seguente a -R passata da riga di comando
        if(optind != argc)      //se -R non è l'ultimo argomento passato
        {       
                nextstring = strdup(argv[optind - 1]);
                //fprintf(stderr, "indice optind ->%d   \n",optind);
                //fprintf(stderr, "ci sono argomenti -> %s  \n",nextstring);
        }
        else
        {
                fprintf(stderr, "non ci sono argomenti  \n");
        }
        if(nextstring != NULL && nextstring[0] != '-') 
        {       //nextstring non è un comando, bisogna controllare se è un numero o una stringa
                if(isNumber(nextstring, &nfacoltativo)) 
                {
                        fprintf(stderr,"sto guardando il numero  \n");
                        nfacoltativo = atoi(nextstring);
                }
                else 
                {       //non è un numero né un parametro, deve dare errore
                        fprintf(stderr, "FATAL ERROR: number is required\n");
                        exit(EXIT_FAILURE);
                }
        }
        //printQueue(q);
        //printf("\n\n\n");
        //printf("nfacoltativo ---->%d \n",nfacoltativo);
        seenR = 1;
        insert(q, 'R', NULL, nfacoltativo);
        //printf("caso R %d\n", nfacoltativo);
        //printQueue(q);
        return 0;    
}
int arg_d(char* optarg)
{
        //fprintf(stderr,"siamo alla d\n");
        savefiledir = malloc(sizeof(char) * strlen(optarg));
        strcpy(savefiledir, optarg);
        return 0;   
}
int arg_t(char* optarg, Queue** q) // controllo se l'argomento è un numero?
{
        timems = atoi(optarg);
        insert(q,'t',optarg, 0);
        return 0;
}
int arg_c(char* optarg, Queue** q)
{
        insert(q,'c',optarg, 0);
        return 0;
}
int arg_p()
{
        verbose = 1;
        return 0;
}
Queue* parser(char* argv[],int argc)
{
    savefiledir = NULL;
    timems = 0;
    verbose = 0;        
    int opt;
    Queue* q = initQueue();
    if(q == NULL)
       fprintf(stderr, "CODA VUOTA\n"); 
    while ((opt = getopt(argc,argv, "hf:w:W:Rd:t:l:u:c:pr:")) != -1) {
        switch(opt) {
        case 'h': 
                arg_h();  //messaggio di help
                break;
        case 'f': 
                arg_f(optarg); //definisco come si chiama il socket del server per connettersi
                break;
        case 'w': 
                arg_w(optarg, &q);  //tokenizzare la stringa per suddividere le richieste dei vari file -w cartella,[n=0]
                break;
        case 'W': 
                arg_W(optarg, &q);  //tokenizzo la stringa per suddividere la richiesta -W file1,[file2] per ogni file 
                break;
        case 'r':
                arg_r(optarg, &q);
                break;
        case 'R':
                arg_R(argv,argc, &q);
                break;
        case 'd':
                arg_d(optarg);
                break;
        case 't':
                arg_t(optarg, &q);
                break;
        case 'c':
                arg_c(optarg, &q);
                break;
        case 'p':
                arg_p();
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
    if(savefiledir != NULL && seenr == 0 && seenR == 0) 
    {
     fprintf(stderr, "errore, l'opzione -d va usata insieme a -r o a -R\n");
     exit(EXIT_FAILURE);
    }
    return q;
}
