#ifndef TIPOS_H
#define TIPOS_H

/* ============================================================================
 * tipos.h — Tipos, enumeracoes e constantes globais do simulador.
 * (Header reconstruido + extensoes do Projeto B: mutexes)
 * ============================================================================ */

/* ---- Constantes de configuracao padrao ---- */
#define QUANTUM       2     /* quantum padrao (sobrescrito pelo arquivo)      */
#define N_CPUs        5     /* qtde de CPUs padrao (sobrescrito pelo arquivo) */
#define MAX_TICKS     1000  /* limite de ticks da simulacao                   */
#define MAX_HISTORICO 200   /* limite de estados guardados no passo-a-passo   */

/* ---- Projeto B: mutexes ---- */
#define MAX_MUTEXES   64    /* quantidade maxima de mutexes suportados        */
#define MAX_ACOES     32    /* quantidade maxima de acoes de mutex por tarefa */

/* ---- Estados de uma tarefa (TCB.estado) ---- */
enum EstadoTarefa
{
    DESCONHECIDO = 0,
    NOVA         = 1,
    PRONTA       = 2,
    EXECUTANDO   = 3,
    TERMINADA    = 4,
    SUSPENSA     = 5
};

/* ---- Algoritmos de escalonamento ---- */
enum Algoritmo
{
    SRTF = 0,
    PRIOP,
    PRIOPENV
};

/* ---- Estado de ocupacao de uma CPU (CPU.ocupado) ---- */
enum Ocupacao
{
    LIVRE   = 0,
    OCUPADA = 1
};

/* ---- Tipo de acao de mutex (AcaoMutex.tipo / DiagramaGantt.evento) ---- */
enum TipoAcao
{
    ACAO_NENHUMA = 0,
    ACAO_ML      = 1,   /* solicitar (lock)   — formato MLxx:nn */
    ACAO_MU      = 2,   /* liberar  (unlock)  — formato MUxx:nn */
    ACAO_IO      = 3    /* operacao de E/S     — formato IO:xx-yy   */
};

/* ---- Motivo de uma suspensao (TCB.motivo_suspensao / celula.motivo_susp) ---- */
enum MotivoSuspensao
{
    SUSP_NENHUM = 0,
    SUSP_MUTEX  = 1,    /* bloqueada aguardando um mutex ocupado */
    SUSP_IO     = 2     /* bloqueada por E/S (reservado p/ futuro) */
};

/* ============================================================================
 * AcaoMutex — uma acao de solicitar/liberar mutex de uma tarefa
 * ============================================================================ */
typedef struct
{
    int tipo;       /* ACAO_ML ou ACAO_MU                                    */
    int mutex;      /* numero do mutex (0..MAX_MUTEXES-1)                    */
    int instante;   /* instante RELATIVO ao inicio da tarefa (ticks exec.)   */
    int duracao;    /* duracao da operacao (apenas E/S; ticks suspensa)     */
    int ordem;      /* ordem de declaracao no arquivo (desempate, req. 2.5)  */
    int executada;  /* 1 apos a acao ter sido efetivada                      */
} AcaoMutex;

/* ============================================================================
 * Celula do diagrama de Gantt [tarefa][tick]
 * ============================================================================ */
typedef struct
{
    int estado;       /* estado da tarefa neste tick                         */
    int cpu;          /* indice da CPU em que executou (-1 se nenhuma)        */
    int sorteio;      /* 1 se houve desempate aleatorio neste tick           */
    int evento;       /* ACAO_NENHUMA / ACAO_ML / ACAO_MU neste tick         */
    int mutex_evento; /* numero do mutex do evento (quando evento != 0)      */
    int motivo_susp;  /* SUSP_MUTEX / SUSP_IO quando estado == SUSPENSA      */
} DiagramaGantt;

/* ============================================================================
 * TCB — Task Control Block
 * ============================================================================ */
