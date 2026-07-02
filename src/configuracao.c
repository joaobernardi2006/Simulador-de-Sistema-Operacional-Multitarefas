/* ============================================================================
 * configuracao.c — Leitura e validacao do arquivo de configuração.
 *
 * Formato do arquivo (requisito 3.3):
 *   Linha 1: algoritmo;quantum;qtde_cpus
 *   Linhas seguintes: id;cor;ingresso;duracao;prioridade;lista_eventos
 *
 * O vetor de tarefas cresce dinamicamente via realloc, sem limite fixo
 * de quantidade (requisito 3.3.1).
 * ============================================================================ */
//Bibliotecas
#include <stdio.h>//entrada/saida (printf, scanf, FILE, fopen, fgets, fclose
#include <stdlib.h>//alocacao de memoria (malloc, realloc, free)
#include <string.h> //manipulacao de strings (strtok, strcmp, strncpy, strlen)
#include <ctype.h>//funcoes de caractere (tolower, isxdigit)
#include "../include/configuracao.h"//importam as structs, funcoes 
#include "../include/escalonador.h"
#include "../include/simulacao.h"

/* -------------------------------------------------------------------------- */
void para_minusculo(char *str)//percorre a string e converte cada caractere para minusculo 
{
    for (int i = 0; str[i]; i++)
        str[i] = (char)tolower((unsigned char)str[i]);
}

/* --------------------------------------------------------------------------
 * ler_linha — le uma linha de comprimento ARBITRARIO de `f` (cresce o buffer
 * conforme necessario), de modo que nao ha limite para o tamanho da linha nem,
 * por consequencia, para o numero de eventos de uma tarefa (req. 3.3.3).
 * Retorna uma string alocada (o chamador deve liberar com free) ou NULL no fim
 * do arquivo. O '\n' final nao e incluido.
 * -------------------------------------------------------------------------- */
static char *ler_linha(FILE *f)
{
    size_t cap = 128, len = 0;
    char  *buf = malloc(cap);
    if (!buf) { perror("ler_linha"); exit(1); }
    int c;
    while ((c = fgetc(f)) != EOF && c != '\n')
    {
        if (len + 1 >= cap)
        {
            cap *= 2;
            char *p = realloc(buf, cap);
            if (!p) { perror("ler_linha realloc"); free(buf); exit(1); }
            buf = p;
        }
        buf[len++] = (char)c;
    }
    if (c == EOF && len == 0) { free(buf); return NULL; } /* fim do arquivo */
    buf[len] = '\0';
    return buf;
}

/* --------------------------------------------------------------------------
 * acoes_reservar — garante que t->acoes comporta ao menos `n` elementos,
 * crescendo o vetor dinamicamente (dobrando a capacidade). Assim o numero de
 * eventos por tarefa nao tem limite fixo (req. 3.3.3).
 * -------------------------------------------------------------------------- */
static void acoes_reservar(TCB *t, int n)
{
    if (n <= t->cap_acoes) return;
    int nova = (t->cap_acoes == 0) ? 8 : t->cap_acoes * 2;
    if (nova < n) nova = n;
    AcaoMutex *p = realloc(t->acoes, (size_t)nova * sizeof(AcaoMutex));
    if (!p) { perror("acoes_reservar"); exit(1); }
    t->acoes     = p;
    t->cap_acoes = nova;
}

/* --------------------------------------------------------------------------
 * liberar_tarefas — libera o vetor de tarefas e, para cada tarefa, o vetor
 * dinamico de acoes. Usar sempre que for liberar um vetor de TCB.
 * -------------------------------------------------------------------------- */
void liberar_tarefas(TCB *tarefas, int qtde)
{
    if (!tarefas) return;
    for (int i = 0; i < qtde; i++)
        free(tarefas[i].acoes);
    free(tarefas);
}

/* -------------------------------------------------------------------------- */
int cor_hex_valida(const char *cor)//valida que a string eh hexadecimal de 6 digitos
{
    if (cor == NULL || strlen(cor) != 6) return 0;//nao
    for (int i = 0; i < 6; i++)
        if (!isxdigit((unsigned char)cor[i])) return 0;
    return 1;//valida
}

