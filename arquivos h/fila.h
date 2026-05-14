#ifndef FILA_H
#define FILA_H

/* ============================================================================
 * fila.h — Interface da fila circular de TCB*.
 *
 * A fila (Queue) é a estrutura de dados que implementa a fila de prontas do
 * sistema operacional simulado.  Todas as operações de inserção, remoção e
 * consulta sobre essa fila estão agrupadas aqui para facilitar a manutenção
 * e eventual substituição por outra estrutura (ex.: heap para SRTF puro).
 * ============================================================================ */

#include "tipos.h"

/* Cria e retorna uma fila com capacidade para `size` elementos. */
Queue *fila_criar(int size);

/* Libera toda a memória da fila (não libera as TCBs apontadas). */
void fila_destruir(Queue *q);

/* Enfileira `tarefa` no final da fila.  Aborta se cheia (overflow). */
void fila_enqueue(Queue *q, TCB *tarefa);

/* Remove e retorna o elemento da frente.  Aborta se vazia (underflow). */
TCB *fila_dequeue(Queue *q);

/* Retorna o ID da tarefa na frente, ou -1 se vazia. */
int fila_front_id(Queue *q);

/* Retorna o ID da tarefa no final, ou -1 se vazia. */
int fila_back_id(Queue *q);

/* Retorna 1 se a fila está vazia; 0 caso contrário. */
int fila_vazia(Queue *q);

/* Retorna 1 se a fila está cheia; 0 caso contrário. */
int fila_cheia(Queue *q);

/* Retorna a capacidade total da fila. */
int fila_tamanho(Queue *q);

/* Retorna 1 se `tarefa` está na fila; 0 caso contrário. */
int fila_contem(Queue *q, TCB *tarefa);

/* Remove `tarefa` da fila, reorganizando os demais elementos. */
void fila_remover(TCB *tarefa, Queue *fila_prontas);

/* Imprime os IDs de todas as tarefas na fila (diagnóstico). */
void fila_imprimir(Queue *q);

/* Cria uma cópia independente (deep copy) da fila, re-mapeando os ponteiros
 * para o novo vetor `novas_tarefas` através do campo TCB.indice. */
Queue *fila_copiar(Queue *fila_original, TCB *novas_tarefas);

#endif /* FILA_H */
