#include "common.h"
#include "shm_utils.h"
#include "msg_utils.h"
#include "registro.h"
#include <limits.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/select.h>

static estrutura_info_t *estruturas = NULL;
static int num_estruturas = 0;
static int paradigma_atual = 0;
static proc_info_t *filhos = NULL;
static int num_filhos = 0;
static int capacidade_filhos = 0;
static struct timespec tempo_inicio_mpmc;
static int fila_ack = -1;     /* fila de confirmações (paradigma 2) */
static int fila_est = -1;     /* fila de estatísticas de consumo (paradigma 2) */
static char *lista_estruturas = NULL;  /* identificadores IPC repassados ao produz */
static char dir_executaveis[PATH_MAX] = ".";
static pid_t pid_mpmc;

/* Sinais tratados pelo mpmc. Ficam bloqueados o tempo todo e só são liberados
 * durante a espera por comandos (pselect). Os tratadores apenas registram o
 * evento, que é processado no laço principal: não há corrida entre a chegada de
 * um sinal e o início da espera, e nenhum comando (nem o shutdown) é
 * interrompido no meio. */
static const int sinais_tratados[] = { SIGCHLD, SIGINT, SIGTERM, SIGHUP, SIGQUIT };
#define NUM_SINAIS_TRATADOS ((int)(sizeof(sinais_tratados) / sizeof(sinais_tratados[0])))
static sigset_t mascara_original;   /* máscara herdada, restaurada nos filhos */
static sigset_t mascara_espera;     /* máscara durante a espera por comandos */
static sigset_t mascara_filho;      /* máscara herdada + SIGUSR1 (aviso de vez) */
static volatile sig_atomic_t encerrar = 0;

