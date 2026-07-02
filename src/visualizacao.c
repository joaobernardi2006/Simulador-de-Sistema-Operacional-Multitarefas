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

/* Detecta a largura do terminal para quebrar o diagrama em faixas de ticks
 * (evita que linhas longas "entortem" quando ha muitos ticks/tarefas). */
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/ioctl.h>
#include <unistd.h>
#endif

/* Simbolo de cadeado usado nas celulas de suspensao por mutex no terminal.
 * O emoji ocupa 2 colunas na maioria dos terminais modernos (Windows Terminal,
 * GNOME Terminal, VS Code). Se no seu terminal o alinhamento quebrar, troque
 * a definicao abaixo por "SM" (2 caracteres) que o layout volta ao normal. */
#define CADEADO "\xF0\x9F\x94\x92" /* U+1F512 (2 colunas de largura) */

/* Retorna a quantidade de colunas do terminal (fallback: 120). */
static int largura_terminal(void)
{
#ifdef _WIN32
    CONSOLE_SCREEN_BUFFER_INFO csbi;
    if (GetConsoleScreenBufferInfo(GetStdHandle(STD_OUTPUT_HANDLE), &csbi))
        return csbi.srWindow.Right - csbi.srWindow.Left + 1;
#else
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col > 0)
        return ws.ws_col;
#endif
    return 120; /* valor razoavel quando a deteccao falha (ex.: saida redirecionada) */
}


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
    const int CELL = 4; /* largura de cada celula em caracteres visiveis */

    /* limpa a tela usando o comando nativo do SO (ESC[2J falha no CMD antigo) */
#ifdef _WIN32
    system("cls");
#else
    if (system("clear") != 0) { /* se "clear" falhar, apenas reimprime abaixo */ }
