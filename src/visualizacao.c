/* ============================================================================
 * visualizacao.c — Renderização do diagrama de Gantt (terminal + SVG).
 *
 * Convenções de cores ANSI (True Color):
 *   EXECUTANDO → ESC[48;2;R;G;Bm (cor da tarefa) + texto branco
 *   SUSPENSA   → ESC[40m (fundo preto) + texto cinza
 *   CPU ociosa → ESC[48;2;100;100;100m (cinza) + "OFF"
 *   CPU ativa  → ESC[48;2;30;120;30m  (verde)  + " ON"
 *   Sorteio    → "S" em vermelho ESC[31m sobre o bloco colorido
 *   Ingresso   → "N" amarelo ESC[33m sobre o bloco (se executando no mesmo tick)
 *
 * O SVG segue o layout do livro do Prof. Maziero (req 2.5):
 *   menor ID → linha mais próxima ao eixo X; maior ID → topo.
 * ============================================================================ */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../include/visualizacao.h"
#include "../include/simulacao.h"   /* tick_valido() */

#ifdef _WIN32
  #include <windows.h>   /* necessário apenas no Windows para manipulação de processos */
#endif

/* ============================================================================
 * abrir_svg — abre o arquivo SVG no visualizador padrão do sistema operacional.
 * ============================================================================ */
void abrir_svg(const char *nome_svg)
{
    /* proteção: não tenta abrir se o nome for nulo ou vazio */
    if (nome_svg == NULL || nome_svg[0] == '\0') return;

    char cmd[512]; /* buffer para montar o comando do sistema operacional */

    /* monta o comando de abertura adequado a cada sistema:
     *   Windows : start ""  "arquivo.svg"  (as aspas duplas extras evitam problemas com espaços no nome)
     *   macOS   : open      "arquivo.svg"
     *   Linux   : xdg-open  "arquivo.svg"  (abre com o visualizador padrão do desktop) */
#ifdef _WIN32
    snprintf(cmd, sizeof(cmd), "start \"\" \"%s\"", nome_svg);
#elif defined(__APPLE__)
    snprintf(cmd, sizeof(cmd), "open \"%s\"", nome_svg);
#else
    snprintf(cmd, sizeof(cmd), "xdg-open \"%s\"", nome_svg);
#endif

    /* executa o comando; falha é apenas um aviso — a simulação continua normalmente */
    if (system(cmd) != 0)
        printf("Aviso: nao foi possivel abrir automaticamente o SVG %s\n", nome_svg);
}

/* ============================================================================
 * imprimir_gantt_terminal — renderiza o diagrama de Gantt no terminal usando
 * códigos de escape ANSI True Color (ESC[48;2;R;G;Bm).
 *
 * Limpa a tela antes de redesenhar para dar a impressão de atualização
 * em tempo real no modo passo-a-passo.
 * ============================================================================ */
