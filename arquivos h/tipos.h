#ifndef TIPOS_H
#define TIPOS_H

/* ============================================================================
 * tipos.h — Definições centrais de tipos, constantes e structs do simulador.
 *
 * Este arquivo é o "contrato" compartilhado entre todos os módulos: qualquer
 * arquivo .c que precise de um tipo ou constante do simulador inclui apenas
 * este header, sem depender da ordem de inclusão de outros headers.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <time.h>

/* --------------------------------------------------------------------------
 * Constantes globais do sistema
 * -------------------------------------------------------------------------- */

#define MAX_TICKS    1000   /* Número máximo de ticks que a simulação pode ter  */
#define N_CPUs       5      /* Qtde padrão de CPUs (sobrescrita pelo arquivo)   */
#define QUANTUM      2      /* Quantum padrão (sobrescrito pelo arquivo)        */
#define MAX_HISTORICO 100   /* Limite de estados salvos no modo passo-a-passo  */

/* --------------------------------------------------------------------------
 * Estados possíveis de uma tarefa (campo TCB.estado)
 * -------------------------------------------------------------------------- */
#define NOVA       1
#define PRONTA     2
#define EXECUTANDO 3
#define TERMINADA  4
#define SUSPENSA   5

/* --------------------------------------------------------------------------
 * Estados possíveis de uma CPU (campo CPU.ocupado)
 * -------------------------------------------------------------------------- */
#define LIVRE   1
#define OCUPADA 0

/* --------------------------------------------------------------------------
 * Flags auxiliares usadas no diagrama de Gantt
 * -------------------------------------------------------------------------- */
#define SORTEADO 6   /* Indica desempate por sorteio no diagrama              */
#define OCIOSA   7   /* (reservado para uso futuro / extensões)               */

/* --------------------------------------------------------------------------
 * Identificadores dos algoritmos de escalonamento
 * -------------------------------------------------------------------------- */
#define SRTF  1   /* Shortest Remaining Time First                            */
#define PRIOP 2   /* Prioridade Preemptivo                                    */

/* ============================================================================
 * Structs
 * ============================================================================ */

/*
 * TCB — Task Control Block
 * Armazena todas as informações de uma tarefa antes, durante e após a simulação.
 * É a estrutura central do simulador (requisito 1.3).
 */
typedef struct
{
    int  id_tarefa;           /* Identificador único definido no arquivo        */
    int  indice;              /* Posição no vetor interno de tarefas (0-based)  */
    int  estado;              /* Estado atual: NOVA, PRONTA, EXECUTANDO, etc.   */
    int  contexto;            /* 1 = tarefa ativa em alguma CPU; 0 = inativa    */

    int  ingresso;            /* Tick em que a tarefa passa de NOVA → PRONTA    */
    int  duracao;             /* Duração total de execução (lida do arquivo)    */
    int  termino;             /* Tick em que a tarefa terminou (-1 se pendente) */
    int  espera;              /* Ticks acumulados esperando na fila de prontas  */

    int  restante;            /* Tempo restante até o fim da execução           */
    int  quantum_restante;    /* Ticks restantes do quantum atual               */
    int  prioridade;          /* Prioridade estática (maior = mais prioritário) */

    char lista_eventos[256];  /* Campo reservado para o Projeto B (mutex/IO)   */
    char cor[8];              /* Cor da tarefa em hex RGB, ex: "F0E0D0"         */
} TCB;


/*
 * CPU — representa um processador do sistema simulado.
 * Cada CPU pode estar livre ou ocupada executando uma tarefa (requisito 1.2).
 */
typedef struct
{
    int  ocupado;                   /* LIVRE (1) ou OCUPADA (0)                */
    TCB *tarefa_atual;              /* Ponteiro para a tarefa em execução       */
    int  tempo_desligado;           /* Total de ticks em que ficou ociosa       */

    /* Vetor tick-a-tick de ociosidade: permite que múltiplas CPUs registrem
     * ociosidade no mesmo tick de forma independente (requisito 1.2). */
    int  ocioso_ticks[MAX_TICKS];
} CPU;


/*
 * Queue — fila circular de ponteiros para TCB.
 * Usada como fila de prontas do escalonador (política FIFO de inserção).
 */
typedef struct
{
    int   front;    /* Índice do primeiro elemento                             */
    int   back;     /* Índice da próxima posição livre (após o último elemento)*/
    int   size;     /* Capacidade total da fila (em slots)                     */
    TCB **array;    /* Vetor de ponteiros para as tarefas                      */
} Queue;


/*
 * DiagramaGantt — célula da matriz de visualização.
 * A matriz tem dimensão [qtde_tarefas][MAX_TICKS]: cada célula registra o
 * estado da tarefa e a CPU usada naquele tick.
 */
typedef struct
{
    int estado;   /* Estado da tarefa neste tick (mesmo enum de TCB.estado)    */
    int cpu;      /* Índice da CPU usada (-1 se não estava em execução)        */
    int sorteio;  /* 1 = este tick foi decidido por sorteio de desempate       */
} DiagramaGantt;


/*
 * ConfigSistema — parâmetros lidos do arquivo de configuração.
 *
 * O ponteiro de função `escalonador` implementa o padrão Strategy: permite
 * adicionar novos algoritmos sem modificar o código da simulação — basta
 * implementar uma função com a mesma assinatura e registrá-la aqui
 * (requisito 4.2).
 */
typedef struct
{
    int  algoritmo_escalonamento;   /* SRTF ou PRIOP                           */
    int  quantum;                   /* Duração máxima de cada fatia de CPU     */
    int  qtde_cpus;                 /* Número de processadores do sistema       */
    char nome_arquivo[256];         /* Nome base do arquivo (sem extensão)     */

    /* Ponteiro para o escalonador escolhido — mesma assinatura para todos:
     *   tarefa_atual : tarefa em execução no momento (pode ser NULL)
     *   fila_prontas : fila de tarefas prontas
     *   diagrama     : matriz do diagrama (para marcar sorteios)
     *   tick         : tick atual da simulação
     * Retorna: ponteiro para a próxima tarefa a executar.                      */
    TCB *(*escalonador)(TCB *, Queue *, DiagramaGantt **, int);
} ConfigSistema;


/*
 * EstadoSistema — nó de uma lista duplamente encadeada de snapshots.
 * Cada nó representa o estado completo do sistema em um dado tick.
 * Permite avançar e retroceder na simulação passo-a-passo (requisito 1.5.2).
 */
typedef struct EstadoSistema
{
    int   tick;
    TCB  *tarefas_novas;      /* Cópia completa do vetor de tarefas            */
    Queue *fila_prontas;      /* Cópia da fila de prontas (deep copy)          */
    CPU  *CPUs;               /* Cópia do vetor de CPUs                        */
    DiagramaGantt **diagrama; /* Cópia da matriz do diagrama                   */

    struct EstadoSistema *anterior; /* Estado anterior (NULL se for o inicial) */
    struct EstadoSistema *proximo;  /* Estado seguinte (NULL se for o mais atual)*/
} EstadoSistema;

#endif /* TIPOS_H */
