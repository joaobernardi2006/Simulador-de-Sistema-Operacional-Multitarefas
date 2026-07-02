/* ============================================================================
 * simulacao.c — Motor de simulação tick-a-tick e gerenciamento de histórico.
 *
 * Este módulo implementa:
 *   • Inicialização de TCBs e CPUs para a simulação.
 *   • simular_tick(): o coração do simulador — avança um passo do relógio.
 *   • execucao_completa(): loop até todas as tarefas terminarem.
 *   • execucao_passo_a_passo(): loop interativo com histórico navegável.
 *   • Funções de histórico (salvar/restaurar/destruir EstadoSistema).
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "../include/simulacao.h"
#include "../include/fila.h"
#include "../include/escalonador.h"
#include "../include/visualizacao.h"

/* ============================================================================
 * Limites dinamicos do sistema (sem teto fixo).
 * Dimensionados a partir do arquivo de configuracao em definir_limites():
 *   g_limite_ticks  -> teto de ticks (carga de trabalho)  -> antigo g_limite_ticks
 *   g_qtde_mutexes  -> numero de mutexes usados            -> antigo g_qtde_mutexes
 * Usar globais aqui evita propagar esses valores por dezenas de assinaturas.
 * ============================================================================ */
static int g_limite_ticks = 1000;
static int g_qtde_mutexes  = 64;
static int g_qtde_cpus     = 0;

void definir_limites(int limite_ticks, int qtde_mutexes, int qtde_cpus)
{
    if (limite_ticks > 0) g_limite_ticks = limite_ticks;
    if (qtde_mutexes >= 0) g_qtde_mutexes = qtde_mutexes;
    if (qtde_cpus > 0) g_qtde_cpus = qtde_cpus;
}
int limite_ticks_atual(void) { return g_limite_ticks; }
int qtde_mutexes_atual(void) { return g_qtde_mutexes; }

/* ============================================================================
 * Funções de inicialização
 * ============================================================================ */

void inicializar_cpu(CPU *cpu)//Zera os campos de uma CPU para o estado inicial (LIVRE, sem tarefa, sem ociosidade)
{
    cpu->ocupado        = LIVRE;//Define a CPU como LIVRE
    cpu->tarefa_atual   = NULL;//Nenhuma tarefa está em execução nesta CPU
    cpu->tempo_desligado = 0;//Inicialmente, a CPU não ficou desligada em nenhum tick
    if (cpu->ocioso_ticks)
        memset(cpu->ocioso_ticks, 0, (size_t)g_limite_ticks * sizeof(int));//Zera o vetor de ociosidade (tamanho dinamico)
}

void inicializar_tcb_padrao(TCB *t, int quantum)//Configura os campos de um TCB para o estado inicial padrão, com base na duração lida do arquivo e no quantum definido
{
    t->estado          = NOVA;//Define o estado da tarefa como NOVA
    t->contexto        = 0;//A tarefa ainda não está ativa em nenhuma CPU
    t->termino         = -1;//A tarefa ainda não terminou, então o término é marcado como -1
    t->espera          = 0;//Inicialmente, a tarefa não acumulou tempo de espera na fila de prontas
    t->restante        = t->duracao; /* duracao já foi preenchida pelo arquivo */
    t->quantum_restante = quantum;//Inicialmente, a tarefa tem o quantum completo disponível para execução
    t->prioridade_dinamica = t->prioridade; /* inicia igual à prioridade estática; ajustada pelo PRIOPEnv */

    /* Projeto B: estado de mutex. Nao mexe em qtde_acoes/acoes (dados lidos do
     * arquivo); apenas reinicia os campos de runtime. */
    t->mutex_esperado    = -1;
    t->motivo_suspensao  = SUSP_NENHUM;
    t->tick_bloqueio     = -1;
    t->io_wake           = -1;   /* nenhuma E/S pendente */
    t->cpu_atual         = -1;   /* sem CPU ainda (afinidade) */
}

/* Reinicia o estado de execucao das acoes de mutex (entre re-execucoes). */
void resetar_acoes(TCB *tarefas, int qtde_tarefas)
{
    for (int i = 0; i < qtde_tarefas; i++)
        for (int a = 0; a < tarefas[i].qtde_acoes; a++)
            tarefas[i].acoes[a].executada = 0;
}

/* Projeto B: deixa todos os mutexes livres. */
void inicializar_mutexes(Mutex *mutexes)
{
    for (int m = 0; m < g_qtde_mutexes; m++)
    {
        mutexes[m].ocupado = 0;
        mutexes[m].dono    = -1;
    }
}

void inicializar_config_padrao(ConfigSistema *config)//Configura os campos de ConfigSistema para os valores padrão, escolhendo o algoritmo SRTF e o escalonador correspondente
{
    config->algoritmo_escalonamento = SRTF;
    config->qtde_cpus               = N_CPUs;
    config->quantum                 = QUANTUM;
    config->alpha                   = 1; /* valor padrão para envelhecimento */
    config->escalonador             = escalonador_SRTF;
    config->qtde_mutexes            = 0;    /* recalculado apos ler as tarefas */
    config->limite_ticks            = 1000; /* recalculado apos ler as tarefas */
}

/* ============================================================================
 * Funções auxiliares
 * ============================================================================ */
/* Verifica se um tick é válido (entre 0 e g_limite_ticks-1) */
int tick_valido(int tick)
{
    return (tick >= 0 && tick < g_limite_ticks); //Garante que o tick esteja dentro dos limites definidos para a simulação
}

int buscar_indice_por_id(TCB *tarefas, int qtde_tarefas, int id)//Busca o índice de uma tarefa no vetor de TCBs com base no seu ID, retornando -1 se não for encontrada
{
    for (int i = 0; i < qtde_tarefas; i++)//Percorre o vetor de tarefas para encontrar a tarefa com o ID especificado
        if (tarefas[i].id_tarefa == id)//Compara o ID da tarefa atual com o ID buscado
            return i;//Se encontrar a tarefa com o ID correspondente, retorna o índice dela no vetor
    return -1;
}

/* -------------------------------------------------------------------------- */
/* Marca tarefas cujo ingresso chegou como PRONTA e atualiza o diagrama de Gantt.
 * Deve ser chamada no início de cada tick para refletir o ingresso de novas
 * tarefas na fila de prontas. */
void marcar_prontas(TCB *tarefas, int tick, DiagramaGantt **diagrama,
                    int qtde_tarefas)
{
    for (int i = 0; i < qtde_tarefas; i++)
    {
        if (tarefas[i].ingresso == tick && tarefas[i].estado == NOVA)//Verifica se a tarefa está ingressando na fila de prontas neste tick e se seu estado atual é NOVA
        {
            tarefas[i].estado = PRONTA;//Marca a tarefa como PRONTA, indicando que ela está pronta para ser escalonada
            if (tick_valido(tick))
                diagrama[i][tick].estado = PRONTA; //Atualiza o diagrama de Gantt para refletir que a tarefa está pronta neste tick
        }
    }
}

/* -------------------------------------------------------------------------- */
/* Enfileira tarefas PRONTA na fila de prontas, garantindo que não sejam enfileiradas
 * tarefas que já estão na fila ou em execução. Deve ser chamada após marcar as
 * tarefas como PRONTA para garantir que a fila de prontas esteja atualizada. */
