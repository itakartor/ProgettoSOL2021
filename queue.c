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

#include <libgen.h>//base name

#include <sys/un.h>
#include <ctype.h>
#include <pthread.h>

#include "queue.h"
#include "util.h"

//#ifndef QUEUE_H_
//#define QUEUE_H_

Queue* initQueue() { //inizializza una coda vuota
  Queue *q = malloc(sizeof(Queue));
  q->head = NULL;
  q->tail = NULL;
  q->len = 0;
  return q;
}

void push(Queue **q, void* el) { //inserimento in coda in una FIFO
  Node *n = malloc(sizeof(Node));
  n->data = el;
  n->next = NULL;
  //inserimento in coda
  if((*q)->head == NULL) { //inserimento in coda vuota
    (*q)->head = n;
    (*q)->tail = n;
    (*q)->len = 1;
  } else { //inserimento in coda
    ((*q)->tail)->next = n;
    (*q)->tail = n;
    (*q)->len++; // = *q->len + 1;
  }
}

void insert(Queue **q, char cmd, char* name, int n) { //crea il NodoComando e lo mette nella coda
  NodoComando *new = malloc(sizeof(NodoComando));
  new->cmd = cmd;
  if(name != NULL)
  {
    new->name = malloc(sizeof(char)*strlen(name)); // abbiamo messo una malloc e la strcpy
    strncpy(new->name,name,strlen(name));
  }
  //printf("sono il nome del file %s\n", new->name);
  new->n = n;
  push(q, new);
}

void* pop(Queue **q) { //restituisce la testa e la rimuove dalla queue
  if((*q)->head == NULL) { //la lista è già vuota
    fprintf(stderr, "lista vuota");
    return NULL;
  } else {
    void *ret = ((*q)->head)->data;
    Node* tmp = (*q)->head;
    (*q)->head = ((*q)->head)->next;
    (*q)->len--;
    if((*q)->head == NULL) //la lista conteneva un solo elemento
      (*q)->tail = NULL;

    free(tmp);
    return ret;
  }
}

void printQueue(Queue *q) {
  Node* tmp = q->head;
  NodoComando *no = NULL;
  while(tmp != NULL) {
    no = tmp->data;
    //printf("nome: -- %s --\n",no->name);
    fprintf(stderr, "comando %c nome %s n %d\n", no->cmd, no->name, no->n);
    tmp = tmp->next;
  }
}

void printQueueFiles(Queue *q) {
  Node* tmp = q->head;
  fileRam *no = NULL;
  while(tmp != NULL) {
    no = tmp->data;
    fprintf(stdout, "nomefile %s length %ld\n", no->nome, no->length);
    tmp = tmp->next;
  }
}

int removeFromQueue(Queue **q, Node* toDelete) 
{
  int ok = 0; //inizialmente errore
  Node* tmp = (*q)->head;
  Node* tmp_prec = NULL;
  while(tmp != NULL) 
  {
    if(toDelete == tmp) 
    { //nodo da cancellare
      ((*q)->len)--;
      if(tmp_prec == NULL) 
      { //cancellazione in testa
        (*q)->head = tmp->next;
        if((*q)->len == 0) //c'era un solo elemento
          (*q)->tail = NULL;
      } 
      else 
      { //cancellazione in mezzo
        tmp_prec->next = tmp->next;
        if(tmp->next == NULL) //ultimo elemento nella coda
          (*q)->tail = tmp_prec;
      }
      free(tmp);
      ok = 1; //successo, cancellato
    }
    tmp_prec = tmp;
    tmp = tmp->next;
  }
  return ok;
}

Node* fileExistsServer(Queue *q, char* nomefile) 
{ // controlla se un fileRam è gia presente nella codaFile del server
  //il nomefile è il pathname del file
  Node* tmp = q->head;
  fileRam *no = NULL;
  while(tmp != NULL) 
  {
    no = tmp->data;
    fprintf(stderr,"nome file nella coda %s\n",no->nome);
    if(strcmp(basename(nomefile), no->nome) == 0) 
    {
      return tmp;
    }
    tmp = tmp->next;
  }
  return NULL;
}

//#endif
