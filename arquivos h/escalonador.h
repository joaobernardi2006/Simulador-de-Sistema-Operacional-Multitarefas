#ifndef ESCALONADOR_H
#define ESCALONADOR_H

/* ============================================================================
 * escalonador.h — Interface dos algoritmos de escalonamento.
 *
 * Cada função implementa um algoritmo diferente, mas todas seguem a mesma
 * assinatura, o que permite registrá-las no ponteiro ConfigSistema.escalonador
 * sem modificar o restante do simulador (padrão Strategy — requisito 4.2).
 *
 * Para adicionar um novo algoritmo:
 *   1. Declare a função aqui com a assinatura abaixo.
 *   2. Implemente-a em escalonador.c.
 *   3. Registre o ponteiro em config.escalonador dentro de lerConfiguracao().
 *   O simulador não precisa de nenhuma outra alteração.
 * ============================================================================ */

#include "tipos.h"

/*
 * Assinatura comum a todos os escalonadores:
 *
 *   tarefa_atual  — tarefa em execução no momento do escalonamento.
 *                   Usada como critério de desempate (evita troca de contexto
 *                   desnecessária — requisito 4.3, critério 1).
 *                   Pode ser NULL quando nenhuma tarefa está em execução.
 *
 *   fila_prontas  — fila de tarefas no estado PRONTA aguardando CPU.
 *
 *   diagrama      — matriz do Gantt; usada apenas para marcar células de
 *                   sorteio (campo DiagramaGantt.sorteio).
 *
 *   tick          — instante atual da simulação (necessário para marcar
 *                   a célula correta no diagrama em caso de sorteio).
 *
 * Retorno: ponteiro para a TCB da próxima tarefa a ser executada.
 */

/* SRTF — Shortest Remaining Time First (preemptivo).
 * Escolhe a tarefa com menor tempo restante.  Em caso de empate aplica
 * os critérios do requisito 4.3 na ordem: tarefa atual → ingresso → duração
 * → sorteio. */
TCB *escalonador_SRTF(TCB *tarefa_atual, Queue *fila_prontas,
                      DiagramaGantt **diagrama, int tick);

/* PRIOP — Prioridade Preemptiva.
 * Escolhe a tarefa com maior prioridade estática.  Em caso de empate aplica
 * primeiro a prioridade (requisito 4.4) e depois os critérios do requisito 4.3. */
TCB *escalonador_PRIOP(TCB *tarefa_atual, Queue *fila_prontas,
                       DiagramaGantt **diagrama, int tick);

#endif /* ESCALONADOR_H */