void enfileirar_prontas(TCB *tarefas, Queue *fila_prontas, int tick,
                        int qtde_tarefas)
{
    for (int i = 0; i < qtde_tarefas && !fila_cheia(fila_prontas); i++)//Garante que a fila de prontas não esteja cheia
    {
        if (tarefas[i].estado == PRONTA && tarefas[i].ingresso <= tick)//Verifica se a tarefa está PRONTA e se seu ingresso já ocorreu (ou seja, se ela já deveria estar na fila de prontas)
            if (!fila_contem(fila_prontas, &tarefas[i]))//Garante que a tarefa não esteja já na fila de prontas para evitar duplicatas
                fila_enqueue(fila_prontas, &tarefas[i]);//Enfileira a tarefa na fila de prontas, tornando-a candidata para escalonamento nas próximas etapas da simulação
    }
}

/* ============================================================================
 * simular_tick — núcleo da simulação
 * ============================================================================ */
/* Esta função avança um passo do relógio, realizando todas as ações necessárias para simular o comportamento do sistema operacional em um tick.
 Ela deve ser chamada iterativamente para avançar a simulação, seja no modo passo-a-passo ou na execução completa. 
 As principais responsabilidades desta função incluem:
   1. Marcar tarefas cujo ingresso chegou como PRONTA.
   2. Enfileirar tarefas PRONTA na fila de prontas.
   3. Contabilizar tempo de espera das tarefas aguardando na fila.
   4. Distribuir tarefas prontas para CPUs livres via escalonador.
   5. Verificar preempções na fila de prontas e preemptar se necessário.
   6. Contabilizar execução das tarefas em CPU e verificar fim de tarefa ou esgotamento de quantum.
*/
/* ----------------------------------------------------------------------------
 * conceder_mutex — ao liberar o mutex `m`, concede-o a uma tarefa que o aguarda.
 *
 * Escolhe, entre as tarefas SUSPENSAS por esse mutex, a que bloqueou primeiro
 * (FIFO por tick_bloqueio; desempate pelo menor indice). A tarefa escolhida
 * passa a deter o mutex, volta para PRONTA e e re-enfileirada, e sua acao ML
 * pendente e marcada como executada (mutex adquirido).
 * -------------------------------------------------------------------------- */
static void conceder_mutex(int m, Mutex *mutexes, TCB *tarefas,
                           int qtde_tarefas, Queue *fila_prontas)
{
    int melhor = -1;
    for (int i = 0; i < qtde_tarefas; i++)
    {
        if (tarefas[i].estado == SUSPENSA &&
            tarefas[i].motivo_suspensao == SUSP_MUTEX &&
            tarefas[i].mutex_esperado == m)
        {
            if (melhor == -1 ||
                tarefas[i].tick_bloqueio < tarefas[melhor].tick_bloqueio ||
                (tarefas[i].tick_bloqueio == tarefas[melhor].tick_bloqueio &&
                 tarefas[i].indice < tarefas[melhor].indice))
                melhor = i;
        }
    }
    if (melhor == -1) return; /* ninguem esperando: mutex fica livre */

    /* concede o mutex a tarefa escolhida */
    mutexes[m].ocupado = 1;
    mutexes[m].dono    = tarefas[melhor].indice;

    tarefas[melhor].estado           = PRONTA;
    tarefas[melhor].motivo_suspensao = SUSP_NENHUM;
    tarefas[melhor].mutex_esperado   = -1;
    tarefas[melhor].tick_bloqueio    = -1;
    tarefas[melhor].prioridade_dinamica = tarefas[melhor].prioridade; /* volta de suspensao: envelhecimento zerado */

    /* a acao ML pendente desse mutex passa a estar executada (adquirida) */
    for (int a = 0; a < tarefas[melhor].qtde_acoes; a++)
        if (!tarefas[melhor].acoes[a].executada &&
            tarefas[melhor].acoes[a].tipo == ACAO_ML &&
            tarefas[melhor].acoes[a].mutex == m)
        {
            tarefas[melhor].acoes[a].executada = 1;
            break;
        }

    if (!fila_contem(fila_prontas, &tarefas[melhor]) && !fila_cheia(fila_prontas))
        fila_enqueue(fila_prontas, &tarefas[melhor]);
}

/* ----------------------------------------------------------------------------
 * cpu_ordem_melhor — 1 se a tarefa `a` deve ocupar uma CPU de indice MENOR que
 * `b` (maior prioridade -> menor CPU), pelos mesmos criterios do escalonador.
 * -------------------------------------------------------------------------- */
static int cpu_ordem_melhor(TCB *a, TCB *b, ConfigSistema *config)
{
    if (config->algoritmo_escalonamento == SRTF)
    {
        if (a->restante != b->restante) return a->restante < b->restante;
    }
    else if (config->algoritmo_escalonamento == PRIOPENV)
    {
        if (a->prioridade_dinamica != b->prioridade_dinamica)
            return a->prioridade_dinamica > b->prioridade_dinamica;
        if (a->prioridade != b->prioridade) return a->prioridade > b->prioridade;
    }
    else /* PRIOP */
    {
        if (a->prioridade != b->prioridade) return a->prioridade > b->prioridade;
    }
    if (a->ingresso != b->ingresso) return a->ingresso < b->ingresso;
    if (a->duracao  != b->duracao)  return a->duracao  < b->duracao;
    return a->indice < b->indice;
}

static int mesma_banda(TCB *a, TCB *b, ConfigSistema *config)
{
    if (config->algoritmo_escalonamento == SRTF)
        return a->restante == b->restante;
    if (config->algoritmo_escalonamento == PRIOPENV)
        return a->prioridade_dinamica == b->prioridade_dinamica;
    return a->prioridade == b->prioridade; /* PRIOP */
}

