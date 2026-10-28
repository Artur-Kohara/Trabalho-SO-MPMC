#include "common.h"
#include "shm_utils.h"
#include "msg_utils.h"

int main(int argc, char *argv[]) {
    int paradigma, p_total, p_prod, n_msgs, ack;
    struct timeval tempo_inicio, tempo_fim;
    pid_t meu_pid = getpid();
    
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
    
    printf("[PRODUZ PID %d] Iniciado - estrutura=%d, n_msgs=%d, ack=%d, paradigma=%d\n", 
           meu_pid, p_prod, n_msgs, ack, paradigma);
    
    if (paradigma == PARADIGMA_MSG) {
        if (p_prod == 0) {
            /* Distribuição striped: (1, 2, ..., p, 1, 2, ..., p) */
            printf("[PRODUZ PID %d] Distribuição striped em %d filas de mensagens\n", meu_pid, p_total);
            for (int i = 0; i < n_msgs; i++) {
                int fila_destino = (i % p_total) + 1;
                mensagem_t msg;
                msg.mtype = 1;
                msg.pid_produtor = meu_pid;
                msg.seq = i + 1;
                gettimeofday(&msg.tempo_envio, NULL);
                snprintf(msg.dados, sizeof(msg.dados), "Msg %lu do produtor %d", msg.seq, meu_pid);
                
                int msgid = obter_fila_mensagem(fila_destino);
                if (msgid == -1) {
                    printf("Erro - fila %d nao existe\n", fila_destino);
                    continue;
                }
                
                if (msgsnd(msgid, &msg, sizeof(mensagem_t) - sizeof(long), 0) == -1) {
                    perror("msgsnd");
                } else {
                    printf("[PRODUZ PID %d] Mensagem %lu enviada para fila %d (pid_prod=%d, seq=%lu)\n", 
                           meu_pid, msg.seq, fila_destino, msg.pid_produtor, msg.seq);
                }
            }
        } else {
            /* Enviar para fila específica */
            int msgid = obter_fila_mensagem(p_prod);
            if (msgid == -1) {
                printf("Erro - fila %d nao existe\n", p_prod);
                gettimeofday(&tempo_fim, NULL);
                printf("[PRODUZ PID %d] Finalizado. Tempo de execução: ", meu_pid);
                imprimir_tempo(&tempo_inicio, &tempo_fim);
                exit(EXIT_FAILURE);
            }
            
            for (int i = 0; i < n_msgs; i++) {
                mensagem_t msg;
                msg.mtype = 1;
                msg.pid_produtor = meu_pid;
                msg.seq = i + 1;
                gettimeofday(&msg.tempo_envio, NULL);
                snprintf(msg.dados, sizeof(msg.dados), "Msg %lu do produtor %d", msg.seq, meu_pid);
                
                if (msgsnd(msgid, &msg, sizeof(mensagem_t) - sizeof(long), 0) == -1) {
                    perror("msgsnd");
                } else {
                    printf("[PRODUZ PID %d] Mensagem %lu enviada para fila %d (pid_prod=%d, seq=%lu)\n", 
                           meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq);
                }
            }
        }
        
        if (ack == ACK_WAIT) {
            printf("[PRODUZ PID %d] Aguardando confirmação: todas as %d mensagens devem ser consumidas\n", 
                   meu_pid, n_msgs);
            /* Aguarda confirmação (implementação simplificada - será aprimorada na etapa 5) */
            sleep(2);
        }
        
    } else if (paradigma == PARADIGMA_SHM) {
        printf("[PRODUZ PID %d] Modo memória compartilhada (shm+sem) - em desenvolvimento\n", meu_pid);
        if (p_prod == 0) {
            printf("[PRODUZ PID %d] Distribuição striped em %d memórias compartilhadas\n", meu_pid, p_total);
        }
        /* Implementação será completada nas próximas etapas */
        sleep(1);
    }
    
    gettimeofday(&tempo_fim, NULL);
    printf("[PRODUZ PID %d] Finalizado. Tempo de execução: ", meu_pid);
    imprimir_tempo(&tempo_inicio, &tempo_fim);
    
    return 0;
}
