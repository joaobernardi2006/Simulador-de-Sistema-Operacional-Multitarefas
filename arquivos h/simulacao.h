#ifndef SIMULACAO_H
#define SIMULACAO_H

/* ============================================================================
 * simulacao.h — Interface do motor de simulação.
 *
 * Este módulo contém toda a lógica de simulação tick-a-tick: avanço do relógio,
 * transições de estado das tarefas, preempção, quantum, contabilização de
 * ociosidade de CPU, e os dois modos de execução (completo e passo-a-passo).
 *
 * As funções de histórico (salvar/restaurar estado) também ficam aqui, pois são
 * exclusivas do modo passo-a-passo.
 * ============================================================================ */

#include "tipos.h"

/* --------------------------------------------------------------------------
 * Funções de inicialização
 * -------------------------------------------------------------------------- */

/* Inicializa uma CPU com valores padrão (livre, sem tarefa, sem ociosidade). */
void inicializar_cpu(CPU *cpu);

/* Inicializa um TCB com os valores de simulação padrão.
 * Os campos id, cor, ingresso, duracao e prioridade são preservados — apenas
 * os contadores de simulação são zerados (estado, restante, quantum, etc.). */
void inicializar_tcb_padrao(TCB *t, int quantum);

/* Inicializa a ConfigSistema com os valores padrão do sistema
 * (SRTF, QUANTUM, N_CPUs). Sobrescrita depois pela leitura do arquivo. */
void inicializar_config_padrao(ConfigSistema *configuracao);

/* --------------------------------------------------------------------------
 * Funções auxiliares da simulação
 * -------------------------------------------------------------------------- */

/* Retorna 1 se `tick` está dentro do intervalo válido [0, MAX_TICKS). */
int tick_valido(int tick);

/* Retorna o índice no vetor de tarefas cujo id_tarefa == `id`, ou -1. */
int buscar_indice_por_id(TCB *tarefas, int qtde_tarefas, int id);

/* Percorre as tarefas e muda de NOVA → PRONTA aquelas cujo ingresso == tick.
 * Atualiza a célula correspondente no diagrama. */
void marcar_prontas(TCB *tarefas, int tick, DiagramaGantt **diagrama,
                    int qtde_tarefas);

/* Insere na fila_prontas todas as tarefas no estado PRONTA cujo ingresso
 * já aconteceu e que ainda não estão na fila. */
void enfileirar_prontas(TCB *tarefas, Queue *fila_prontas, int tick,
                        int qtde_tarefas);

/* --------------------------------------------------------------------------
 * Motor principal — executa um tick da simulação
 * -------------------------------------------------------------------------- */

/*
 * simular_tick — avança a simulação em um passo (um tick de relógio).
 *
 * Sequência de ações em cada tick:
 *   1. Marca tarefas novas como PRONTA (ingresso == tick).
 *   2. Insere tarefas PRONTA na fila de prontas.
 *   3. Contabiliza tempo de espera das tarefas em PRONTA.
 *   4. Distribui tarefas prontas para CPUs livres (via escalonador).
 *   5. Verifica preempções (tarefa na fila com prioridade/restante superior).
 *   6. Decrementa restante/quantum das tarefas em execução.
 *   7. Finaliza tarefas com restante == 0; devolve tarefas com quantum esgotado.
 *
 * `tarefas_finalizadas` é incrementado a cada tarefa que termina neste tick.
 */
void simular_tick(TCB *tarefas, int *tick, Queue *fila_prontas, CPU *CPUs,
                  DiagramaGantt **diagrama, ConfigSistema *configuracao,
                  int qtde_tarefas, int *tarefas_finalizadas);

/* --------------------------------------------------------------------------
 * Modos de execução (requisito 1.5)
 * -------------------------------------------------------------------------- */

/* Execução completa — roda todos os ticks sem interação do usuário e exibe
 * o resultado final no terminal e em SVG (requisito 1.5.3 / 1.5.2-b). */
void execucao_completa(TCB *tarefas, int *tick, Queue *fila_prontas, CPU *CPUs,
                       int *tick_max, DiagramaGantt **diagrama,
                       ConfigSistema *configuracao, int qtde_tarefas);

/* Execução passo-a-passo — loop interativo com opções de avançar, retroceder
 * e modificar o estado de tarefas (requisito 1.5.1 / 1.5.2-a). */
void execucao_passo_a_passo(TCB *tarefas, Queue *fila_prontas, CPU *CPUs,
                             int *tick_max, DiagramaGantt **diagrama,
                             ConfigSistema *configuracao, int qtde_tarefas);

/* --------------------------------------------------------------------------
 * Histórico de estados (usado exclusivamente pelo modo passo-a-passo)
 * -------------------------------------------------------------------------- */

/* Aloca e preenche um novo nó EstadoSistema com cópia completa do estado
 * atual (deep copy de tarefas, fila, CPUs e diagrama). */
EstadoSistema *salvar_estado(int tick, TCB *tarefas, Queue *fila,
                              CPU *CPUs, DiagramaGantt **diagrama,
                              int qtde_tarefas, int qtde_cpus);

/* Executa um tick a partir de `atual`, salva o resultado como novo nó e
 * retorna o ponteiro para esse nó. Liga `novo->anterior = atual`. */
EstadoSistema *executar_tick(EstadoSistema *atual, ConfigSistema *configuracao,
                              int qtde_tarefas);

/* Libera todos os nós da lista a partir de `inicio` (inclusive). */
void destruir_historico(EstadoSistema *inicio, int qtde_tarefas);

/* Libera todos os nós após `estado` (estados futuros).
 * Chamado quando o usuário modifica o estado atual, invalidando o futuro. */
void destruir_futuros(EstadoSistema *estado, int qtde_tarefas);

/* Conta quantos nós existem na lista inteira (vai ao início e percorre). */
int contar_historico(EstadoSistema *atual);

/* Remove e libera o nó mais antigo da lista; retorna o novo início. */
EstadoSistema *remover_estado_mais_antigo(EstadoSistema *inicio,
                                           int qtde_tarefas);

#endif /* SIMULACAO_H */