#endif

    printf("+==================================================+\n");
    printf("|         DIAGRAMA DE GANTT - TERMINAL             |\n");
    printf("+==================================================+\n\n");

    /* ordena as tarefas por id_tarefa crescente (menor ID = base do diagrama,
     * requisito 2.5). O vetor `ordem` guarda indices; o original nao e alterado. */
    int *ordem = malloc((size_t)qtde_tarefas * sizeof(int));
    for (int k = 0; k < qtde_tarefas; k++) ordem[k] = k;
    for (int a = 0; a < qtde_tarefas - 1; a++)
        for (int b = a + 1; b < qtde_tarefas; b++)
            if (tarefas[ordem[a]].id_tarefa > tarefas[ordem[b]].id_tarefa)
            {
                int tmp = ordem[a]; ordem[a] = ordem[b]; ordem[b] = tmp;
            }

    /* ---- QUEBRA EM FAIXAS ------------------------------------------------
     * O diagrama e dividido em faixas horizontais de ticks conforme a largura
     * do terminal, para que nenhuma linha exceda a tela e "entorte" o layout.
     * Cada faixa repete o cabecalho de ticks, as linhas das tarefas e as
     * linhas das CPUs referentes aquele intervalo de ticks. */
    /* Pre-computa o tick de termino de cada tarefa (celula TERMINADA no
     * diagrama). Usado para desenhar a FAIXA DE CONTINUIDADE: as celulas em
     * que a tarefa existe mas esta ociosa (pronta, aguardando CPU) sao
     * preenchidas com um cinza discreto, dando a impressao de uma trilha
     * continua da chegada ("nova") ate o fim ("fim"), em vez de celulas
     * soltas. O cinza neutro preserva o req. 2.1 (pronta = ausencia da COR
     * da tarefa). Tarefas nao finalizadas tem a faixa ate o ultimo tick. */
    int *fim_tick = malloc((size_t)qtde_tarefas * sizeof(int));
    for (int i = 0; i < qtde_tarefas; i++)
    {
        fim_tick[i] = tick_max; /* padrao: nao terminou (deadlock/limite) */
        for (int t = 0; t <= tick_max; t++)
            if (diagrama[i][t].estado == TERMINADA) { fim_tick[i] = t; break; }
    }

    int colunas = largura_terminal();
    int ticks_por_faixa = (colunas - 8) / CELL; /* 8 = label "T<id>"/"CPU<n>" + folga */
    if (ticks_por_faixa < 5) ticks_por_faixa = 5; /* minimo utilizavel */

    for (int t_ini = 0; t_ini <= tick_max; t_ini += ticks_por_faixa)
    {
        int t_fim = t_ini + ticks_por_faixa - 1;
        if (t_fim > tick_max) t_fim = tick_max;

        /* cabecalho da faixa: intervalo de ticks coberto */
        if (tick_max >= ticks_por_faixa)
            printf("--- Ticks %d a %d ---\n", t_ini, t_fim);

        /* linha de numeros dos ticks (eixo X) */
        printf("      ");
        for (int t = t_ini; t <= t_fim; t++) printf("%-*d", CELL, t);
        printf("\n      ");
        for (int t = t_ini; t <= t_fim; t++) printf("----");
        printf("\n");

        /* imprime do maior ID (topo) ao menor ID (base), espelhando o SVG */
        for (int k = qtde_tarefas - 1; k >= 0; k--)
        {
            int i = ordem[k];
            printf("T%-5d", tarefas[i].id_tarefa);

            for (int t = t_ini; t <= t_fim; t++)
            {
                int estado = diagrama[i][t].estado;

                /* CHEGADA: sempre visivel, independente do que mais acontecer
                 * neste tick (antes, eventos de mutex/E-S no mesmo tick
                 * "engoliam" o marcador e nem todas as chegadas apareciam).
                 * Quadrado branco com o texto "nova" (4 colunas exatas). */
                if (t == tarefas[i].ingresso)
                {
                    printf("\033[48;2;255;255;255m\033[30mnova\033[0m");
                    continue;
                }

                /* TERMINO: quadrado branco com "fim" no lugar da barra preta */
                if (estado == TERMINADA)
                {
                    printf("\033[48;2;255;255;255m\033[30mfim \033[0m");
                    continue;
                }

                if (estado == EXECUTANDO)
                {
                    unsigned int r = 0, g = 0, b = 0;
                    sscanf(tarefas[i].cor, "%2x%2x%2x", &r, &g, &b);
                    printf("\033[48;2;%u;%u;%um\033[37m", r, g, b);

                    if (diagrama[i][t].evento == ACAO_ML)
                        printf("\033[93mL\033[37m%-*d", CELL - 1, diagrama[i][t].mutex_evento);
                    else if (diagrama[i][t].evento == ACAO_MU)
                        printf("\033[92mU\033[37m%-*d", CELL - 1, diagrama[i][t].mutex_evento);
                    else if (diagrama[i][t].sorteio)
                        printf("\033[31mS\033[37mC%-*d", CELL - 2, diagrama[i][t].cpu);
                    else
                        printf("C%-*d", CELL - 1, diagrama[i][t].cpu);

                    printf("\033[0m");
                }
                else if (estado == SUSPENSA)
                {
                    /* suspensa: tons NEUTROS de cinza com padroes limpos,
                     * mantendo a diferenciacao exigida pelo req. 2.9/3.8:
                     *   mutex -> cinza escuro + CADEADO (2 col) + "M "
                     *   E/S   -> cinza claro  + "IO--"/"SI--" (linhas)     */
                    if (diagrama[i][t].motivo_susp == SUSP_IO)
                    {
                        const char *txt = (diagrama[i][t].evento == ACAO_IO) ? "IO" : "SI";
                        printf("\033[48;2;170;170;170m\033[30m%s\xe2\x94\x80\xe2\x94\x80\033[0m", txt);
                    }
                    else
                    {
                        printf("\033[48;2;110;110;110m\033[37m" CADEADO "M \033[0m");
                    }
                }
                else if (t > tarefas[i].ingresso && t < fim_tick[i])
                    /* tarefa viva porem ociosa (pronta na fila): faixa cinza
                     * discreta para dar continuidade visual entre nova e fim */
                    printf("\033[48;2;58;58;58m%-*s\033[0m", CELL, " ");
                else
                    printf("%-*s", CELL, " "); /* fora da vida da tarefa: vazio */
            }
            printf("\n");
        }

        printf("      ");
        for (int t = t_ini; t <= t_fim; t++) printf("----");
        printf("\n");

        /* secao de CPUs desta faixa (requisito 1.2) */
        for (int c = 0; c < qtde_cpus; c++)
        {
            printf("CPU%-3d", c);
            for (int t = t_ini; t <= t_fim; t++)
            {
                if (tick_valido(t) && CPUs[c].ocioso_ticks[t])
                    printf("\033[48;2;100;100;100m\033[37m%-*s\033[0m", CELL, "OFF");
                else if (tick_valido(t))
                    printf("\033[48;2;30;120;30m\033[37m%-*s\033[0m", CELL, " ON");
                else
                    printf("%-*s", CELL, "   ");
            }
            printf("\n");
        }
        printf("\n"); /* separa as faixas */
    }
    free(ordem);
    free(fim_tick);

    /* totais de ociosidade (uma vez so, apos todas as faixas) */
    printf("Ociosidade por CPU:\n");
    for (int c = 0; c < qtde_cpus; c++)
        printf("  CPU %d: desligada por %d tick(s)\n", c, CPUs[c].tempo_desligado);

    /* legenda unica ao final, com amostras coloridas reais */
    printf("\n");
    printf("+==================================================+\n");
    printf("| LEGENDA (ciclo de vida da tarefa)                |\n");
    printf("+--------------------------------------------------+\n");
    printf("| \033[48;2;255;255;255m\033[30mnova\033[0m%-43s|\n", " Chegada da tarefa no sistema");
    printf("| \033[48;2;255;255;255m\033[30mfim \033[0m%-43s|\n", " Tarefa terminada");
    printf("| \033[48;2;58;58;58m    \033[0m%-43s|\n", " Pronta, aguardando CPU (celula cheia)");
    printf("| \033[48;2;80;120;200m\033[37mC0  \033[0m%-43s|\n", " Executando (cor da tarefa, C# = CPU)");
    printf("| \033[48;2;110;110;110m\033[37m" CADEADO "M \033[0m%-43s|\n", " Suspensa aguardando mutex (cadeado)");
    printf("| \033[48;2;170;170;170m\033[30mSI\xe2\x94\x80\xe2\x94\x80\033[0m%-43s|\n", " Suspensa por operacao de E/S");
    printf("| \033[48;2;80;120;200m\033[93mL\033[37m0  \033[0m%-43s|\n", " Solicita o mutex # (lock)");
    printf("| \033[48;2;80;120;200m\033[92mU\033[37m0  \033[0m%-43s|\n", " Libera o mutex # (unlock)");
    printf("| \033[48;2;170;170;170m\033[30mIO\xe2\x94\x80\xe2\x94\x80\033[0m%-43s|\n", " Inicio de operacao de E/S");
    printf("| \033[48;2;80;120;200m\033[31mS\033[37mC0 \033[0m%-43s|\n", " Escolhida por sorteio (desempate)");
    printf("| \033[48;2;30;120;30m\033[37m ON \033[0m%-43s|\n", " CPU ativa no tick");
    printf("| \033[48;2;100;100;100m\033[37mOFF \033[0m%-43s|\n", " CPU ociosa/desligada no tick");
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
    /* limita tick_max ao intervalo válido para não gerar SVG com dimensões absurdas */
    if (tick_max < 0)         tick_max = 0;
    if (tick_max > limite_ticks_atual()) tick_max = limite_ticks_atual();

    /* constantes de layout do SVG em pixels:
     *   LARGURA_TICK  → largura de cada coluna (tick)
     *   ALTURA_TAREFA → altura de cada linha (tarefa)
     *   GRADE_BASE    → coordenada y onde a grade começa (espaço para título e ticks)
     *   ALTURA_CPU    → altura de cada linha na seção de CPUs abaixo da grade */
    const int LARGURA_TICK  = 40;
    const int ALTURA_TAREFA = 50;
    const int MB = 5; /* margem vertical da BARRA de cada tarefa: cria o respiro
                       * entre as linhas e permite o contorno fino que delimita
                       * cada tarefa como uma barra continua e separada */
    const int GRADE_BASE    = 65;
    const int ALTURA_CPU    = 25;

    /* largura mínima de 600px para que a legenda caiba sem ser cortada */
    int largura = tick_max * LARGURA_TICK + 100;
    if (largura < 600) largura = 600;

    /* altura total: grade + seção de CPUs + legenda VERTICAL + margem.
     * A legenda tem 12 itens de 24px cada, mais o resumo de ociosidade
     * (uma linha por CPU) e margens. */
    int altura = GRADE_BASE
               + qtde_tarefas * ALTURA_TAREFA   /* linhas das tarefas */
               + 44 + qtde_cpus * ALTURA_CPU    /* cabeçalho + linhas das CPUs */
               + 40 + 12 * 24                   /* titulo + itens da legenda */
               + 30 + qtde_cpus * 18 + 20;      /* ociosidade por CPU + margem */

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
    /* Padroes NEUTROS (tons de cinza) com linhas finas e espacadas, mantendo a
     * diferenciacao grafica entre os dois motivos de suspensao (req. 2.9/3.8):
     *   suspMutex : cinza medio + linhas HORIZONTAIS finas (+ cadeado por celula)
     *   suspIO    : cinza claro + linhas DIAGONAIS finas                        */
    fprintf(f,
        "<pattern id='suspMutex' width='10' height='10' patternUnits='userSpaceOnUse'>"
        "<rect width='10' height='10' fill='#d9d9d9'/>"
        "<path d='M0 5 H10' stroke='#8f8f8f' stroke-width='1'/>"
        "</pattern>"
        "<pattern id='suspIO' width='10' height='10' patternUnits='userSpaceOnUse'>"
        "<rect width='10' height='10' fill='#efefef'/>"
        "<path d='M0 10 L10 0' stroke='#a5a5a5' stroke-width='1'/>"
        "</pattern>\n");
    /* Icone de cadeado reutilizavel (desenhado em cada celula suspensa por
     * mutex via <use>): arco = alca, retangulo = corpo, ponto = fechadura. */
    fprintf(f,
        "<g id='cadeado'>"
        "<path d='M-4 -1 v-3 a4 4 0 0 1 8 0 v3' fill='none' stroke='#555' stroke-width='2'/>"
        "<rect x='-6' y='-1' width='12' height='10' rx='2' fill='#555'/>"
        "<circle cx='0' cy='4' r='1.6' fill='#d9d9d9'/>"
        "</g>"
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

        /* CONTINUIDADE: faixa cinza-clara de ALTURA CHEIA cobrindo toda a
         * vida da tarefa (da chegada ao termino). Como ocupa a celula
         * inteira, os periodos ociosos (pronta na fila) aparecem como
         * celulas totalmente preenchidas e continuas, sem "linha passando".
         * As celulas de execucao/suspensao sao desenhadas POR CIMA. O cinza
         * neutro preserva o req. 2.1 (pronta = ausencia da COR da tarefa). */
        /* A tarefa termina na FRONTEIRA entre seu ultimo tick executado e o
         * tick seguinte (onde o estado TERMINADA e registrado, mantendo o
         * turnaround = fim - ingresso correto). A barra, portanto, termina
         * exatamente nessa fronteira, sem avancar pela celula seguinte; o
         * badge "fim" e desenhado colado nela, do lado de fora da barra. */
        int t_fim_vida = -1; /* tick em que TERMINADA foi registrado */
        for (int t = 0; t <= tick_max; t++)
            if (diagrama[i][t].estado == TERMINADA) { t_fim_vida = t; break; }
        int x_barra_ini = tarefas[i].ingresso * LARGURA_TICK + 50;
        int x_barra_fim = (t_fim_vida >= 0)
            ? t_fim_vida * LARGURA_TICK + 50          /* fronteira do termino  */
            : (tick_max + 1) * LARGURA_TICK + 50;     /* nao terminou: ate o fim */
        fprintf(f, "<rect x='%d' y='%d' width='%d' height='%d' fill='#ebebeb'/>\n",
                x_barra_ini, y + MB, x_barra_fim - x_barra_ini, ALTURA_TAREFA - 2 * MB);

        for (int t = 0; t <= tick_max; t++)
        {
            int x = t * LARGURA_TICK + 50; /* coordenada x da célula atual */

            if (diagrama[i][t].estado == EXECUTANDO)
            {
                /* bloco colorido com a cor da tarefa (fill="#RRGGBB") */
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" fill=\"#%s\"/>\n",
                        x, y + MB, LARGURA_TICK, ALTURA_TAREFA - 2 * MB, tarefas[i].cor);
                /* texto "CPU N" sobre o bloco para indicar qual CPU executou
                 * (abaixo do meio para nao colidir com os badges do topo) */
                fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"10\" fill=\"white\">CPU %d</text>\n",
                        x + 2, y + ALTURA_TAREFA / 2 + 6, diagrama[i][t].cpu);

                /* marcador de sorteio: quadradinho vermelho com "S" no canto
                 * inferior esquerdo do bloco (nao colide com nova/L#/U#) */
                if (diagrama[i][t].sorteio)
                {
                    fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"12\" height=\"12\" rx=\"2\" "
                               "fill=\"#c0392b\" stroke=\"white\" stroke-width=\"0.5\"/>\n",
                            x + 2, y + ALTURA_TAREFA - MB - 14);
                    fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"9\" font-weight=\"bold\" "
                               "fill=\"white\">S</text>\n",
                            x + 5, y + ALTURA_TAREFA - MB - 4);
                }
            }
            else if (diagrama[i][t].estado == SUSPENSA)
            {
                /* suspensa: preenchimento por padrao conforme o motivo (req. 2.9).
                 * mutex -> quadriculado roxo; E/S -> diagonais azuis. */
                const char *fill = (diagrama[i][t].motivo_susp == SUSP_IO)
                                   ? "url(#suspIO)" : "url(#suspMutex)";
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"%d\" height=\"%d\" "
                           "fill=\"%s\"/>\n",
                        x, y + MB, LARGURA_TICK, ALTURA_TAREFA - 2 * MB, fill);
                /* cadeado centralizado em CADA celula suspensa por mutex,
                 * deixando explicito que a tarefa aguarda o recurso travado */
                if (diagrama[i][t].motivo_susp != SUSP_IO)
                    fprintf(f, "<use href=\"#cadeado\" x=\"%d\" y=\"%d\"/>\n",
                            x + LARGURA_TICK / 2, y + ALTURA_TAREFA / 2 - 2);
            }

            else if (diagrama[i][t].estado == TERMINADA)
            {
                /* terminada: quadradinho branco com "fim" COLADO na fronteira
                 * do ultimo tick executado (x = inicio desta celula), fora da
                 * barra — deixa claro que a tarefa acabou NA fronteira e nao
                 * ocupou este tick */
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"28\" height=\"16\" rx=\"2\" "
                           "fill=\"white\" stroke=\"#333\" stroke-width=\"1\"/>\n",
                        x + 1, y + ALTURA_TAREFA / 2 - 8);
                fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"11\" fill=\"black\">fim</text>\n",
                        x + 6, y + ALTURA_TAREFA / 2 + 4);
            }

            /* demais células vazias: não geram elemento SVG (chegada tratada abaixo) */

            /* CHEGADA: badge branco "nova" desenhado SEMPRE no tick de
             * ingresso, por cima de qualquer estado/evento. Antes, chegadas
             * que coincidiam com lock/E-S no mesmo tick nao apareciam. */
            if (t == tarefas[i].ingresso)
            {
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"32\" height=\"14\" rx=\"2\" "
                           "fill=\"white\" stroke=\"#333\" stroke-width=\"1\"/>\n",
                        x + 1, y + MB + 2);
                fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"10\" fill=\"black\">nova</text>\n",
                        x + 5, y + MB + 13);
            }

            /* Projeto B (req. 2.8 / 5.1): marcador grafico de evento.
             * "L<m>" laranja = solicitou mutex (ML); "U<m>" verde = liberou (MU);
             * "IO" azul = inicio de uma operacao de E/S. */
            if (diagrama[i][t].evento == ACAO_ML || diagrama[i][t].evento == ACAO_MU)
            {
                int  ml   = (diagrama[i][t].evento == ACAO_ML);
                const char *cor_ev = ml ? "#e67e22" : "#27ae60";
                char letra = ml ? 'L' : 'U';
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"18\" height=\"14\" "
                           "rx=\"2\" fill=\"%s\"/>\n",
                        x + LARGURA_TICK - 20, y + MB + 2, cor_ev);
                fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"10\" font-weight=\"bold\" "
                           "fill=\"white\">%c%d</text>\n",
                        x + LARGURA_TICK - 18, y + MB + 12, letra, diagrama[i][t].mutex_evento);
            }
            else if (diagrama[i][t].evento == ACAO_IO)
            {
                /* inicio de E/S: etiqueta "IO" azul-escuro no canto da celula */
                fprintf(f, "<rect x=\"%d\" y=\"%d\" width=\"20\" height=\"14\" "
                           "rx=\"2\" fill=\"#566573\"/>\n",
                        x + LARGURA_TICK - 22, y + MB + 2);
                fprintf(f, "<text x=\"%d\" y=\"%d\" font-size=\"9\" font-weight=\"bold\" "
                           "fill=\"white\">IO</text>\n",
                        x + LARGURA_TICK - 20, y + MB + 12);
            }
        }

        /* CONTORNO DA BARRA: retangulo fino cinza-escuro desenhado por cima
         * de todo o conteudo da tarefa, delimitando-a como uma barra unica e
         * separando visualmente cada tarefa das demais. */
        fprintf(f, "<rect x='%d' y='%d' width='%d' height='%d' fill='none' "
                   "stroke='#444' stroke-width='1.2'/>\n",
                x_barra_ini, y + MB, x_barra_fim - x_barra_ini, ALTURA_TAREFA - 2 * MB);
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

    /* ------------------------------------------------------------------
     * LEGENDA — coluna unica, VERTICAL, ordenada pelo ciclo de vida da
     * tarefa: chegada -> pronta -> executando -> suspensoes -> eventos ->
     * termino -> estado das CPUs. Cada item ocupa uma linha de 24px, com o
     * icone em x=20..46 e o texto descritivo em x=54.
     * ------------------------------------------------------------------ */
    int y_leg = GRADE_BASE + qtde_tarefas * ALTURA_TAREFA
                + 44 + qtde_cpus * ALTURA_CPU + 12;

    fprintf(f, "<text x='20' y='%d' font-size='14' font-weight='bold'>Legenda</text>\n", y_leg);

    int yl = y_leg + 12;          /* y do topo do primeiro item */
    const int PASSO_LEG = 24;     /* altura de cada linha da legenda */

    /* 1. chegada da tarefa */
    fprintf(f, "<rect x='20' y='%d' width='32' height='15' rx='2' fill='white' "
               "stroke='#333' stroke-width='1'/>\n", yl + 2);
    fprintf(f, "<text x='25' y='%d' font-size='10' fill='black'>nova</text>\n", yl + 13);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Chegada da tarefa no sistema</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 2. termino da tarefa (logo apos a chegada: inicio e fim juntos) */
    fprintf(f, "<rect x='20' y='%d' width='28' height='15' rx='2' fill='white' "
               "stroke='#333' stroke-width='1'/>\n", yl + 2);
    fprintf(f, "<text x='26' y='%d' font-size='10' fill='black'>fim</text>\n", yl + 13);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Tarefa terminada</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 3. pronta aguardando CPU (celula preenchida, contorno da barra) */
    fprintf(f, "<rect x='20' y='%d' width='18' height='18' fill='#ebebeb' "
               "stroke='#444' stroke-width='1.2'/>\n", yl + 1);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Pronta, aguardando CPU (fila de prontos)</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 4. executando */
    fprintf(f, "<rect x='20' y='%d' width='18' height='18' "
               "fill='url(#execGrad)'/>\n", yl + 1);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Executando (cor da tarefa, rotulo CPU #)</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 5. suspensa por mutex (cadeado) */
    fprintf(f, "<rect x='20' y='%d' width='18' height='18' fill='url(#suspMutex)'/>\n", yl + 1);
    fprintf(f, "<use href='#cadeado' x='29' y='%d'/>\n", yl + 9);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Suspensa aguardando mutex (cadeado)</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 6. suspensa por E/S */
    fprintf(f, "<rect x='20' y='%d' width='18' height='18' fill='url(#suspIO)'/>\n", yl + 1);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Suspensa por operacao de E/S</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 7. solicita mutex */
    fprintf(f, "<rect x='20' y='%d' width='18' height='14' rx='2' fill='#e67e22'/>\n", yl + 3);
    fprintf(f, "<text x='22' y='%d' font-size='10' font-weight='bold' fill='white'>L#</text>\n", yl + 13);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Solicita o mutex # (lock)</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 8. libera mutex */
    fprintf(f, "<rect x='20' y='%d' width='18' height='14' rx='2' fill='#27ae60'/>\n", yl + 3);
    fprintf(f, "<text x='22' y='%d' font-size='10' font-weight='bold' fill='white'>U#</text>\n", yl + 13);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Libera o mutex # (unlock)</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 9. inicio de E/S */
    fprintf(f, "<rect x='20' y='%d' width='22' height='14' rx='2' fill='#566573'/>\n", yl + 3);
    fprintf(f, "<text x='24' y='%d' font-size='10' font-weight='bold' fill='white'>IO</text>\n", yl + 13);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Inicio de operacao de E/S</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 10. sorteio de desempate (quadradinho) */
    fprintf(f, "<rect x='20' y='%d' width='14' height='14' rx='2' fill='#c0392b' "
               "stroke='white' stroke-width='0.5'/>\n", yl + 3);
    fprintf(f, "<text x='24' y='%d' font-size='9' font-weight='bold' fill='white'>S</text>\n", yl + 14);
    fprintf(f, "<text x='60' y='%d' font-size='12'>Escolhida por sorteio (desempate)</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 11. CPU ativa */
    fprintf(f, "<rect x='20' y='%d' width='18' height='18' fill='#1e781e'/>\n", yl + 1);
    fprintf(f, "<text x='60' y='%d' font-size='12'>CPU ativa no tick (ON)</text>\n", yl + 14);
    yl += PASSO_LEG;

    /* 12. CPU ociosa */
    fprintf(f, "<rect x='20' y='%d' width='18' height='18' fill='gray'/>\n", yl + 1);
    fprintf(f, "<text x='60' y='%d' font-size='12'>CPU ociosa/desligada no tick (OFF)</text>\n", yl + 14);
    yl += PASSO_LEG;

    yl += 8;
    /* resumo de ociosidade: uma linha por CPU, tambem vertical */
    yl += 8;
    fprintf(f, "<text x='20' y='%d' font-size='13' font-weight='bold'>"
               "Ociosidade por CPU:</text>\n", yl + 10);
    yl += 18;
    for (int i = 0; i < qtde_cpus; i++)
    {
        fprintf(f,
            "<rect x='20' y='%d' width='10' height='10' fill='gray'/>"
            "<text x='36' y='%d' font-size='12'>CPU %d: desligada por %d tick(s)</text>\n",
            yl + 2, yl + 11, i, CPUs[i].tempo_desligado);
        yl += 18;
    }

    /* fecha a tag raiz do SVG e o arquivo */
    fprintf(f, "</svg>");
    fclose(f);
}
