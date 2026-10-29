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
                    printf("[PRODUZ PID %d] Mensagem %lu enviada para fila %d | pid_prod=%d | seq=%lu\n", 
                           meu_pid, msg.seq, fila_destino, msg.pid_produtor, msg.seq);
                }
            }
            printf("[PRODUZ PID %d] Total de %d mensagens enviadas com distribuição striped\n", 
                   meu_pid, n_msgs);
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
            
            printf("[PRODUZ PID %d] Enviando %d mensagens para fila %d\n", meu_pid, n_msgs, p_prod);
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
                    printf("[PRODUZ PID %d] Mensagem %lu enviada para fila %d | pid_prod=%d | seq=%lu\n", 
                           meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq);
                }
            }
            printf("[PRODUZ PID %d] Total de %d mensagens enviadas para fila %d\n", 
                   meu_pid, n_msgs, p_prod);
        }
        
        if (ack == ACK_WAIT) {
            printf("[PRODUZ PID %d] ack=1: aguardando confirmação de consumo\n", meu_pid);
            sleep(1);
        } else {
            printf("[PRODUZ PID %d] ack=2: não aguardando confirmação\n", meu_pid);
        }
        
    } else if (paradigma == PARADIGMA_SHM) {
        if (p_prod == 0) {
            /* Distribuição striped em memórias compartilhadas */
            printf("[PRODUZ PID %d] Distribuição striped em %d memórias compartilhadas\n", meu_pid, p_total);
            for (int i = 0; i < n_msgs; i++) {
                int shm_destino = (i % p_total) + 1;
                /* Por simplicidade, vamos usar apenas a primeira estrutura por enquanto */
                /* Será refinado na etapa 5 */
                key_t key;
                FILE *f = fopen(SHM_KEY_FILE, "a");
                if (f) fclose(f);
                key = ftok(SHM_KEY_FILE, shm_destino);
                int shmid = shmget(key, SHM_SIZE, 0666);
                if (shmid == -1) {
                    printf("Erro – estrutura %d nao existe\n", shm_destino);
                    continue;
                }
                void *addr = shmat(shmid, NULL, 0);
                if (addr == (void *)-1) {
                    perror("shmat");
                    continue;
                }
                
                /* Acessar semáforos */
                FILE *sf = fopen(SEM_KEY_FILE, "a");
                if (sf) fclose(sf);
                key_t skey = ftok(SEM_KEY_FILE, shm_destino);
                int semid = semget(skey, 3, 0666);
                
                if (semid != -1) {
                    sem_p(semid, 1); /* esperar vazio */
                    sem_p(semid, 0); /* mutex */
                }
                
                shm_header_t *hdr = (shm_header_t *)addr;
                if (hdr->count < hdr->capacidade) {
                    mensagem_t *buf = (mensagem_t *)(addr + sizeof(shm_header_t));
                    int idx = hdr->cabeca;
                    buf[idx].mtype = 1;
                    buf[idx].pid_produtor = meu_pid;
                    buf[idx].seq = i + 1;
                    gettimeofday(&buf[idx].tempo_envio, NULL);
                    snprintf(buf[idx].dados, sizeof(buf[idx].dados), "Msg %lu do produtor %d", buf[idx].seq, meu_pid);
                    
                    hdr->cabeca = (hdr->cabeca + 1) % hdr->capacidade;
                    hdr->count++;
                    hdr->total_produzidas++;
                    
                    printf("[PRODUZ PID %d] Mensagem %lu inserida na memória %d | pid_prod=%d | seq=%lu\n", 
                           meu_pid, buf[idx].seq, shm_destino, meu_pid, buf[idx].seq);
                } else {
                    printf("[PRODUZ PID %d] Buffer cheio na memória %d\n", meu_pid, shm_destino);
                }
                
                if (semid != -1) {
                    sem_v(semid, 0); /* liberar mutex */
                    sem_v(semid, 2); /* sinalizar cheio */
                }
                
                shmdt(addr);
            }
            printf("[PRODUZ PID %d] Total de %d mensagens enviadas com distribuição striped\n", 
                   meu_pid, n_msgs);
        } else {
            /* Enviar para memória compartilhada específica */
            key_t key;
            FILE *f = fopen(SHM_KEY_FILE, "a");
            if (f) fclose(f);
            key = ftok(SHM_KEY_FILE, p_prod);
            int shmid = shmget(key, SHM_SIZE, 0666);
            if (shmid == -1) {
                printf("Erro - estrutura %d nao existe\n", p_prod);
                gettimeofday(&tempo_fim, NULL);
                printf("[PRODUZ PID %d] Finalizado. Tempo de execução: ", meu_pid);
                imprimir_tempo(&tempo_inicio, &tempo_fim);
                exit(EXIT_FAILURE);
            }
            
            void *addr = shmat(shmid, NULL, 0);
            if (addr == (void *)-1) {
                perror("shmat");
                gettimeofday(&tempo_fim, NULL);
                printf("[PRODUZ PID %d] Finalizado. Tempo de execução: ", meu_pid);
                imprimir_tempo(&tempo_inicio, &tempo_fim);
                exit(EXIT_FAILURE);
            }
            
            FILE *sf = fopen(SEM_KEY_FILE, "a");
            if (sf) fclose(sf);
            key_t skey = ftok(SEM_KEY_FILE, p_prod);
            int semid = semget(skey, 3, 0666);
            
            printf("[PRODUZ PID %d] Enviando %d mensagens para memória compartilhada %d\n", 
                   meu_pid, n_msgs, p_prod);
            
            for (int i = 0; i < n_msgs; i++) {
                if (semid != -1) {
                    sem_p(semid, 1); /* esperar por espaço vazio */
                    sem_p(semid, 0); /* entrar em região crítica */
                }
                
                shm_header_t *hdr = (shm_header_t *)addr;
                mensagem_t *buf = (mensagem_t *)(addr + sizeof(shm_header_t));
                
                int idx = hdr->cabeca;
                buf[idx].mtype = 1;
                buf[idx].pid_produtor = meu_pid;
                buf[idx].seq = i + 1;
                gettimeofday(&buf[idx].tempo_envio, NULL);
                snprintf(buf[idx].dados, sizeof(buf[idx].dados), "Msg %lu do produtor %d", buf[idx].seq, meu_pid);
                
                hdr->cabeca = (hdr->cabeca + 1) % hdr->capacidade;
                hdr->count++;
                hdr->total_produzidas++;
                
                printf("[PRODUZ PID %d] Mensagem %lu inserida na memória %d | pid_prod=%d | seq=%lu\n", 
                       meu_pid, buf[idx].seq, p_prod, meu_pid, buf[idx].seq);
                
                if (semid != -1) {
                    sem_v(semid, 0); /* sair da região crítica */
                    sem_v(semid, 2); /* sinalizar que há mensagem cheia */
                }
            }
            
            if (ack == ACK_WAIT) {
                printf("[PRODUZ PID %d] ack=1: aguardando confirmação de consumo\n", meu_pid);
                sleep(1);
            } else {
                printf("[PRODUZ PID %d] ack=2: não aguardando confirmação\n", meu_pid);
            }
            
            shmdt(addr);
        }
    }
    
    gettimeofday(&tempo_fim, NULL);
    printf("[PRODUZ PID %d] Finalizado. Tempo de execução: ", meu_pid);
    imprimir_tempo(&tempo_inicio, &tempo_fim);
    
    return 0;
}
