#ifndef VISUALIZACAO_H
#define VISUALIZACAO_H

/* ============================================================================
 * visualizacao.h — Interface do módulo de visualização do diagrama de Gantt.
 *
 * Contém as funções responsáveis por:
 *   • Renderizar o diagrama de Gantt no terminal usando cores ANSI (True Color).
 *   • Gerar o arquivo SVG do diagrama (requisito 2.4).
 *   • Abrir o SVG automaticamente no visualizador padrão do sistema.
 *
 * Convenções visuais (terminal e SVG):
 *   EXECUTANDO → bloco colorido com a cor da tarefa + número da CPU
 *   PRONTA     → célula vazia
 *   SUSPENSA   → bloco preto
 *   TERMINADA  → linha vertical marcando o fim
 *   Ingresso   → texto "nova" / letra "N" no tick de chegada
 *   Sorteio    → marcador "S" em vermelho sobre o bloco de execução
 *   CPU ociosa → bloco cinza "OFF" na seção inferior de CPUs
 * ============================================================================ */

#include "tipos.h"

/*
 * imprimir_gantt_terminal — renderiza o diagrama de Gantt no terminal.
 *
 * Limpa a tela antes de redesenhar (cls no Windows, clear no Linux/Mac).
 * Usa códigos ANSI True Color (ESC[48;2;R;G;Bm) para colorir os blocos
 * de execução com a cor real da tarefa (campo TCB.cor em hex RGB).
 *
 * A ordem das tarefas no eixo Y segue o requisito 2.5:
 *   menor ID → mais próximo ao eixo X (linha inferior).
 *
 * Exibe também a seção de CPUs com blocos "OFF"/"ON" por tick.
 */
void imprimir_gantt_terminal(DiagramaGantt **diagrama, TCB *tarefas,
                              int tick_max, int qtde_tarefas,
                              CPU *CPUs, int qtde_cpus);

/*
 * criar_svg — gera o arquivo SVG do diagrama de Gantt.
 *
 * nome_arquivo : nome base (sem extensão) lido do arquivo de configuração.
 * passo_a_passo: 1 → nome "tick_N de <arquivo>.svg"
 *                0 → "execucao completa de <arquivo>.svg"
 * modificado   : 1 → acrescenta " modificado" ao nome (edição manual).
 *
 * O arquivo é salvo no diretório de trabalho atual.  Após salvar, chama
 * `abrir_svg` para abrir no visualizador padrão do sistema (requisito 2.4).
 */
void criar_svg(DiagramaGantt **diagrama, TCB *tarefas, int tick_max,
               int qtde_tarefas, const char *nome_arquivo,
               int passo_a_passo, int modificado,
               CPU *CPUs, int qtde_cpus);

/*
 * abrir_svg — abre o arquivo SVG usando o visualizador padrão do SO.
 *
 * Windows : start ""  "<arquivo>"
 * macOS   : open      "<arquivo>"
 * Linux   : xdg-open  "<arquivo>"
 *
 * Falhas na abertura são reportadas como aviso; a simulação continua.
 */
void abrir_svg(const char *nome_svg);

#endif /* VISUALIZACAO_H */
