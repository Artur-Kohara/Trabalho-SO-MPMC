#include "common.h"
#include "shm_utils.h"
#include "msg_utils.h"

/* Informa ao mpmc quantas mensagens este consumidor retirou da fila (paradigma 2).
 * Feito ao terminar, inclusive quando encerrado pelo shutdown. Envio sem bloqueio,
 * com novas tentativas, para não prender o processo se a fila estiver cheia. */
static void informar_consumo(int fila_est, int estrutura, int consumidas) {
    contador_msg_t c;
    int tentativa;
    
    if (fila_est == -1 || consumidas == 0) {
        return;
    }
    c.mtype = estrutura;
    c.quantidade = consumidas;
    for (tentativa = 0; tentativa < 100; tentativa++) {
        if (msgsnd(fila_est, &c, sizeof(c) - sizeof(long), IPC_NOWAIT) == 0) {
            return;
        }
        if (errno != EAGAIN && errno != EINTR) {
            break;
        }
        usleep(10000);
    }
    perror("msgsnd estatísticas");
}

/* Confirma ao produtor o consumo da mensagem, se ele pediu confirmação (ack=1).
 * A fila de confirmações guarda um contador por produtor (mtype = número do
 * produtor): o consumidor retira o contador que estiver na fila, se houver,
 * incrementa e devolve. Assim a fila tem poucas mensagens, qualquer que seja o
 * volume de mensagens consumidas. */
static void confirmar_msg(int fila_ack, const mensagem_t *msg) {
    contador_msg_t c;
    
    if (msg->ack != ACK_WAIT || fila_ack == -1) {
        return;
    }
    if (msgrcv(fila_ack, &c, sizeof(c) - sizeof(long), msg->ack_id, IPC_NOWAIT) == -1) {
        c.mtype = msg->ack_id;
        c.quantidade = 0;
    }
    c.quantidade++;
    while (msgsnd(fila_ack, &c, sizeof(c) - sizeof(long), 0) == -1) {
        if (errno == EINTR) continue;
        perror("msgsnd confirmação");
        break;
    }
}

/* Paradigma 1: incrementa o semáforo de confirmação do produtor, se ele pediu
 * confirmação. O semáforo pode já ter sido removido (produtor encerrado). */
static void confirmar_shm(mensagem_t *msg) {
    struct sembuf op;
    
    if (msg->ack != ACK_WAIT || msg->ack_id == -1) {
        return;
    }
    op.sem_num = 0;
    op.sem_op = 1;
    op.sem_flg = 0;
    while (semop(msg->ack_id, &op, 1) == -1 && errno == EINTR) {
    }
}

static volatile sig_atomic_t vez_recebida = 0;

static void tratar_vez(int sig) {
    (void)sig;
    vez_recebida = 1;
}

/* Espera o aviso (SIGUSR1) do mpmc de que chegou a vez deste consumidor na
 * estrutura. O SIGUSR1 chega bloqueado desde o fork, então um aviso enviado
 * antes desta espera fica pendente e não se perde. Os sinais de término também
 * ficam bloqueados fora do sigsuspend, para não haver corrida com a espera. */
static void aguardar_vez(void) {
    int sinais[] = { SIGUSR1, SIGTERM, SIGINT, SIGHUP, SIGQUIT };
    struct sigaction sa;
    sigset_t bloquear, anterior, espera;
    size_t i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = tratar_vez;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGUSR1, &sa, NULL);

    sigemptyset(&bloquear);
    for (i = 0; i < sizeof(sinais) / sizeof(sinais[0]); i++) {
        sigaddset(&bloquear, sinais[i]);
    }
    sigprocmask(SIG_BLOCK, &bloquear, &anterior);
    espera = anterior;
    for (i = 0; i < sizeof(sinais) / sizeof(sinais[0]); i++) {
        sigdelset(&espera, sinais[i]);
    }
    while (!vez_recebida && !terminar_processo) {
        sigsuspend(&espera);
    }
    sigprocmask(SIG_SETMASK, &anterior, NULL);
}

/* Uso: consome <paradigma> <p_prod> <n_msgs> <ack> <aguardar_vez> <fila_ack> <fila_est> <estrutura>
 *   aguardar_vez : 1 se outro consumidor bloqueante da mesma estrutura chegou antes;
 *                  o mpmc envia SIGUSR1 quando chegar a vez deste
 *   estrutura    : identificadores IPC da estrutura: "shmid:semid" no paradigma 1,
 *                  "msgid" no paradigma 2
 *   fila_ack, fila_est : filas de confirmações e de estatísticas (paradigma 2) */