void simular_tick(TCB *tarefas, int *tick, Queue *fila_prontas, CPU *CPUs,
                  Mutex *mutexes, DiagramaGantt **diagrama, ConfigSistema *config,
                  int qtde_tarefas, int *tarefas_finalizadas)
{
    /* 0. IRQ de E/S: acorda as tarefas cuja operacao de E/S terminou neste
     *    tick (o IRQ ocorre no instante imediatamente apos o termino, req. 3.4).
     *    A tarefa volta de SUSPENSA para PRONTA e sera enfileirada no passo 2. */
    for (int i = 0; i < qtde_tarefas; i++)
        if (tarefas[i].estado == SUSPENSA &&
            tarefas[i].motivo_suspensao == SUSP_IO &&
            tarefas[i].io_wake == *tick)
        {
            tarefas[i].estado            = PRONTA;
            tarefas[i].motivo_suspensao  = SUSP_NENHUM;
            tarefas[i].io_wake           = -1;
            tarefas[i].quantum_restante  = config->quantum; /* quantum novo ao retornar */
            tarefas[i].prioridade_dinamica = tarefas[i].prioridade; /* volta de suspensao: envelhecimento zerado */
            if (!fila_contem(fila_prontas, &tarefas[i]) && !fila_cheia(fila_prontas))
                fila_enqueue(fila_prontas, &tarefas[i]);
        }

    /* 1. Marca tarefas cujo ingresso chegou como PRONTA */
    marcar_prontas(tarefas, *tick, diagrama, qtde_tarefas);

    /* 2. Insere tarefas PRONTA na fila de prontas */
    enfileirar_prontas(tarefas, fila_prontas, *tick, qtde_tarefas);

    /* 3. Atualiza prioridades dinamicas para o algoritmo PRIOPEnv.
     * O envelhecimento usa a espera acumulada ate o tick anterior (a espera
     * deste tick so e contabilizada apos o despacho, no passo 5.1), de modo
     * que a decisao de escalonamento reflete o tempo ja aguardado. */
    if (config->algoritmo_escalonamento == PRIOPENV)
        atualizar_prioridades_dinamicas(fila_prontas, config->alpha);

    /* Protecao contra ping-pong no mesmo turno: uma tarefa que RECEBEU a CPU
     * neste tick (despacho ou preempcao) nao pode ser preemptada ainda neste
     * mesmo tick. Sem isso, no PRIOPEnv a tarefa envelhecida que preempta tem
     * sua prioridade dinamica resetada (pd <- pe) e e imediatamente preemptada
     * de volta pela tarefa que acabou de sair, anulando o envelhecimento.
     * CORRECAO: o vetor e alocado dinamicamente com o tamanho real de CPUs
     * (antes era um vetor fixo de 256 posicoes, que estourava a pilha quando o
     * usuario configurava mais de 256 processadores — a especificacao nao
     * impoe teto para a quantidade de CPUs). */
    int *despacho_no_tick = calloc((size_t)config->qtde_cpus, sizeof(int));
    if (!despacho_no_tick) { perror("simular_tick despacho"); exit(1); }

    /* 4. Distribui tarefas prontas para CPUs livres via escalonador.
     *    Define apenas QUAIS tarefas executam; a CPU final de cada uma e
     *    decidida no passo 5.5 (reatribuicao por prioridade + afinidade). */
    for (int i = 0; i < config->qtde_cpus; i++)
    {
        if (CPUs[i].ocupado == LIVRE && !fila_vazia(fila_prontas))
        {
            TCB *proxima = config->escalonador(NULL, fila_prontas, diagrama, *tick);
            fila_remover(proxima, fila_prontas);
            CPUs[i].tarefa_atual          = proxima;
            CPUs[i].ocupado               = OCUPADA;
            CPUs[i].tarefa_atual->contexto = 1;
            CPUs[i].tarefa_atual->estado  = EXECUTANDO;
            /* Maziero: a tarefa escolhida tem sua prioridade dinamica igualada
             * a estatica (pd <- pe); o envelhecimento recomeca do zero. */
            CPUs[i].tarefa_atual->prioridade_dinamica = CPUs[i].tarefa_atual->prioridade;
            despacho_no_tick[i] = 1; /* nao preemptavel neste mesmo tick */
        }
    }

    /* 5. Verifica preempcoes na fila de prontas.
     *    Repete enquanto a melhor candidata da fila for estritamente melhor
     *    que alguma tarefa em execucao, preemptando a PIOR delas a cada passo.
     *    Sem o laco, apenas UMA preempcao ocorreria por tick, fazendo uma
     *    tarefa de maior prioridade esperar enquanto uma de menor continua
     *    executando (sob contencao com varias chegadas no mesmo tick). */
    while (!fila_vazia(fila_prontas))
    {
        TCB *candidata = config->escalonador(NULL, fila_prontas, diagrama, *tick);//Obtem a melhor tarefa candidata da fila
        if (candidata == NULL)
            break;

        int pior_cpu   = -1;//indice do CPU com a pior tarefa em execucao
        int pior_valor = 0; /* quanto maior, pior a tarefa neste CPU */

        for (int i = 0; i < config->qtde_cpus; i++)
        {
            if (CPUs[i].ocupado != OCUPADA || CPUs[i].tarefa_atual == NULL)//CPU livre/sem tarefa nao pode ser preemptada
                continue;
            if (despacho_no_tick[i]) /* recebeu a CPU neste tick: protegida */
                continue;

            int deve_preemptar = 0;
            int valor          = 0;

            if (config->algoritmo_escalonamento == SRTF)
            {
                deve_preemptar = (candidata->restante < CPUs[i].tarefa_atual->restante);
                valor          = CPUs[i].tarefa_atual->restante; /* mais restante = pior */
            }
            else if (config->algoritmo_escalonamento == PRIOP)
            {
                deve_preemptar = (candidata->prioridade > CPUs[i].tarefa_atual->prioridade);
                valor          = -CPUs[i].tarefa_atual->prioridade; /* menos prio = pior */
            }
            else /* PRIOPENV */
            {
                deve_preemptar = (candidata->prioridade_dinamica > CPUs[i].tarefa_atual->prioridade_dinamica);
                valor          = -CPUs[i].tarefa_atual->prioridade_dinamica; /* menos prio_din = pior */
            }

            if (deve_preemptar && (pior_cpu == -1 || valor > pior_valor))
            {
                pior_cpu   = i;
                pior_valor = valor;
            }
        }

        /* Nenhuma tarefa em execucao e pior que a candidata: encerra o laco. */
        if (pior_cpu == -1)
            break;

        /* Devolve a tarefa preemptada a fila de prontas com quantum resetado */
        CPUs[pior_cpu].tarefa_atual->estado              = PRONTA;
        CPUs[pior_cpu].tarefa_atual->quantum_restante    = config->quantum;
        CPUs[pior_cpu].tarefa_atual->prioridade_dinamica = CPUs[pior_cpu].tarefa_atual->prioridade; /* reinicia envelhecimento */
        fila_enqueue(fila_prontas, CPUs[pior_cpu].tarefa_atual);

        /* Coloca a candidata no CPU liberado */
        fila_remover(candidata, fila_prontas);
        CPUs[pior_cpu].tarefa_atual           = candidata;
        CPUs[pior_cpu].tarefa_atual->estado   = EXECUTANDO;
        CPUs[pior_cpu].tarefa_atual->contexto = 1;
        /* Maziero: pd <- pe ao receber o processador */
        CPUs[pior_cpu].tarefa_atual->prioridade_dinamica = CPUs[pior_cpu].tarefa_atual->prioridade;
        despacho_no_tick[pior_cpu] = 1; /* nao preemptavel neste mesmo tick */
    }

    /* 5.5. Reatribuicao de CPUs (regra do gabarito).
     *
     * O conjunto de tarefas que executa neste tick ja esta definido (passos 4
     * e 5). Agora decidimos a CPU de cada uma:
     *   - processa as tarefas em execucao em ORDEM DE PRIORIDADE (maior
     *     prioridade -> menor indice de CPU);
     *   - cada tarefa volta para a CPU em que rodou no tick anterior
     *     (cpu_atual) se ela ainda nao foi tomada neste tick; caso contrario,
     *     ocupa a menor CPU livre.
     * Assim, uma tarefa de maior prioridade que reentra "empurra" uma de menor
     * para uma CPU de indice maior, e uma tarefa que continua executando
     * mantem sua CPU (afinidade) — reproduzindo o gabarito. */
    {
        /* CORRECAO: vetores dimensionados pela quantidade real de CPUs (antes
         * eram fixos em 256 posicoes e estouravam a pilha com mais CPUs). */
        TCB **run = malloc((size_t)config->qtde_cpus * sizeof(TCB *));
        if (!run) { perror("simular_tick run"); exit(1); }
        int  nrun = 0;
        for (int i = 0; i < config->qtde_cpus; i++)
            if (CPUs[i].ocupado == OCUPADA && CPUs[i].tarefa_atual != NULL)
                run[nrun++] = CPUs[i].tarefa_atual;

        for (int i = 0; i < config->qtde_cpus; i++)
        {
            CPUs[i].tarefa_atual = NULL;
            CPUs[i].ocupado      = LIVRE;
        }

        /* ordena por prioridade (insertion sort; nrun <= numero de CPUs) */
        for (int x = 1; x < nrun; x++)
        {
            TCB *chave = run[x];
            int  y = x - 1;
            while (y >= 0 && cpu_ordem_melhor(chave, run[y], config))
            {
                run[y + 1] = run[y];
                y--;
            }
            run[y + 1] = chave;
        }

        /* atribui CPUs processando por BANDA de prioridade (run[] ja esta
         * ordenado por prioridade). Dentro de cada banda, em 3 fases:
         *   (1) afinidade: quem tem cpu_atual livre fica nela;
         *   (2) novas (cpu_atual == -1): menor CPU livre, em ordem de ingresso;
         *   (3) deslocadas (tinham CPU, mas foi tomada): menor CPU livre.
         * Isso reproduz o gabarito: a prioridade manda no nivel da CPU, e dentro
         * da banda a tarefa que continua mantem sua CPU, a nova preenche o
         * primeiro buraco, e a deslocada vai para CPUs maiores. */
        int *claimed = calloc((size_t)(config->qtde_cpus > 0 ? config->qtde_cpus : 1), sizeof(int));
        int *feito   = calloc((size_t)(config->qtde_cpus > 0 ? config->qtde_cpus : 1), sizeof(int)); /* 1 se run[k] ja recebeu CPU */
        if (!claimed || !feito) { perror("simular_tick claimed/feito"); exit(1); }
        int i = 0;
        while (i < nrun)
        {
            int j = i;
            while (j < nrun && mesma_banda(run[i], run[j], config))
                j++; /* [i, j) = mesma banda de prioridade */

            /* fase 1: afinidade */
            for (int k = i; k < j; k++)
            {
                int p = run[k]->cpu_atual;
                if (p >= 0 && p < config->qtde_cpus && !claimed[p])
                {
                    claimed[p] = 1; feito[k] = 1;
                    CPUs[p].tarefa_atual = run[k];
                    CPUs[p].ocupado      = OCUPADA;
                    run[k]->cpu_atual    = p;
                }
            }
            /* fase 2: novas (sem CPU anterior) */
            for (int k = i; k < j; k++)
            {
                if (feito[k] || run[k]->cpu_atual != -1) continue;
                int c = 0; while (c < config->qtde_cpus && claimed[c]) c++;
                claimed[c] = 1; feito[k] = 1;
                CPUs[c].tarefa_atual = run[k];
                CPUs[c].ocupado      = OCUPADA;
                run[k]->cpu_atual    = c;
            }
            /* fase 3: deslocadas (perderam a CPU anterior) */
            for (int k = i; k < j; k++)
            {
                if (feito[k]) continue;
                int c = 0; while (c < config->qtde_cpus && claimed[c]) c++;
                claimed[c] = 1; feito[k] = 1;
                CPUs[c].tarefa_atual = run[k];
                CPUs[c].ocupado      = OCUPADA;
                run[k]->cpu_atual    = c;
            }
            i = j;
        }

        /* CPUs nao usadas: registra ociosidade (req. 1.2) */
        for (int c = 0; c < config->qtde_cpus; c++)
            if (!claimed[c])
            {
                CPUs[c].tempo_desligado++;
                if (tick_valido(*tick))
                    CPUs[c].ocioso_ticks[*tick] = 1;
            }

        /* tarefas que NAO rodam neste tick perdem a afinidade: ao voltar de um
         * buraco (espera/suspensao) reentram pela menor CPU livre. */
        for (int i = 0; i < qtde_tarefas; i++)
            if (tarefas[i].estado != EXECUTANDO)
                tarefas[i].cpu_atual = -1;

        /* libera os vetores auxiliares alocados dinamicamente neste bloco */
        free(run);
        free(claimed);
        free(feito);
    }

    /* 5.1. Contabiliza o tempo de espera deste tick.
     * Apos despacho e preempcoes, as tarefas que continuam PRONTA sao as que
     * realmente nao receberam CPU neste tick — essas, e somente essas,
     * aguardaram. Assim a espera nao inclui o tick em que a tarefa e
     * despachada nem os ticks de requeue por quantum em que ela retoma a CPU
     * imediatamente. */
    for (int i = 0; i < qtde_tarefas; i++)
        if (tarefas[i].estado == PRONTA)
            tarefas[i].espera++;

    /* 6 & 7. Acoes de mutex, execucao da tarefa, fim de tarefa / quantum.
     *
     * Para cada CPU ocupada, antes de consumir o tick, processa as acoes de
     * mutex cujo instante (relativo ao inicio da tarefa = duracao - restante)
     * coincide com o progresso atual, na ordem de declaracao (req. 2.5/2.6).
     *   - ML com mutex livre  -> adquire e continua executando.
     *   - ML com mutex ocupado-> a tarefa e SUSPENSA (por mutex) e libera o CPU.
     *   - MU                  -> libera o mutex e acorda quem esperava.
     * Se nao bloquear, a tarefa consome 1 tick normalmente. */
    for (int i = 0; i < config->qtde_cpus; i++)
    {
        if (CPUs[i].ocupado != OCUPADA || CPUs[i].tarefa_atual == NULL ||
            CPUs[i].tarefa_atual->estado != EXECUTANDO)
            continue;

        TCB *t   = CPUs[i].tarefa_atual;
        int  idx = t->indice;
        int  executados = t->duracao - t->restante; /* instante atual da tarefa */
        int  bloqueou   = 0;

        /* processa todas as acoes do instante atual, na ordem do arquivo */
        for (int a = 0; a < t->qtde_acoes && !bloqueou; a++)
        {
            if (t->acoes[a].executada || t->acoes[a].instante != executados)
                continue;

            int m = t->acoes[a].mutex;

            if (t->acoes[a].tipo == ACAO_ML)
            {
                if (mutexes[m].ocupado == 0 || mutexes[m].dono == idx)
                {
                    /* mutex livre (ou ja e dono): adquire e segue executando */
                    mutexes[m].ocupado = 1;
                    mutexes[m].dono    = idx;
                    t->acoes[a].executada = 1;
                    if (tick_valido(*tick))
                    {
                        diagrama[idx][*tick].evento       = ACAO_ML;
                        diagrama[idx][*tick].mutex_evento = m;
                    }
                }
                else
                {
                    /* mutex ocupado por outra tarefa: bloqueia (suspende) */
                    t->estado           = SUSPENSA;
                    t->motivo_suspensao = SUSP_MUTEX;
                    t->mutex_esperado   = m;
                    t->tick_bloqueio    = *tick;
                    t->contexto         = 0;
                    CPUs[i].tarefa_atual = NULL;
                    CPUs[i].ocupado      = LIVRE;
                    if (tick_valido(*tick))
                    {
                        diagrama[idx][*tick].estado       = SUSPENSA;
                        diagrama[idx][*tick].motivo_susp  = SUSP_MUTEX;
                        diagrama[idx][*tick].evento       = ACAO_ML;
                        diagrama[idx][*tick].mutex_evento = m;
                    }
                    bloqueou = 1; /* a acao sera concluida ao receber o mutex */
                }
            }
            else if (t->acoes[a].tipo == ACAO_IO)
            {
                /* Operacao de E/S: a tarefa solicita a E/S, e suspensa (libera
                 * o CPU) e so volta quando o IRQ ocorrer, `duracao` ticks depois
                 * (req. 3.4 / 3.7). O instante da E/S ja foi atingido (a tarefa
                 * (re)iniciou e executou ate aqui, conforme req. 3.6). */
                t->acoes[a].executada = 1;
                t->estado           = SUSPENSA;
                t->motivo_suspensao = SUSP_IO;
                t->mutex_esperado   = -1;
                t->tick_bloqueio    = *tick;
                t->io_wake          = *tick + t->acoes[a].duracao; /* IRQ apos a E/S */
                t->contexto         = 0;
                CPUs[i].tarefa_atual = NULL;
                CPUs[i].ocupado      = LIVRE;
                if (tick_valido(*tick))
                {
                    diagrama[idx][*tick].estado      = SUSPENSA;
                    diagrama[idx][*tick].motivo_susp = SUSP_IO;
                    diagrama[idx][*tick].evento      = ACAO_IO;
                }
                bloqueou = 1; /* tarefa suspensa por E/S: nao consome este tick */
            }
            else /* ACAO_MU */
            {
                t->acoes[a].executada = 1;
                if (tick_valido(*tick))
                {
                    diagrama[idx][*tick].evento       = ACAO_MU;
                    diagrama[idx][*tick].mutex_evento = m;
                }
                if (mutexes[m].dono == idx)
                {
                    mutexes[m].ocupado = 0;
                    mutexes[m].dono    = -1;
                    conceder_mutex(m, mutexes, tarefas, qtde_tarefas, fila_prontas);
                }
            }
        }

        if (bloqueou)
            continue; /* tarefa suspensa: nao consome este tick */

        /* consome 1 tick de execucao */
        if (tick_valido(*tick))
        {
            diagrama[idx][*tick].estado = EXECUTANDO;
            diagrama[idx][*tick].cpu    = i;
        }
        t->restante--;
        t->quantum_restante--;

        if (t->restante == 0)
        {
            /* Tarefa concluída: dispara acoes do instante final (ex.: MU no fim) */
            int exec_fim = t->duracao;
            for (int a = 0; a < t->qtde_acoes; a++)
            {
                if (t->acoes[a].executada || t->acoes[a].instante != exec_fim)
                    continue;
                int m = t->acoes[a].mutex;
                t->acoes[a].executada = 1;
                if (t->acoes[a].tipo == ACAO_MU && mutexes[m].dono == idx)
                {
                    mutexes[m].ocupado = 0;
                    mutexes[m].dono    = -1;
                    if (tick_valido(*tick))
                    {
                        diagrama[idx][*tick].evento       = ACAO_MU;
                        diagrama[idx][*tick].mutex_evento = m;
                    }
                    conceder_mutex(m, mutexes, tarefas, qtde_tarefas, fila_prontas);
                }
            }
            /* libera quaisquer mutexes ainda retidos pela tarefa que termina */
            for (int m = 0; m < g_qtde_mutexes; m++)
                if (mutexes[m].dono == idx)
                {
                    mutexes[m].ocupado = 0;
                    mutexes[m].dono    = -1;
                    conceder_mutex(m, mutexes, tarefas, qtde_tarefas, fila_prontas);
                }

            t->contexto = 0;
            t->estado   = TERMINADA;
            t->termino  = *tick;

            int tick_fim = (*tick + 1 < g_limite_ticks) ? *tick + 1 : *tick;
            diagrama[idx][tick_fim].estado = TERMINADA;
            diagrama[idx][tick_fim].cpu    = -1;

            CPUs[i].ocupado      = LIVRE;
            CPUs[i].tarefa_atual = NULL;
            (*tarefas_finalizadas)++;
        }
        else if (t->quantum_restante == 0)
        {
            /* Quantum esgotado — devolve à fila de prontas */
            t->estado              = PRONTA;
            t->quantum_restante    = config->quantum;
            t->prioridade_dinamica = t->prioridade; /* reinicia envelhecimento */
            fila_enqueue(fila_prontas, t);
            CPUs[i].tarefa_atual = NULL;
            CPUs[i].ocupado      = LIVRE;
        }
    }

    /* 8. Pinta as celulas das tarefas que PERMANECEM suspensas neste tick,
     *    para que o tempo de suspensao apareca no diagrama (req. 2.9), com o
     *    motivo (mutex ou E/S) registrado para diferenciacao grafica. */
    for (int i = 0; i < qtde_tarefas; i++)
        if (tarefas[i].estado == SUSPENSA && tick_valido(*tick))
        {
            diagrama[tarefas[i].indice][*tick].estado      = SUSPENSA;
            diagrama[tarefas[i].indice][*tick].motivo_susp = tarefas[i].motivo_suspensao;
        }

    /* libera o vetor auxiliar de protecao contra ping-pong (alocado no topo) */
    free(despacho_no_tick);
}

