#include "common.h"

void erro(char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

/* Instante atual no relógio monotônico, imune a ajustes do relógio do sistema */
void agora(struct timespec *t) {
    clock_gettime(CLOCK_MONOTONIC, t);
}

/* Formata a duração entre dois instantes como "segundos.microssegundos" */
void formatar_duracao(char *buf, size_t tamanho, const struct timespec *inicio, const struct timespec *fim) {
    long segundos = fim->tv_sec - inicio->tv_sec;
    long nanossegundos = fim->tv_nsec - inicio->tv_nsec;
    
    if (nanossegundos < 0) {
        segundos--;
        nanossegundos += 1000000000L;
    }
    snprintf(buf, tamanho, "%ld.%06ld", segundos, nanossegundos / 1000);
}

/* Lê uma lista "a:b,a:b,..." (ou "a,a,...") de identificadores IPC.
 * Retorna a quantidade de itens, ou -1 se a lista for inválida. */
int ler_ids(const char *texto, int *a, int *b, int max) {
    const char *p = texto;
    char *fim;
    int n = 0;

    while (*p != '\0') {
        if (n == max) {
            return -1;
        }
        a[n] = (int)strtol(p, &fim, 10);
        if (fim == p) {
            return -1;
        }
        b[n] = -1;
        if (*fim == ':') {
            p = fim + 1;
            b[n] = (int)strtol(p, &fim, 10);
            if (fim == p) {
                return -1;
            }
        }
        n++;
        if (*fim == ',') {
            fim++;
        } else if (*fim != '\0') {
            return -1;
        }
        p = fim;
    }
    return n;
}

volatile sig_atomic_t terminar_processo = 0;

static void tratar_termino(int sig) {
    terminar_processo = sig;
}

void instalar_encerramento_gracioso(void) {
    int sinais[] = { SIGTERM, SIGINT, SIGHUP, SIGQUIT };
    struct sigaction sa;
    size_t i;

    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = tratar_termino;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;   /* sem SA_RESTART: chamadas bloqueantes retornam EINTR */
    for (i = 0; i < sizeof(sinais) / sizeof(sinais[0]); i++) {
        sigaction(sinais[i], &sa, NULL);
    }
}

/* Imprime o tempo de execução e termina: com o código dado ou, se um sinal de
 * término foi recebido, pelo próprio sinal */
void finalizar_processo(const char *nome, const struct timespec *inicio, int codigo) {
    struct timespec fim;
    char duracao[32];
    int sig = terminar_processo;

    agora(&fim);
    formatar_duracao(duracao, sizeof(duracao), inicio, &fim);
    if (sig) {
        printf("[%s PID %d] Encerrado pelo sinal %d (%s). Tempo de execução: %s segundos\n",
               nome, getpid(), sig, strsignal(sig), duracao);
        fflush(stdout);
        signal(sig, SIG_DFL);
        raise(sig);
    }
    printf("[%s PID %d] Finalizado. Tempo de execução: %s segundos\n", nome, getpid(), duracao);
    exit(codigo);
}
