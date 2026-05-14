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
#include "simulacao.h"
#include "fila.h"
#include "escalonador.h"
#include "visualizacao.h"

/* ============================================================================
 * Funções de inicialização
 * ============================================================================ */

void inicializar_cpu(CPU *cpu)//Zera os campos de uma CPU para o estado inicial (LIVRE, sem tarefa, sem ociosidade)
{
    cpu->ocupado        = LIVRE;//Define a CPU como LIVRE
    cpu->tarefa_atual   = NULL;//Nenhuma tarefa está em execução nesta CPU
    cpu->tempo_desligado = 0;//Inicialmente, a CPU não ficou desligada em nenhum tick
    memset(cpu->ocioso_ticks, 0, sizeof(cpu->ocioso_ticks));//Zera o vetor de ociosidade, indicando que a CPU não ficou ociosa em nenhum tick
}

void inicializar_tcb_padrao(TCB *t, int quantum)//Configura os campos de um TCB para o estado inicial padrão, com base na duração lida do arquivo e no quantum definido
{
    t->estado          = NOVA;//Define o estado da tarefa como NOVA
    t->contexto        = 0;//A tarefa ainda não está ativa em nenhuma CPU
    t->termino         = -1;//A tarefa ainda não terminou, então o término é marcado como -1
    t->espera          = 0;//Inicialmente, a tarefa não acumulou tempo de espera na fila de prontas
    t->restante        = t->duracao; /* duracao já foi preenchida pelo arquivo */
    t->quantum_restante = quantum;//Inicialmente, a tarefa tem o quantum completo disponível para execução
}

void inicializar_config_padrao(ConfigSistema *config)//Configura os campos de ConfigSistema para os valores padrão, escolhendo o algoritmo SRTF e o escalonador correspondente
{
    config->algoritmo_escalonamento = SRTF;
    config->qtde_cpus               = N_CPUs;
    config->quantum                 = QUANTUM;
    config->escalonador             = escalonador_SRTF;
}

/* ============================================================================
 * Funções auxiliares
 * ============================================================================ */