/* ============================================================================
 * Execução completa (modo b — requisito 1.5.3)
 * ============================================================================ */
void execucao_completa(TCB *tarefas, int *tick, Queue *fila_prontas, CPU *CPUs,
                       Mutex *mutexes, int *tick_max, DiagramaGantt **diagrama,
                       ConfigSistema *config, int qtde_tarefas)
{
    int tarefas_finalizadas = 0;

    /* Inicializa tarefas, CPUs e mutexes para a simulação */
    for (int i = 0; i < qtde_tarefas; i++)
        inicializar_tcb_padrao(&tarefas[i], config->quantum);
    resetar_acoes(tarefas, qtde_tarefas);          /* zera flags executada */
    for (int i = 0; i < config->qtde_cpus; i++)
        inicializar_cpu(&CPUs[i]);
    inicializar_mutexes(mutexes);

    /* Zera o diagrama */
    for (int i = 0; i < qtde_tarefas; i++)
        for (int j = 0; j < g_limite_ticks; j++)
        {
            diagrama[i][j].estado       = 0;
            diagrama[i][j].cpu          = -1;
            diagrama[i][j].sorteio      = 0;
            diagrama[i][j].evento       = ACAO_NENHUMA;
            diagrama[i][j].mutex_evento = 0;
            diagrama[i][j].motivo_susp  = SUSP_NENHUM;
        }

    /* Loop principal: um tick por iteração */
    while (tarefas_finalizadas < qtde_tarefas && *tick < g_limite_ticks)
    {
        simular_tick(tarefas, tick, fila_prontas, CPUs, mutexes, diagrama,
                     config, qtde_tarefas, &tarefas_finalizadas);
        (*tick)++;
        if (*tick > *tick_max) *tick_max = *tick;
    }

    /* Relatório final */
    if (tarefas_finalizadas < qtde_tarefas)
    {
        printf("\nNem todas as tarefas foram finalizadas.\n");
        for (int i = 0; i < qtde_tarefas; i++)
            if (tarefas[i].estado != TERMINADA)
                printf("  Tarefa %d: estado %d\n",
                       tarefas[i].id_tarefa, tarefas[i].estado);
    }

    printf("\nOciosidade por CPU:\n");
    for (int i = 0; i < config->qtde_cpus; i++)
        printf("  CPU %d: desligada por %d tick(s)\n",
               i, CPUs[i].tempo_desligado);

    printf("\nTempo de espera por tarefa:\n");
    for (int i = 0; i < qtde_tarefas; i++)
        printf("  Tarefa %d: esperou %d tick(s) na fila de prontas\n",
               tarefas[i].id_tarefa, tarefas[i].espera);

    imprimir_gantt_terminal(diagrama, tarefas, *tick_max, qtde_tarefas,
                            CPUs, config->qtde_cpus);

    criar_svg(diagrama, tarefas, *tick_max, qtde_tarefas,
              config->nome_arquivo, 0, 0, CPUs, config->qtde_cpus);
}