/* -------------------------------------------------------------------------- */
/*percorre as tarefas ja lidas e verifica se alguma ja possui o ID informado.
 retorna 1 se encontrou duplicata, 0 caso contrário.
 duas tarefas poderiam ter o mesmo ID, causando ambiguidade na hora 
 de modificar ou identificar tarefas na simulação.*/
int id_ja_existe(TCB *tarefas, int qtde_tarefas, int id)
{
    for (int i = 0; i < qtde_tarefas; i++)
        if (tarefas[i].id_tarefa == id) return 1;
    return 0;
}

/* --------------------------------------------------------------------------
 * parsear_acoes — interpreta as acoes de mutex de uma tarefa (Projeto B).
 *
 * Recebe o texto cru que vem APOS a prioridade na linha do arquivo, no
 * formato "MLxx:nn;MUyy:mm;..." (separadas por ';'), e preenche t->acoes.
 *   MLxx:nn -> solicitar (lock)  do mutex xx no instante nn
 *   MUxx:nn -> liberar  (unlock) do mutex xx no instante nn
 * Os instantes sao relativos ao inicio da tarefa (req. 2.4). A ordem de
 * declaracao e guardada em `ordem` para o desempate do req. 2.5.
 * -------------------------------------------------------------------------- */
void parsear_acoes(TCB *t, const char *txt)
{
    t->acoes      = NULL;
    t->cap_acoes  = 0;
    t->qtde_acoes = 0;
    if (txt == NULL) return;

    /* copia dinamica do texto (sem limite de tamanho) para uso com strtok */
    size_t len = strlen(txt);
    char  *buf = malloc(len + 1);
    if (!buf) { perror("parsear_acoes"); exit(1); }
    memcpy(buf, txt, len + 1);

    int   ordem = 0;
    char *tok   = strtok(buf, ";");
    while (tok != NULL)
    {
        /* Um mesmo token pode conter VARIOS eventos concatenados sem ';'
         * (ex.: "IO:01-02MU01:03" no caso de teste mc-010). Por isso, em vez
         * de um unico sscanf por token, percorremos o token com um cursor:
         * a cada posicao tentamos casar ML/MU/IO; %n informa quantos
         * caracteres foram consumidos, e o cursor avanca para o proximo
         * evento. Caracteres nao reconhecidos (espacos, lixo) sao pulados. */
        const char *p = tok;
        while (*p != '\0')
        {
            int mut = -1, inst = -1, dur = 0, n = 0, tipo = ACAO_NENHUMA;

            if ((p[0] == 'M' || p[0] == 'm') && (p[1] == 'L' || p[1] == 'l') &&
                sscanf(p + 2, "%d:%d%n", &mut, &inst, &n) == 2)
            {
                tipo = ACAO_ML;
                p += 2 + n; /* avanca alem de "MLxx:nn" */
            }
            else if ((p[0] == 'M' || p[0] == 'm') && (p[1] == 'U' || p[1] == 'u') &&
                     sscanf(p + 2, "%d:%d%n", &mut, &inst, &n) == 2)
            {
                tipo = ACAO_MU;
                p += 2 + n; /* avanca alem de "MUxx:nn" */
            }
            else if ((p[0] == 'I' || p[0] == 'i') && (p[1] == 'O' || p[1] == 'o') &&
                     p[2] == ':' &&
                     sscanf(p + 3, "%d-%d%n", &inst, &dur, &n) == 2)
            {
                tipo = ACAO_IO;
                p += 3 + n; /* avanca alem de "IO:xx-yy" */
            }
            else
            {
                p++; /* caractere nao reconhecido: pula e tenta a partir do proximo */
                continue;
            }

            if (tipo == ACAO_IO && inst >= 0)
            {
                if (dur < 1) dur = 1; /* duracao minima de uma unidade (req. 3.4) */
                acoes_reservar(t, t->qtde_acoes + 1);
                AcaoMutex *a = &t->acoes[t->qtde_acoes++];
                a->tipo = ACAO_IO; a->mutex = -1; a->instante = inst;
                a->duracao = dur;  a->ordem = ordem++; a->executada = 0;
            }
            else if ((tipo == ACAO_ML || tipo == ACAO_MU) &&
                     mut >= 0 && inst >= 0)
            {
                acoes_reservar(t, t->qtde_acoes + 1);
                AcaoMutex *a = &t->acoes[t->qtde_acoes++];
                a->tipo = tipo; a->mutex = mut; a->instante = inst;
                a->duracao = 0; a->ordem = ordem++; a->executada = 0;
            }
        }
        tok = strtok(NULL, ";");
    }
    free(buf);
}

