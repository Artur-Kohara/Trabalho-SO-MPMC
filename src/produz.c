#include "common.h"
#include "shm_utils.h"
#include "msg_utils.h"

/* Uso: produz <paradigma> <p_prod> <n_msgs> <ack> <ack_id> <fila_ack> <estruturas>
 *   estruturas : identificadores IPC das p estruturas, separados por vírgula:
 *                "shmid:semid" no paradigma 1, "msgid" no paradigma 2
 *   ack_id     : paradigma 1 - semáforo de confirmação deste produtor (-1 se ack=2)
 *                paradigma 2 - número deste produtor (mtype das confirmações)
 *   fila_ack   : paradigma 2 - fila de confirmações (-1 no paradigma 1) */

static int ids_a[MAX_PROD], ids_b[MAX_PROD];
static void *enderecos[MAX_PROD];

int main(int argc, char *argv[]) {
    int paradigma, p_total, p_prod, n_msgs, ack, ack_id, fila_ack;
    struct timespec tempo_inicio;
    pid_t meu_pid = getpid();
    unsigned long total_enviadas = 0;
    int falhou = 0;   /* alguma operação IPC falhou: o processo termina com código 1 */

    if (argc < 8) {
        fprintf(stderr, "Uso: %s <paradigma> <p_prod> <n_msgs> <ack> <ack_id> <fila_ack> <estruturas>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    paradigma = atoi(argv[1]);
    p_prod = atoi(argv[2]);
    n_msgs = atoi(argv[3]);
    ack = atoi(argv[4]);
    ack_id = atoi(argv[5]);
    fila_ack = atoi(argv[6]);
    p_total = ler_ids(argv[7], ids_a, ids_b, MAX_PROD);
    if (p_total < 1 || p_prod < 0 || p_prod > p_total) {
        fprintf(stderr, "produz: lista de estruturas inválida\n");
        exit(EXIT_FAILURE);
    }

    setvbuf(stdout, NULL, _IOLBF, 0);
    instalar_encerramento_gracioso();
    agora(&tempo_inicio);

    printf("[PRODUZ PID %d] Iniciado - estrutura=%d, n_msgs=%d, ack=%d, paradigma=%d\n",
           meu_pid, p_prod, n_msgs, ack, paradigma);

    if (paradigma == PARADIGMA_MSG) {
        if (p_prod == 0) {
            printf("[PRODUZ PID %d] Distribuição striped em %d filas de mensagens\n", meu_pid, p_total);
        } else {
            printf("[PRODUZ PID %d] Enviando %d mensagens para fila %d\n", meu_pid, n_msgs, p_prod);
        }

        for (int i = 0; i < n_msgs && !terminar_processo; i++) {
            /* Distribuição striped: (1, 2, ..., p, 1, 2, ..., p) */
            int fila_destino = (p_prod == 0) ? (i % p_total) + 1 : p_prod;
            mensagem_t msg;
            msg.mtype = 1;
            msg.pid_produtor = meu_pid;
            msg.seq = i + 1;
            msg.ack = ack;
            msg.ack_id = ack_id;

            /* O tempo de envio é registrado imediatamente antes de cada tentativa;
             * com a fila cheia, o msgsnd espera por espaço */
            int enviada;
            do {
                gettimeofday(&msg.tempo_envio, NULL);
                enviada = msgsnd(ids_a[fila_destino - 1], &msg, sizeof(mensagem_t) - sizeof(long), 0);
            } while (enviada == -1 && errno == EINTR && !terminar_processo);
            if (enviada == -1) {
                if (!terminar_processo) {
                    perror("msgsnd");
                    falhou = 1;
                }
                break;
            }
            printf("[PRODUZ PID %d] Mensagem %lu enviada para fila %d | pid_prod=%d | seq=%lu\n",
                   meu_pid, msg.seq, fila_destino, msg.pid_produtor, msg.seq);
            total_enviadas++;
        }

        if (p_prod == 0) {
            printf("[PRODUZ PID %d] Total de %lu mensagens enviadas com distribuição striped\n",
                   meu_pid, total_enviadas);
        } else {
            printf("[PRODUZ PID %d] Total de %lu mensagens enviadas para fila %d\n",
                   meu_pid, total_enviadas, p_prod);
        }

        if (ack == ACK_WAIT && !terminar_processo) {
            printf("[PRODUZ PID %d] ack=1: aguardando confirmação de que TODAS as %lu mensagens foram consumidas...\n",
                   meu_pid, total_enviadas);
            /* Os consumidores mantêm na fila de confirmações um contador por produtor
             * (mtype = número do produtor): cada mensagem consumida incrementa o
             * contador que estiver na fila ou cria um novo. O produtor soma os
             * contadores que recebe até atingir o total enviado. */
            unsigned long confirmadas = 0;
            while (confirmadas < total_enviadas) {
                contador_msg_t c;
                if (msgrcv(fila_ack, &c, sizeof(c) - sizeof(long), ack_id, 0) == -1) {
                    if (errno == EINTR && !terminar_processo) continue;
                    if (!terminar_processo) perror("msgrcv confirmação");
                    break;
                }
                confirmadas += c.quantidade;
                printf("[PRODUZ PID %d] Confirmação recebida: +%d mensagens consumidas (total: %lu/%lu)\n",
                       meu_pid, c.quantidade, confirmadas, total_enviadas);
            }
            if (confirmadas == total_enviadas) {
                printf("[PRODUZ PID %d] TODAS as mensagens foram consumidas. Confirmação completa!\n", meu_pid);
            } else if (!terminar_processo) {
                printf("[PRODUZ PID %d] Erro: apenas %lu de %lu mensagens confirmadas\n",
                       meu_pid, confirmadas, total_enviadas);
                falhou = 1;
            }
        } else if (ack != ACK_WAIT) {
            printf("[PRODUZ PID %d] ack=2: não aguardando confirmação\n", meu_pid);
        }

    } else if (paradigma == PARADIGMA_SHM) {
        if (p_prod == 0) {
            printf("[PRODUZ PID %d] Distribuição striped em %d memórias compartilhadas\n", meu_pid, p_total);
        } else {
            printf("[PRODUZ PID %d] Enviando %d mensagens para memória compartilhada %d\n",
                   meu_pid, n_msgs, p_prod);
        }

        for (int i = 0; i < n_msgs && !terminar_processo; i++) {
            /* Distribuição striped: (1, 2, ..., p, 1, 2, ..., p) */
            int shm_destino = (p_prod == 0) ? (i % p_total) + 1 : p_prod;
            int e = shm_destino - 1;
            int semid = ids_b[e];

            if (enderecos[e] == NULL && anexar_segmento_shm(ids_a[e], &enderecos[e]) == -1) {
                enderecos[e] = NULL;
                falhou = 1;
                break;
            }

            /* Espera uma posição livre e entra na região crítica numa única operação
             * atômica. O mutex usa SEM_UNDO: se o processo morrer dentro da região
             * crítica, o kernel o libera e a estrutura não fica travada. */
            struct sembuf entrar[2] = {
                { .sem_num = SEM_VAZIO, .sem_op = -1, .sem_flg = 0 },
                { .sem_num = SEM_MUTEX, .sem_op = -1, .sem_flg = SEM_UNDO }
            };
            struct sembuf sair[2] = {
                { .sem_num = SEM_MUTEX, .sem_op = 1, .sem_flg = SEM_UNDO },
                { .sem_num = SEM_CHEIO, .sem_op = 1, .sem_flg = 0 }
            };
            if (sem_ops(semid, entrar, 2) == -1) {
                if (!terminar_processo) {
                    perror("semop");
                    falhou = 1;
                }
                break;
            }

            shm_header_t *hdr = (shm_header_t *)enderecos[e];
            mensagem_t *buf = (mensagem_t *)((char *)enderecos[e] + sizeof(shm_header_t));
            mensagem_t *msg = &buf[hdr->cabeca];
            msg->mtype = 1;
            msg->pid_produtor = meu_pid;
            msg->seq = i + 1;
            msg->ack = ack;
            msg->ack_id = ack_id;
            gettimeofday(&msg->tempo_envio, NULL);

            hdr->cabeca = (hdr->cabeca + 1) % hdr->capacidade;
            hdr->count++;
            hdr->total_produzidas++;

            if (sem_ops(semid, sair, 2) == -1) {
                perror("semop");
                falhou = 1;
                break;
            }

            /* Impressão fora da região crítica */
            printf("[PRODUZ PID %d] Mensagem %d inserida na memória %d | pid_prod=%d | seq=%d\n",
                   meu_pid, i + 1, shm_destino, meu_pid, i + 1);
            total_enviadas++;
        }

        if (p_prod == 0) {
            printf("[PRODUZ PID %d] Total de %lu mensagens enviadas com distribuição striped\n",
                   meu_pid, total_enviadas);
        }

        if (ack == ACK_WAIT && !terminar_processo) {
            printf("[PRODUZ PID %d] ack=1: aguardando confirmação de que TODAS as %lu mensagens foram consumidas...\n",
                   meu_pid, total_enviadas);
            if (ack_id == -1) {
                printf("[PRODUZ PID %d] Erro: semáforo de confirmação ausente\n", meu_pid);
                falhou = 1;
            } else if (total_enviadas > 0) {
                /* Bloqueia até o semáforo atingir total_enviadas confirmações */
                struct sembuf op;
                int confirmado;
                op.sem_num = 0;
                op.sem_op = -(short)total_enviadas;
                op.sem_flg = 0;
                while ((confirmado = semop(ack_id, &op, 1)) == -1 && errno == EINTR &&
                       !terminar_processo) {
                }
                if (confirmado == 0) {
                    printf("[PRODUZ PID %d] TODAS as mensagens foram consumidas. Confirmação completa!\n", meu_pid);
                } else if (!terminar_processo) {
                    perror("semop ack");
                    falhou = 1;
                }
            }
        } else if (ack != ACK_WAIT) {
            printf("[PRODUZ PID %d] ack=2: não aguardando confirmação\n", meu_pid);
        }
    }

    finalizar_processo("PRODUZ", &tempo_inicio, falhou ? EXIT_FAILURE : EXIT_SUCCESS);
}