/* ============================================================================
 * Funções de histórico (usadas pelo modo passo-a-passo)
 * ============================================================================ */

EstadoSistema *salvar_estado(int tick, TCB *tarefas, Queue *fila,
                              CPU *CPUs, Mutex *mutexes, DiagramaGantt **diagrama,
                              int qtde_tarefas, int qtde_cpus)
{
    EstadoSistema *novo = malloc(sizeof(EstadoSistema));
    if (!novo) { perror("salvar_estado"); exit(1); }

    novo->tarefas_novas = malloc((size_t)qtde_tarefas * sizeof(TCB));
    novo->CPUs          = malloc((size_t)qtde_cpus    * sizeof(CPU));
    novo->mutexes       = malloc((size_t)(g_qtde_mutexes > 0 ? g_qtde_mutexes : 1) * sizeof(Mutex));
    novo->diagrama      = malloc((size_t)qtde_tarefas * sizeof(DiagramaGantt *));
    if (!novo->tarefas_novas || !novo->CPUs || !novo->mutexes || !novo->diagrama)
        { perror("salvar_estado alloc"); exit(1); }

    for (int i = 0; i < qtde_tarefas; i++)
        novo->diagrama[i] = malloc(g_limite_ticks * sizeof(DiagramaGantt));

    novo->tick = tick;

    /* Copia as tarefas. O vetor `acoes` e dinamico, entao fazemos COPIA
     * PROFUNDA: cada estado do historico tem seu proprio vetor de acoes (o
     * campo `executada` muda durante a simulacao e precisa ser independente
     * por estado para o retroceder funcionar). */
    for (int i = 0; i < qtde_tarefas; i++)
    {
        novo->tarefas_novas[i] = tarefas[i];           /* copia rasa dos campos */
        if (tarefas[i].acoes != NULL && tarefas[i].qtde_acoes > 0)
        {
            size_t n = (size_t)tarefas[i].qtde_acoes;
            novo->tarefas_novas[i].acoes = malloc(n * sizeof(AcaoMutex));
            if (!novo->tarefas_novas[i].acoes) { perror("salvar_estado acoes"); exit(1); }
            memcpy(novo->tarefas_novas[i].acoes, tarefas[i].acoes,
                   n * sizeof(AcaoMutex));
            novo->tarefas_novas[i].cap_acoes = tarefas[i].qtde_acoes;
        }
        else
        {
            novo->tarefas_novas[i].acoes     = NULL;
            novo->tarefas_novas[i].cap_acoes = 0;
        }
    }

    /* Copia a tabela de mutexes (dono e indice, valido entre copias) */
    for (int m = 0; m < g_qtde_mutexes; m++)
        novo->mutexes[m] = mutexes[m];

    /* Deep copy da fila (re-mapeia ponteiros para o novo vetor) */
    novo->fila_prontas = fila_copiar(fila, novo->tarefas_novas);

    /* Copia as CPUs e atualiza ponteiros tarefa_atual. O vetor ocioso_ticks
     * e dinamico, entao fazemos COPIA PROFUNDA (cada estado tem o seu, para o
     * retroceder mostrar a ociosidade correta de cada tick). */
    for (int j = 0; j < qtde_cpus; j++)
    {
        novo->CPUs[j] = CPUs[j];                 /* copia rasa dos campos */
        novo->CPUs[j].ocioso_ticks = malloc((size_t)g_limite_ticks * sizeof(int));
        if (!novo->CPUs[j].ocioso_ticks) { perror("salvar_estado ocioso"); exit(1); }
        if (CPUs[j].ocioso_ticks)
            memcpy(novo->CPUs[j].ocioso_ticks, CPUs[j].ocioso_ticks,
                   (size_t)g_limite_ticks * sizeof(int));
        else
            memset(novo->CPUs[j].ocioso_ticks, 0, (size_t)g_limite_ticks * sizeof(int));

        if (CPUs[j].tarefa_atual != NULL)
        {
            int idx = CPUs[j].tarefa_atual->indice;
            novo->CPUs[j].tarefa_atual = &novo->tarefas_novas[idx];
            /* Garante que a tarefa em execução não esteja também na fila */
            fila_remover(&novo->tarefas_novas[idx], novo->fila_prontas);
        }
        else
            novo->CPUs[j].tarefa_atual = NULL;
    }

    /* Copia o diagrama */
    for (int i = 0; i < qtde_tarefas; i++)
        for (int j = 0; j < g_limite_ticks; j++)
            novo->diagrama[i][j] = diagrama[i][j];

    novo->anterior = NULL;
    novo->proximo  = NULL;
    return novo;
}