typedef struct
{
    int  id_tarefa;            /* identificador (rotulo) da tarefa            */
    char cor[8];               /* cor "RRGGBB" (6 hex + terminador)           */
    int  ingresso;             /* instante de chegada                         */
    int  duracao;              /* duracao total (burst)                       */
    int  prioridade;           /* prioridade estatica                         */
    int  prioridade_dinamica;  /* prioridade envelhecida (PRIOPEnv)           */
    int  restante;             /* tempo restante de execucao                  */
    int  quantum_restante;     /* quantum restante na CPU atual               */
    int  estado;               /* estado atual (enum EstadoTarefa)            */
    int  contexto;             /* 1 se a tarefa esta ativa em alguma CPU      */
    int  termino;              /* tick de termino (-1 se nao terminou)        */
    int  espera;               /* ticks aguardando na fila de prontas         */
    int  indice;               /* posicao da tarefa no vetor (0-based)        */
    char lista_eventos[256];   /* texto cru dos eventos (compatibilidade)     */

    /* ---- Projeto B: mutexes ---- */
    AcaoMutex *acoes;          /* acoes/eventos (vetor dinamico, sem limite fixo) */
    int  cap_acoes;            /* capacidade alocada de `acoes`                */
    int  qtde_acoes;           /* quantidade de acoes em `acoes`              */
    int  mutex_esperado;       /* mutex que a tarefa aguarda (-1 se nenhum)   */
    int  motivo_suspensao;     /* SUSP_NENHUM / SUSP_MUTEX / SUSP_IO          */
    int  tick_bloqueio;        /* tick em que bloqueou (FIFO de espera; -1)   */
    int  io_wake;              /* tick do IRQ que encerra a E/S (-1 se nenhum)*/
    int  cpu_atual;            /* CPU em que rodou por ultimo (afinidade; -1) */
} TCB;

/* ============================================================================
 * Mutex — recurso de exclusao mutua
 * ============================================================================ */
typedef struct
{
    int ocupado;   /* 1 se algum dono o detem                                */
    int dono;      /* indice da tarefa dona (-1 se livre)                    */
} Mutex;

/* ============================================================================
 * Fila circular de ponteiros para TCB
 * ============================================================================ */
typedef struct
{
    int   front;
    int   back;
    int   size;
    TCB **array;
} Queue;

/* ============================================================================
 * CPU
 * ============================================================================ */
typedef struct
{
    int  ocupado;
    TCB *tarefa_atual;
    int  tempo_desligado;
    int *ocioso_ticks;         /* ociosidade por tick (dinamico, tam. limite_ticks) */
} CPU;

/* ---- Ponteiro de funcao para os escalonadores (Strategy Pattern) ---- */
typedef TCB *(*Escalonador)(TCB *tarefa_atual, Queue *fila_prontas,
                            DiagramaGantt **diagrama, int tick);

/* ============================================================================
 * Configuracao do sistema (lida do arquivo)
 * ============================================================================ */
typedef struct
{
    int          algoritmo_escalonamento;
    int          quantum;
    int          qtde_cpus;
    int          alpha;
    Escalonador  escalonador;
    char         nome_arquivo[256];
    int          qtde_mutexes;   /* mutexes usados (dinamico; sem limite fixo)   */
    int          limite_ticks;   /* teto de ticks dimensionado pela carga         */
} ConfigSistema;

/* ============================================================================
 * Estado do sistema (no da lista duplamente ligada do historico)
 * ============================================================================ */
typedef struct EstadoSistema
{
    int                   tick;
    TCB                  *tarefas_novas;
    Queue                *fila_prontas;
    CPU                  *CPUs;
    Mutex                *mutexes;      /* Projeto B: tabela de mutexes        */
    DiagramaGantt       **diagrama;
    struct EstadoSistema *anterior;
    struct EstadoSistema *proximo;
} EstadoSistema;

#endif /* TIPOS_H */
