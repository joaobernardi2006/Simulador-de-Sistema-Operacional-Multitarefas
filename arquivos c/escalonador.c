/* ============================================================================
 * escalonador.c — Implementação dos algoritmos de escalonamento.
 *
 * Ambas as funções seguem a mesma assinatura (Strategy Pattern), permitindo
 * que o ponteiro ConfigSistema.escalonador aponte para qualquer uma delas
 * sem alterações no restante do simulador (requisito 4.2).
 *
 * Critérios de desempate comuns (requisito 4.3), aplicados em ordem:
 *   1. Tarefa já em execução (evita troca de contexto desnecessária).
 *   2. Menor instante de ingresso (chegou antes, sai na frente).
 *   3. Menor duração total.
 *   4. Sorteio aleatório (marca a célula do diagrama com sorteio = 1).
 *
 * Para PRIOP: antes dos critérios acima, aplica-se maior prioridade estática
 * (requisito 4.4).
 * ============================================================================ */

#include <stdlib.h>
#include "escalonador.h"
#include "fila.h"
#include "simulacao.h"   /* tick_valido() */

/* ============================================================================
 * SRTF — Shortest Remaining Time First
 *
 * Percorre a fila de prontas e seleciona a tarefa com menor `restante`.
 * Em cada comparação, o perdedor permanece como `menor_restante` e o
 * vencedor o substitui, seguindo os critérios de desempate se necessário.
 * ============================================================================ */
TCB *escalonador_SRTF(TCB *tarefa_atual, Queue *fila_prontas,
                      DiagramaGantt **diagrama, int tick)
{
    if (fila_vazia(fila_prontas))
        return tarefa_atual; /* Nada na fila: mantém a tarefa atual */

    TCB *melhor = tarefa_atual; /* Candidata com menor restante até agora */

    for (int i = fila_prontas->front; i != fila_prontas->back;//percorre a fila de prontas do início ao fim, verificando cada tarefa candidata
         i = (i + 1) % fila_prontas->size)//incrementa o índice circularmente, voltando ao início da fila quando chegar ao final
    {
        TCB *candidata = fila_prontas->array[i];//pega a tarefa candidata na posição atual da fila

        if (candidata->estado != PRONTA)//verifica se a tarefa candidata está no estado PRONTA Se não estiver, ela é ignorada e o loop continua para a próxima tarefa.
            continue;

        /* Inicializa com a primeira tarefa válida encontrada */
        if (melhor == NULL)// Se a variavel melhor ainda não tiver sido definida, a primeira válida é atribuída a ela
        {
            melhor = candidata;
            continue;
        }

        /* Candidata com restante MENOR vence claramente */
        if (candidata->restante < melhor->restante)// Se a tarefa candidata tiver um tempo restante menor do que a tarefa atualmente considerada a melhor, ela se torna a nova melhor escolha.
        {
            melhor = candidata;
        }
        /* Empate — aplica critérios 4.3 em ordem */
        else if (candidata->restante == melhor->restante)// Se a tarefa candidata tiver o mesmo tempo restante que a melhor, os critérios de desempate são aplicados em ordem para determinar qual tarefa deve ser escolhida.
        {
            /* Critério 1: prefere a tarefa já em execução */
            if (candidata == tarefa_atual)       { melhor = candidata; continue; }
            // Se a tarefa candidata for a mesma que a tarefa atualmente em execução, ela é escolhida como a melhor e o loop continua para a próxima tarefa.  
            if (melhor    == tarefa_atual)        { continue; }
            // Se a tarefa atualmente considerada a melhor for a mesma que a tarefa em execução, o loop continua para a próxima tarefa sem alterar a melhor escolha. 

            /* Critério 2: menor instante de ingresso */
            if (candidata->ingresso < melhor->ingresso)
            // Se a tarefa candidata tiver um instante de ingresso menor do que a melhor, ela se torna a nova melhor escolha.
                melhor = candidata;
            else if (candidata->ingresso > melhor->ingresso)
            // Se a tarefa candidata tiver um instante de ingresso maior do que a melhor, o loop continua para a próxima tarefa sem alterar a melhor escolha.
                continue;

            /* Critério 3: menor duração total */
            else if (candidata->duracao < melhor->duracao)
            // Se a tarefa candidata tiver uma duração total menor do que a melhor, ela se torna a nova melhor escolha.
                melhor = candidata;
            else if (candidata->duracao > melhor->duracao)
            // Se a tarefa candidata tiver uma duração total maior do que a melhor, o loop continua para a próxima tarefa sem alterar a melhor escolha.
                continue;

            /* Critério 4: sorteio — marca ambas as células no diagrama */
            else{
            // Se a tarefa candidata tiver o mesmo tempo restante, instante de ingresso e duração total que a melhor, um sorteio é realizado
                if (tick_valido(tick))
                {
                
                    diagrama[candidata->indice][tick].sorteio = 1;
                    //marca ambas as tarefas no diagrama como sorteadas 
                    diagrama[melhor->indice][tick].sorteio    = 1;
                    
                }
                if ((rand() % 2) == 0)
                // Um número aleatório é gerado usando rand() % 2, que retorna 0 ou 1. Se o resultado for 0, a tarefa candidata se torna a nova melhor escolha.
                    melhor = candidata;
            }
        }
    }

    return melhor;
}