int main(int argc, char *argv[]) {
    int paradigma, p_prod, n_msgs, ack, esperar_vez, fila_ack, fila_est;
    int id_a, id_b;
    struct timespec tempo_inicio;
    pid_t meu_pid = getpid();
    int consumidas = 0;

    if (argc < 9) {
        fprintf(stderr, "Uso: %s <paradigma> <p_prod> <n_msgs> <ack> <aguardar_vez> <fila_ack> <fila_est> <estrutura>\n",
                argv[0]);
        exit(EXIT_FAILURE);
    }

    paradigma = atoi(argv[1]);
    p_prod = atoi(argv[2]);
    n_msgs = atoi(argv[3]);
    ack = atoi(argv[4]);
    esperar_vez = atoi(argv[5]);
    fila_ack = atoi(argv[6]);
    fila_est = atoi(argv[7]);
    if (ler_ids(argv[8], &id_a, &id_b, 1) != 1) {
        fprintf(stderr, "consome: identificadores da estrutura inválidos\n");
        exit(EXIT_FAILURE);
    }
    
    setvbuf(stdout, NULL, _IOLBF, 0);
    instalar_encerramento_gracioso();
    agora(&tempo_inicio);

    printf("[CONSOME PID %d] Iniciado - estrutura=%d, n_msgs=%d, ack=%d, paradigma=%d\n",
           meu_pid, p_prod, n_msgs, ack, paradigma);

    if (esperar_vez) {
        printf("[CONSOME PID %d] Aguardando a vez: outro consumidor bloqueante da estrutura %d chegou antes\n",
               meu_pid, p_prod);
        aguardar_vez();
        if (!terminar_processo) {
            printf("[CONSOME PID %d] Vez recebida\n", meu_pid);
        }
    }
    
    if (paradigma == PARADIGMA_MSG) {
        int msgid = id_a;
        
        if (ack == ACK_WAIT) {
            printf("[CONSOME PID %d] Modo bloqueante (ack=1) - aguardando consumir %d mensagens da fila %d\n", 
                   meu_pid, n_msgs, p_prod);
        } else {
            printf("[CONSOME PID %d] Modo não-bloqueante (ack=2) - tentando consumir %d mensagens da fila %d\n", 
                   meu_pid, n_msgs, p_prod);
        }
        
        while (consumidas < n_msgs && !terminar_processo) {
            mensagem_t msg;
            /* ack=2 não bloqueia: ENOMSG indica que não há mais mensagens */
            if (msgrcv(msgid, &msg, sizeof(mensagem_t) - sizeof(long), 0,
                       ack == ACK_WAIT ? 0 : IPC_NOWAIT) == -1) {
                if (errno == EINTR) continue;
                if (errno != ENOMSG && !terminar_processo) perror("msgrcv");
                break;
            }
            consumidas++;
            confirmar_msg(fila_ack, &msg);
            printf("[CONSOME PID %d] Mensagem %lu consumida da fila %d | pid_produtor=%d | seq=%lu | tempo_envio=%ld.%06ld\n", 
                   meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq, 
                   msg.tempo_envio.tv_sec, msg.tempo_envio.tv_usec);
        }
        informar_consumo(fila_est, p_prod, consumidas);
        
    } else if (paradigma == PARADIGMA_SHM) {
        int semid = id_b;
        void *addr;
        if (anexar_segmento_shm(id_a, &addr) == -1) {
            finalizar_processo("CONSOME", &tempo_inicio, EXIT_FAILURE);
        }
        
        if (ack == ACK_WAIT) {
            printf("[CONSOME PID %d] Modo bloqueante (ack=1) - aguardando consumir %d mensagens da memória %d\n", 
                   meu_pid, n_msgs, p_prod);
        } else {
            printf("[CONSOME PID %d] Modo não-bloqueante (ack=2) - tentando consumir %d mensagens da memória %d\n", 
                   meu_pid, n_msgs, p_prod);
        }
        
        while (consumidas < n_msgs && !terminar_processo) {
            /* Espera uma mensagem e entra na região crítica numa única operação
             * atômica; com ack=2 desiste (EAGAIN) se não houver mensagem. O mutex
             * usa SEM_UNDO: se o processo morrer dentro da região crítica, o kernel
             * o libera. */
            struct sembuf entrar[2] = {
                { .sem_num = SEM_CHEIO, .sem_op = -1, .sem_flg = (ack == ACK_WAIT) ? 0 : IPC_NOWAIT },
                { .sem_num = SEM_MUTEX, .sem_op = -1, .sem_flg = SEM_UNDO }
            };
            struct sembuf sair[2] = {
                { .sem_num = SEM_MUTEX, .sem_op = 1, .sem_flg = SEM_UNDO },
                { .sem_num = SEM_VAZIO, .sem_op = 1, .sem_flg = 0 }
            };
            if (sem_ops(semid, entrar, 2) == -1) {
                if (errno != EAGAIN && !terminar_processo) perror("semop");
                break;
            }
            
            shm_header_t *hdr = (shm_header_t *)addr;
            mensagem_t *buf = (mensagem_t *)((char *)addr + sizeof(shm_header_t));
            mensagem_t msg = buf[hdr->cauda];
            hdr->cauda = (hdr->cauda + 1) % hdr->capacidade;
            hdr->count--;
            hdr->total_consumidas++;
            
            if (sem_ops(semid, sair, 2) == -1) {
                perror("semop");
                break;
            }
            
            consumidas++;
            confirmar_shm(&msg);
            printf("[CONSOME PID %d] Mensagem %lu consumida da memória %d | pid_produtor=%d | seq=%lu | tempo_envio=%ld.%06ld\n", 
                   meu_pid, msg.seq, p_prod, msg.pid_produtor, msg.seq, 
                   msg.tempo_envio.tv_sec, msg.tempo_envio.tv_usec);
        }
        
        shmdt(addr);
    }
    
    if (consumidas == n_msgs) {
        printf("[CONSOME PID %d] Todas as %d mensagens solicitadas foram consumidas.\n", 
               meu_pid, consumidas);
    } else if (terminar_processo) {
        printf("[CONSOME PID %d] Interrompido após consumir %d de %d mensagens.\n", 
               meu_pid, consumidas, n_msgs);
    } else if (consumidas == 0) {
        printf("[CONSOME PID %d] Erro: não há mensagens disponíveis. Foram consumidas 0 mensagens.\n", 
               meu_pid);
    } else {
        printf("[CONSOME PID %d] Erro: foram consumidas apenas %d mensagens\n", 
               meu_pid, consumidas);
    }
    
    finalizar_processo("CONSOME", &tempo_inicio, consumidas == n_msgs ? EXIT_SUCCESS : EXIT_FAILURE);
}
