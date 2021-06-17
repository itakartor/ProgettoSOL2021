#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <string.h>

#include "queue.h"

//#ifndef QUEUE_H_
//#define QUEUE_H_

Queue* initQueue() { //inizializza una coda vuota
  Queue *q = malloc(sizeof(Queue));
  //Node n = malloc(sizeof(Node));
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

//#endif
