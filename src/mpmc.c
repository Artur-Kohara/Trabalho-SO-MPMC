#include "common.h"
#include "shm_utils.h"
#include "msg_utils.h"

#define MAX_FILHOS 4096

static estrutura_info_t *estruturas = NULL;
static proc_info_t *filhos = NULL;
static int num_estruturas = 0;
static int paradigma_atual = 0;
static int num_filhos = 0;
static struct timeval tempo_inicio_mpmc;

void inicializar_sistema(int p, int paradigma) {
    int i;
    
    num_estruturas = p;
    paradigma_atual = paradigma;
    
    estruturas = (estrutura_info_t *)calloc(num_estruturas, sizeof(estrutura_info_t));
    filhos = (proc_info_t *)calloc(MAX_FILHOS, sizeof(proc_info_t));
    
    if (estruturas == NULL || filhos == NULL) {
        erro("malloc/calloc");
    }
    
    gettimeofday(&tempo_inicio_mpmc, NULL);
    
    if (paradigma == PARADIGMA_SHM) {
        printf("Inicializando sistema com %d memórias compartilhadas (paradigma 1)...\n", p);
        printf("Cada segmento terá tamanho de %d bytes.\n", SHM_SIZE);
        for (i = 0; i < num_estruturas; i++) {
            estruturas[i].id = i + 1;
            estruturas[i].tipo = PARADIGMA_SHM;
            estruturas[i].shmid = criar_segmento_shm(i + 1);
            estruturas[i].semid = criar_semaforos_shm(i + 1);
            estruturas[i].msgid = -1;
            estruturas[i].ack_msgid = -1;
            estruturas[i].shm_addr = NULL;
            estruturas[i].cnt_addr = NULL;
            estruturas[i].cnt_shmid = -1;
            estruturas[i].total_consumidas = 0;
            estruturas[i].total_produzidas = 0;
            if (estruturas[i].shmid != -1) {
                anexar_segmento_shm(estruturas[i].shmid, &estruturas[i].shm_addr);
                if (estruturas[i].shm_addr != NULL && estruturas[i].shm_addr != (void *)-1) {
                    shm_header_t *hdr = (shm_header_t *)estruturas[i].shm_addr;
                    hdr->cabeca = 0;
                    hdr->cauda = 0;
                    hdr->count = 0;
                    hdr->total_produzidas = 0;
                    hdr->total_consumidas = 0;
                    size_t espaco_disponivel = SHM_SIZE - sizeof(shm_header_t);
                    hdr->capacidade = espaco_disponivel / sizeof(mensagem_t);
                    hdr->iniciado = 1;
                    printf("  Estrutura %d: capacidade para %d mensagens\n", i + 1, hdr->capacidade);
                }
            }
            if (estruturas[i].semid != -1) {
                if (estruturas[i].shm_addr != NULL && estruturas[i].shm_addr != (void *)-1) {
                    shm_header_t *hdr = (shm_header_t *)estruturas[i].shm_addr;
                    inicializar_semaforos(estruturas[i].semid, hdr->capacidade);
                } else {
                    inicializar_semaforos(estruturas[i].semid, 10);
                }
            }
        }
        printf("Memórias compartilhadas e semáforos criados com sucesso.\n");
    } else if (paradigma == PARADIGMA_MSG) {
        printf("Inicializando sistema com %d filas de mensagens (paradigma 2)...\n", p);
        for (i = 0; i < num_estruturas; i++) {
            estruturas[i].id = i + 1;
            estruturas[i].tipo = PARADIGMA_MSG;
            estruturas[i].shmid = -1;
            estruturas[i].semid = -1;
            estruturas[i].msgid = criar_fila_mensagem(i + 1);
            estruturas[i].ack_msgid = criar_fila_ack(i + 1);
            estruturas[i].shm_addr = NULL;
            estruturas[i].cnt_addr = NULL;
            estruturas[i].cnt_shmid = -1;
            estruturas[i].total_consumidas = 0;
            estruturas[i].total_produzidas = 0;
            if (estruturas[i].msgid == -1) {
                printf("Aviso: falha ao criar fila %d\n", i + 1);
            }
            if (estruturas[i].ack_msgid == -1) {
                printf("Aviso: falha ao criar fila ACK %d\n", i + 1);
            }
            /* Criar memória compartilhada para contador de mensagens consumidas */
            estruturas[i].cnt_shmid = criar_segmento_shm(10000 + i + 1);
            if (estruturas[i].cnt_shmid != -1) {
                anexar_segmento_shm(estruturas[i].cnt_shmid, &estruturas[i].cnt_addr);
                if (estruturas[i].cnt_addr != NULL && estruturas[i].cnt_addr != (void *)-1) {
                    memset(estruturas[i].cnt_addr, 0, sizeof(unsigned long));
                }
            }
        }
        printf("Filas de mensagens e filas de confirmação criadas com sucesso.\n");
    } else {
        printf("Erro: paradigma inválido. Use 1 (memória compartilhada) ou 2 (troca de mensagens).\n");
        exit(EXIT_FAILURE);
    }
}

