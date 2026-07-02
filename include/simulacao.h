#ifndef SIMULACAO_H
#define SIMULACAO_H
#include "tipos.h"
void inicializar_cpu(CPU *cpu);
void inicializar_tcb_padrao(TCB *t, int quantum);
void inicializar_config_padrao(ConfigSistema *config);
void inicializar_mutexes(Mutex *mutexes);
void resetar_acoes(TCB *tarefas, int qtde_tarefas);
int  tick_valido(int tick);
void definir_limites(int limite_ticks, int qtde_mutexes, int qtde_cpus);
int  limite_ticks_atual(void);
int  qtde_mutexes_atual(void);
int  buscar_indice_por_id(TCB *tarefas, int qtde_tarefas, int id);
void marcar_prontas(TCB *tarefas, int tick, DiagramaGantt **diagrama, int qtde_tarefas);
void enfileirar_prontas(TCB *tarefas, Queue *fila_prontas, int tick, int qtde_tarefas);
void simular_tick(TCB *tarefas, int *tick, Queue *fila_prontas, CPU *CPUs,
                  Mutex *mutexes, DiagramaGantt **diagrama, ConfigSistema *config,
                  int qtde_tarefas, int *tarefas_finalizadas);
void execucao_completa(TCB *tarefas, int *tick, Queue *fila_prontas, CPU *CPUs,
                       Mutex *mutexes, int *tick_max, DiagramaGantt **diagrama,
                       ConfigSistema *config, int qtde_tarefas);
EstadoSistema *salvar_estado(int tick, TCB *tarefas, Queue *fila, CPU *CPUs,
                             Mutex *mutexes, DiagramaGantt **diagrama,
                             int qtde_tarefas, int qtde_cpus);
EstadoSistema *executar_tick(EstadoSistema *atual, ConfigSistema *config, int qtde_tarefas);
void destruir_historico(EstadoSistema *inicio, int qtde_tarefas);
void destruir_futuros(EstadoSistema *estado, int qtde_tarefas);
int  contar_historico(EstadoSistema *atual);
EstadoSistema *remover_estado_mais_antigo(EstadoSistema *inicio, int qtde_tarefas);
void execucao_passo_a_passo(TCB *tarefas, Queue *fila_prontas, CPU *CPUs,
                            Mutex *mutexes, int *tick_max, DiagramaGantt **diagrama,
                            ConfigSistema *config, int qtde_tarefas);
#endif
