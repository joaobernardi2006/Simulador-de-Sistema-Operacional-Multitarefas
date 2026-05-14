/* ============================================================================
 * fila.c — Implementação da fila circular de TCB*.
 *
 * A fila circular usa dois índices (front/back) sobre um vetor alocado
 * dinamicamente. O slot extra (size = n + 1) separa fila cheia de fila vazia
 * sem precisar de um contador adicional.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include "fila.h"

/* -------------------------------------------------------------------------- */
/* fila_criar — aloca e inicializa uma fila circular com capacidade para `size`
 * elementos.  O vetor interno tem `size + 1` slots para distinguir cheio de
 * vazio. */
Queue *fila_criar(int size)
{
    Queue *q = (Queue *)malloc(sizeof(Queue));
    if (q == NULL)
    {
        perror("fila_criar");
        exit(1);
    }                                                        // checagem de alocação
    q->front = 0;                                            // início da fila
    q->back = 0;                                             // próxima posição livre
    q->size = size;                                          // capacidade total (em slots)
    q->array = (TCB **)malloc((size_t)size * sizeof(TCB *)); // vetor de ponteiros para TCBs
    if (q->array == NULL)
    {
        perror("fila_criar array");
        exit(1);
    } // checagem de alocação
    return q;
}

/* -------------------------------------------------------------------------- */
/* fila_destruir — libera a memória alocada para a fila.  Não libera as TCBs
 * apontadas, pois elas são gerenciadas separadamente. */
void fila_destruir(Queue *q) // libera a memória da fila circular
{
    if (q == NULL)
        return;     // checagem de ponteiro nulo
    free(q->array); // libera o vetor de ponteiros
    free(q);
}

/* -------------------------------------------------------------------------- */
/* fila_enqueue — insere uma tarefa no final da fila. */
void fila_enqueue(Queue *q, TCB *tarefa)
{
    if (fila_cheia(q)) // checagem de overflow
    {
        fprintf(stderr, "ERRO: fila overflow!\n");
        exit(1);
    }
    q->array[q->back] = tarefa;        // insere a tarefa no slot de back
    q->back = (q->back + 1) % q->size; // avança o índice de back circularmente
}

/* -------------------------------------------------------------------------- */
TCB *fila_dequeue(Queue *q) // remove e retorna a tarefa da frente da fila
{
    if (fila_vazia(q)) // checagem de underflow
    {
        fprintf(stderr, "ERRO: fila underflow!\n");
        exit(1);
    }
    TCB *tarefa = q->array[q->front];    // obtém a tarefa da frente
    q->front = (q->front + 1) % q->size; // avança o índice de front circularmente
    return tarefa;
}

/* -------------------------------------------------------------------------- */
int fila_front_id(Queue *q) // retorna o ID da tarefa na frente da fila, ou -1 se vazia
{
    if (fila_vazia(q))
        return -1;
    else
        return q->array[q->front]->id_tarefa;
}

/* -------------------------------------------------------------------------- */
int fila_back_id(Queue *q) // retorna o ID da tarefa no final da fila, ou -1 se vazia
{
    if (fila_vazia(q))
        return -1;                                                 // se a fila está vazia, não há tarefa no final
    return q->array[(q->back - 1 + q->size) % q->size]->id_tarefa; // índice do último elemento é back - 1, ajustado circularmente
}

/* -------------------------------------------------------------------------- */
int fila_vazia(Queue *q) // retorna 1 se a fila está vazia, ou 0 caso contrário
{
    return (q->front == q->back); // fila vazia quando front e back são iguais
}

/* -------------------------------------------------------------------------- */
int fila_cheia(Queue *q) // retorna 1 se a fila está cheia, ou 0 caso contrário
{
    return (q->front == (q->back + 1) % q->size); // fila cheia quando o próximo slot de back é front
}

/* -------------------------------------------------------------------------- */
int fila_tamanho(Queue *q) // retorna a capacidade total da fila (em slots)
{
    return q->size; 
}

/* -------------------------------------------------------------------------- */
int fila_contem(Queue *q, TCB *tarefa) // retorna 1 se a tarefa está na fila, ou 0 caso contrário
{
    for (int i = q->front; i != q->back; i = (i + 1) % q->size) // percorre a fila circularmente
        if (q->array[i] == tarefa)                              // compara os ponteiros para verificar se a tarefa está presente
            return 1;
    return 0;
}

/* --------------------------------------------------------------------------
 * fila_remover — remove uma tarefa específica da fila.
 *
 * Cria uma fila temporária, copia todos os elementos exceto o
 * alvo, depois repõe os elementos na fila original.
 * -------------------------------------------------------------------------- */
void fila_remover(TCB *tarefa, Queue *fila_prontas)
{
    Queue *temp = fila_criar(fila_prontas->size);

    /* Remove tudo e reinsere apenas o que não é o alvo */
    while (!fila_vazia(fila_prontas)) // enquanto a fila de prontas não estiver vazia
    {
        TCB *t = fila_dequeue(fila_prontas); // remove a tarefa da frente da fila
        if (t != NULL && t != tarefa)        // se a tarefa removida não é nula e não é a tarefa alvo
            fila_enqueue(temp, t);           // enfileira a tarefa na fila temporária
    }

    /* Repõe os elementos que sobraram */
    while (!fila_vazia(temp))                           // enquanto a fila temporária não estiver vazia
        fila_enqueue(fila_prontas, fila_dequeue(temp)); // enfileira de volta na fila original

    fila_destruir(temp);
}

/* -------------------------------------------------------------------------- */
void fila_imprimir(Queue *q) // imprime os IDs de todas as tarefas na fila para diagnóstico
{
    printf("Fila: ");
    for (int i = q->front; i != q->back; i = (i + 1) % q->size) // percorre a fila circularmente
        printf("%d ", q->array[i]->id_tarefa);                  // imprime o ID da tarefa no slot atual
    printf("\n");
}

/* --------------------------------------------------------------------------
 * fila_copiar — deep copy da fila, re-mapeando ponteiros para `novas_tarefas`.
 *
 * A cópia usa TCB.indice para encontrar a tarefa correspondente no novo vetor,
 * garantindo que os ponteiros internos não apontem para o vetor original.
 * -------------------------------------------------------------------------- */
Queue *fila_copiar(Queue *fila_original, TCB *novas_tarefas)
{
    Queue *nova = fila_criar(fila_original->size);               // cria uma nova fila com a mesma capacidade da original
    for (int i = fila_original->front; i != fila_original->back; // percorre a fila original circularmente
         i = (i + 1) % fila_original->size)                      // para cada tarefa na fila original
    {
        int indice = fila_original->array[i]->indice; // obtém o índice da tarefa no vetor original
        fila_enqueue(nova, &novas_tarefas[indice]);   // enfileira a tarefa correspondente do novo vetor na nova fila
    }
    return nova;
}
