#ifndef VISUALIZACAO_H
#define VISUALIZACAO_H
#include "tipos.h"
void abrir_svg(const char *nome_svg);
void imprimir_gantt_terminal(DiagramaGantt **diagrama, TCB *tarefas,
                             int tick_max, int qtde_tarefas,
                             CPU *CPUs, int qtde_cpus);
void criar_svg(DiagramaGantt **diagrama, TCB *tarefas, int tick_max,
               int qtde_tarefas, const char *nome_arquivo,
               int passo_a_passo, int modificado,
               CPU *CPUs, int qtde_cpus);
#endif
