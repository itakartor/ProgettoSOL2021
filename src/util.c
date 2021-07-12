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

int updatemax(fd_set set, int fdmax) {
    for(int i=(fdmax-1);i>=0;--i)
	if (FD_ISSET(i, &set)) return i;
    //assert(1==0);
    return -1;
}


int isNumber(const char* s, int* n) {
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

int isPipe(int numWorkers, long connfd, int ** p)
{
  int risultato = 0;
        for(int j = 0; j < numWorkers; j++) 
        {
          //fprintf(stderr, "sono nel for, connfd %ld p[j][0] %d\n", connfd, p[j][0]);
          if(!risultato)
          {
            if(connfd == p[j][0])
              risultato = 1;
            else
              risultato = 0; 
          }
        }
        return risultato;
}

void Pthread_mutex_lock(pthread_mutex_t *mtx) {
  int err;
  if ( ( err=pthread_mutex_lock(mtx)) != 0 ) {
    errno=err;
    perror("pthread_mutex_lock");
    exit(EXIT_FAILURE);
  }
}

void Pthread_mutex_unlock(pthread_mutex_t *mtx) {
  int err;
  if ( ( err=pthread_mutex_unlock(mtx)) != 0 ) {
     errno=err;
     perror("pthread_mutex_unlock");
     exit(EXIT_FAILURE);
  }
}