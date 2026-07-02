/* ============================================================================
 * main.c — Ponto de entrada do simulador de SO multitarefa.
 *
 * Responsabilidades:
 *   1. Habilitar cores ANSI no Windows (se necessário).
 *   2. Chamar ler_configuracao() para obter tarefas e parâmetros do arquivo.
 *   3. Alocar fila de prontas, CPUs e matriz do diagrama de Gantt.
 *   4. Perguntar ao usuário o modo de execução (passo-a-passo ou completo).
 *   5. Delegar a execução para execucao_passo_a_passo() ou execucao_completa().
 *   6. Liberar toda a memória alocada antes de sair.
 *
 * A lógica de simulação e escalonamento está distribuída nos demais módulos:
 *   fila.c, escalonador.c, simulacao.c, configuracao.c, visualizacao.c
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#ifdef _WIN32 // para habilitar cores ANSI no Windows 10+
  #include <windows.h> // para manipulação de console no Windows
  #ifndef ENABLE_VIRTUAL_TERMINAL_PROCESSING // caso a constante não esteja definida, define com o valor correto
    #define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004// habilita processamento de sequências ANSI
  #endif
#endif

#include "../include/tipos.h"
#include "../include/fila.h"
#include "../include/simulacao.h"
#include "../include/configuracao.h"
#include "../include/visualizacao.h"

int main(void)
{
    /* ------------------------------------------------------------------
     * Habilita cores ANSI no Windows 10+.
     * Em Linux/macOS os terminais já suportam ANSI nativamente; o bloco
     * abaixo é ignorado pelo pré-processador nessas plataformas.
     * ------------------------------------------------------------------ */
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);// garante UTF-8 na saida (caracteres de textura ░▒)
    HANDLE h = GetStdHandle(STD_OUTPUT_HANDLE);// obtém o handle do console
    if (h != INVALID_HANDLE_VALUE)// verifica se o handle é válido
    {
        DWORD modo = 0; // variável para armazenar o modo atual do console
        if (GetConsoleMode(h, &modo)) // obtém o modo atual do console
            SetConsoleMode(h, modo | ENABLE_VIRTUAL_TERMINAL_PROCESSING);// habilita o processamento de sequências ANSI
    }