void imprimir_gantt_terminal(DiagramaGantt **diagrama, TCB *tarefas,
                              int tick_max, int qtde_tarefas,
                              CPU *CPUs, int qtde_cpus)
{
    const int CELL = 4; /* largura de cada célula em caracteres: comporta "C0  ", "SUS ", "OFF ", " ON " */

    /* CORRECAO (off-by-one): quando a simulacao para por atingir o teto de
     * ticks (ex.: deadlock), tick_max chega a limite_ticks, mas o vetor
     * diagrama[i] possui exatamente limite_ticks posicoes (indices validos
     * 0..limite_ticks-1). Como os lacos abaixo usam "t <= tick_max", ler a
     * celula tick_max estouraria o vetor. Limitamos ao ultimo indice valido. */
    if (tick_max >= limite_ticks_atual())
        tick_max = limite_ticks_atual() - 1;
    if (tick_max < 0) tick_max = 0;

    /* limpa a tela usando o comando nativo do SO.
     * Não usa ESC[2J pois ele não funciona corretamente no CMD do Windows,
     * aparecendo como caractere estranho em vez de limpar a tela. */
#ifdef _WIN32
    system("cls");
#else
    if (system("clear") != 0) { /* se "clear" falhar (ex.: Windows), ignora: o diagrama e apenas reimpresso abaixo */ }
#endif

    /* cabeçalho em ASCII puro (sem Unicode) para garantir compatibilidade com o CMD */
    printf("+==================================================+\n");
    printf("|         DIAGRAMA DE GANTT - TERMINAL             |\n");
    printf("+==================================================+\n\n");

    /* linha de números dos ticks (eixo X): cada número ocupa CELL caracteres */
    printf("      "); /* recuo para alinhar com os labels "T<id>" das tarefas */
    for (int t = 0; t <= tick_max; t++) printf("%-*d", CELL, t);
    printf("\n");

    /* separador horizontal entre os números dos ticks e as linhas das tarefas */
    printf("      ");
    for (int t = 0; t <= tick_max; t++) printf("----");
    printf("\n");

    /* ordena as tarefas por id_tarefa crescente usando bubble sort.
     * O vetor `ordem` guarda os índices reordenados sem alterar o vetor original.
     * Menor ID = base do diagrama (linha mais próxima ao eixo X) — requisito 2.5. */
    int *ordem = malloc((size_t)qtde_tarefas * sizeof(int));
    for (int k = 0; k < qtde_tarefas; k++) ordem[k] = k; /* inicializa com índices sequenciais */
    for (int a = 0; a < qtde_tarefas - 1; a++)
        for (int b = a + 1; b < qtde_tarefas; b++)
            if (tarefas[ordem[a]].id_tarefa > tarefas[ordem[b]].id_tarefa)
            {
                int tmp = ordem[a]; ordem[a] = ordem[b]; ordem[b] = tmp; /* troca os índices */
            }

    /* imprime do maior ID (topo) ao menor ID (base), espelhando o layout do SVG:
     * k = qtde_tarefas-1 → maior ID → primeira linha impressa (topo)
     * k = 0              → menor ID → última linha impressa (base, próxima ao eixo X) */
    for (int k = qtde_tarefas - 1; k >= 0; k--)
    {
        int i = ordem[k]; /* índice real da tarefa no vetor original */
        printf("T%-5d", tarefas[i].id_tarefa); /* label "T<id>" com 6 caracteres fixos */

        for (int t = 0; t <= tick_max; t++)
        {
            int estado = diagrama[i][t].estado;

            if (estado == EXECUTANDO)
            {
                /* converte a cor hex "RRGGBB" armazenada em TCB.cor para componentes R, G, B.
                 * sscanf com "%2x" lê exatamente 2 dígitos hexadecimais por componente. */
                unsigned int r = 0, g = 0, b = 0;
                sscanf(tarefas[i].cor, "%2x%2x%2x", &r, &g, &b);

                /* ativa fundo RGB verdadeiro (ESC[48;2;R;G;Bm) e texto branco (ESC[37m).
                 * True Color ANSI é suportado pelo Windows Terminal, VS Code, GNOME Terminal etc. */
                printf("\033[48;2;%u;%u;%um\033[37m", r, g, b);

                if (diagrama[i][t].evento == ACAO_ML)
                    /* solicitou mutex: "L<m>" em amarelo claro sobre o bloco */
                    printf("\033[93mL\033[37m%-*d", CELL - 1, diagrama[i][t].mutex_evento);
                else if (diagrama[i][t].evento == ACAO_MU)
                    /* liberou mutex: "U<m>" em verde claro sobre o bloco */
                    printf("\033[92mU\033[37m%-*d", CELL - 1, diagrama[i][t].mutex_evento);
                else if (t == tarefas[i].ingresso)
                    /* tick de ingresso + execução: "N" amarelo indica chegada + número da CPU */
                    printf("\033[33mN\033[37mC%-*d", CELL - 2, diagrama[i][t].cpu);
                else if (diagrama[i][t].sorteio)
                    /* sorteio de desempate: "S" vermelho + número da CPU */
                    printf("\033[31mS\033[37mC%-*d", CELL - 2, diagrama[i][t].cpu);
                else
                    /* execução normal: "C<numero_cpu>" preenchendo a célula */
                    printf("C%-*d", CELL - 1, diagrama[i][t].cpu);

                printf("\033[0m"); /* reseta todas as cores ao fim de cada célula */
            }
            else if (estado == SUSPENSA)
            {
                /* suspensa: cor + rotulo + TEXTURA por motivo (req. 2.9/3.8),
                 * espelhando os padroes do SVG:
                 *   E/S   -> fundo azul, rotulo IO/SI + pontilhado ░ (diagonais)
                 *   mutex -> fundo roxo, rotulo SM     + quadriculado ▒ (xadrez) */
                if (diagrama[i][t].motivo_susp == SUSP_IO)
                {
                    const char *txt = (diagrama[i][t].evento == ACAO_IO) ? "IO" : "SI";
                    printf("\033[48;2;31;97;141m\033[37m%s", txt);
                    for (int k = 2; k < CELL; k++) printf("\xe2\x96\x91"); /* ░ */
                    printf("\033[0m");
                }
                else
                {
                    printf("\033[48;2;108;52;131m\033[37mSM");
                    for (int k = 2; k < CELL; k++) printf("\xe2\x96\x92"); /* ▒ */
                    printf("\033[0m");
                }
            }

            else if (estado == TERMINADA)
                /* terminada: barra vertical indicando o fim da tarefa (equivale à linha no SVG) */
                printf("%-*s", CELL, "| ");

            else
            {
                if (t == tarefas[i].ingresso)
                    /* tick de chegada sem execução: exibe "N" de "nova" */
                    printf("%-*s", CELL, "N");
                else
                    /* célula vazia: tarefa ainda não chegou ou está apenas pronta */
                    printf("%-*s", CELL, " ");
            }
        }
        printf("\n");
    }
    free(ordem); /* libera o vetor de ordenação após o uso */

    /* separador inferior após as linhas das tarefas */
    printf("      ");
    for (int t = 0; t <= tick_max; t++) printf("----");
    printf("\n");

    /* seção de CPUs: exibe o estado (ociosa/ativa) de cada CPU tick a tick.
     * Cada CPU tem sua própria linha, permitindo visualizar múltiplas CPUs ociosas
     * no mesmo tick simultaneamente (requisito 1.2). */
    printf("\nCPUs:\n");
    for (int c = 0; c < qtde_cpus; c++)
    {
        printf("CPU%-3d", c); /* label "CPU0", "CPU1", etc. com 6 caracteres */
        for (int t = 0; t <= tick_max; t++)
        {
            if (tick_valido(t) && CPUs[c].ocioso_ticks[t])
                /* CPU ociosa neste tick: fundo cinza escuro + "OFF" */
                printf("\033[48;2;100;100;100m\033[37m%-*s\033[0m", CELL, "OFF");
            else if (tick_valido(t))
                /* CPU ativa neste tick: fundo verde escuro + " ON" */
                printf("\033[48;2;30;120;30m\033[37m%-*s\033[0m", CELL, " ON");
            else
                /* tick fora do intervalo válido: célula em branco */
                printf("%-*s", CELL, "   ");
        }
        /* exibe o total de ticks ociosos desta CPU ao final da linha */
        printf("  (desligada %d tick(s))\n", CPUs[c].tempo_desligado);
    }

    /* legenda: explica cada convenção visual usada no diagrama.
     * Cada linha usa o mesmo formato de escape ANSI das células acima,
     * permitindo que o usuário veja exemplos coloridos reais. */
    printf("\n");
    printf("+==================================================+\n");
    printf("| LEGENDA                                          |\n");
    printf("+--------------------------------------------------+\n");
    printf("| \033[48;2;80;120;200m\033[37mC0  \033[0m%-43s|\n", " Executando (cor da tarefa, C# = CPU)");
    printf("| \033[48;2;80;120;200m\033[33mN\033[37mC0 \033[0m%-43s|\n", " Ingresso + executando (N amarelo)");
    printf("| \033[48;2;108;52;131m\033[37mSM\xe2\x96\x92\xe2\x96\x92\033[0m%-43s|\n", " Suspensa por mutex (roxo, xadrez)");
    printf("| \033[48;2;31;97;141m\033[37mSI\xe2\x96\x91\xe2\x96\x91\033[0m%-43s|\n",  " Suspensa por E/S (azul, pontilhado)");
    printf("| \033[48;2;31;97;141m\033[37mIO\xe2\x96\x91\xe2\x96\x91\033[0m%-43s|\n",  " Inicio de operacao de E/S");
    printf("| \033[48;2;80;120;200m\033[93mL\033[37m0  \033[0m%-43s|\n", " Solicita mutex (L#)");
    printf("| \033[48;2;80;120;200m\033[92mU\033[37m0  \033[0m%-43s|\n", " Libera mutex (U#)");
    printf("| %-4s%-43s|\n", "|",  " Terminada (marca de fim da tarefa)");
    printf("| %-4s%-43s|\n", "N",  " Nova / Ingresso (aguardando execucao)");
    printf("| \033[48;2;80;120;200m\033[31mS\033[37mC0 \033[0m%-43s|\n", " Sorteio de desempate (S em vermelho)");
    printf("| \033[48;2;100;100;100m\033[37mOFF \033[0m%-43s|\n",         " CPU ociosa/desligada neste tick");
    printf("| \033[48;2;30;120;30m\033[37m ON \033[0m%-43s|\n",           " CPU ativa neste tick");
    printf("+==================================================+\n\n");
}

