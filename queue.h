#ifndef QUEUE_H_
#define QUEUE_H_

typedef struct _Node {
    void*           data;
    struct _Node*    next;
} Node;

typedef struct _NodoComandi {
  char cmd;
  char* name;
  int n;
} NodoComando;

typedef struct _comandoclient {
  char comando;
  char* parametro;
  long connfd;
} ComandoClient;

typedef struct msg {
    int len;
    char comando;    //lunghezza del messaggio
    char *arg;       //messaggio
} msg_t;
/**
*
*/
typedef struct _Queue {
    Node*             head;
    Node*             tail;
    unsigned long       len;
    pthread_mutex_t     lock;
    pthread_cond_t      cond;
} Queue;

typedef struct _file {
  char* nome;
  long size;
  char* buffer; //contenuto
  long length; //per fare la read e la write, meglio se memorizzato
} fileRam;

void push(Queue **q, void* el);
Queue* initQueue();
void* pop(Queue **q);
void printQueue(Queue *q);
void insert(Queue **q, char cmd, char* name, int n);
void push(Queue **q, void* el);
void printQueueFiles(Queue *q);
int removeFromQueue(Queue **q, Node* toDelete);
Node* fileExistsServer(Queue *q, char* nomefile);
#endif