/* -------------------------------------------------------------------------- */

ConfigSistema ler_configuracao(TCB **tarefas_ptr, int *qtde_tarefas)
{
    ConfigSistema config;//informacoes do arquivo vao ser armazenadas nessa struct, que eh retornada no final da funcao
    inicializar_config_padrao(&config);//preenche conf com os valores padrao 

    ConfigSistema erro = {0}; /* retornado em caso de falha */
    char linha[256];//buffer para leitura de linhas do arquivo
    int  capacidade = 16; //espaco para 16 tarefas inicialmente, cresce dinamicamente se necessario
    int  i          = 0;

    TCB *tarefas = malloc((size_t)capacidade * sizeof(TCB));//aloca o vetor de tarefas dinamicamente
    if (!tarefas)
    {
        printf("Erro de alocacao de memoria.\n");
        return erro;
    }

    /* Exibe valores padrao antes de solicitar o arquivo (requisito 3.2) */
    printf("Valores padrao do sistema:\n");
    printf("  Algoritmo : SRTF\n");
    printf("  Quantum   : %d tick(s)\n", QUANTUM);
    printf("  CPUs      : %d\n", N_CPUs);
    printf("(Esses valores sao sobrescritos pelo arquivo de configuracao)\n\n");

    /* Solicita o nome do arquivo. Le a LINHA inteira (fgets) para aceitar
     * CAMINHOS COM ESPACOS, permitindo apontar direto para um arquivo em um
     * pendrive sem copiar para a pasta do projeto, p.ex.:
     *   /media/usuario/MEU PEN/caso.txt   (Linux)
     *   E:\Meus Arquivos\caso.txt         (Windows)
     * Tambem remove aspas em volta (caso o caminho seja colado/arrastado). */
    char arquivo[1024];
    printf("Digite o nome ou caminho completo do arquivo de configuracao: ");
    if (!fgets(arquivo, sizeof(arquivo), stdin))
    {
        printf("Erro ao ler o nome do arquivo.\n");
        free(tarefas);
        return erro;
    }
    arquivo[strcspn(arquivo, "\r\n")] = '\0';            /* remove o '\n' final */
    {
        char  *ini = arquivo;
        while (*ini == ' ' || *ini == '\t') ini++;       /* espacos a esquerda  */
        size_t L = strlen(ini);
        while (L > 0 && (ini[L-1] == ' ' || ini[L-1] == '\t')) ini[--L] = '\0';
        if (L >= 2 && ((ini[0] == '"'  && ini[L-1] == '"') ||
                       (ini[0] == '\'' && ini[L-1] == '\'')))   /* tira aspas    */
        {
            ini[L-1] = '\0';
            ini++;
        }
        if (ini != arquivo) memmove(arquivo, ini, strlen(ini) + 1);
    }

    /* Base para o nome dos SVGs: usa apenas o NOME do arquivo (sem o diretorio),
     * para que a imagem seja gerada na pasta atual e nao no pendrive/origem. */
    const char *base = arquivo;
    for (const char *p = arquivo; *p; p++)
        if (*p == '/' || *p == '\\') base = p + 1;   /* depois da ultima barra */
    strncpy(config.nome_arquivo, base, sizeof(config.nome_arquivo) - 1);
    config.nome_arquivo[sizeof(config.nome_arquivo) - 1] = '\0';
    char *ponto = strrchr(config.nome_arquivo, '.');//procura o ultimo ponto na string
    if (ponto) *ponto = '\0';

    FILE *arq = fopen(arquivo, "r");//abre o arquivo para leitura
    if (!arq)//verifica se o arquivo não foi aberto 
    {
        printf("Erro ao abrir o arquivo de configuracao.\n");
        free(tarefas);
        return erro;
    }

    /* ---- Le a linha de configuração do sistema (linha 1) ---- */
    if (!fgets(linha, sizeof(linha), arq))//tenta ler a primeira linha; se retornar NULL, o arquivo esta vazio ou houve erro
    {
        printf("Erro: arquivo vazio.\n");
        fclose(arq);
        free(tarefas);
        return erro;
    }

    {
        char *tok = strtok(linha, ";");//divide a linha em tokens usando ";" como delimitador

        /* Campo 1: algoritmo de escalonamento */
        if (tok != NULL)//verifica se o token existe
        {
            para_minusculo(tok);//converte o token para minusculo para facilitar a comparação
            if (strcmp(tok, "srtf") == 0)//compara o token com "srtf" e "priop" para determinar qual algoritmo de escalonamento usar
            {
                config.algoritmo_escalonamento = SRTF;
                config.escalonador             = escalonador_SRTF;
            }
            else if (strcmp(tok, "priop") == 0)
            {
                config.algoritmo_escalonamento = PRIOP;
                config.escalonador             = escalonador_PRIOP;
            }
            else if (strcmp(tok, "priopenv") == 0)
            {
                config.algoritmo_escalonamento = PRIOPENV;
                config.escalonador             = escalonador_PRIOPENV;
            }
            else
            {
                printf("Algoritmo nao reconhecido: \"%s\".\n", tok);
                fclose(arq);
                free(tarefas);
                return erro;
            }
        }

        /* Campo 2: quantum */
        tok = strtok(NULL, ";");//pega o proximo token, que deve ser o quantum
        if (tok) sscanf(tok, "%d", &config.quantum);//se o token existe, converte para inteiro; se NULL, mantem o valor padrao
        if (config.quantum <= 0)//verifica se o quantum é valido (deve ser maior ou igual a 1)
        {
            printf("Quantum invalido (deve ser >= 1).\n");
            fclose(arq);
            free(tarefas);
            return erro;
        }

        /* Campo 3: quantidade de CPUs */
        tok = strtok(NULL, ";");//pega o proximo token, que deve ser a quantidade de CPUs
        if (tok) sscanf(tok, "%d", &config.qtde_cpus);//converte o token para inteiro e armazena na configuracao
        if (config.qtde_cpus < 2)//verifica se a quantidade de CPUs eh valida (deve ser maior ou igual a 2)
        {
            printf("Quantidade de CPUs invalida (minimo 2).\n");
            fclose(arq);
            free(tarefas);
            return erro;
        }

        /* Campo 4 (opcional): alpha — fator de envelhecimento para PRIOPEnv.
         * Formato da linha 1: PRIOPEnv;quantum;qtde_cpus;alpha             */
        tok = strtok(NULL, ";\n");//pega o proximo token, que pode ser o alpha
        if (tok)
        {
            int alpha_lido = 0;
            if (sscanf(tok, "%d", &alpha_lido) == 1 && alpha_lido > 0)
                config.alpha = alpha_lido;
            else if (config.algoritmo_escalonamento == PRIOPENV)
            {
                printf("Alpha invalido para PRIOPEnv (deve ser inteiro > 0).\n");
                fclose(arq);
                free(tarefas);
                return erro;
            }
        }
        else if (config.algoritmo_escalonamento == PRIOPENV)
        {
            printf("Aviso: alpha nao informado para PRIOPEnv; usando alpha = %d.\n",
                   config.alpha);
        }
    }

    /* ---- Le as linhas de tarefas (linha 2 em diante) ---- */
    char *linha_evt;
    while ((linha_evt = ler_linha(arq)) != NULL)
    {
        /* ignora linhas em branco (sem campos) */
        if (linha_evt[0] == '\0') { free(linha_evt); continue; }

        /* Expande o vetor dinamicamente se necessário */
        if (i >= capacidade)
        {
            capacidade *= 2;//dobra a capacidade do vetor quando o limite for atingido
            TCB *temp = realloc(tarefas, (size_t)capacidade * sizeof(TCB));//realoca o vetor de tarefas com a nova capacidade
            if (!temp)//verifica se a realocação foi bem-sucedida
            {
                printf("Erro de alocacao ao expandir tarefas.\n");
                fclose(arq);
                free(tarefas);
                return erro;//em caso de erro, fecha o arquivo, libera a memória alocada para as tarefas e retorna a configuração de erro (qtde_cpus == 0)
            }
            tarefas = temp;//atualiza o ponteiro para o vetor de tarefas com o novo endereço retornado pelo realloc
        }

        inicializar_tcb_padrao(&tarefas[i], config.quantum);//inicializa a tarefa com valores padrão, usando o quantum definido na configuração
        tarefas[i].indice = i;//define o índice da tarefa no vetor, que pode ser usado para referência durante a simulação
        tarefas[i].acoes      = NULL;  /* vetor de acoes ainda nao alocado */
        tarefas[i].cap_acoes  = 0;
        tarefas[i].qtde_acoes = 0;

        char *tok = strtok(linha_evt, ";");//divide a linha em tokens usando ";" como delimitador para extrair os campos da tarefa

        /* Campo: id
         * Aceita tanto IDs puramente numericos ("12") quanto IDs com prefixo
         * alfabetico ("t12", "task5"): pula caracteres nao-digitos iniciais e
         * converte a parte numerica. Isso permite ler os casos de teste no
         * formato "tNN" sem deixar de aceitar IDs numericos. */
        const char *p_id = tok;
        if (p_id)
            while (*p_id && !isdigit((unsigned char)*p_id))
                p_id++;
        if (!tok || p_id == NULL || *p_id == '\0' ||
            sscanf(p_id, "%d", &tarefas[i].id_tarefa) != 1)//verifica se o token existe e se contem um inteiro
        {
            printf("ID invalido na linha %d.\n", i + 2);//em caso de erro, exibe a linha onde ocorreu o problema 
            fclose(arq);
            free(tarefas);
            return erro;
        }
        if (id_ja_existe(tarefas, i, tarefas[i].id_tarefa))//verifica se o ID da tarefa já existe no vetor de tarefas lidas até o momento, para evitar duplicatas
        {
            printf("ID repetido: %d\n", tarefas[i].id_tarefa);
            fclose(arq);
            free(tarefas);
            return erro;
        }

        /* Campo: cor (hex 6 dígitos) */
        tok = strtok(NULL, ";");
        if (!tok || !cor_hex_valida(tok))//verifica se o token existe e se é uma cor hexadecimal válida de 6 dígitos
        {
            printf("Cor invalida para tarefa %d. Use 6 digitos hex.\n",
                   tarefas[i].id_tarefa);
            fclose(arq);
            free(tarefas);
            return erro;
        }
        strncpy(tarefas[i].cor, tok, sizeof(tarefas[i].cor) - 1);//copia a cor para a tarefa, garantindo que não ultrapasse o tamanho do buffer
        tarefas[i].cor[sizeof(tarefas[i].cor) - 1] = '\0';//garante que a string de cor seja terminada com null

        /* Campo: ingresso */
        tok = strtok(NULL, ";");//pega o próximo token, que deve ser o ingresso da tarefa
        if (tok) sscanf(tok, "%d", &tarefas[i].ingresso);//converte o token para inteiro e armazena no campo de ingresso da tarefa
        if (tarefas[i].ingresso < 0)//verifica se o ingresso é válido (deve ser maior ou igual a 0)
        {
            printf("Ingresso invalido para tarefa %d.\n", tarefas[i].id_tarefa);
            fclose(arq);
            free(tarefas);
            return erro;
        }

        /* Campo: duração */
        tok = strtok(NULL, ";");//pega o próximo token, que deve ser a duração da tarefa
        if (tok)//verifica se o token não é nulo antes de tentar convertê-lo para inteiro e armazená-lo no campo de duração da tarefa
        {
            sscanf(tok, "%d", &tarefas[i].duracao);//converte o token para inteiro e armazena no campo de duração da tarefa
            tarefas[i].restante = tarefas[i].duracao;//inicializa o campo de tempo restante da tarefa com o valor da duração, para ser usado durante a simulação
        }//restante começa igual à duração; será decrementado a cada tick durante a simulação
        if (tarefas[i].duracao <= 0)
        {
            printf("Duracao invalida para tarefa %d.\n", tarefas[i].id_tarefa);
            fclose(arq);
            free(tarefas);
            return erro;
        }

        /* Campo: prioridade */
        tok = strtok(NULL, ";");//pega o próximo token, que deve ser a prioridade da tarefa
        if (tok) sscanf(tok, "%d", &tarefas[i].prioridade);//converte o token para inteiro e armazena no campo de prioridade da tarefa
        if (tarefas[i].prioridade < 0)//verifica se a prioridade é válida (deve ser maior ou igual a 0)
        {
            printf("Prioridade invalida para tarefa %d.\n", tarefas[i].id_tarefa);
            fclose(arq);
            free(tarefas);
            return erro;
        }

        /* Campo: lista_eventos / acoes (Projeto B). Usa o token COMPLETO (sem
         * truncar): parsear_acoes aloca o vetor de acoes dinamicamente, entao
         * nao ha limite para o numero de eventos por tarefa (req. 3.3.3).
         * `lista_eventos` guarda apenas uma copia (possivelmente truncada) para
         * fins de exibicao/compatibilidade. */
        tok = strtok(NULL, "\n");
        if (tok)
        {
            strncpy(tarefas[i].lista_eventos, tok,
                    sizeof(tarefas[i].lista_eventos) - 1);
            tarefas[i].lista_eventos[sizeof(tarefas[i].lista_eventos) - 1] = '\0';
            parsear_acoes(&tarefas[i], tok);//interpreta o texto completo de eventos
        }
        else
        {
            tarefas[i].lista_eventos[0] = '\0';
            tarefas[i].acoes      = NULL;
            tarefas[i].cap_acoes  = 0;
            tarefas[i].qtde_acoes = 0;//sem acoes nesta tarefa
        }

        i++;
        free(linha_evt);//libera a linha lida dinamicamente nesta iteracao
    }

    *qtde_tarefas = i;//atualiza a quantidade de tarefas lidas, que é retornada por referência através do ponteiro qtde_tarefas
    *tarefas_ptr  = tarefas;//atualiza o ponteiro para o vetor de tarefas, que é retornado por referência através do ponteiro tarefas_ptr
    fclose(arq);

    /* ---- Dimensiona os limites dinamicos a partir da carga (sem teto fixo) ----
     * qtde_mutexes : maior numero de mutex usado + 1 (0 se nao houver mutex);
     * limite_ticks : cota superior segura para o numero de ticks da simulacao =
     *   maior ingresso + soma de todas as duracoes + soma das duracoes de E/S +
     *   folga. Garante espaco para qualquer escalonamento (e serve de backstop
     *   para deadlock, no lugar do antigo MAX_TICKS). */
    {
        int  max_mutex = -1, max_ingresso = 0;
        long soma = 0;
        for (int k = 0; k < i; k++)
        {
            if (tarefas[k].ingresso > max_ingresso) max_ingresso = tarefas[k].ingresso;
            soma += tarefas[k].duracao;
            for (int a = 0; a < tarefas[k].qtde_acoes; a++)
            {
                if ((tarefas[k].acoes[a].tipo == ACAO_ML ||
                     tarefas[k].acoes[a].tipo == ACAO_MU) &&
                    tarefas[k].acoes[a].mutex > max_mutex)
                    max_mutex = tarefas[k].acoes[a].mutex;
                if (tarefas[k].acoes[a].tipo == ACAO_IO)
                    soma += tarefas[k].acoes[a].duracao; /* tempo suspensa em E/S */
            }
        }
        config.qtde_mutexes = max_mutex + 1;                 /* 0 se nao ha mutex */
        config.limite_ticks = max_ingresso + (int)soma + i + 16; /* folga */
        if (config.limite_ticks < 64) config.limite_ticks = 64;
    }

    return config;
}