/* -------------------------------------------------------------------------- */
EstadoSistema *executar_tick(EstadoSistema *atual, ConfigSistema *config,
                              int qtde_tarefas)
{
    int tarefas_finalizadas = 0;

    /* Cria um novo nó a partir do estado atual */
    EstadoSistema *novo = salvar_estado(
        atual->tick,
        atual->tarefas_novas, atual->fila_prontas,
        atual->CPUs, atual->mutexes, atual->diagrama,
        qtde_tarefas, config->qtde_cpus);

    /* Liga os nós */
    novo->anterior  = atual;
    atual->proximo  = novo;

    /* Avança a simulação no novo nó */
    simular_tick(novo->tarefas_novas, &novo->tick, novo->fila_prontas,
                 novo->CPUs, novo->mutexes, novo->diagrama, config,
                 qtde_tarefas, &tarefas_finalizadas);
    novo->tick++;
    return novo;
}

/* -------------------------------------------------------------------------- */
static void liberar_no(EstadoSistema *no, int qtde_tarefas)
{
    fila_destruir(no->fila_prontas);
    /* libera o vetor dinamico de acoes copiado para este estado */
    for (int i = 0; i < qtde_tarefas; i++)
        free(no->tarefas_novas[i].acoes);
    free(no->tarefas_novas);
    /* libera o vetor dinamico de ociosidade de cada CPU deste estado */
    if (no->CPUs)
        for (int j = 0; no->CPUs && j < g_qtde_cpus; j++)
            free(no->CPUs[j].ocioso_ticks);
    free(no->CPUs);
    free(no->mutexes);
    for (int i = 0; i < qtde_tarefas; i++)
        if (no->diagrama[i]) free(no->diagrama[i]);
    free(no->diagrama);
    free(no);
}

void destruir_historico(EstadoSistema *inicio, int qtde_tarefas)
{
    EstadoSistema *atual = inicio;
    while (atual != NULL)
    {
        EstadoSistema *prox = atual->proximo;
        liberar_no(atual, qtde_tarefas);
        atual = prox;
    }
}