#endif

    srand((unsigned int)time(NULL));// semente para geração de números aleatórios (sorteios)

    /* ------------------------------------------------------------------
     * Leitura do arquivo de configuração
     * ------------------------------------------------------------------ */
    int  qtde_tarefas = 0;// quantidade de tarefas lida do arquivo
    TCB *tarefas      = NULL;// vetor de TCBs preenchido pela leitura do arquivo

    ConfigSistema config = ler_configuracao(&tarefas, &qtde_tarefas);// lê a configuração do sistema e as tarefas do arquivo, preenchendo os ponteiros correspondentes

    /* Verifica se a leitura teve êxito */
    if (config.qtde_cpus <= 0 || tarefas == NULL)
    {
        printf("Falha na leitura da configuracao.\n");
        free(tarefas);
        return 1;
    }
    if (qtde_tarefas < 1)
    {
        printf("O arquivo deve conter ao menos uma tarefa.\n");
        free(tarefas);
        return 1;
    }

    /* ------------------------------------------------------------------
     * Alocação das estruturas principais
     * ------------------------------------------------------------------ */

    /* Fila de prontas: slot extra para a fila circular não confundir
     * fila cheia com fila vazia (invariante front == back+1 → cheia). */
    Queue *fila_prontas = fila_criar(qtde_tarefas + 1);

    /* Define os limites dinamicos (sem teto fixo) a partir do arquivo:
     * teto de ticks dimensionado pela carga e numero de mutexes usados. */
    definir_limites(config.limite_ticks, config.qtde_mutexes, config.qtde_cpus);

    /* Vetor de CPUs (cada uma com seu vetor dinamico de ociosidade por tick) */
    CPU *CPUs = malloc((size_t)config.qtde_cpus * sizeof(CPU));
    if (!CPUs) { perror("malloc CPUs"); return 1; }
    for (int i = 0; i < config.qtde_cpus; i++)
    {
        CPUs[i].ocioso_ticks = calloc((size_t)config.limite_ticks, sizeof(int));
        if (!CPUs[i].ocioso_ticks) { perror("malloc ocioso_ticks"); return 1; }
        inicializar_cpu(&CPUs[i]);
    }

    /* Projeto B: tabela de mutexes (dimensionada pelo arquivo; todos livres) */
    int n_mtx = (config.qtde_mutexes > 0 ? config.qtde_mutexes : 1);
    Mutex *mutexes = malloc((size_t)n_mtx * sizeof(Mutex));
    if (!mutexes) { perror("malloc mutexes"); return 1; }
    inicializar_mutexes(mutexes);

    /* Matriz do diagrama de Gantt [tarefa][tick].
     * Alocada no heap para evitar stack overflow em simulações longas. */
    DiagramaGantt **diagrama = malloc((size_t)qtde_tarefas * sizeof(DiagramaGantt *));// aloca vetor de ponteiros para cada tarefa
    if (!diagrama) { perror("malloc diagrama"); return 1; }// checagem de alocação
    for (int i = 0; i < qtde_tarefas; i++)// aloca o vetor de ticks para cada tarefa
        diagrama[i] = malloc((size_t)config.limite_ticks * sizeof(DiagramaGantt));// aloca vetor de DiagramaGantt para cada tarefa

    /* ------------------------------------------------------------------
     * Inicialização dos TCBs para a simulação
     * (duracao/ingresso/prioridade/cor já foram preenchidos pelo arquivo)
     * ------------------------------------------------------------------ */
    for (int i = 0; i < qtde_tarefas; i++)// para cada tarefa lida do arquivo
        inicializar_tcb_padrao(&tarefas[i], config.quantum);// inicializa os campos de controle do TCB para o estado padrão (NOVA, restante = duracao, etc.)

    /* ------------------------------------------------------------------
     * Escolha do modo de execução
     * ------------------------------------------------------------------ */
    int tick     = 0;// usado apenas na execucao_completa
    int tick_max = 0;// tick máximo alcançado durante a simulação (para visualização)
    char opcao;// opção escolhida pelo usuário para o modo de execução (passo-a-passo ou completo)

    printf("Como deseja realizar a simulacao? Passo-a-passo(a) ou completa(b)? ");
    if (scanf(" %c", &opcao) != 1) opcao = '?'; /* entrada invalida/EOF: cai na mensagem de opcao invalida */
    { int ch; while ((ch = getchar()) != '\n' && ch != EOF) {} }//limpa o resto da linha

    if (opcao == 'a' || opcao == 'A')// execução passo-a-passo: o usuário avança um tick por vez, permitindo inspeção detalhada
    {
        execucao_passo_a_passo(tarefas, fila_prontas, CPUs, mutexes,
                               &tick_max, diagrama, &config, qtde_tarefas);
    }
    else if (opcao == 'b' || opcao == 'B')// execução completa: a simulação roda até o final sem pausas, mostrando apenas o resultado final
    {
        execucao_completa(tarefas, &tick, fila_prontas, CPUs, mutexes,
                          &tick_max, diagrama, &config, qtde_tarefas);
    }
    else
    {
        printf("Opcao invalida! Use 'a' para passo-a-passo ou 'b' para completa.\n");
    }

    /* ------------------------------------------------------------------
     * Liberação de memória
     * ------------------------------------------------------------------ */
    for (int i = 0; i < qtde_tarefas; i++)// libera o vetor de ticks para cada tarefa
        free(diagrama[i]);
    free(diagrama);
    for (int i = 0; i < config.qtde_cpus; i++)// libera o vetor de ociosidade de cada CPU
        free(CPUs[i].ocioso_ticks);
    free(CPUs);
    free(mutexes);
    fila_destruir(fila_prontas);
    liberar_tarefas(tarefas, qtde_tarefas);//libera tarefas + vetores de acoes

    /* Pausa final: impede que a janela feche antes de o usuario ver o diagrama
     * (necessario ao rodar por F5/janela externa ou dando duplo-clique no .exe;
     * no terminal, e so apertar ENTER para sair). */
    printf("\nPressione ENTER para sair...");
    { int ch; while ((ch = getchar()) != '\n' && ch != EOF) {} }

    return 0;
}
