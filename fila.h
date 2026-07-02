#ifndef FILA_H
#define FILA_H
#include "tipos.h"
Queue *fila_criar(int size);
void   fila_destruir(Queue *q);
void   fila_enqueue(Queue *q, TCB *tarefa);
TCB   *fila_dequeue(Queue *q);
int    fila_front_id(Queue *q);
int    fila_back_id(Queue *q);
int    fila_vazia(Queue *q);
int    fila_cheia(Queue *q);
int    fila_tamanho(Queue *q);
int    fila_contem(Queue *q, TCB *tarefa);
void   fila_remover(TCB *tarefa, Queue *fila_prontas);
void   fila_imprimir(Queue *q);
Queue *fila_copiar(Queue *fila_original, TCB *novas_tarefas);
#endif