/* Verifica se um tick é válido (entre 0 e MAX_TICKS-1) */
int tick_valido(int tick)
{
    return (tick >= 0 && tick < MAX_TICKS); //Garante que o tick esteja dentro dos limites definidos para a simulação
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
void simular_tick(TCB *tarefas, int *tick, Queue *fila_prontas, CPU *CPUs,
                  DiagramaGantt **diagrama, ConfigSistema *config,
                  int qtde_tarefas, int *tarefas_finalizadas)
{
    /* 1. Marca tarefas cujo ingresso chegou como PRONTA */
    marcar_prontas(tarefas, *tick, diagrama, qtde_tarefas);

    /* 2. Insere tarefas PRONTA na fila de prontas */
    enfileirar_prontas(tarefas, fila_prontas, *tick, qtde_tarefas);

    /* 3. Contabiliza tempo de espera das tarefas aguardando na fila */
    for (int i = 0; i < qtde_tarefas; i++)
        if (tarefas[i].estado == PRONTA)
            tarefas[i].espera++;

    /* 4. Distribui tarefas prontas para CPUs livres via escalonador */
    for (int i = 0; i < config->qtde_cpus; i++)
    {
        if (CPUs[i].ocupado == LIVRE && !fila_vazia(fila_prontas))//Verifica se a CPU está livre e se há tarefas prontas na fila para serem escalonadas
        {
            TCB *proxima = config->escalonador(NULL, fila_prontas, diagrama, *tick);//Chama o escalonador para escolher a próxima tarefa a ser executada, passando NULL para indicar que não há tarefa atualmente em execução nesta CPU
            fila_remover(proxima, fila_prontas);//Remove a tarefa escolhida pelo escalonador da fila de prontas, pois ela agora será executada nesta CPU
            CPUs[i].tarefa_atual          = proxima;//Atribui a tarefa escolhida para o campo tarefa_atual da CPU, indicando que esta CPU agora está executando essa tarefa
            CPUs[i].ocupado               = OCUPADA;//Marca a CPU como OCUPADA, indicando que ela não está mais livre para receber outra tarefa
            CPUs[i].tarefa_atual->contexto = 1;//Marca a tarefa como ativa em alguma CPU, indicando que ela está em execução
            CPUs[i].tarefa_atual->estado  = EXECUTANDO;//Atualiza o estado da tarefa para EXECUTANDO, indicando que ela está atualmente em execução nesta CPU
        }

        /* CPU sem tarefa e sem candidatos: registra ociosidade */
        if (CPUs[i].ocupado == LIVRE && fila_vazia(fila_prontas))
        {
            CPUs[i].tempo_desligado++;
            if (tick_valido(*tick))
                CPUs[i].ocioso_ticks[*tick] = 1;//Marca que esta CPU ficou ociosa neste tick, para fins de visualização e relatório de ociosidade por tick (requisito 1.2)
        }
    }

    /* 5. Verifica preempções na fila de prontas.
     *    Encontra o CPU com a pior tarefa em execução e preempta se a
     *    candidata da fila for melhor (maior prioridade / menor restante).
     *    "Pior" é definido pelo próprio critério do algoritmo ativo. */
    if (!fila_vazia(fila_prontas))
    {
        TCB *candidata = config->escalonador(NULL, fila_prontas, diagrama, *tick);//Obtém a tarefa candidata para execução de acordo com escalonador
        int pior_cpu   = -1;//Inicializa a variável para rastrear o índice do CPU com a pior tarefa atualmente em execução, começando com -1 para indicar que ainda não foi encontrado nenhum CPU a ser preemptado
        int pior_valor = 0; /* quanto maior, pior a tarefa neste CPU */

        /*percorre todas as CPUs para encontrar a CPU com a pior tarefa em execução, comparando a tarefa candidata da fila de prontas
         com a tarefa atualmente em execução em cada CPU*/
        for (int i = 0; i < config->qtde_cpus; i++)
        {
            if (CPUs[i].ocupado != OCUPADA || CPUs[i].tarefa_atual == NULL)//Se a CPU não estiver ocupada ou não tiver uma tarefa atualmente em execução, ela não pode ser preemptada, então o loop continua para a próxima CPU
                continue;

            int deve_preemptar = 0;//Inicializa a variável para determinar se a tarefa candidata deve preemptar a tarefa atualmente em execução nesta CPU, começando com 0 (falso)
            int valor          = 0;//Inicializa a variável para calcular o valor que determina a "pior" tarefa nesta CPU, que será usado para comparar com a tarefa candidata e decidir se deve ocorrer a preempção

            /*Se o algoritmo de escalonamento for SRTF, deve_preemptar será verdadeiro se o tempo restante da tarefa
             candidata for menor do que o tempo restante da tarefa atualmente em execução nesta CPU, e o valor para comparação
              será o tempo restante da tarefa atualmente em execução (quanto maior, pior a tarefa). */
             
            if (config->algoritmo_escalonamento == SRTF)
            {
                deve_preemptar = (candidata->restante < CPUs[i].tarefa_atual->restante);
                valor          = CPUs[i].tarefa_atual->restante; /* mais restante = pior */
            }
            /*Se o algoritmo for PRIOP, deve_preemptar será verdadeiro se a prioridade da tarefa candidata
             for maior do que a prioridade da tarefa atualmente em execução nesta CPU, e o valor para comparação
              será o negativo da prioridade da tarefa atualmente em execução (quanto menor a prioridade, mais negativo, pior a tarefa).*/
            else 
            {
                deve_preemptar = (candidata->prioridade > CPUs[i].tarefa_atual->prioridade);
                valor          = -CPUs[i].tarefa_atual->prioridade; /* menos prio = mais negativo = pior */
            }

            /* Atualiza pior_cpu apenas se este for realmente o pior até agora */
            if (deve_preemptar && (pior_cpu == -1 || valor > pior_valor))
            {
                pior_cpu   = i;
                pior_valor = valor;
            }
        }

        if (pior_cpu != -1)
        {
            /* Devolve a tarefa preemptada à fila de prontas com quantum resetado */
            CPUs[pior_cpu].tarefa_atual->estado           = PRONTA;
            CPUs[pior_cpu].tarefa_atual->quantum_restante = config->quantum;
            fila_enqueue(fila_prontas, CPUs[pior_cpu].tarefa_atual);

            /* Coloca a candidata no CPU liberado */
            fila_remover(candidata, fila_prontas);
            CPUs[pior_cpu].tarefa_atual          = candidata;
            CPUs[pior_cpu].tarefa_atual->estado  = EXECUTANDO;
            CPUs[pior_cpu].tarefa_atual->contexto = 1;
        }
    }

    /* 6 & 7. Contabiliza execução e verifica fim de tarefa / esgotamento de quantum */
    for (int i = 0; i < config->qtde_cpus; i++)
    {
        /* Tarefa suspensa: libera o CPU imediatamente */
        if (CPUs[i].tarefa_atual != NULL &&
            CPUs[i].tarefa_atual->estado == SUSPENSA)
        {
            CPUs[i].tarefa_atual = NULL;
            CPUs[i].ocupado      = LIVRE;
            continue;
        }

        if (CPUs[i].ocupado != OCUPADA || CPUs[i].tarefa_atual == NULL ||
            CPUs[i].tarefa_atual->estado != EXECUTANDO)
            continue;

        int idx = CPUs[i].tarefa_atual->indice;
        if (tick_valido(*tick))
        {
            diagrama[idx][*tick].estado = EXECUTANDO;
            diagrama[idx][*tick].cpu    = i;
        }

        CPUs[i].tarefa_atual->restante--;
        CPUs[i].tarefa_atual->quantum_restante--;

        if (CPUs[i].tarefa_atual->restante == 0)
        {
            /* Tarefa concluída */
            int idx_fim = CPUs[i].tarefa_atual->indice;
            CPUs[i].tarefa_atual->contexto = 0;
            CPUs[i].tarefa_atual->estado   = TERMINADA;
            CPUs[i].tarefa_atual->termino  = *tick;

            int tick_fim = (*tick + 1 < MAX_TICKS) ? *tick + 1 : *tick;
            diagrama[idx_fim][tick_fim].estado = TERMINADA;
            diagrama[idx_fim][tick_fim].cpu    = -1;

            CPUs[i].ocupado      = LIVRE;
            CPUs[i].tarefa_atual = NULL;
            (*tarefas_finalizadas)++;
        }
        else if (CPUs[i].tarefa_atual->quantum_restante == 0)
        {
            /* Quantum esgotado — devolve à fila de prontas */
            CPUs[i].tarefa_atual->estado           = PRONTA;
            CPUs[i].tarefa_atual->quantum_restante = config->quantum;
            fila_enqueue(fila_prontas, CPUs[i].tarefa_atual);
            CPUs[i].tarefa_atual = NULL;
            CPUs[i].ocupado      = LIVRE;
        }
    }
}

/* ============================================================================
 * Execução completa (modo b — requisito 1.5.3)
 * ============================================================================ */
void execucao_completa(TCB *tarefas, int *tick, Queue *fila_prontas, CPU *CPUs,
                       int *tick_max, DiagramaGantt **diagrama,
                       ConfigSistema *config, int qtde_tarefas)
{
    int tarefas_finalizadas = 0;

    /* Inicializa tarefas e CPUs para a simulação */
    for (int i = 0; i < qtde_tarefas; i++)
        inicializar_tcb_padrao(&tarefas[i], config->quantum);
    for (int i = 0; i < config->qtde_cpus; i++)
        inicializar_cpu(&CPUs[i]);

    /* Zera o diagrama */
    for (int i = 0; i < qtde_tarefas; i++)
        for (int j = 0; j < MAX_TICKS; j++)
        {
            diagrama[i][j].estado  = 0;
            diagrama[i][j].cpu     = -1;
            diagrama[i][j].sorteio = 0;
        }

    /* Loop principal: um tick por iteração */
    while (tarefas_finalizadas < qtde_tarefas && *tick < MAX_TICKS)
    {
        simular_tick(tarefas, tick, fila_prontas, CPUs, diagrama,
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
                              CPU *CPUs, DiagramaGantt **diagrama,
                              int qtde_tarefas, int qtde_cpus)
{
    EstadoSistema *novo = malloc(sizeof(EstadoSistema));
    if (!novo) { perror("salvar_estado"); exit(1); }

    novo->tarefas_novas = malloc((size_t)qtde_tarefas * sizeof(TCB));
    novo->CPUs          = malloc((size_t)qtde_cpus    * sizeof(CPU));
    novo->diagrama      = malloc((size_t)qtde_tarefas * sizeof(DiagramaGantt *));
    if (!novo->tarefas_novas || !novo->CPUs || !novo->diagrama)
        { perror("salvar_estado alloc"); exit(1); }

    for (int i = 0; i < qtde_tarefas; i++)
        novo->diagrama[i] = malloc(MAX_TICKS * sizeof(DiagramaGantt));

    novo->tick = tick;

    /* Copia as tarefas */
    for (int i = 0; i < qtde_tarefas; i++)
        novo->tarefas_novas[i] = tarefas[i];

    /* Deep copy da fila (re-mapeia ponteiros para o novo vetor) */
    novo->fila_prontas = fila_copiar(fila, novo->tarefas_novas);

    /* Copia as CPUs e atualiza ponteiros tarefa_atual */
    for (int j = 0; j < qtde_cpus; j++)
    {
        novo->CPUs[j] = CPUs[j];
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
        for (int j = 0; j < MAX_TICKS; j++)
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
        atual->CPUs, atual->diagrama,
        qtde_tarefas, config->qtde_cpus);

    /* Liga os nós */
    novo->anterior  = atual;
    atual->proximo  = novo;

    /* Avança a simulação no novo nó */
    simular_tick(novo->tarefas_novas, &novo->tick, novo->fila_prontas,
                 novo->CPUs, novo->diagrama, config,
                 qtde_tarefas, &tarefas_finalizadas);
    novo->tick++;
    return novo;
}

/* -------------------------------------------------------------------------- */
static void liberar_no(EstadoSistema *no, int qtde_tarefas)
{
    fila_destruir(no->fila_prontas);
    free(no->tarefas_novas);
    free(no->CPUs);
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
                             int *tick_max, DiagramaGantt **diagrama,
                             ConfigSistema *config, int qtde_tarefas)
{
    /* Zera o diagrama */
    for (int i = 0; i < qtde_tarefas; i++)
        for (int j = 0; j < MAX_TICKS; j++)
        {
            diagrama[i][j].estado  = 0;
            diagrama[i][j].cpu     = -1;
            diagrama[i][j].sorteio = 0;
        }

    /* Salva o estado inicial (tick 0) */
    EstadoSistema *inicio_estado = salvar_estado(
        0, tarefas, fila_prontas, CPUs, diagrama,
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
        printf("M - Modificar tarefa\n");
        printf("S - Sair\n");

        if (aviso[0] != '\0')
        {
            printf("\n! %s\n", aviso);
            aviso[0] = '\0';
        }

        printf("\n> ");
        scanf(" %c", &opcao);
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
                if (atual_estado->tick >= MAX_TICKS)
                {
                    snprintf(aviso, sizeof(aviso),
                             "Limite maximo de ticks atingido (%d).", MAX_TICKS);
                    continue;
                }

                EstadoSistema *novo = executar_tick(atual_estado, config, qtde_tarefas);

                /* Reconstrói ponteiro para o início da lista */
                EstadoSistema *temp = novo;
                while (temp->anterior != NULL) temp = temp->anterior;
                inicio_estado = temp;
                atual_estado  = novo;

                /* Limita o histórico a MAX_HISTORICO entradas (requisito 1.5.2):
                 * remove os estados mais antigos quando o limite é ultrapassado. */
                while (contar_historico(atual_estado) > MAX_HISTORICO &&
                       inicio_estado != atual_estado)
                    inicio_estado = remover_estado_mais_antigo(
                        inicio_estado, qtde_tarefas);
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
            scanf("%d", &id);

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
                scanf("%d", &novo_estado);

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
                        t->contexto = 1;
                        for (int i = 0; i < config->qtde_cpus; i++)
                            if (atual_estado->CPUs[i].ocupado == LIVRE)
                            {
                                atual_estado->CPUs[i].tarefa_atual = t;
                                atual_estado->CPUs[i].ocupado      = OCUPADA;
                                break;
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

                    /* Registra o novo estado no diagrama */
                    atual_estado->diagrama[idx][atual_estado->tick].estado = novo_estado;

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
