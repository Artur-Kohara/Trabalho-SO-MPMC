#include "common.h"
#include "shm_utils.h"
#include "msg_utils.h"

int main(int argc, char *argv[]) {
    int paradigma, p_total, p_prod, n_msgs, ack;
    struct timeval tempo_inicio, tempo_fim;
    pid_t meu_pid = getpid();
    int consumidas = 0;
    
    if (argc < 6) {
        fprintf(stderr, "Uso: %s <paradigma> <p_total> <p_prod> <n_msgs> <ack>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    paradigma = atoi(argv[1]);
    p_total = atoi(argv[2]);
    p_prod = atoi(argv[3]);
    n_msgs = atoi(argv[4]);
    ack = atoi(argv[5]);
    
    gettimeofday(&tempo_inicio, NULL);
    
    printf("[CONSOME PID %d] Iniciado - estrutura=%d, n_msgs=%d, ack=%d, paradigma=%d\n", 
           meu_pid, p_prod, n_msgs, ack, paradigma);
    
    if (paradigma == PARADIGMA_MSG) {
        int msgid = obter_fila_mensagem(p_prod);
        if (msgid == -1) {
            printf("Erro - fila %d nao existe\n", p_prod);
            gettimeofday(&tempo_fim, NULL);
            printf("[CONSOME PID %d] Finalizado. Tempo de execução: ", meu_pid);
            imprimir_tempo(&tempo_inicio, &tempo_fim);
            exit(EXIT_FAILURE);
        }
        
        if (ack == ACK_WAIT) {
            /* ack=1: bloqueado até consumir todas as n_msgs solicitadas */
            printf("[CONSOME PID %d] Modo bloqueante (ack=1) - aguardando %d mensagens da fila %d\n", 
                   meu_pid, n_msgs, p_prod);
            while (consumidas < n_msgs) {
                mensagem_t msg;
                if (msgrcv(msgid, &msg, sizeof(mensagem_t) - sizeof(long), 0, 0) == -1) {
                    if (errno == EINTR) {
                        continue;
                    }
                    perror("msgrcv");
                    break;
                }
                consumidas++;
                printf("[CONSOME PID %d] Mensagem %lu consumida da fila %d | pid_produtor=%d | seq=%lu\n", 
                       meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq);
            }
            printf("[CONSOME PID %d] Todas as %d mensagens solicitadas foram consumidas.\n", 
                   meu_pid, consumidas);
            
        } else { /* ACK_NO_WAIT = 2 */
            /* ack=2: se houver menos mensagens, retorna erro informando quantas foram consumidas */
            printf("[CONSOME PID %d] Modo não-bloqueante (ack=2) - tentando consumir %d mensagens da fila %d\n", 
                   meu_pid, n_msgs, p_prod);
            while (consumidas < n_msgs) {
                mensagem_t msg;
                if (msgrcv(msgid, &msg, sizeof(mensagem_t) - sizeof(long), 0, IPC_NOWAIT) == -1) {
                    if (errno == ENOMSG) {
                        /* Não há mais mensagens disponíveis */
                        if (consumidas == 0) {
                            printf("[CONSOME PID %d] Erro: não há mensagens disponíveis. Foram consumidas 0 mensagens.\n", 
                                   meu_pid);
                        } else if (consumidas < n_msgs) {
                            printf("[CONSOME PID %d] Erro: foram consumidas apenas %d mensagens\n", 
                                   meu_pid, consumidas);
                        }
                        break;
                    }
                    if (errno == EINTR) {
                        continue;
                    }
                    perror("msgrcv");
                    break;
                }
                consumidas++;
                printf("[CONSOME PID %d] Mensagem %lu consumida da fila %d | pid_produtor=%d | seq=%lu\n", 
                       meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq);
            }
            if (consumidas == n_msgs) {
                printf("[CONSOME PID %d] Todas as %d mensagens solicitadas foram consumidas.\n", 
                       meu_pid, consumidas);
            }
        }
        
    } else if (paradigma == PARADIGMA_SHM) {
        printf("[CONSOME PID %d] Modo memória compartilhada (shm+sem) - em desenvolvimento\n", meu_pid);
        sleep(1);
    }
    
    gettimeofday(&tempo_fim, NULL);
    printf("[CONSOME PID %d] Finalizado. Tempo de execução: ", meu_pid);
    imprimir_tempo(&tempo_inicio, &tempo_fim);
    
    return 0;
}
