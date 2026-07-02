#ifndef ESCALONADOR_H
#define ESCALONADOR_H
#include "tipos.h"
TCB *escalonador_SRTF(TCB *tarefa_atual, Queue *fila_prontas,
                      DiagramaGantt **diagrama, int tick);
TCB *escalonador_PRIOP(TCB *tarefa_atual, Queue *fila_prontas,
                       DiagramaGantt **diagrama, int tick);
TCB *escalonador_PRIOPENV(TCB *tarefa_atual, Queue *fila_prontas,
                          DiagramaGantt **diagrama, int tick);
void atualizar_prioridades_dinamicas(Queue *fila_prontas, int alpha);
#endif
