#ifndef CONFIGURACAO_H
#define CONFIGURACAO_H

/* ============================================================================
 * configuracao.h — Interface do módulo de leitura e validação do arquivo de
 * configuração do simulador.
 *
 * O arquivo de configuração segue o formato definido no requisito 3.3:
 *
 *   Linha 1: algoritmo;quantum;qtde_cpus
 *   Linha 2+: id;cor;ingresso;duracao;prioridade;lista_eventos
 *
 * Funções de validação e normalização de strings são exportadas para que
 * outros módulos possam reutilizá-las (ex.: conversão para minúsculo,
 * validação de cor hex).
 * ============================================================================ */

#include "tipos.h"

/* Converte todos os caracteres de `str` para minúsculo in-place.
 * Permite que o arquivo aceite "SRTF", "srtf" ou "SrTf" igualmente
 * (requisito 3.3.2). */
void para_minusculo(char *str);

/* Retorna 1 se `cor` é uma string de exatamente 6 dígitos hexadecimais;
 * 0 caso contrário. */
int cor_hex_valida(const char *cor);

/* Retorna 1 se já existe uma tarefa com `id` no vetor `tarefas[0..n-1]`. */
int id_ja_existe(TCB *tarefas, int qtde_tarefas, int id);

/*
 * ler_configuracao — ponto de entrada principal deste módulo.
 *
 * Solicita ao usuário o nome do arquivo, lê e valida todas as linhas e
 * preenche `*tarefas_ptr` (alocado dinamicamente via realloc) e
 * `*qtde_tarefas`.
 *
 * Retorna a ConfigSistema preenchida em caso de sucesso, ou uma struct
 * zerada (qtde_cpus == 0) em caso de erro.
 *
 * Exibe os valores padrão antes da leitura (requisito 3.2).
 */
ConfigSistema ler_configuracao(TCB **tarefas_ptr, int *qtde_tarefas);

#endif /* CONFIGURACAO_H */
