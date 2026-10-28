#include "common.h"
#include "shm_utils.h"
#include "msg_utils.h"

#define MAX_FILHOS 2048

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
    
    estruturas = (estrutura_info_t *)malloc(num_estruturas * sizeof(estrutura_info_t));
    filhos = (proc_info_t *)malloc(MAX_FILHOS * sizeof(proc_info_t));
    
    gettimeofday(&tempo_inicio_mpmc, NULL);
    
    if (paradigma == PARADIGMA_SHM) {
        printf("Inicializando sistema com %d memórias compartilhadas (paradigma 1)...\n", p);
        for (i = 0; i < num_estruturas; i++) {
            estruturas[i].id = i + 1;
            estruturas[i].tipo = PARADIGMA_SHM;
            estruturas[i].shmid = criar_segmento_shm(i + 1);
            estruturas[i].semid = criar_semaforos_shm(i + 1);
            estruturas[i].msgid = -1;
            estruturas[i].shm_addr = NULL;
            estruturas[i].total_consumidas = 0;
            estruturas[i].total_produzidas = 0;
            anexar_segmento_shm(estruturas[i].shmid, &estruturas[i].shm_addr);
        }
        printf("Memórias compartilhadas criadas com sucesso (tamanho: %d bytes).\n", SHM_SIZE);
    } else if (paradigma == PARADIGMA_MSG) {
        printf("Inicializando sistema com %d filas de mensagens (paradigma 2)...\n", p);
        for (i = 0; i < num_estruturas; i++) {
            estruturas[i].id = i + 1;
            estruturas[i].tipo = PARADIGMA_MSG;
            estruturas[i].shmid = -1;
            estruturas[i].semid = -1;
            estruturas[i].msgid = criar_fila_mensagem(i + 1);
            estruturas[i].shm_addr = NULL;
            estruturas[i].total_consumidas = 0;
            estruturas[i].total_produzidas = 0;
        }
        printf("Filas de mensagens criadas com sucesso.\n");
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
        return;
    }
    
    p_prod = atoi(args[1]);
    n_msgs = atoi(args[2]);
    ack = atoi(args[3]);
    
    /* Validação */
    if (n_msgs < 1 || n_msgs > 1024) {
        printf("Erro: n_msgs deve estar entre 1 e 1024\n");
        return;
    }
    
    if (ack != 1 && ack != 2) {
        printf("Erro: ack deve ser 1 ou 2\n");
        return;
    }
    
    if (p_prod != 0 && (p_prod < 1 || p_prod > num_estruturas)) {
        if (paradigma_atual == PARADIGMA_MSG) {
            printf("Erro – fila %d nao existe\n", p_prod);
        } else {
            printf("Erro – estrutura %d nao existe\n", p_prod);
        }
        return;
    }
    
    pid = fork();
    if (pid == 0) {
        /* Processo filho - executar produz */
        char p_str[16], n_str[16], ack_str[16], par_str[16], total_str[16];
        sprintf(p_str, "%d", p_prod);
        sprintf(n_str, "%d", n_msgs);
        sprintf(ack_str, "%d", ack);
        sprintf(par_str, "%d", paradigma_atual);
        sprintf(total_str, "%d", num_estruturas);
        
        execl("./produz", "produz", par_str, total_str, p_str, n_str, ack_str, NULL);
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
    
    /* Validação */
    if (n_msgs < 1 || n_msgs > 1024) {
        printf("Erro: n_msgs deve estar entre 1 e 1024\n");
        return;
    }
    
    if (ack != 1 && ack != 2) {
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
        /* Processo filho - executar consome */
        char p_str[16], n_str[16], ack_str[16], par_str[16], total_str[16];
        sprintf(p_str, "%d", p_prod);
        sprintf(n_str, "%d", n_msgs);
        sprintf(ack_str, "%d", ack);
        sprintf(par_str, "%d", paradigma_atual);
        sprintf(total_str, "%d", num_estruturas);
        
        execl("./consome", "consome", par_str, total_str, p_str, n_str, ack_str, NULL);
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
    
    printf("\nIniciando shutdown...\n");
    
    /* Terminar todos os processos filhos ativos */
    for (i = 0; i < num_filhos; i++) {
        if (!filhos[i].finalizado) {
            printf("Terminando processo %s PID %d...\n", 
                   filhos[i].tipo == 'P' ? "produz" : "consome", 
                   filhos[i].pid);
            kill(filhos[i].pid, SIGTERM);
        }
    }
    
    /* Aguardar finalização de todos os filhos */
    for (i = 0; i < num_filhos; i++) {
        if (!filhos[i].finalizado) {
            waitpid(filhos[i].pid, &status, 0);
            gettimeofday(&filhos[i].fim, NULL);
            filhos[i].finalizado = 1;
            printf("Processo %s (PID %d) finalizado. Tempo de execução: ", 
                   filhos[i].tipo == 'P' ? "produz" : "consome", 
                   filhos[i].pid);
            imprimir_tempo(&filhos[i].inicio, &filhos[i].fim);
        }
    }
    
    /* Remover mecanismos IPC */
    if (paradigma_atual == PARADIGMA_SHM) {
        printf("\nRemovendo memórias compartilhadas e semáforos...\n");
        for (i = 0; i < num_estruturas; i++) {
            if (estruturas[i].shm_addr != NULL) {
                desanexar_segmento_shm(estruturas[i].shm_addr);
            }
            if (estruturas[i].shmid != -1) {
                remover_segmento_shm(estruturas[i].shmid);
            }
            if (estruturas[i].semid != -1) {
                remover_semaforos_shm(estruturas[i].semid);
            }
        }
    } else if (paradigma_atual == PARADIGMA_MSG) {
        printf("\nRemovendo filas de mensagens...\n");
        for (i = 0; i < num_estruturas; i++) {
            if (estruturas[i].msgid != -1) {
                remover_fila_mensagem(estruturas[i].msgid);
            }
        }
    }
    
    /* Imprimir número total de mensagens consumidas em cada estrutura */
    printf("\n=== Mensagens consumidas por estrutura ===\n");
    for (i = 0; i < num_estruturas; i++) {
        printf("Estrutura %d: %lu mensagens consumidas\n", 
               estruturas[i].id, estruturas[i].total_consumidas);
    }
    
    /* Imprimir tempo de operação do mpmc */
    gettimeofday(&tempo_fim_mpmc, NULL);
    printf("\n=== Tempo de operação do mpmc ===\n");
    printf("Tempo total: ");
    imprimir_tempo(&tempo_inicio_mpmc, &tempo_fim_mpmc);
    
    if (estruturas) free(estruturas);
    if (filhos) free(filhos);
    
    printf("\nSistema finalizado.\n");
    exit(EXIT_SUCCESS);
}

int main(int argc, char *argv[]) {
    int p, paradigma;
    char linha[256];
    char *args[10];
    char *token;
    int i;
    
    if (argc < 3) {
        printf("Uso: %s <p> <paradigma> [&]\n", argv[0]);
        printf("  p: número de produtores/estruturas\n");
        printf("  paradigma: 1 (memória compartilhada) ou 2 (troca de mensagens)\n");
        exit(EXIT_FAILURE);
    }
    
    p = atoi(argv[1]);
    paradigma = atoi(argv[2]);
    
    if (p < 1) {
        printf("Erro: p deve ser maior que 0\n");
        exit(EXIT_FAILURE);
    }
    
    inicializar_sistema(p, paradigma);
    
    while (1) {
        printf(PROMPT);
        fflush(stdout);
        
        if (fgets(linha, sizeof(linha), stdin) == NULL) {
            break;
        }
        
        linha[strcspn(linha, "\n")] = 0;
        linha[strcspn(linha, "\r")] = 0;
        
        if (strlen(linha) == 0) {
            continue;
        }
        
        /* Parse argumentos */
        i = 0;
        token = strtok(linha, " \t");
        while (token != NULL && i < 9) {
            args[i++] = token;
            token = strtok(NULL, " \t");
        }
        args[i] = NULL;
        
        /* Processar comandos */
        if (strcmp(args[0], "produz") == 0) {
            comando_produz(args);
        } else if (strcmp(args[0], "consome") == 0) {
            comando_consume(args);
        } else if (strcmp(args[0], "shutdown") == 0) {
            comando_shutdown();
        } else {
            printf("Comando desconhecido: %s\n", args[0]);
            printf("Comandos disponíveis: produz, consome, shutdown\n");
        }
    }
    
    return 0;
}