/* produz e consome ficam no mesmo diretório do executável do mpmc */
static void definir_dir_executaveis(void) {
    char caminho[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", caminho, sizeof(caminho) - 1);
    if (n > 0) {
        caminho[n] = '\0';
        char *barra = strrchr(caminho, '/');
        if (barra != NULL) {
            *barra = '\0';
            snprintf(dir_executaveis, sizeof(dir_executaveis), "%s", caminho);
        }
    }
}

/* Converte texto em inteiro, rejeitando valores não numéricos */
static int ler_inteiro(const char *texto, int *valor) {
    char *fim;
    long v;
    errno = 0;
    v = strtol(texto, &fim, 10);
    if (errno != 0 || fim == texto || *fim != '\0' || v < INT_MIN || v > INT_MAX) {
        return -1;
    }
    *valor = (int)v;
    return 0;
}

static void tratar_sigchld(int sig) {
    /* Basta interromper a espera: os filhos são coletados no laço principal */
    (void)sig;
}

static void tratar_termino(int sig) {
    encerrar = sig;
}

static void configurar_sinais(void) {
    struct sigaction sa;
    sigset_t tratados;
    int i;

    sigemptyset(&tratados);
    for (i = 0; i < NUM_SINAIS_TRATADOS; i++) {
        sigaddset(&tratados, sinais_tratados[i]);
    }
    /* SIGUSR1 é o aviso de vez dos consumidores: fica sempre bloqueado no mpmc
     * para que os filhos já nasçam com ele bloqueado e o aviso não se perca */
    sigaddset(&tratados, SIGUSR1);
    sigprocmask(SIG_BLOCK, &tratados, &mascara_original);
    mascara_filho = mascara_original;
    sigaddset(&mascara_filho, SIGUSR1);
    mascara_espera = mascara_filho;
    for (i = 0; i < NUM_SINAIS_TRATADOS; i++) {
        sigdelset(&mascara_espera, sinais_tratados[i]);
    }

    memset(&sa, 0, sizeof(sa));
    sigemptyset(&sa.sa_mask);
    sa.sa_handler = tratar_sigchld;
    sa.sa_flags = SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    sa.sa_handler = tratar_termino;
    sa.sa_flags = 0;
    for (i = 0; i < NUM_SINAIS_TRATADOS; i++) {
        if (sinais_tratados[i] != SIGCHLD) {
            sigaction(sinais_tratados[i], &sa, NULL);
        }
    }
    /* Escrever num pipe fechado (ex.: saída redirecionada para head) não deve
     * matar o mpmc antes de remover os recursos */
    signal(SIGPIPE, SIG_IGN);
}

/* Executado no filho entre o fork e o exec: restaura o tratamento padrão dos
 * sinais e a máscara herdada (um SIGTERM recebido nesse intervalo ficou
 * pendente e agora encerra o filho normalmente), mantendo SIGUSR1 bloqueado, e
 * faz o filho terminar junto com o mpmc, mesmo que este seja morto com SIGKILL */
static void preparar_filho(void) {
    struct sigaction padrao;
    int i;

    memset(&padrao, 0, sizeof(padrao));
    padrao.sa_handler = SIG_DFL;
    sigemptyset(&padrao.sa_mask);
    for (i = 0; i < NUM_SINAIS_TRATADOS; i++) {
        sigaction(sinais_tratados[i], &padrao, NULL);
    }
    sigaction(SIGPIPE, &padrao, NULL);
    prctl(PR_SET_PDEATHSIG, SIGTERM);
    if (getppid() != pid_mpmc) {
        _exit(EXIT_FAILURE);
    }
    sigprocmask(SIG_SETMASK, &mascara_filho, NULL);
}

/* Cria um processo filho que executa o programa nome, do diretório do mpmc */
static pid_t criar_filho(const char *nome, char *argv[]) {
    char programa[PATH_MAX + 16];
    pid_t pid;

    snprintf(programa, sizeof(programa), "%s/%s", dir_executaveis, nome);
    pid = fork();
    if (pid == 0) {
        preparar_filho();
        execv(programa, argv);
        perror(programa);
        _exit(EXIT_FAILURE);
    }
    if (pid == -1) {
        perror("fork");
    }
    return pid;
}

/* Reserva uma entrada na tabela de filhos, que cresce conforme necessário */
static proc_info_t *reservar_filho(void) {
    if (num_filhos == capacidade_filhos) {
        int nova = capacidade_filhos > 0 ? capacidade_filhos * 2 : 64;
        proc_info_t *tabela = (proc_info_t *)realloc(filhos, (size_t)nova * sizeof(proc_info_t));
        if (tabela == NULL) {
            printf("Erro: memória insuficiente para registrar um novo processo\n");
            return NULL;
        }
        filhos = tabela;
        capacidade_filhos = nova;
    }
    memset(&filhos[num_filhos], 0, sizeof(proc_info_t));
    filhos[num_filhos].ack_semid = -1;
    return &filhos[num_filhos];
}

/* Registra o término de um filho: instante, status e tempo de CPU */
static void registrar_termino(pid_t pid, int status, const struct rusage *uso) {
    int i;
    for (i = 0; i < num_filhos; i++) {
        if (filhos[i].pid == pid && !filhos[i].finalizado) {
            agora(&filhos[i].fim);
            filhos[i].status = status;
            timeradd(&uso->ru_utime, &uso->ru_stime, &filhos[i].cpu);
            filhos[i].finalizado = 1;
            return;
        }
    }
}

/* Coleta, sem bloquear, os filhos que já terminaram */
static void coletar_terminados(void) {
    struct rusage uso;
    int status;
    pid_t pid;
    while ((pid = wait4(-1, &status, WNOHANG, &uso)) > 0) {
        registrar_termino(pid, status, &uso);
    }
}

static int filhos_ativos(void) {
    int i, ativos = 0;
    for (i = 0; i < num_filhos; i++) {
        if (!filhos[i].finalizado) ativos++;
    }
    return ativos;
}

/* Soma as quantidades informadas pelos consumidores que terminaram (paradigma 2) */
static void drenar_estatisticas(void) {
    contador_msg_t c;
    if (fila_est == -1) {
        return;
    }
    while (msgrcv(fila_est, &c, sizeof(c) - sizeof(long), 0, IPC_NOWAIT) != -1) {
        if (c.mtype >= 1 && c.mtype <= num_estruturas) {
            estruturas[c.mtype - 1].total_consumidas += c.quantidade;
        }
    }
}

/* Remove os semáforos de confirmação de produtores que já terminaram */
static void liberar_semaforos_ack(void) {
    int i;
    for (i = 0; i < num_filhos; i++) {
        if (filhos[i].finalizado && filhos[i].ack_semid != -1) {
            remover_semaforos(filhos[i].ack_semid);
            filhos[i].ack_semid = -1;
        }
    }
}

/* Consumidores bloqueantes (ack=1) de uma mesma estrutura são atendidos por
 * ordem de chegada: só um tem a vez de cada estrutura, e os demais esperam o
 * SIGUSR1 do mpmc. A vez passa ao próximo quando o atual termina, por qualquer
 * motivo (inclusive kill -9), pois o mpmc observa o término pelo SIGCHLD. */
static int vez_ocupada(int estrutura, int incluir_espera) {
    int i;
    for (i = 0; i < num_filhos; i++) {
        if (filhos[i].tipo == 'C' && filhos[i].estrutura == estrutura && !filhos[i].finalizado &&
            (filhos[i].tem_vez || (incluir_espera && filhos[i].aguarda_vez))) {
            return 1;
        }
    }
    return 0;
}

static void conceder_vez(void) {
    int i;
    for (i = 0; i < num_filhos; i++) {
        proc_info_t *c = &filhos[i];
        if (c->tipo == 'C' && c->aguarda_vez && !c->tem_vez && !c->finalizado &&
            !vez_ocupada(c->estrutura, 0)) {
            c->tem_vez = 1;
            kill(c->pid, SIGUSR1);
        }
    }
}

/* Trata o que ocorreu desde a última verificação: filhos que terminaram, vez
 * dos consumidores, semáforos de confirmação que deixaram de ser necessários e
 * quantidades informadas pelos consumidores */
static void processar_eventos(void) {
    coletar_terminados();
    conceder_vez();
    liberar_semaforos_ack();
    drenar_estatisticas();
}

/* Tempo de CPU (ns) já consumido pelo processo, ou -1 se ele estiver em
 * execução (estado R ou D). Um processo que já terminou conta como parado. */
static long long cpu_processo(pid_t pid) {
    char caminho[64], buf[512], estado;
    unsigned long utime, stime;
    unsigned long long ns;
    size_t n;
    char *p;
    FILE *f;

    snprintf(caminho, sizeof(caminho), "/proc/%d/stat", (int)pid);
    f = fopen(caminho, "r");
    if (f == NULL) {
        return 0;
    }
    n = fread(buf, 1, sizeof(buf) - 1, f);
    fclose(f);
    buf[n] = '\0';
    /* O nome do programa, entre parênteses, pode conter espaços */
    p = strrchr(buf, ')');
    if (p == NULL || sscanf(p + 2, "%c %*d %*d %*d %*d %*d %*u %*u %*u %*u %*u %lu %lu",
                            &estado, &utime, &stime) != 3) {
        return 0;
    }
    if (estado == 'R' || estado == 'D') {
        return -1;
    }
    snprintf(caminho, sizeof(caminho), "/proc/%d/schedstat", (int)pid);
    f = fopen(caminho, "r");
    if (f != NULL) {
        int lido = fscanf(f, "%llu", &ns);
        fclose(f);
        if (lido == 1) {
            return (long long)ns;
        }
    }
    return (long long)(utime + stime) * 10000000LL;   /* em ticks de 10 ms */
}

/* Antes de encerrar os filhos, espera (até 2 s) que todos estejam bloqueados,
 * sem consumir CPU. Quando os comandos chegam em sequência rápida (colados no
 * terminal ou lidos de um arquivo), os processos recém-criados ainda não tiveram
 * tempo de produzir e consumir; esperar que o sistema se estabilize dá o mesmo
 * resultado de comandos digitados um a um. */
static void aguardar_estabilidade(void) {
    long long anterior = -1;
    int estaveis = 0, tentativas, i;

    for (tentativas = 0; tentativas < 100 && estaveis < 3; tentativas++) {
        long long total = 0;

        processar_eventos();
        if (filhos_ativos() == 0) {
            return;
        }
        for (i = 0; i < num_filhos && total >= 0; i++) {
            if (!filhos[i].finalizado) {
                long long cpu = cpu_processo(filhos[i].pid);
                total = (cpu < 0) ? -1 : total + cpu;
            }
        }
        if (total >= 0 && total == anterior) {
            estaveis++;
        } else {
            estaveis = 0;
        }
        anterior = total;
        usleep(20000);
    }
}

static void imprimir_processo(const proc_info_t *f) {
    char situacao[96], duracao[32];
    if (WIFEXITED(f->status)) {
        snprintf(situacao, sizeof(situacao), "encerrado, código %d", WEXITSTATUS(f->status));
    } else if (WIFSIGNALED(f->status)) {
        snprintf(situacao, sizeof(situacao), "terminado pelo sinal %d (%s)",
                 WTERMSIG(f->status), strsignal(WTERMSIG(f->status)));
    } else {
        snprintf(situacao, sizeof(situacao), "situação desconhecida");
    }
    formatar_duracao(duracao, sizeof(duracao), &f->inicio, &f->fim);
    printf("Processo %s (PID %d) - estrutura %d, n_msgs %d, ack %d [%s]: tempo de execução = %s segundos "
           "(CPU: %ld.%06ld segundos)\n",
           f->tipo == 'P' ? "PRODUZ" : "CONSOME", f->pid, f->estrutura, f->n_msgs, f->ack, situacao,
           duracao, (long)f->cpu.tv_sec, (long)f->cpu.tv_usec);
}

/* Remove todos os recursos IPC criados pelo mpmc */
static void liberar_ipcs(void) {
    int i;
    for (i = 0; i < num_estruturas; i++) {
        desanexar_segmento_shm(estruturas[i].shm_addr);
        estruturas[i].shm_addr = NULL;
        remover_segmento_shm(estruturas[i].shmid);
        remover_semaforos(estruturas[i].semid);
        remover_fila(estruturas[i].msgid);
    }
    remover_fila(fila_ack);
    remover_fila(fila_est);
}

static void inicializar_sistema(int p, int paradigma) {
    int i, falhou = 0, capacidade = 0;
    char *pos;

    num_estruturas = p;
    paradigma_atual = paradigma;

    estruturas = (estrutura_info_t *)calloc(num_estruturas, sizeof(estrutura_info_t));
    /* Cada item da lista tem no máximo 22 caracteres ("shmid:semid,") */
    lista_estruturas = (char *)malloc((size_t)num_estruturas * 24 + 1);

    if (estruturas == NULL || lista_estruturas == NULL) {
        erro("malloc/calloc");
    }

    for (i = 0; i < num_estruturas; i++) {
        estruturas[i].id = i + 1;
        estruturas[i].shmid = -1;
        estruturas[i].semid = -1;
        estruturas[i].msgid = -1;
        estruturas[i].shm_addr = NULL;
        estruturas[i].total_consumidas = 0;
    }

    agora(&tempo_inicio_mpmc);

    if (paradigma == PARADIGMA_SHM) {
        printf("Inicializando sistema com %d memórias compartilhadas (paradigma 1)...\n", p);
        printf("Cada segmento terá tamanho de %d bytes.\n", SHM_SIZE);
        for (i = 0; i < num_estruturas; i++) {
            estruturas[i].shmid = criar_segmento_shm(SHM_SIZE);
            registrar_ipc(REG_SHM, estruturas[i].shmid);
            estruturas[i].semid = criar_semaforos(3);
            registrar_ipc(REG_SEM, estruturas[i].semid);
            if (estruturas[i].shmid == -1 || estruturas[i].semid == -1 ||
                anexar_segmento_shm(estruturas[i].shmid, &estruturas[i].shm_addr) == -1) {
                estruturas[i].shm_addr = NULL;
                falhou = 1;
                break;
            }
            shm_header_t *hdr = (shm_header_t *)estruturas[i].shm_addr;
            hdr->cabeca = 0;
            hdr->cauda = 0;
            hdr->count = 0;
            hdr->total_produzidas = 0;
            hdr->total_consumidas = 0;
            hdr->capacidade = (int)((SHM_SIZE - sizeof(shm_header_t)) / sizeof(mensagem_t));
            hdr->iniciado = 1;
            capacidade = hdr->capacidade;
            inicializar_semaforos(estruturas[i].semid, hdr->capacidade);
        }
    } else {
        printf("Inicializando sistema com %d filas de mensagens (paradigma 2)...\n", p);
        for (i = 0; i < num_estruturas && !falhou; i++) {
            estruturas[i].msgid = criar_fila();
            registrar_ipc(REG_FILA, estruturas[i].msgid);
            falhou = (estruturas[i].msgid == -1);
        }
        if (!falhou) {
            /* Fila única de confirmações: o mtype identifica o produtor */
            fila_ack = criar_fila();
            registrar_ipc(REG_FILA, fila_ack);
            /* Fila de estatísticas: cada consumidor informa quantas mensagens consumiu */
            fila_est = criar_fila();
            registrar_ipc(REG_FILA, fila_est);
            falhou = (fila_ack == -1 || fila_est == -1);
        }
    }

    if (falhou) {
        printf("Erro: não foi possível criar os recursos IPC.\n");
        liberar_ipcs();
        encerrar_registro();
        exit(EXIT_FAILURE);
    }

    /* Identificadores de todas as estruturas, repassados ao produz (modo striped) */
    pos = lista_estruturas;
    *pos = '\0';
    for (i = 0; i < num_estruturas; i++) {
        if (paradigma == PARADIGMA_SHM) {
            pos += sprintf(pos, "%s%d:%d", i > 0 ? "," : "", estruturas[i].shmid, estruturas[i].semid);
        } else {
            pos += sprintf(pos, "%s%d", i > 0 ? "," : "", estruturas[i].msgid);
        }
    }

    if (paradigma == PARADIGMA_SHM) {
        printf("Memórias compartilhadas e semáforos criados com sucesso (capacidade de %d mensagens por estrutura).\n",
               capacidade);
    } else {
        printf("Filas de mensagens, fila de confirmações e fila de estatísticas criadas com sucesso.\n");
    }
}

static void comando_produz(char *args[]) {
    int p_prod, n_msgs, ack;
    int ack_semid = -1;
    proc_info_t *f;
    pid_t pid;
    char par_str[16], p_str[16], n_str[16], ack_str[16], ack_id_str[16], fila_ack_str[16];

    if (ler_inteiro(args[1], &p_prod) == -1 || ler_inteiro(args[2], &n_msgs) == -1 ||
        ler_inteiro(args[3], &ack) == -1) {
        printf("Erro: os argumentos devem ser números inteiros\n");
        return;
    }

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

    f = reservar_filho();
    if (f == NULL) {
        return;
    }

    /* Paradigma 1 com ack=1: semáforo privado (valor inicial 0) que os consumidores
     * incrementam a cada mensagem deste produtor retirada do buffer */
    if (paradigma_atual == PARADIGMA_SHM && ack == ACK_WAIT) {
        union semun arg;
        ack_semid = criar_semaforos(1);
        if (ack_semid == -1) {
            return;
        }
        registrar_ipc(REG_SEM, ack_semid);
        arg.val = 0;
        semctl(ack_semid, 0, SETVAL, arg);
    }

    sprintf(par_str, "%d", paradigma_atual);
    sprintf(p_str, "%d", p_prod);
    sprintf(n_str, "%d", n_msgs);
    sprintf(ack_str, "%d", ack);
    /* Destino das confirmações: o semáforo do produtor (paradigma 1) ou o seu
     * número nesta execução, usado como mtype na fila de confirmações (paradigma 2) */
    sprintf(ack_id_str, "%d", paradigma_atual == PARADIGMA_SHM ? ack_semid : num_filhos + 1);
    sprintf(fila_ack_str, "%d", fila_ack);
    char *argv_filho[] = { "produz", par_str, p_str, n_str, ack_str, ack_id_str, fila_ack_str,
                           lista_estruturas, NULL };

    pid = criar_filho("produz", argv_filho);
    if (pid == -1) {
        remover_semaforos(ack_semid);
        return;
    }
    f->pid = pid;
    f->tipo = 'P';
    f->estrutura = p_prod;
    f->n_msgs = n_msgs;
    f->ack = ack;
    f->ack_semid = ack_semid;
    agora(&f->inicio);
    num_filhos++;
    printf("Processo produz (PID %d) criado.\n", pid);
}

static void comando_consome(char *args[]) {
    int p_prod, n_msgs, ack;
    proc_info_t *f;
    pid_t pid;
    char par_str[16], p_str[16], n_str[16], ack_str[16], fila_ack_str[16], fila_est_str[16];
    char estrutura_str[32];
    int aguarda_vez;

    if (ler_inteiro(args[1], &p_prod) == -1 || ler_inteiro(args[2], &n_msgs) == -1 ||
        ler_inteiro(args[3], &ack) == -1) {
        printf("Erro: os argumentos devem ser números inteiros\n");
        return;
    }

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
            printf("Erro - fila %d nao existe\n", p_prod);
        } else {
            printf("Erro - estrutura %d nao existe\n", p_prod);
        }
        return;
    }

    f = reservar_filho();
    if (f == NULL) {
        return;
    }

    /* Consumidor bloqueante com outro na frente na mesma estrutura: espera a vez */
    aguarda_vez = (ack == ACK_WAIT && vez_ocupada(p_prod, 1));

    estrutura_info_t *e = &estruturas[p_prod - 1];
    sprintf(par_str, "%d", paradigma_atual);
    sprintf(p_str, "%d", p_prod);
    sprintf(n_str, "%d", n_msgs);
    sprintf(ack_str, "%d", ack);
    sprintf(fila_ack_str, "%d", fila_ack);
    sprintf(fila_est_str, "%d", fila_est);
    if (paradigma_atual == PARADIGMA_SHM) {
        sprintf(estrutura_str, "%d:%d", e->shmid, e->semid);
    } else {
        sprintf(estrutura_str, "%d", e->msgid);
    }
    char *argv_filho[] = { "consome", par_str, p_str, n_str, ack_str, aguarda_vez ? "1" : "0",
                           fila_ack_str, fila_est_str, estrutura_str, NULL };

    pid = criar_filho("consome", argv_filho);
    if (pid == -1) {
        return;
    }
    f->pid = pid;
    f->tipo = 'C';
    f->estrutura = p_prod;
    f->n_msgs = n_msgs;
    f->ack = ack;
    f->aguarda_vez = aguarda_vez;
    f->tem_vez = (ack == ACK_WAIT && !aguarda_vez);
    agora(&f->inicio);
    num_filhos++;
    printf("Processo consome (PID %d) criado.\n", pid);
}