void destruir_futuros(EstadoSistema *estado, int qtde_tarefas)
{
    EstadoSistema *atual = estado->proximo;
    while (atual != NULL)
    {
        EstadoSistema *prox = atual->proximo;
        liberar_no(atual, qtde_tarefas);
        atual = prox;
    }
    estado->proximo = NULL;
}

int contar_historico(EstadoSistema *atual)
{
    int cont = 0;
    EstadoSistema *temp = atual;
    while (temp->anterior != NULL) temp = temp->anterior; /* vai ao início */
    while (temp != NULL) { cont++; temp = temp->proximo; }
    return cont;
}

EstadoSistema *remover_estado_mais_antigo(EstadoSistema *inicio, int qtde_tarefas)
{
    if (inicio == NULL) return NULL;
    EstadoSistema *novo_inicio = inicio->proximo;
    liberar_no(inicio, qtde_tarefas);
    if (novo_inicio != NULL) novo_inicio->anterior = NULL;
    return novo_inicio;
}

/* ============================================================================
 * Execução passo-a-passo (modo a — requisito 1.5.1 / 1.5.2)
 * ============================================================================ */
void execucao_passo_a_passo(TCB *tarefas, Queue *fila_prontas, CPU *CPUs,
                             Mutex *mutexes, int *tick_max, DiagramaGantt **diagrama,
                             ConfigSistema *config, int qtde_tarefas)
{
    /* Garante estado inicial limpo de tarefas/mutexes/acoes */
    resetar_acoes(tarefas, qtde_tarefas);
    inicializar_mutexes(mutexes);

    /* Zera o diagrama */
    for (int i = 0; i < qtde_tarefas; i++)
        for (int j = 0; j < g_limite_ticks; j++)
        {
            diagrama[i][j].estado       = 0;
            diagrama[i][j].cpu          = -1;
            diagrama[i][j].sorteio      = 0;
            diagrama[i][j].evento       = ACAO_NENHUMA;
            diagrama[i][j].mutex_evento = 0;
            diagrama[i][j].motivo_susp  = SUSP_NENHUM;
        }

    /* Salva o estado inicial (tick 0) */
    EstadoSistema *inicio_estado = salvar_estado(
        0, tarefas, fila_prontas, CPUs, mutexes, diagrama,
        qtde_tarefas, config->qtde_cpus);
    EstadoSistema *atual_estado = inicio_estado;

    char opcao;
    /* Aviso para exibir após o redesenho do diagrama (system cls/clear apaga
     * printf anteriores, por isso o aviso é guardado e exibido depois) */
    char aviso[128] = "";

    while (1)
    {
        /* Verifica se todas as tarefas terminaram */
        int finalizadas = 0;
        for (int i = 0; i < qtde_tarefas; i++)
            if (atual_estado->tarefas_novas[i].estado == TERMINADA)
                finalizadas++;

        if (finalizadas == qtde_tarefas)
        {
            printf("\nTodas as tarefas foram finalizadas!\n");
            break;
        }

        /* Redesenha o diagrama e o menu */
        imprimir_gantt_terminal(atual_estado->diagrama,
                                atual_estado->tarefas_novas,
                                *tick_max, qtde_tarefas,
                                atual_estado->CPUs, config->qtde_cpus);

        printf("Tick atual: %d\n\n", atual_estado->tick);
        printf("A - Avancar\n");
        printf("R - Retroceder\n");
        printf("E - Examinar tarefas (detalhes)\n");
        printf("M - Modificar tarefa\n");
        printf("S - Sair\n");

        if (aviso[0] != '\0')
        {
            printf("\n! %s\n", aviso);
            aviso[0] = '\0';
        }

        printf("\n> ");
        if (scanf(" %c", &opcao) != 1) opcao = 'S'; /* EOF/erro de leitura: encerra o modo passo-a-passo com seguranca */
        opcao = (char)toupper((unsigned char)opcao);

        /* ---- Avançar ---- */
        if (opcao == 'A')
        {
            if (atual_estado->proximo != NULL)
            {
                atual_estado = atual_estado->proximo;
            }
            else
            {
                if (atual_estado->tick >= g_limite_ticks)
                {
                    snprintf(aviso, sizeof(aviso),
                             "Limite maximo de ticks atingido (%d).", g_limite_ticks);
                    continue;
                }

                EstadoSistema *novo = executar_tick(atual_estado, config, qtde_tarefas);

                /* Reconstrói ponteiro para o início da lista */
                EstadoSistema *temp = novo;
                while (temp->anterior != NULL) temp = temp->anterior;
                inicio_estado = temp;
                atual_estado  = novo;

                /* Sem limite de historico: todos os estados sao mantidos para
                 * permitir retroceder ate o inicio da simulacao (limitado apenas
                 * pela memoria disponivel). */
            }
        }

        /* ---- Retroceder ---- */
        else if (opcao == 'R')
        {
            if (atual_estado->anterior != NULL)
                atual_estado = atual_estado->anterior;
            else
                snprintf(aviso, sizeof(aviso),
                         "Ja esta no tick inicial, nao e possivel retroceder.");
        }

        /* ---- Examinar tarefas: debugger com o estado individual (req. 1.5.1) ---- */
        else if (opcao == 'E')
        {
            const char *nomes_estados[] =
                {"DESCONHECIDO","NOVA","PRONTA","EXECUTANDO","TERMINADA","SUSPENSA"};
            int env = (config->algoritmo_escalonamento == PRIOPENV);
            printf("\n=== Estado das tarefas no tick %d ===\n", atual_estado->tick);
            printf("ID  | Estado      | %-8s | Ingr | Dur | Rest | Qtm | CPU | Obs\n",
                   env ? "Prio/Din" : "Prio");
            for (int i = 0; i < qtde_tarefas; i++)
            {
                TCB *t = &atual_estado->tarefas_novas[i];
                int est = t->estado;
                const char *nome = (est >= NOVA && est <= SUSPENSA)
                                   ? nomes_estados[est] : "DESCONHECIDO";
                /* descobre em qual CPU a tarefa esta executando */
                int cpu = -1;
                for (int c = 0; c < config->qtde_cpus; c++)
                    if (atual_estado->CPUs[c].tarefa_atual == t) { cpu = c; break; }
                char cpu_txt[16];
                if (cpu >= 0) snprintf(cpu_txt, sizeof(cpu_txt), "%d", cpu);
                else          snprintf(cpu_txt, sizeof(cpu_txt), "-");
                /* prioridade: "estatica/dinamica" no PRIOPEnv, so a estatica nos demais */
                char prio_txt[16];
                if (env) snprintf(prio_txt, sizeof(prio_txt), "%d/%d", t->prioridade, t->prioridade_dinamica);
                else     snprintf(prio_txt, sizeof(prio_txt), "%d", t->prioridade);
                /* observacao: motivo de suspensao / E/S pendente */
                char obs[48] = "";
                if (est == SUSPENSA && t->motivo_suspensao == SUSP_MUTEX)
                    snprintf(obs, sizeof(obs), "suspensa por mutex %d", t->mutex_esperado);
                else if (est == SUSPENSA && t->motivo_suspensao == SUSP_IO)
                    snprintf(obs, sizeof(obs), "suspensa por E/S (IRQ no tick %d)", t->io_wake);

                printf("T%-2d | %-11s | %-8s | %4d | %3d | %4d | %3d | %3s | %s\n",
                       t->id_tarefa, nome, prio_txt,
                       t->ingresso, t->duracao, t->restante, t->quantum_restante,
                       cpu_txt, obs);
            }
            printf("\nPressione ENTER para voltar ao menu...");
            { int ch; while ((ch = getchar()) != '\n' && ch != EOF) {} getchar(); }
            continue;
        }

        /* ---- Modificar estado de uma tarefa ---- */
        else if (opcao == 'M')
        {
            /* Invalida os estados futuros, pois o presente foi alterado */
            destruir_futuros(atual_estado, qtde_tarefas);

            /* Lista as tarefas disponíveis */
            printf("\nTarefas disponiveis:\n");
            const char *nomes_estados[] =
                {"DESCONHECIDO", "NOVA", "PRONTA", "EXECUTANDO", "TERMINADA", "SUSPENSA"};
            for (int i = 0; i < qtde_tarefas; i++)
            {
                int est = atual_estado->tarefas_novas[i].estado;
                const char *nome = (est >= NOVA && est <= SUSPENSA)
                                   ? nomes_estados[est] : "DESCONHECIDO";
                printf("  ID %d | Estado: %s | Restante: %d\n",
                       atual_estado->tarefas_novas[i].id_tarefa,
                       nome,
                       atual_estado->tarefas_novas[i].restante);
            }

            int id, novo_estado;
            printf("\nDigite o ID da tarefa a modificar: ");
            if (scanf("%d", &id) != 1) { id = -1; } /* leitura invalida: id inexistente forca mensagem de erro */

            int idx = buscar_indice_por_id(
                atual_estado->tarefas_novas, qtde_tarefas, id);

            if (idx == -1)
            {
                snprintf(aviso, sizeof(aviso),
                         "ID invalido. Nenhuma alteracao realizada.");
            }
            else
            {
                printf("\nEscolha o novo estado para a Tarefa %d:\n", id);
                printf("  1 - NOVA\n  2 - PRONTA\n  3 - EXECUTANDO\n"
                       "  4 - TERMINADA\n  5 - SUSPENSA\n");
                printf("Digite o numero do estado: ");
                if (scanf("%d", &novo_estado) != 1) { novo_estado = -1; } /* leitura invalida: estado invalido e rejeitado adiante */

                if (novo_estado < 1 || novo_estado > 5)
                {
                    snprintf(aviso, sizeof(aviso),
                             "Estado invalido. Nenhuma alteracao realizada.");
                }
                else
                {
                    TCB *t = &atual_estado->tarefas_novas[idx];

                    /* Remove a tarefa de onde quer que esteja */
                    fila_remover(t, atual_estado->fila_prontas);
                    for (int i = 0; i < config->qtde_cpus; i++)
                        if (atual_estado->CPUs[i].tarefa_atual == t)
                        {
                            atual_estado->CPUs[i].tarefa_atual = NULL;
                            atual_estado->CPUs[i].ocupado      = LIVRE;
                        }
                    t->contexto = 0;

                    /* Aplica o novo estado e efeitos colaterais */
                    t->estado = novo_estado;

                    if (novo_estado == PRONTA && !fila_contem(atual_estado->fila_prontas, t))
                        fila_enqueue(atual_estado->fila_prontas, t);

                    if (novo_estado == EXECUTANDO)
                    {
                        /* CORRECAO: se nenhuma CPU estiver livre, a tarefa NAO
                         * pode ficar marcada como EXECUTANDO sem estar em CPU
                         * alguma — ela viraria uma "tarefa fantasma" que nunca
                         * consome ticks nem volta a ser escalonada (a simulacao
                         * jamais terminaria). Nesse caso, a tarefa e colocada
                         * como PRONTA na fila, e o escalonador decidira quando
                         * ela recebera um processador. */
                        int cpu_livre = -1;
                        for (int i = 0; i < config->qtde_cpus; i++)
                            if (atual_estado->CPUs[i].ocupado == LIVRE)
                            {
                                cpu_livre = i;
                                break;
                            }
                        if (cpu_livre >= 0)
                        {
                            t->contexto = 1;
                            atual_estado->CPUs[cpu_livre].tarefa_atual = t;
                            atual_estado->CPUs[cpu_livre].ocupado      = OCUPADA;
                        }
                        else
                        {
                            t->estado = PRONTA;
                            if (!fila_contem(atual_estado->fila_prontas, t))
                                fila_enqueue(atual_estado->fila_prontas, t);
                            snprintf(aviso, sizeof(aviso),
                                     "Sem CPU livre: tarefa %d ficou PRONTA na fila.", id);
                        }
                    }

                    if (novo_estado == TERMINADA)  t->restante = 0;

                    if (novo_estado == SUSPENSA)
                        for (int i = 0; i < config->qtde_cpus; i++)
                            if (atual_estado->CPUs[i].tarefa_atual != NULL &&
                                atual_estado->CPUs[i].tarefa_atual->id_tarefa == id)
                            {
                                atual_estado->CPUs[i].tarefa_atual = NULL;
                                atual_estado->CPUs[i].ocupado      = LIVRE;
                            }

                    /* Registra o novo estado no diagrama (somente se o tick
                     * atual e um indice valido do vetor; no cenario raro em que
                     * o usuario chega ao teto de ticks — ex.: deadlock — o tick
                     * atual pode ser igual a limite_ticks e escrever nessa
                     * posicao estouraria o vetor). */
                    if (tick_valido(atual_estado->tick))
                        atual_estado->diagrama[idx][atual_estado->tick].estado = t->estado;

                    printf("Tarefa %d atualizada para estado %d com sucesso.\n",
                           id, novo_estado);

                    /* Gera SVG "modificado" para registrar a edição manual */
                    criar_svg(atual_estado->diagrama, atual_estado->tarefas_novas,
                              atual_estado->tick, qtde_tarefas,
                              config->nome_arquivo, 1, 1,
                              atual_estado->CPUs, config->qtde_cpus);
                }
            }
        }

        /* ---- Sair ---- */
        else if (opcao == 'S')
        {
            break;
        }
        else
        {
            snprintf(aviso, sizeof(aviso),
                     "Opcao '%c' invalida. Use A, R, M ou S.", opcao);
        }

        /* Atualiza tick_max e gera SVG do passo atual */
        if (atual_estado->tick > *tick_max)
            *tick_max = atual_estado->tick;

        criar_svg(atual_estado->diagrama, atual_estado->tarefas_novas,
                  *tick_max, qtde_tarefas, config->nome_arquivo,
                  1, 0, atual_estado->CPUs, config->qtde_cpus);
    }

    /* SVG e relatório final da sessão passo-a-passo */
    if (*tick_max > 0)
    {
        printf("\nOciosidade por CPU:\n");
        for (int i = 0; i < config->qtde_cpus; i++)
            printf("  CPU %d: desligada por %d tick(s)\n",
                   i, atual_estado->CPUs[i].tempo_desligado);

        criar_svg(atual_estado->diagrama, atual_estado->tarefas_novas,
                  atual_estado->tick, qtde_tarefas, config->nome_arquivo,
                  1, 0, atual_estado->CPUs, config->qtde_cpus);
    }

    destruir_historico(inicio_estado, qtde_tarefas);
}