/* ============================================================================
 * PRIOP — Prioridade Preemptiva
 *
 * Percorre a fila de prontas e seleciona a tarefa com maior `prioridade`
 * estática.  Em caso de empate de prioridade, aplica os critérios de 4.3.
 * ============================================================================ */
TCB *escalonador_PRIOP(TCB *tarefa_atual, Queue *fila_prontas,
                       DiagramaGantt **diagrama, int tick)
{
    TCB *melhor = tarefa_atual; /* Candidata com maior prioridade até agora */

    for (int i = fila_prontas->front; i != fila_prontas->back;//percorre a fila de prontas do início ao fim, verificando cada tarefa candidata
         i = (i + 1) % fila_prontas->size)//incrementa o índice circularmente, voltando ao início da fila quando chegar ao final
    {
        TCB *candidata = fila_prontas->array[i];//pega a tarefa candidata na posição atual da fila

        if (candidata->estado != PRONTA)//verifica se a tarefa candidata está no estado PRONTA Se não estiver, ela é ignorada e o loop continua para a próxima tarefa.
            continue;

        if (melhor == NULL)// Se a variavel melhor ainda não tiver sido definida, a primeira válida é atribuída a ela
        {
            melhor = candidata;
            continue;
        }

        /* Candidata com prioridade MAIOR vence claramente */
        if (candidata->prioridade > melhor->prioridade)// Se a tarefa candidata tiver uma prioridade maior do que a melhor, ela se torna a nova melhor escolha.
        {
            melhor = candidata;
        }
        /* Empate de prioridade — aplica critérios 4.3 */
        else if (candidata->prioridade == melhor->prioridade)
        // Se a tarefa candidata tiver a mesma prioridade que a melhor, os critérios de desempate são aplicados em ordem para determinar qual tarefa deve ser escolhida.
        {
            /* Critério 1: prefere a tarefa já em execução */
            if (candidata == tarefa_atual)       { melhor = candidata; continue; }
            if (melhor    == tarefa_atual)        { continue; }

            /* Critério 2: menor instante de ingresso */
            if (candidata->ingresso < melhor->ingresso)
                melhor = candidata;
            else if (candidata->ingresso > melhor->ingresso)
                continue;

            /* Critério 3: menor duração total */
            else if (candidata->duracao < melhor->duracao)
                melhor = candidata;
            else if (candidata->duracao > melhor->duracao)
                continue;

            /* Critério 4: sorteio */
            else
            {
                if (tick_valido(tick))// Se o tick for válido, as células correspondentes à tarefa candidata e à melhor no diagrama de Gantt são marcadas com sorteio = 1.
                {
                    diagrama[candidata->indice][tick].sorteio = 1;// Se o tick for válido, a célula correspondente à tarefa candidata no diagrama de Gantt é marcada com sorteio = 1.
                    diagrama[melhor->indice][tick].sorteio    = 1;// Se o tick for válido, a célula correspondente à tarefa melhor no diagrama de Gantt é marcada com sorteio = 1.
                }
                if ((rand() % 2) == 0)// Um número aleatório é gerado usando rand() % 2, que retorna 0 ou 1. Se o resultado for 0, a tarefa candidata se torna a nova melhor escolha.
                    melhor = candidata;
            }
        }
    }

    return melhor;
}