/* ============================================================================
 * criar_svg — gera o arquivo SVG do diagrama de Gantt.
 *
 * O arquivo é nomeado conforme o modo de execução:
 *   passo_a_passo=1, modificado=0 → "tick_N de <arquivo>.svg"
 *   passo_a_passo=1, modificado=1 → "tick_N de <arquivo> modificado.svg"
 *   passo_a_passo=0               → "execucao completa de <arquivo>.svg"
 * ============================================================================ */
void criar_svg(DiagramaGantt **diagrama, TCB *tarefas, int tick_max,
               int qtde_tarefas, const char *nome_arquivo,
               int passo_a_passo, int modificado,
               CPU *CPUs, int qtde_cpus)
{
    /* limita tick_max ao intervalo válido para não gerar SVG com dimensões absurdas.
     * CORRECAO (off-by-one): os lacos de celulas usam "t <= tick_max" e o vetor
     * diagrama[i] tem indices validos 0..limite_ticks-1; se a simulacao parou
     * no teto de ticks (ex.: deadlock), tick_max == limite_ticks e a celula
     * tick_max estouraria o vetor. Por isso o limite e limite_ticks - 1. */
    if (tick_max < 0)         tick_max = 0;
    if (tick_max >= limite_ticks_atual()) tick_max = limite_ticks_atual() - 1;

    /* constantes de layout do SVG em pixels:
     *   LARGURA_TICK  → largura de cada coluna (tick)
     *   ALTURA_TAREFA → altura de cada linha (tarefa)
     *   GRADE_BASE    → coordenada y onde a grade começa (espaço para título e ticks)
     *   ALTURA_CPU    → altura de cada linha na seção de CPUs abaixo da grade */
    const int LARGURA_TICK  = 40;
    const int ALTURA_TAREFA = 50;
    const int GRADE_BASE    = 65;
    const int ALTURA_CPU    = 25;

    /* largura mínima de 600px para que a legenda caiba sem ser cortada */
    int largura = tick_max * LARGURA_TICK + 100;
    if (largura < 600) largura = 600;

    /* altura total: grade + seção de CPUs + legenda + margem */
    int altura = GRADE_BASE
               + qtde_tarefas * ALTURA_TAREFA   /* linhas das tarefas */
               + 44 + qtde_cpus * ALTURA_CPU    /* cabeçalho + linhas das CPUs */
               + 175;                           /* legenda + margem inferior */

    /* monta o nome do arquivo SVG conforme o modo de execução */
    char nome_svg[300];
    if (passo_a_passo)
    {
        if (modificado)
            /* passo a passo com edição manual: sufixo "modificado" para diferenciar */
            snprintf(nome_svg, sizeof(nome_svg),
                     "tick_%d de %s modificado.svg", tick_max, nome_arquivo);
        else
            /* passo a passo normal: nome inclui o tick atual */
            snprintf(nome_svg, sizeof(nome_svg),
                     "tick_%d de %s.svg", tick_max, nome_arquivo);
    }
    else
        /* execução completa: nome único para o resultado final */
        snprintf(nome_svg, sizeof(nome_svg),
                 "execucao completa de %s.svg", nome_arquivo);

    /* abre o arquivo SVG para escrita; retorna sem gerar erro fatal se falhar */
    FILE *f = fopen(nome_svg, "w");
    if (!f) { printf("Erro ao criar SVG: %s\n", nome_svg); return; }

    /* cabeçalho SVG: define dimensões totais do arquivo */
    fprintf(f, "<svg xmlns='http://www.w3.org/2000/svg' width='%d' height='%d'>\n",
            largura, altura);

    /* gradiente linear usado na legenda para indicar que a cor de execução varia por tarefa.
     * Definido em <defs> para ser referenciado como fill='url(#execGrad)'. */
    fprintf(f,
        "<defs><linearGradient id='execGrad' x1='0' y1='0' x2='1' y2='0'>"
        "<stop offset='0%%'   stop-color='#e74c3c'/>"  /* vermelho */
        "<stop offset='33%%'  stop-color='#3498db'/>"  /* azul */
        "<stop offset='66%%'  stop-color='#2ecc71'/>"  /* verde */
        "<stop offset='100%%' stop-color='#f1c40f'/>"  /* amarelo */
        "</linearGradient>\n");

    /* Projeto B — padroes de preenchimento das suspensoes (req. 2.9):
     *   suspMutex : quadriculado (cross-hatch) sobre fundo roxo  -> suspensa por mutex
     *   suspIO    : listras diagonais sobre fundo azul          -> suspensa por E/S    */
    fprintf(f,
        "<pattern id='suspMutex' width='8' height='8' patternUnits='userSpaceOnUse'>"
        "<rect width='8' height='8' fill='#6c3483'/>"
        "<path d='M0 0 H8 M0 4 H8 M0 0 V8 M4 0 V8' stroke='white' stroke-width='1'/>"
        "</pattern>"
        "<pattern id='suspIO' width='8' height='8' patternUnits='userSpaceOnUse'>"
        "<rect width='8' height='8' fill='#1f618d'/>"
        "<path d='M0 8 L8 0' stroke='white' stroke-width='1.5'/>"
        "</pattern>"
        "</defs>\n");

    /* título "Diagrama de Gantt" posicionado acima da grade, alinhado ao tick 2 */
    int x_titulo = 2 * LARGURA_TICK + 50;
    fprintf(f, "<text x='%d' y='38' font-size='22' font-weight='bold'>"
               "Diagrama de Gantt</text>\n", x_titulo);

    /* numeração dos ticks no eixo X: centralizada em cada coluna (text-anchor='middle') */
    for (int t = 0; t < tick_max; t++)
        fprintf(f, "<text x='%d' y='58' font-size='12' text-anchor='middle'>%d</text>\n",
                t * LARGURA_TICK + 50 + LARGURA_TICK / 2, t);

    /* grade: linhas verticais separando cada tick */
    for (int t = 0; t <= tick_max; t++)
    {
        int x = t * LARGURA_TICK + 50;
        fprintf(f, "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='lightgray'/>\n",
                x, GRADE_BASE, x, GRADE_BASE + qtde_tarefas * ALTURA_TAREFA);
    }

    /* grade: linhas horizontais separando cada tarefa */
    for (int i = 0; i <= qtde_tarefas; i++)
    {
        int y = i * ALTURA_TAREFA + GRADE_BASE;
        fprintf(f, "<line x1='50' y1='%d' x2='%d' y2='%d' stroke='lightgray'/>\n",
                y, tick_max * LARGURA_TICK + 50, y);
    }

    /* ordena tarefas por id_tarefa crescente: menor ID = linha inferior (eixo X).
     * Mesmo bubble sort do terminal; opera sobre índices para não alterar o vetor. */
    int *ordem = malloc((size_t)qtde_tarefas * sizeof(int));
    for (int k = 0; k < qtde_tarefas; k++) ordem[k] = k;
    for (int a = 0; a < qtde_tarefas - 1; a++)
        for (int b = a + 1; b < qtde_tarefas; b++)
            if (tarefas[ordem[a]].id_tarefa > tarefas[ordem[b]].id_tarefa)
            {
                int tmp = ordem[a]; ordem[a] = ordem[b]; ordem[b] = tmp;
            }

    /* desenha os blocos das tarefas tick a tick */
    for (int k = 0; k < qtde_tarefas; k++)
    {
        int i = ordem[k];
        /* k=0 → menor ID → linha mais baixa (maior y, próxima ao eixo X).
         * A fórmula inverte k: qtde_tarefas-1-k=0 para o menor ID → y máximo. */
        int y = (qtde_tarefas - 1 - k) * ALTURA_TAREFA + GRADE_BASE;

        /* label "T<id>" à esquerda da grade, centralizado verticalmente na linha */
        fprintf(f, "<text x='10' y='%d' font-size='14'>T%d</text>\n",
                y + 25, tarefas[i].id_tarefa);

        for (int t = 0; t <= tick_max; t++)
        {
            int x = t * LARGURA_TICK + 50; /* coordenada x da célula atual */

            if (diagrama[i][t].estado == EXECUTANDO)
            {
                /* bloco colorido com a cor da tarefa (fill="#RRGGBB") */
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#%s\"/>\n",
                        x, y, LARGURA_TICK, ALTURA_TAREFA, tarefas[i].cor);
                /* texto "CPU N" sobre o bloco para indicar qual CPU executou */
                fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"10\" fill=\"white\">CPU %d</text>\n",
                        x + 2, y + 20, diagrama[i][t].cpu);

                /* overlay "nova" em branco no tick de ingresso sobre o bloco colorido */
                if (t == tarefas[i].ingresso)
                    fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"9\" "
                               "font-weight=\"bold\" fill=\"white\">nova</text>\n",
                            x + 2, y + 12);

                /* marcador de sorteio: círculo vermelho com "S" sobre o bloco */
                if (diagrama[i][t].sorteio)
                {
                    fprintf(f, "<circle cx=\"%d\" cy=\"%d\" r=\"6\" fill=\"red\"/>\n",
                            x + 30, y + 10);
                    fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"8\" fill=\"white\">S</text>\n",
                            x + 27, y + 13);
                }
            }
            else if (diagrama[i][t].estado == SUSPENSA)
            {
                /* suspensa: preenchimento por padrao conforme o motivo (req. 2.9).
                 * mutex -> quadriculado roxo; E/S -> diagonais azuis. */
                const char *fill = (diagrama[i][t].motivo_susp == SUSP_IO)
                                   ? "url(#suspIO)" : "url(#suspMutex)";
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" "
                           "fill=\"%s\" stroke=\"#222\" stroke-width=\"0.5\"/>\n",
                        x, y, LARGURA_TICK, ALTURA_TAREFA, fill);
            }

            else if (diagrama[i][t].estado == TERMINADA)
                /* terminada: linha vertical fina na borda esquerda da célula */
                fprintf(f, "<line x1=\"%d\" y1=\"%d\" x2=\"%d\" y2=\"%d\" "
                           "stroke=\"black\" stroke-width=\"3\"/>\n",
                        x, y + 5, x, y + ALTURA_TAREFA - 5);

            else if (diagrama[i][t].estado == 0 && t == tarefas[i].ingresso)
                /* célula vazia no tick de ingresso: texto "nova" indicando chegada da tarefa */
                fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"12\" fill=\"black\">nova</text>\n",
                        x + 2, y + 25);
            /* demais células vazias (estado == 0, tick != ingresso): não gera nenhum elemento SVG */

            /* Projeto B (req. 2.8 / 5.1): marcador grafico de evento.
             * "L<m>" laranja = solicitou mutex (ML); "U<m>" verde = liberou (MU);
             * "IO" azul = inicio de uma operacao de E/S. */
            if (diagrama[i][t].evento == ACAO_ML || diagrama[i][t].evento == ACAO_MU)
            {
                int  ml   = (diagrama[i][t].evento == ACAO_ML);
                const char *cor_ev = ml ? "#e67e22" : "#27ae60";
                char letra = ml ? 'L' : 'U';
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"18\" height=\"14\" "
                           "rx=\"3\" fill=\"%s\" stroke=\"black\" stroke-width=\"0.5\"/>\n",
                        x + LARGURA_TICK - 20, y + 3, cor_ev);
                fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"10\" font-weight=\"bold\" "
                           "fill=\"white\">%c%d</text>\n",
                        x + LARGURA_TICK - 18, y + 13, letra, diagrama[i][t].mutex_evento);
            }
            else if (diagrama[i][t].evento == ACAO_IO)
            {
                /* inicio de E/S: etiqueta "IO" azul-escuro no canto da celula */
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"20\" height=\"14\" "
                           "rx=\"3\" fill=\"#1f618d\" stroke=\"black\" stroke-width=\"0.5\"/>\n",
                        x + LARGURA_TICK - 22, y + 3);
                fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"9\" font-weight=\"bold\" "
                           "fill=\"white\">IO</text>\n",
                        x + LARGURA_TICK - 20, y + 13);
            }
        }
    }
    free(ordem); /* libera o vetor de ordenação após o desenho de todas as tarefas */

    /* seção de CPUs: uma linha por CPU mostrando ociosidade tick a tick.
     * Posicionada abaixo da grade, separada por um pequeno espaço (10px). */
    int y_cpu = GRADE_BASE + qtde_tarefas * ALTURA_TAREFA + 10;
    fprintf(f, "<text x='10' y='%d' font-size='13' font-weight='bold'>CPUs:</text>\n",
            y_cpu + 16);

    /* linhas verticais da seção de CPUs, alinhadas com a grade principal */
    for (int t = 0; t <= tick_max; t++)
    {
        int x = t * LARGURA_TICK + 50;
        fprintf(f, "<line x1='%d' y1='%d' x2='%d' y2='%d' stroke='lightgray'/>\n",
                x, y_cpu + 22, x, y_cpu + 22 + qtde_cpus * ALTURA_CPU);
    }

    for (int c = 0; c < qtde_cpus; c++)
    {
        int y_row = y_cpu + 22 + c * ALTURA_CPU; /* y da linha desta CPU */

        /* label "CPU<n>" à esquerda da seção, centralizado verticalmente */
        fprintf(f, "<text x='10' y='%d' font-size='12'>CPU%d</text>\n",
                y_row + ALTURA_CPU / 2 + 4, c);
        /* linha horizontal superior da linha desta CPU */
        fprintf(f, "<line x1='50' y1='%d' x2='%d' y2='%d' stroke='lightgray'/>\n",
                y_row, tick_max * LARGURA_TICK + 50, y_row);

        /* bloco cinza com "OFF" (ociosa) ou verde com "ON" (ativa) por tick */
        for (int t = 0; t < tick_max; t++)
        {
            int x = t * LARGURA_TICK + 50;
            if (!tick_valido(t)) continue;
            if (CPUs[c].ocioso_ticks[t])
            {
                /* CPU ociosa neste tick: bloco cinza + "OFF" */
                fprintf(f, "<rect x='%d' y='%d' width='%d' height='%d' fill='gray'/>\n",
                        x, y_row, LARGURA_TICK, ALTURA_CPU);
                fprintf(f, "<text x='%d' y='%d' font-size='9' fill='black'>OFF</text>\n",
                        x + 4, y_row + 14);
            }
            else
            {
                /* CPU ativa neste tick: bloco verde + "ON" (espelha o terminal) */
                fprintf(f, "<rect x='%d' y='%d' width='%d' height='%d' fill='#1e781e'/>\n",
                        x, y_row, LARGURA_TICK, ALTURA_CPU);
                fprintf(f, "<text x='%d' y='%d' font-size='9' fill='white'>ON</text>\n",
                        x + 6, y_row + 14);
            }
        }

        /* linha horizontal inferior da linha desta CPU */
        fprintf(f, "<line x1='50' y1='%d' x2='%d' y2='%d' stroke='lightgray'/>\n",
                y_row + ALTURA_CPU, tick_max * LARGURA_TICK + 50, y_row + ALTURA_CPU);
    }

    /* legenda: posicionada abaixo da seção de CPUs com margem de 12px.
     * Dividida em duas linhas de ícones + texto descritivo. */
    int y_leg = GRADE_BASE + qtde_tarefas * ALTURA_TAREFA
                + 44 + qtde_cpus * ALTURA_CPU + 12;

    fprintf(f, "<text x='20' y='%d' font-size='14' font-weight='bold'>Legenda</text>\n", y_leg);

    /* linha 1 da legenda: Executando | Suspensa | CPU Ociosa */

    /* ícone de executando: gradiente multicolorido para indicar que a cor varia por tarefa */
    fprintf(f, "<rect x='20' y='%d' width='18' height='18' "
               "fill='url(#execGrad)' stroke='#999' stroke-width='0.5'/>\n", y_leg + 6);
    fprintf(f, "<text x='44' y='%d' font-size='12'>Executando (cor da tarefa)</text>\n", y_leg + 19);

    /* ícone de inicio de E/S: bloco azul com "IO" (mesmo estilo do marcador no diagrama) */
    fprintf(f, "<rect x='245' y='%d' width='22' height='18' rx='3' fill='#1f618d' "
               "stroke='black' stroke-width='0.5'/>\n", y_leg + 6);
    fprintf(f, "<text x='247' y='%d' font-size='10' font-weight='bold' fill='white'>IO</text>\n", y_leg + 19);
    fprintf(f, "<text x='273' y='%d' font-size='12'>Inicio de E/S</text>\n", y_leg + 19);

    /* ícone de CPU ociosa: bloco cinza (mesmo estilo da seção de CPUs) */
    fprintf(f, "<rect x='420' y='%d' width='18' height='18' fill='gray'/>\n", y_leg + 6);
    fprintf(f, "<text x='444' y='%d' font-size='12'>CPU Ociosa</text>\n", y_leg + 19);

    /* ícone de CPU ativa: bloco verde (mesmo estilo da seção de CPUs) */
    fprintf(f, "<rect x='560' y='%d' width='18' height='18' fill='#1e781e'/>\n", y_leg + 6);
    fprintf(f, "<text x='584' y='%d' font-size='12'>CPU Ativa</text>\n", y_leg + 19);

    /* linha 2 da legenda: Terminada | Nova | Sorteio */
    int y_leg2 = y_leg + 32; /* deslocamento vertical para a segunda linha da legenda */

    /* ícone de terminada: linha vertical fina preta (mesmo estilo da grade) */
    fprintf(f, "<line x1='29' y1='%d' x2='29' y2='%d' stroke='black' stroke-width='3'/>\n",
            y_leg2 + 4, y_leg2 + 18);
    fprintf(f, "<text x='44' y='%d' font-size='12'>Terminada</text>\n", y_leg2 + 15);

    /* ícone de nova/ingresso: retângulo tracejado com texto "nova" */
    fprintf(f, "<rect x='245' y='%d' width='40' height='18' fill='none' "
               "stroke='#aaa' stroke-dasharray='3,2'/>\n", y_leg2 + 4);
    fprintf(f, "<text x='248' y='%d' font-size='11' fill='black'>nova</text>\n", y_leg2 + 16);
    fprintf(f, "<text x='291' y='%d' font-size='12'>Chegada da tarefa</text>\n", y_leg2 + 16);

    /* ícone de sorteio: círculo vermelho com "S" (mesmo estilo dos blocos do diagrama) */
    fprintf(f, "<circle cx='429' cy='%d' r='8' fill='red'/>\n", y_leg2 + 13);
    fprintf(f, "<text x='426' y='%d' font-size='8' fill='white'>S</text>\n", y_leg2 + 16);
    fprintf(f, "<text x='444' y='%d' font-size='12'>Sorteio (desempate)</text>\n", y_leg2 + 17);

    /* linha 3 da legenda (Projeto B): suspensoes e eventos de mutex */
    int y_leg3 = y_leg2 + 32;

    /* suspensa por mutex: amostra do padrao quadriculado roxo */
    fprintf(f, "<rect x='20' y='%d' width='18' height='18' fill='url(#suspMutex)' "
               "stroke='#222' stroke-width='0.5'/>\n", y_leg3 + 4);
    fprintf(f, "<text x='44' y='%d' font-size='12'>Suspensa por mutex</text>\n", y_leg3 + 17);

    /* suspensa por E/S: amostra do padrao diagonal azul */
    fprintf(f, "<rect x='245' y='%d' width='18' height='18' fill='url(#suspIO)' "
               "stroke='#222' stroke-width='0.5'/>\n", y_leg3 + 4);
    fprintf(f, "<text x='269' y='%d' font-size='12'>Suspensa por E/S</text>\n", y_leg3 + 17);

    /* evento solicitar (ML) */
    fprintf(f, "<rect x='420' y='%d' width='18' height='14' rx='3' fill='#e67e22' "
               "stroke='black' stroke-width='0.5'/>\n", y_leg3 + 5);
    fprintf(f, "<text x='422' y='%d' font-size='10' font-weight='bold' fill='white'>L#</text>\n", y_leg3 + 15);
    fprintf(f, "<text x='444' y='%d' font-size='12'>Solicita mutex #</text>\n", y_leg3 + 17);

    /* evento liberar (MU) */
    fprintf(f, "<rect x='600' y='%d' width='18' height='14' rx='3' fill='#27ae60' "
               "stroke='black' stroke-width='0.5'/>\n", y_leg3 + 5);
    fprintf(f, "<text x='602' y='%d' font-size='10' font-weight='bold' fill='white'>U#</text>\n", y_leg3 + 15);
    fprintf(f, "<text x='624' y='%d' font-size='12'>Libera mutex #</text>\n", y_leg3 + 17);

    /* resumo de ociosidade por CPU: um quadrado cinza + texto "CPU N: X tick(s)" por CPU.
     * x_cpu avança 130px a cada CPU para exibi-las lado a lado na mesma linha. */
    int y_ocioso = y_leg3 + 38;
    fprintf(f, "<text x='20' y='%d' font-size='13' font-weight='bold'>"
               "Ociosidade por CPU:</text>\n", y_ocioso);
    int x_cpu = 20; /* posição x inicial do primeiro item */
    for (int i = 0; i < qtde_cpus; i++)
    {
        fprintf(f,
            "<rect x='%d' y='%d' width='10' height='10' fill='gray'/>"  /* quadrado cinza */
            "<text x='%d' y='%d' font-size='12'> CPU %d: %d tick(s)</text>\n", /* texto descritivo */
            x_cpu, y_ocioso + 6,
            x_cpu + 12, y_ocioso + 16,
            i, CPUs[i].tempo_desligado);
        x_cpu += 130; /* avança para a próxima CPU na mesma linha */
    }

    /* fecha a tag raiz do SVG e o arquivo */
    fprintf(f, "</svg>");
    fclose(f);
}
