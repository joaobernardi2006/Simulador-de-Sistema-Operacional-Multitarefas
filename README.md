# Simulador de SO multitarefa — Projeto A + Projeto B (mutexes)

## Como compilar e executar
    make            # ou: make rebuild
    ./simulador
Informe o nome do arquivo de configuracao e escolha o modo:
- `a` passo-a-passo: `A` avancar, `R` retroceder, `M` modificar tarefa, `S` sair
- `b` execucao completa

## Formato do arquivo
Linha 1: `algoritmo;quantum;qtde_cpus[;alpha]`  (algoritmos: SRTF, PRIOP, PRIOPEnv)
Demais linhas (1 por tarefa):
    id;cor;ingresso;duracao;prioridade;<acoes de mutex>
- `id` aceita numerico (`1`) ou com prefixo (`t01`).
- prioridade: numero MAIOR = prioridade MAIOR.

### Acoes de E/S (req. 3)
- `IO:xx-yy` -> operacao de E/S no instante `xx` (relativo ao inicio), com duracao `yy`.
- A tarefa fica SUSPENSA por `yy` ticks; ao terminar, um IRQ no instante seguinte a acorda.
- Duracao minima 1. Exemplo: `1;FF0000;0;8;5;IO:3-4`

### Acoes de mutex (Projeto B), apos a prioridade, separadas por `;`
- `MLxx:nn` -> solicitar (lock) o mutex `xx` no instante `nn`
- `MUxx:nn` -> liberar  (unlock) o mutex `xx` no instante `nn`
- `nn` e relativo ao inicio da tarefa (conta ticks executados; instante 0 = inicio).
- Varias acoes no mesmo instante sao executadas na ordem do arquivo.
Exemplo:  `1;FF0000;0;8;9;ML00:0;MU00:5`

## Representacao grafica (SVG e terminal)
- Solicitar mutex: marcador laranja `L<n>` | Liberar: marcador verde `U<n>`.
- Suspensa por mutex: preenchimento quadriculado roxo (terminal: `SM`).
- Suspensa por E/S:   preenchimento diagonal azul   (terminal: `SI`; inicio = `IO`).
- Inicio de E/S: etiqueta azul `IO` na celula (req. 5.1).

## Observacoes
- Os headers em `include/` foram reconstruidos/estendidos para o Projeto B;
  se voce ja tinha os seus, compare-os com estes (apenas adicoes de campos).
- Se ocorrer deadlock, a simulacao encerra ao atingir o limite de ticks e
  reporta as tarefas que ficaram suspensas.