/* Executado com os sinais tratados bloqueados (como todo o código fora da
 * espera por comandos): nenhuma etapa é interrompida */
static void comando_shutdown(void) {
    int i, tentativas, status;
    struct rusage uso;
    struct timespec tempo_fim_mpmc;
    char duracao[32];

    aguardar_estabilidade();

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

    /* Aguardar até 500 ms pela finalização */
    for (tentativas = 0; tentativas < 50 && filhos_ativos() > 0; tentativas++) {
        usleep(10000);
        processar_eventos();
    }

    /* Forçar terminação caso ainda estejam rodando */
    for (i = 0; i < num_filhos; i++) {
        if (!filhos[i].finalizado) {
            printf("Forçando término do processo %s PID %d...\n",
                   filhos[i].tipo == 'P' ? "produz" : "consome",
                   filhos[i].pid);
            kill(filhos[i].pid, SIGKILL);
            pid_t pid;
            while ((pid = wait4(filhos[i].pid, &status, 0, &uso)) == -1 && errno == EINTR) {
            }
            if (pid > 0) {
                registrar_termino(pid, status, &uso);
            }
        }
    }

    processar_eventos();

    printf("\n=== Tempos de execução dos processos ===\n");
    for (i = 0; i < num_filhos; i++) {
        imprimir_processo(&filhos[i]);
    }

    if (num_filhos == 0) {
        printf("Nenhum processo filho foi criado.\n");
    }

    /* No paradigma 1 os consumidores contabilizam no cabeçalho do segmento; no
     * paradigma 2 as quantidades informadas já foram somadas */
    if (paradigma_atual == PARADIGMA_SHM) {
        for (i = 0; i < num_estruturas; i++) {
            if (estruturas[i].shm_addr != NULL) {
                estruturas[i].total_consumidas = ((shm_header_t *)estruturas[i].shm_addr)->total_consumidas;
            }
        }
    }

    /* Remover mecanismos IPC */
    printf("\n=== Removendo mecanismos IPC ===\n");
    liberar_ipcs();
    encerrar_registro();
    if (paradigma_atual == PARADIGMA_SHM) {
        printf("Memórias compartilhadas e semáforos removidos.\n");
    } else {
        printf("Filas de mensagens removidas.\n");
    }

    printf("\n=== Número total de mensagens consumidas em cada estrutura ===\n");
    for (i = 0; i < num_estruturas; i++) {
        printf("Estrutura %d: %lu mensagens consumidas\n",
               estruturas[i].id, estruturas[i].total_consumidas);
    }

    agora(&tempo_fim_mpmc);
    formatar_duracao(duracao, sizeof(duracao), &tempo_inicio_mpmc, &tempo_fim_mpmc);
    printf("\n=== Tempo de operação do mpmc ===\n");
    printf("Tempo total: %s segundos\n", duracao);

    free(estruturas);
    free(filhos);
    free(lista_estruturas);

    printf("\nSistema finalizado com sucesso.\n");
    exit(EXIT_SUCCESS);
}