void adicionar_filho(pid_t pid, char tipo, int estrutura, int n_msgs, int ack) {
    if (num_filhos < MAX_FILHOS) {
        filhos[num_filhos].pid = pid;
        filhos[num_filhos].tipo = tipo;
        filhos[num_filhos].estrutura = estrutura;
        filhos[num_filhos].n_msgs = n_msgs;
        filhos[num_filhos].ack = ack;
        gettimeofday(&filhos[num_filhos].inicio, NULL);
        filhos[num_filhos].fim.tv_sec = 0;
        filhos[num_filhos].fim.tv_usec = 0;
        filhos[num_filhos].finalizado = 0;
        num_filhos++;
    }
}

void comando_produz(char *args[]) {
    int p_prod, n_msgs, ack;
    pid_t pid;
    
    if (args[1] == NULL || args[2] == NULL || args[3] == NULL) {
        printf("Erro: uso correto: produz <p_prod> <n_msgs> <ack>\n");
        printf("       p_prod=0 para distribuição striped (1,2,...,p,...)\n");
        return;
    }
    
    p_prod = atoi(args[1]);
    n_msgs = atoi(args[2]);
    ack = atoi(args[3]);
    
    if (n_msgs < 1 || n_msgs > MAX_MSGS) {
        printf("Erro: n_msgs deve estar entre 1 e %d\n", MAX_MSGS);
        return;
    }
    
    if (ack != ACK_WAIT && ack != ACK_NO_WAIT) {
        printf("Erro: ack deve ser 1 ou 2\n");
        return;
    }
    
    if (p_prod != 0 && (p_prod < 1 || p_prod > num_estruturas)) {
        if (paradigma_atual == PARADIGMA_MSG) {
            printf("Erro - fila %d nao existe\n", p_prod);
        } else {
            printf("Erro - estrutura %d nao existe\n", p_prod);
        }
        return;
    }
    
    pid = fork();
    if (pid == 0) {
        char p_str[32], n_str[32], ack_str[32], par_str[32], total_str[32];
        sprintf(p_str, "%d", p_prod);
        sprintf(n_str, "%d", n_msgs);
        sprintf(ack_str, "%d", ack);
        sprintf(par_str, "%d", paradigma_atual);
        sprintf(total_str, "%d", num_estruturas);
        
        execl("./produz", "produz", par_str, total_str, p_str, n_str, ack_str, (char *)NULL);
        perror("execl produz");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        adicionar_filho(pid, 'P', p_prod, n_msgs, ack);
        printf("Processo produz (PID %d) criado.\n", pid);
    } else {
        perror("fork");
    }
}

void comando_consume(char *args[]) {
    int p_prod, n_msgs, ack;
    pid_t pid;
    
    if (args[1] == NULL || args[2] == NULL || args[3] == NULL) {
        printf("Erro: uso correto: consome <p_prod> <n_msgs> <ack>\n");
        return;
    }
    
    p_prod = atoi(args[1]);
    n_msgs = atoi(args[2]);
    ack = atoi(args[3]);
    
    if (n_msgs < 1 || n_msgs > MAX_MSGS) {
        printf("Erro: n_msgs deve estar entre 1 e %d\n", MAX_MSGS);
        return;
    }
    
    if (ack != ACK_WAIT && ack != ACK_NO_WAIT) {
        printf("Erro: ack deve ser 1 ou 2\n");
        return;
    }
    
    if (p_prod < 1 || p_prod > num_estruturas) {
        if (paradigma_atual == PARADIGMA_MSG) {
            printf("Erro – fila %d nao existe\n", p_prod);
        } else {
            printf("Erro – estrutura %d nao existe\n", p_prod);
        }
        return;
    }
    
    pid = fork();
    if (pid == 0) {
        char p_str[32], n_str[32], ack_str[32], par_str[32], total_str[32];
        sprintf(p_str, "%d", p_prod);
        sprintf(n_str, "%d", n_msgs);
        sprintf(ack_str, "%d", ack);
        sprintf(par_str, "%d", paradigma_atual);
        sprintf(total_str, "%d", num_estruturas);
        
        execl("./consome", "consome", par_str, total_str, p_str, n_str, ack_str, (char *)NULL);
        perror("execl consome");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        adicionar_filho(pid, 'C', p_prod, n_msgs, ack);
        printf("Processo consome (PID %d) criado.\n", pid);
    } else {
        perror("fork");
    }
}

