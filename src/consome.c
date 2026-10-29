#include "common.h"
#include "shm_utils.h"
#include "msg_utils.h"

static void incrementar_contador_msg(int p_prod) {
    key_t key;
    FILE *f = fopen(SHM_KEY_FILE, "a");
    if (f) fclose(f);
    key = ftok(SHM_KEY_FILE, 10000 + p_prod);
    int shmid = shmget(key, sizeof(unsigned long), 0666);
    if (shmid == -1) {
        return;
    }
    void *addr = shmat(shmid, NULL, 0);
    if (addr == (void *)-1) {
        return;
    }
    unsigned long *cnt = (unsigned long *)addr;
    (*cnt)++;
    shmdt(addr);
}

int main(int argc, char *argv[]) {
    int paradigma, p_prod, n_msgs, ack;
    struct timeval tempo_inicio, tempo_fim;
    pid_t meu_pid = getpid();
    int consumidas = 0;
    mensagem_t ultimo_msg;
    ultimo_msg.pid_produtor = 0;
    
    if (argc < 6) {
        fprintf(stderr, "Uso: %s <paradigma> <p_total> <p_prod> <n_msgs> <ack>\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    paradigma = atoi(argv[1]);
    /* p_total = atoi(argv[2]); */
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
        
        int ack_msgid = obter_fila_ack(p_prod);
        
        if (ack == ACK_WAIT) {
            printf("[CONSOME PID %d] Modo bloqueante (ack=1) - aguardando consumir %d mensagens da fila %d\n", 
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
                ultimo_msg = msg;
                incrementar_contador_msg(p_prod);
                printf("[CONSOME PID %d] Mensagem %lu consumida da fila %d | pid_produtor=%d | seq=%lu | tempo_envio=%ld.%06ld\n", 
                       meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq, 
                       msg.tempo_envio.tv_sec, msg.tempo_envio.tv_usec);
            }
            printf("[CONSOME PID %d] Todas as %d mensagens solicitadas foram consumidas.\n", 
                   meu_pid, consumidas);
            if (ack_msgid != -1 && ultimo_msg.pid_produtor != 0) {
                ack_msg_t ack_msg;
                ack_msg.mtype = ultimo_msg.pid_produtor;
                ack_msg.pid_produtor = meu_pid;
                ack_msg.produtor_pid_num = ultimo_msg.pid_produtor;
                ack_msg.mensagens_consumidas = consumidas;
                if (msgsnd(ack_msgid, &ack_msg, sizeof(ack_msg_t) - sizeof(long), 0) == -1) {
                    perror("msgsnd ack");
                } else {
                    printf("[CONSOME PID %d] Confirmação enviada ao produtor PID %d\n", meu_pid, ultimo_msg.pid_produtor);
                }
            }
            
        } else {
            printf("[CONSOME PID %d] Modo não-bloqueante (ack=2) - tentando consumir %d mensagens da fila %d\n", 
                   meu_pid, n_msgs, p_prod);
            while (consumidas < n_msgs) {
                mensagem_t msg;
                if (msgrcv(msgid, &msg, sizeof(mensagem_t) - sizeof(long), 0, IPC_NOWAIT) == -1) {
                    if (errno == ENOMSG) {
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
                incrementar_contador_msg(p_prod);
                printf("[CONSOME PID %d] Mensagem %lu consumida da fila %d | pid_produtor=%d | seq=%lu | tempo_envio=%ld.%06ld\n", 
                       meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq, 
                       msg.tempo_envio.tv_sec, msg.tempo_envio.tv_usec);
            }
            if (consumidas == n_msgs) {
                printf("[CONSOME PID %d] Todas as %d mensagens solicitadas foram consumidas.\n", 
                       meu_pid, consumidas);
            }
        }
        
    } else if (paradigma == PARADIGMA_SHM) {
        key_t key;
        FILE *f = fopen(SHM_KEY_FILE, "a");
        if (f) fclose(f);
        key = ftok(SHM_KEY_FILE, p_prod);
        int shmid = shmget(key, SHM_SIZE, 0666);
        if (shmid == -1) {
            printf("Erro - estrutura %d nao existe\n", p_prod);
            gettimeofday(&tempo_fim, NULL);
            printf("[CONSOME PID %d] Finalizado. Tempo de execução: ", meu_pid);
            imprimir_tempo(&tempo_inicio, &tempo_fim);
            exit(EXIT_FAILURE);
        }
        
        void *addr = shmat(shmid, NULL, 0);
        if (addr == (void *)-1) {
            perror("shmat");
            gettimeofday(&tempo_fim, NULL);
            printf("[CONSOME PID %d] Finalizado. Tempo de execução: ", meu_pid);
            imprimir_tempo(&tempo_inicio, &tempo_fim);
            exit(EXIT_FAILURE);
        }
        
        FILE *sf = fopen(SEM_KEY_FILE, "a");
        if (sf) fclose(sf);
        key_t skey = ftok(SEM_KEY_FILE, p_prod);
        int semid = semget(skey, 3, 0666);
        
        if (ack == ACK_WAIT) {
            printf("[CONSOME PID %d] Modo bloqueante (ack=1) - aguardando consumir %d mensagens da memória %d\n", 
                   meu_pid, n_msgs, p_prod);
            while (consumidas < n_msgs) {
                if (semid != -1) {
                    sem_p(semid, 2);
                    sem_p(semid, 0);
                }
                
                shm_header_t *hdr = (shm_header_t *)addr;
                if (hdr->count > 0) {
                    mensagem_t *buf = (mensagem_t *)(addr + sizeof(shm_header_t));
                    int idx = hdr->cauda;
                    mensagem_t msg = buf[idx];
                    
                    hdr->cauda = (hdr->cauda + 1) % hdr->capacidade;
                    hdr->count--;
                    hdr->total_consumidas++;
                    
                    consumidas++;
                    printf("[CONSOME PID %d] Mensagem %lu consumida da memória %d | pid_produtor=%d | seq=%lu | tempo_envio=%ld.%06ld\n", 
                           meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq, 
                           msg.tempo_envio.tv_sec, msg.tempo_envio.tv_usec);
                }
                
                if (semid != -1) {
                    sem_v(semid, 0);
                    sem_v(semid, 1);
                }
                
                if (hdr->count == 0 && consumidas < n_msgs) {
                    usleep(10000);
                }
            }
            printf("[CONSOME PID %d] Todas as %d mensagens solicitadas foram consumidas.\n", 
                   meu_pid, consumidas);
            
        } else {
            printf("[CONSOME PID %d] Modo não-bloqueante (ack=2) - tentando consumir %d mensagens da memória %d\n", 
                   meu_pid, n_msgs, p_prod);
            while (consumidas < n_msgs) {
                if (semid != -1) {
                    struct sembuf op;
                    op.sem_num = 2;
                    op.sem_op = -1;
                    op.sem_flg = IPC_NOWAIT;
                    if (semop(semid, &op, 1) == -1) {
                        if (errno == EAGAIN || errno == EBUSY) {
                            if (consumidas == 0) {
                                printf("[CONSOME PID %d] Erro: não há mensagens disponíveis. Foram consumidas 0 mensagens.\n", 
                                       meu_pid);
                            } else if (consumidas < n_msgs) {
                                printf("[CONSOME PID %d] Erro: foram consumidas apenas %d mensagens\n", 
                                       meu_pid, consumidas);
                            }
                            break;
                        }
                        if (errno == EINTR) continue;
                        perror("semop");
                        break;
                    }
                    sem_p(semid, 0);
                } else {
                    shm_header_t *hdr = (shm_header_t *)addr;
                    if (hdr->count == 0) {
                        if (consumidas == 0) {
                            printf("[CONSOME PID %d] Erro: não há mensagens disponíveis. Foram consumidas 0 mensagens.\n", 
                                   meu_pid);
                        } else {
                            printf("[CONSOME PID %d] Erro: foram consumidas apenas %d mensagens\n", 
                                   meu_pid, consumidas);
                        }
                        break;
                    }
                }
                
                shm_header_t *hdr = (shm_header_t *)addr;
                if (hdr->count > 0) {
                    mensagem_t *buf = (mensagem_t *)(addr + sizeof(shm_header_t));
                    int idx = hdr->cauda;
                    mensagem_t msg = buf[idx];
                    
                    hdr->cauda = (hdr->cauda + 1) % hdr->capacidade;
                    hdr->count--;
                    hdr->total_consumidas++;
                    
                    consumidas++;
                    printf("[CONSOME PID %d] Mensagem %lu consumida da memória %d | pid_produtor=%d | seq=%lu | tempo_envio=%ld.%06ld\n", 
                           meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq, 
                           msg.tempo_envio.tv_sec, msg.tempo_envio.tv_usec);
                }
                
                if (semid != -1) {
                    sem_v(semid, 0);
                    sem_v(semid, 1);
                }
            }
            if (consumidas == n_msgs) {
                printf("[CONSOME PID %d] Todas as %d mensagens solicitadas foram consumidas.\n", 
                       meu_pid, consumidas);
            }
        }
        
        shmdt(addr);
    }
    
    gettimeofday(&tempo_fim, NULL);
    printf("[CONSOME PID %d] Finalizado. Tempo de execução: ", meu_pid);
    imprimir_tempo(&tempo_inicio, &tempo_fim);
    
    return 0;
}