/* Iniciado com '&' num terminal, o mpmc fica em segundo plano e é parado pelo
 * sinal SIGTTIN quando tenta ler o teclado; avisa como trazê-lo de volta */
static void avisar_segundo_plano(void) {
    static int avisado = 0;
    if (!avisado && isatty(STDIN_FILENO) && tcgetpgrp(STDIN_FILENO) != getpgrp()) {
        printf("\n[mpmc em segundo plano: use 'fg' para digitar os comandos]\n");
        avisado = 1;
    }
}

/* Separa a linha em palavras. O '&' no fim dos comandos é aceito com ou sem
 * espaço antes e ignorado, pois produz e consome sempre executam de forma
 * concorrente; um prompt ">>" copiado junto com o comando também é ignorado. */
#define SEPARADORES " \t\r\n&"
static void processar_linha(char *linha) {
    char *args[5] = { NULL };
    char *token;
    int n = 0;

    while (*linha == ' ' || *linha == '\t' || *linha == '>') {
        linha++;
    }
    for (token = strtok(linha, SEPARADORES); token != NULL; token = strtok(NULL, SEPARADORES)) {
        if (n < 4) {
            args[n] = token;
        }
        n++;
    }

    if (n == 0) {
        return;
    }

    if (strcmp(args[0], "produz") == 0 || strcmp(args[0], "consome") == 0) {
        if (n != 4) {
            printf("Erro: uso correto: %s <p_prod> <n_msgs> <ack>\n", args[0]);
            return;
        }
        if (args[0][0] == 'p') {
            comando_produz(args);
        } else {
            comando_consome(args);
        }
    } else if (strcmp(args[0], "shutdown") == 0) {
        if (n != 1) {
            printf("Erro: uso correto: shutdown\n");
            return;
        }
        comando_shutdown();
    } else {
        printf("Comando desconhecido: '%s'\n", args[0]);
        printf("Comandos disponíveis: produz, consome, shutdown\n");
    }
}

