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
        ec_null((socknameconfig = malloc(sizeof(char) * (strlen(optarg)+1))), "malloc");
        strcpy(socknameconfig, optarg);
        socknameconfig[strlen(optarg)] = '\0';
        //fprintf(stderr,"sockname %s\n", optarg);
        return 0;
}

int arg_w(char* optarg, Queue **q)
{
        //compio l'optarg in una variabile temporanea arg
        char* arg;
        int LenArg = strlen(optarg);

        ec_null((arg = malloc(sizeof(char) * (LenArg + 1))), "malloc");
        strncpy(arg,optarg,LenArg);
        arg[LenArg] = '\0';
        
        //tokenizzo la stringa e vedo il numero di virgole
        char* save = NULL;
        char* token = strtok_r(arg, ",", &save);
        char* dirname; //dirname è il primo paramentro della w
        int LenDirname = strlen(token) ;
        ec_null((dirname = malloc(sizeof(char) * (LenDirname + 1))), "malloc");
        strncpy(dirname, token, LenDirname);
        dirname[LenDirname] = '\0';
        int contavirgole = -1;
        char* tmp;      //prende l'ultimo token dopo la virgola per vedere se è il numero opzionale
        int num = 0;    //numero opzionale del comando

        while(token) 
        {
                contavirgole++;
                tmp = token;
                token = strtok_r(NULL, ",", &save);
        }
        
        if(contavirgole > 1) //se ho più di due argomenti mi da errore dove al più posso avere -w cartella,5
        {
                fprintf(stderr, "[PARSER]: troppi argomenti\n");
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
                                fprintf(stderr, "[PARSER]: numero richiesto\n");
                                exit(EXIT_FAILURE);
                        }
                }
        } 
            insert(q, 'w', dirname, num);


            free(arg);
            free(dirname);
            return 0;  
}
int arg_W(char* optarg, Queue** q)
{
        int LenArg = strlen(optarg);
        char* arg;//copio momentanemente l'argomento preso dalla getopt per evitare di conpromettere le prossime
        ec_null((arg = malloc(sizeof(char) * (LenArg + 1))), "malloc");
        strncpy(arg, optarg, LenArg);             //modifiche all'input
        arg[LenArg] = '\0';

        char* save = NULL;
        char* token = strtok_r(arg, ",", &save); // Attenzione: l’argomento stringa viene modificato!
        while(token) 
        {
                //fprintf(stderr,"sto inserendo nella coda \n");
                        
                insert(q, 'W', token, 0);//inserisco tanti comandi in coda quanti sono i file del comando -W file1,[file2]
                
                
                token = strtok_r(NULL, ",", &save);
        }
        free(arg);    
        return 0;
}
int arg_r(char* optarg, Queue** q)
{
        int LenArg = strlen(optarg);
        char* arg;
        ec_null((arg = malloc(sizeof(char) * (LenArg + 1))), "malloc");
        strncpy(arg, optarg, LenArg);
        arg[LenArg] = '\0';

        char* save = NULL;
        char* token = strtok_r(arg, ",", &save); // Attenzione: l’argomento stringa viene modificato!
        while(token) 
        {
                insert(q, 'r', token, 0);       //inserisco tanti comandi in coda quanti sono i file del comando -W file1,[file2]
                token = strtok_r(NULL, ",", &save);
        }
        free(arg);
        seenr = 1; 
        return 0;
}

int arg_R(char* argv[],int argc,Queue** q)
{
        //R ha il numero opzionale che deve essere gestito diversamente dal resto
        int nfacoltativo = 0;
        char* nextstring = NULL; //verifico se dopo R c'è un altro comando oppure l'argomento di R
        if(optind != argc)      
        {       
                nextstring = strdup(argv[optind - 1]);
        }
        else
        {       
                if(verbose)
                        fprintf(stdout, "[Avviso]: R non ha argomenti\n");
        }
        if(nextstring != NULL && nextstring[0] != '-') 
        {       //nextstring non è un comando, bisogna controllare se è un numero o una stringa
                if(isNumber(nextstring, &nfacoltativo)) 
                {
                        fprintf(stderr,"[Avviso]: Guardo se c'è l'argomento di R\n");
                }
                else 
                {       //non è un numero né un parametro, deve dare errore
                        fprintf(stderr, "[PARSER]: numero richiesto\n");
                        exit(EXIT_FAILURE);
                }
        }
        seenR = 1;
        insert(q, 'R', NULL, nfacoltativo);
        return 0;    
}
int arg_d(char* optarg)
{
        int LenOptarg = strlen(optarg);
        ec_null((savefiledir = malloc(sizeof(char) * (LenOptarg + 1))), "malloc");
        strcpy(savefiledir, optarg);
        savefiledir[LenOptarg] = '\0';
        return 0;   
}
int arg_t(char* optarg) // controllo se l'argomento è un numero?
{
        timems = atoi(optarg);
        return 0;
}
int arg_c(char* optarg, Queue** q)
{
        char* arg;
        int LenOptarg = strlen(optarg);
        ec_null((arg = malloc(sizeof(char) * (LenOptarg + 1))), "malloc");
        strncpy(arg, optarg, LenOptarg);
        arg[LenOptarg] = '\0';
        
        //tokenizzo gli argomenti di c
        char* save = NULL;
        char* token = strtok_r(arg, ",", &save);
        
        while(token) 
        {
              insert(q, 'c', token, 0);
              token = strtok_r(NULL, ",", &save);
        }

        free(arg);
        return 0;
}
int arg_p()
{
        if(seenp == 1) 
        {
                fprintf(stderr, "[Parser]: l'opzione -p va specificata una volta sola\n");
                exit(EXIT_FAILURE);
        }
        seenp = 1;
        verbose = 1;
        return 0;
}
Queue* parser(char* argv[],int argc)
{
        savefiledir = NULL;
        timems = 0;
        verbose = 0;
        seenf = 0;
        seenp = 0;       
        int opt;
        Queue* q = initQueue();
        if(q == NULL)
        {
                perror("Coda Parser vuota");
                exit(EXIT_FAILURE);
        } 
    while ((opt = getopt(argc,argv, "hf:w:W:Rd:t:l:u:c:pr:")) != -1) {
        switch(opt) {
        case 'h': 
                arg_h();  //messaggio di help
                exit(EXIT_SUCCESS);
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
                arg_t(optarg);
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
