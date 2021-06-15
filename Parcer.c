#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <getopt.h>
// numero delle opzioni che voglio gestire
#define NOPTIONS       4
// limite sulla lunghezza della stringa passata come argomento
#define MY_ARGV_MAX  512

int arg_W(optarg)
{
        return 1;
} 
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

int parcel(char* argv,int argc, int p)
{
    int opt;
    while ((opt = getopt(argc,argv, "W:w:l:h")) != -1) {
        switch(opt) {
        case 'W': 
                arg_W(optarg);  
                break;
        case 'w': 
                arg_w(optarg);  
                break;
        case 'l': 
                arg_l(optarg);  
                break;
        case 'h': 
                arg_h(argv[0]); 
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