int main(int argc, char *argv[]) {
    int p, paradigma;
    char linha[512];
    int mostrar_prompt = 1;

    if (argc < 3) {
        printf("Uso: %s <p> <paradigma>\n", argv[0]);
        printf("  p         : número de produtores/estruturas (1 a %d)\n", MAX_PROD);
        printf("  paradigma : 1 (memória compartilhada com shm+sem) ou 2 (troca de mensagens com msg)\n");
        printf("\nExemplo: %s 4 1\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    if (ler_inteiro(argv[1], &p) == -1 || ler_inteiro(argv[2], &paradigma) == -1) {
        printf("Erro: p e paradigma devem ser números inteiros\n");
        exit(EXIT_FAILURE);
    }

    if (p < 1 || p > MAX_PROD) {
        printf("Erro: p deve estar entre 1 e %d\n", MAX_PROD);
        exit(EXIT_FAILURE);
    }

    if (paradigma != PARADIGMA_SHM && paradigma != PARADIGMA_MSG) {
        printf("Erro: paradigma deve ser 1 ou 2\n");
        exit(EXIT_FAILURE);
    }

    pid_mpmc = getpid();
    setvbuf(stdout, NULL, _IOLBF, 0);
    /* Sem buffer na entrada: o pselect observa o descritor, e nenhum comando
     * pode ficar retido no buffer do stdio sem ser processado */
    setvbuf(stdin, NULL, _IONBF, 0);
    /* Antes de criar qualquer recurso: um sinal recebido durante a inicialização
     * fica pendente e leva ao shutdown, que remove o que já foi criado */
    configurar_sinais();
    limpar_execucoes_anteriores();
    iniciar_registro();
    definir_dir_executaveis();
    inicializar_sistema(p, paradigma);

    printf("\nSistema MPMC inicializado. Aguardando comandos...\n");
    printf("Digite 'shutdown' para finalizar o sistema.\n\n");

    while (1) {
        fd_set leitura;

        processar_eventos();
        if (encerrar) {
            printf("\nSinal %d (%s) recebido.\n", (int)encerrar, strsignal(encerrar));
            comando_shutdown();
        }

        if (mostrar_prompt) {
            avisar_segundo_plano();
            printf("%s", PROMPT);
            fflush(stdout);
            mostrar_prompt = 0;
        }

        /* Os sinais tratados só são liberados durante esta espera */
        FD_ZERO(&leitura);
        FD_SET(STDIN_FILENO, &leitura);
        if (pselect(STDIN_FILENO + 1, &leitura, NULL, NULL, NULL, &mascara_espera) == -1) {
            if (errno != EINTR) {
                perror("pselect");
                comando_shutdown();
            }
            continue;
        }

        mostrar_prompt = 1;
        if (fgets(linha, sizeof(linha), stdin) == NULL) {
            /* EOF na entrada: encerra liberando os recursos */
            printf("\nFim da entrada.\n");
            comando_shutdown();
        }
        if (strchr(linha, '\n') == NULL && !feof(stdin)) {
            /* Linha maior que o buffer: descarta o restante para que ele não
             * seja interpretado como outro comando */
            int c;
            while ((c = getchar()) != EOF && c != '\n') {
            }
            printf("Erro: linha muito longa (máximo de %d caracteres)\n", (int)sizeof(linha) - 2);
            continue;
        }
        processar_linha(linha);
    }

    return 0;
}
