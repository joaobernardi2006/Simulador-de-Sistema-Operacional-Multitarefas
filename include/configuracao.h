#ifndef CONFIGURACAO_H
#define CONFIGURACAO_H
#include "tipos.h"
void          para_minusculo(char *str);
int           cor_hex_valida(const char *cor);
int           id_ja_existe(TCB *tarefas, int qtde_tarefas, int id);
void          parsear_acoes(TCB *t, const char *txt);
void          liberar_tarefas(TCB *tarefas, int qtde);
ConfigSistema ler_configuracao(TCB **tarefas_ptr, int *qtde_tarefas);
#endif
