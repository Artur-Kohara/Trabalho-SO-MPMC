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
    
    printf("[CONSOME PID %d] Iniciado - p_prod=%d, n_msgs=%d, ack=%d, paradigma=%d\n", 
           meu_pid, p_prod, n_msgs, ack, paradigma);
    
    if (paradigma == PARADIGMA_MSG) {
        int msgid = obter_fila_mensagem(p_prod);
        if (msgid == -1) {
            if (paradigma == PARADIGMA_MSG) {
                printf("Erro: fila %d nao existe\n", p_prod);
            } else {
                printf("Erro: estrutura %d nao existe\n", p_prod);
            }
            gettimeofday(&tempo_fim, NULL);
            printf("[CONSOME PID %d] Finalizado. Tempo de execução: ", meu_pid);
            imprimir_tempo(&tempo_inicio, &tempo_fim);
            exit(EXIT_FAILURE);
        }
        
        if (ack == ACK_WAIT) {
            /* Bloquear até consumir n_msgs */
            printf("[CONSOME PID %d] Aguardando %d mensagens da fila %d...\n", 
                   meu_pid, n_msgs, p_prod);
            for (int i = 0; i < n_msgs; i++) {
                mensagem_t msg;
                if (msgrcv(msgid, &msg, sizeof(mensagem_t) - sizeof(long), 0, 0) == -1) {
                    if (errno == EINTR) {
                        i--;
                        continue;
                    }
                    perror("msgrcv");
                    break;
                }
                consumidas++;
                printf("[CONSOME PID %d] Mensagem %lu consumida da fila %d (produtor PID %d)\n", 
                       meu_pid, msg.seq, p_prod, msg.pid_produtor);
            }
        } else { /* ACK_NO_WAIT */
            printf("[CONSOME PID %d] Tentando consumir %d mensagens da fila %d (sem bloqueio)...\n", 
                   meu_pid, n_msgs, p_prod);
            for (int i = 0; i < n_msgs; i++) {
                mensagem_t msg;
                if (msgrcv(msgid, &msg, sizeof(mensagem_t) - sizeof(long), 0, IPC_NOWAIT) == -1) {
                    if (errno == ENOMSG) {
                        printf("Erro: foram consumidas apenas %d mensagens\n", consumidas);
                        break;
                    }
                    if (errno == EINTR) {
                        i--;
                        continue;
                    }
                    perror("msgrcv");
                    break;
                }
                consumidas++;
                printf("[CONSOME PID %d] Mensagem %lu consumida da fila %d (produtor PID %d)\n", 
                       meu_pid, msg.seq, p_prod, msg.pid_produtor);
            }
            if (consumidas == n_msgs) {
                printf("[CONSOME PID %d] Todas as %d mensagens consumidas.\n", meu_pid, consumidas);
            }
        }
        
    } else if (paradigma == PARADIGMA_SHM) {
        printf("[CONSOME PID %d] Modo memória compartilhada - implementação em etapa futura\n", meu_pid);
        sleep(1);
    }
    
    gettimeofday(&tempo_fim, NULL);
    printf("[CONSOME PID %d] Finalizado. Tempo de execução: ", meu_pid);
    imprimir_tempo(&tempo_inicio, &tempo_fim);
    
    return 0;
}