void comando_shutdown() {
    int i, status;
    struct timeval tempo_fim_mpmc;
    
    printf("\n=== SHUTDOWN ===\n");
    
    /* Terminar todos os processos filhos ativos */
    for (i = 0; i < num_filhos; i++) {
        if (!filhos[i].finalizado) {
            printf("Terminando processo %s PID %d...\n", 
                   filhos[i].tipo == 'P' ? "produz" : "consome", 
                   filhos[i].pid);
            kill(filhos[i].pid, SIGTERM);
        }
    }
    
    /* Aguardar um pouco para finalização */
    usleep(100000);
    
    /* Forçar terminação caso ainda estejam rodando */
    for (i = 0; i < num_filhos; i++) {
        if (!filhos[i].finalizado) {
            if (kill(filhos[i].pid, 0) == 0) {
                printf("Forçando término do processo %s PID %d...\n", 
                       filhos[i].tipo == 'P' ? "produz" : "consome", 
                       filhos[i].pid);
                kill(filhos[i].pid, SIGKILL);
            }
        }
    }
    
    /* Aguardar finalização de todos os filhos e coletar tempos */
    printf("\n=== Tempos de execução dos processos ===\n");
    for (i = 0; i < num_filhos; i++) {
        if (!filhos[i].finalizado) {
            if (waitpid(filhos[i].pid, &status, 0) > 0) {
                gettimeofday(&filhos[i].fim, NULL);
                filhos[i].finalizado = 1;
                printf("Processo %s (PID %d) - estrutura %d, n_msgs %d, ack %d: tempo de execução = ", 
                       filhos[i].tipo == 'P' ? "PRODUZ" : "CONSOME", 
                       filhos[i].pid,
                       filhos[i].estrutura,
                       filhos[i].n_msgs,
                       filhos[i].ack);
                imprimir_tempo(&filhos[i].inicio, &filhos[i].fim);
            }
        } else {
            printf("Processo %s (PID %d) - estrutura %d, n_msgs %d, ack %d: tempo de execução = ", 
                   filhos[i].tipo == 'P' ? "PRODUZ" : "CONSOME", 
                   filhos[i].pid,
                   filhos[i].estrutura,
                   filhos[i].n_msgs,
                   filhos[i].ack);
            imprimir_tempo(&filhos[i].inicio, &filhos[i].fim);
        }
    }
    
    if (num_filhos == 0) {
        printf("Nenhum processo filho foi criado.\n");
    }
    
    /* Atualizar contadores */
    if (paradigma_atual == PARADIGMA_SHM) {
        for (i = 0; i < num_estruturas; i++) {
            if (estruturas[i].shm_addr != NULL && estruturas[i].shm_addr != (void *)-1) {
                shm_header_t *hdr = (shm_header_t *)estruturas[i].shm_addr;
                if (hdr->total_consumidas > 0) {
                    estruturas[i].total_consumidas = hdr->total_consumidas;
                }
            }
        }
    } else if (paradigma_atual == PARADIGMA_MSG) {
        for (i = 0; i < num_estruturas; i++) {
            if (estruturas[i].cnt_addr != NULL && estruturas[i].cnt_addr != (void *)-1) {
                estruturas[i].total_consumidas = *(unsigned long *)estruturas[i].cnt_addr;
            }
        }
    }
    
    /* Remover mecanismos IPC */
    if (paradigma_atual == PARADIGMA_SHM) {
        printf("\n=== Removendo mecanismos IPC ===\n");
        printf("Removendo memórias compartilhadas e semáforos...\n");
        for (i = 0; i < num_estruturas; i++) {
            if (estruturas[i].shm_addr != NULL && estruturas[i].shm_addr != (void *)-1) {
                desanexar_segmento_shm(estruturas[i].shm_addr);
            }
            if (estruturas[i].shmid != -1) {
                remover_segmento_shm(estruturas[i].shmid);
            }
            if (estruturas[i].semid != -1) {
                remover_semaforos_shm(estruturas[i].semid);
            }
        }
        printf("Memórias compartilhadas e semáforos removidos.\n");
    } else if (paradigma_atual == PARADIGMA_MSG) {
        printf("\n=== Removendo mecanismos IPC ===\n");
        printf("Removendo filas de mensagens...\n");
        for (i = 0; i < num_estruturas; i++) {
            if (estruturas[i].msgid != -1) {
                remover_fila_mensagem(estruturas[i].msgid);
            }
            if (estruturas[i].ack_msgid != -1) {
                remover_fila_ack(estruturas[i].ack_msgid);
            }
            if (estruturas[i].cnt_addr != NULL && estruturas[i].cnt_addr != (void *)-1) {
                desanexar_segmento_shm(estruturas[i].cnt_addr);
            }
            if (estruturas[i].cnt_shmid != -1) {
                remover_segmento_shm(estruturas[i].cnt_shmid);
            }
        }
        printf("Filas de mensagens removidas.\n");
    }
    
    /* Remover arquivos chave IPC */
    unlink(MSG_KEY_FILE);
    unlink(SHM_KEY_FILE);
    unlink(SEM_KEY_FILE);
    unlink(ACK_KEY_FILE);
    
    printf("\n=== Número total de mensagens consumidas em cada estrutura ===\n");
    for (i = 0; i < num_estruturas; i++) {
        printf("Estrutura %d: %lu mensagens consumidas\n", 
               estruturas[i].id, estruturas[i].total_consumidas);
    }
    
    gettimeofday(&tempo_fim_mpmc, NULL);
    printf("\n=== Tempo de operação do mpmc ===\n");
    printf("Tempo total: ");
    imprimir_tempo(&tempo_inicio_mpmc, &tempo_fim_mpmc);
    
    if (estruturas) free(estruturas);
    if (filhos) free(filhos);
    
    printf("\nSistema finalizado com sucesso.\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    int p, paradigma;
    char linha[512];
    char *args[12];
    char *token;
    int i;
    
    if (argc < 3) {
        printf("Uso: %s <p> <paradigma>\n", argv[0]);
        printf("  p         : número de produtores/estruturas\n");
        printf("  paradigma : 1 (memória compartilhada com shm+sem) ou 2 (troca de mensagens com msg)\n");
        printf("\nExemplo: %s 4 1\n", argv[0]);
        exit(EXIT_FAILURE);
    }
    
    p = atoi(argv[1]);
    paradigma = atoi(argv[2]);
    
    if (p < 1) {
        printf("Erro: p deve ser maior que 0\n");
        exit(EXIT_FAILURE);
    }
    
    if (paradigma != 1 && paradigma != 2) {
        printf("Erro: paradigma deve ser 1 ou 2\n");
        exit(EXIT_FAILURE);
    }
    
    inicializar_sistema(p, paradigma);
    
    printf("\nSistema MPMC inicializado. Aguardando comandos...\n");
    printf("Digite 'shutdown' para finalizar o sistema.\n\n");
    
    while (1) {
        printf("%s", PROMPT);
        fflush(stdout);
        
        if (fgets(linha, sizeof(linha), stdin) == NULL) {
            break;
        }
        
        linha[strcspn(linha, "\n")] = 0;
        linha[strcspn(linha, "\r")] = 0;
        
        i = 0;
        while (linha[i] == ' ' || linha[i] == '\t') i++;
        if (linha[i] == '\0') {
            continue;
        }
        memmove(linha, linha + i, strlen(linha) + 1 - i);
        
        if (strlen(linha) == 0) {
            continue;
        }
        
        i = 0;
        token = strtok(linha, " \t");
        while (token != NULL && i < 11) {
            args[i++] = token;
            token = strtok(NULL, " \t");
        }
        args[i] = NULL;
        
        if (args[0] == NULL) {
            continue;
        }
        
        if (strcmp(args[0], "produz") == 0) {
            comando_produz(args);
        } else if (strcmp(args[0], "consome") == 0) {
            comando_consume(args);
        } else if (strcmp(args[0], "shutdown") == 0) {
            comando_shutdown();
        } else {
            printf("Comando desconhecido: '%s'\n", args[0]);
            printf("Comandos disponíveis: produz, consome, shutdown\n");
        }
    }
    
    return 0;
}
