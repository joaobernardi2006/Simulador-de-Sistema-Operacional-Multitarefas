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

#include "tipos.h"
#include "fila.h"
#include "simulacao.h"
#include "configuracao.h"
#include "visualizacao.h"

int main(void)
{
    /* ------------------------------------------------------------------
     * Habilita cores ANSI no Windows 10+.
     * Em Linux/macOS os terminais já suportam ANSI nativamente; o bloco
     * abaixo é ignorado pelo pré-processador nessas plataformas.
     * ------------------------------------------------------------------ */
#ifdef _WIN32
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

    /* Vetor de CPUs */
    CPU *CPUs = malloc((size_t)config.qtde_cpus * sizeof(CPU));
    if (!CPUs) { perror("malloc CPUs"); return 1; }
    for (int i = 0; i < config.qtde_cpus; i++)
        inicializar_cpu(&CPUs[i]);

    /* Matriz do diagrama de Gantt [tarefa][tick].
     * Alocada no heap para evitar stack overflow em simulações longas. */
    DiagramaGantt **diagrama = malloc((size_t)qtde_tarefas * sizeof(DiagramaGantt *));// aloca vetor de ponteiros para cada tarefa
    if (!diagrama) { perror("malloc diagrama"); return 1; }// checagem de alocação
    for (int i = 0; i < qtde_tarefas; i++)// aloca o vetor de ticks para cada tarefa
        diagrama[i] = malloc(MAX_TICKS * sizeof(DiagramaGantt));// aloca vetor de DiagramaGantt para cada tarefa

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
    scanf(" %c", &opcao);

    if (opcao == 'a' || opcao == 'A')// execução passo-a-passo: o usuário avança um tick por vez, permitindo inspeção detalhada
    {
        execucao_passo_a_passo(tarefas, fila_prontas, CPUs,
                               &tick_max, diagrama, &config, qtde_tarefas);
    }
    else if (opcao == 'b' || opcao == 'B')// execução completa: a simulação roda até o final sem pausas, mostrando apenas o resultado final
    {
        execucao_completa(tarefas, &tick, fila_prontas, CPUs,
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
    free(CPUs);
    fila_destruir(fila_prontas);
    free(tarefas);

    return 0;
}
