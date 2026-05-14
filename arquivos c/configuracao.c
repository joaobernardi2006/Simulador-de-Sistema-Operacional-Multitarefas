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
#include "configuracao.h"//importam as structs, funcoes 
#include "escalonador.h"
#include "simulacao.h"

/* -------------------------------------------------------------------------- */
void para_minusculo(char *str)//percorre a string e converte cada caractere para minusculo 
{
    for (int i = 0; str[i]; i++)
        str[i] = (char)tolower((unsigned char)str[i]);
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

    /* Solicita o nome do arquivo */
    char arquivo[256];
    printf("Digite o nome do arquivo de configuracao: ");
    scanf("%255s", arquivo);

    /* Remove extensão para usar como base no nome dos SVGs */
    strncpy(config.nome_arquivo, arquivo, sizeof(config.nome_arquivo) - 1);//copia o nome do arquivo para a struct de configuração, garantindo que não ultrapasse o tamanho do buffer
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
    }

    /* ---- Le as linhas de tarefas (linha 2 em diante) ---- */
    while (fgets(linha, sizeof(linha), arq))
    {
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

        char *tok = strtok(linha, ";");//divide a linha em tokens usando ";" como delimitador para extrair os campos da tarefa

        /* Campo: id */
        if (!tok || sscanf(tok, "%d", &tarefas[i].id_tarefa) != 1)//verifica se o token existe e se pode ser convertido para inteiro, que deve ser o ID da tarefa
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

        /* Campo: lista_eventos (reservado para o Projeto B) */
        tok = strtok(NULL, "\n");
        if (tok) strcpy(tarefas[i].lista_eventos, tok);

        i++;
    }

    *qtde_tarefas = i;//atualiza a quantidade de tarefas lidas, que é retornada por referência através do ponteiro qtde_tarefas
    *tarefas_ptr  = tarefas;//atualiza o ponteiro para o vetor de tarefas, que é retornado por referência através do ponteiro tarefas_ptr
    fclose(arq);
    return config;
}
