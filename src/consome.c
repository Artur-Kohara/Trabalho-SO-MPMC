#include "common.h"
#include "shm_utils.h"
#include "msg_utils.h"

int main(int argc, char *argv[]) {
    int paradigma, p_prod, n_msgs, ack;
    struct timeval tempo_inicio, tempo_fim;
    pid_t meu_pid = getpid();
    int consumidas = 0;
    
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
        
        if (ack == ACK_WAIT) {
            /* ack=1: bloqueado até consumir todas as n_msgs solicitadas */
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
                printf("[CONSOME PID %d] Mensagem %lu consumida da fila %d | pid_produtor=%d | seq=%lu | tempo_envio=%ld.%06ld\n", 
                       meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq, 
                       msg.tempo_envio.tv_sec, msg.tempo_envio.tv_usec);
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
                    sem_p(semid, 2); /* esperar por mensagem cheia */
                    sem_p(semid, 0); /* entrar em região crítica */
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
                    sem_v(semid, 0); /* sair da região crítica */
                    sem_v(semid, 1); /* sinalizar espaço vazio */
                }
                
                if (hdr->count == 0 && consumidas < n_msgs) {
                    /* Pequena pausa para evitar busy wait caso necessário */
                    usleep(10000);
                }
            }
            printf("[CONSOME PID %d] Todas as %d mensagens solicitadas foram consumidas.\n", 
                   meu_pid, consumidas);
            
        } else { /* ACK_NO_WAIT */
            printf("[CONSOME PID %d] Modo não-bloqueante (ack=2) - tentando consumir %d mensagens da memória %d\n", 
                   meu_pid, n_msgs, p_prod);
            while (consumidas < n_msgs) {
                if (semid != -1) {
                    /* Tentar pegar sem bloqueio - usar operação com flag IPC_NOWAIT */
                    struct sembuf op;
                    op.sem_num = 2;
                    op.sem_op = -1;
                    op.sem_flg = IPC_NOWAIT;
                    if (semop(semid, &op, 1) == -1) {
                        if (errno == EAGAIN || errno == ENOSPC) {
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
                    /* Sem semáforos, apenas verificar */
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
