#ifndef COMMON_H
#define COMMON_H

/* Habilita as interfaces POSIX/GNU usadas (sigaction, pselect, wait4, prctl...)
 * mesmo quando o código é compilado com -std=c99 ou -std=c11 */
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/time.h>
#include <sys/wait.h>
#include <signal.h>
#include <time.h>
#include <errno.h>

#include "config.h"

/* Estrutura de mensagem conforme especificação */
typedef struct {
    long mtype;
    pid_t pid_produtor;
    unsigned long seq;        /* número sequencial da mensagem para o processo emissor */
    struct timeval tempo_envio;
    int ack;                  /* modo do produtor: ACK_WAIT pede confirmação */
    int ack_id;               /* destino da confirmação: semáforo do produtor (paradigma 1)
                                 ou número do produtor na fila de confirmações (paradigma 2) */
} mensagem_t;

/* Contador de mensagens (paradigma 2). Na fila de confirmações, mtype é o
 * número do produtor; na fila de estatísticas, a estrutura de onde o
 * consumidor retirou as mensagens. */
typedef struct {
    long mtype;
    int quantidade;
} contador_msg_t;

/* Informações de cada estrutura (memória compartilhada ou fila de mensagens) */
typedef struct {
    int id;                   /* índice 1-based */
    int shmid;                /* para shm */
    int semid;                /* para semáforos */
    int msgid;                /* para fila de mensagens */
    void *shm_addr;           /* endereço anexado */
    unsigned long total_consumidas;
} estrutura_info_t;

/* Informações dos processos filhos */
typedef struct {
    pid_t pid;
    char tipo;                /* 'P' para produz, 'C' para consome */
    int estrutura;
    int n_msgs;
    int ack;
    struct timespec inicio;   /* relógio monotônico */
    struct timespec fim;
    struct timeval cpu;       /* tempo de CPU (usuário + sistema) informado por wait4 */
    int finalizado;
    int status;               /* status retornado por wait4 */
    int ack_semid;            /* semáforo de confirmação do produtor (paradigma 1, ack=1) */
    int aguarda_vez;          /* consumidor ack=1 que chegou com outro na frente */
    int tem_vez;              /* consumidor ack=1 que está sendo atendido na estrutura */
} proc_info_t;

/* Funções utilitárias */
void erro(char *msg);
void agora(struct timespec *t);
void formatar_duracao(char *buf, size_t tamanho, const struct timespec *inicio, const struct timespec *fim);

/* Identificadores IPC repassados pelo mpmc na linha de comando */
int ler_ids(const char *texto, int *a, int *b, int max);

/* Encerramento gracioso de produz e consome: SIGTERM, SIGINT, SIGHUP e SIGQUIT
 * apenas registram o sinal em terminar_processo, e as chamadas bloqueantes
 * interrompidas retornam EINTR. O processo sai num ponto seguro e então termina
 * pelo mesmo sinal, para que o mpmc registre a causa do término. */
extern volatile sig_atomic_t terminar_processo;
void instalar_encerramento_gracioso(void);
void finalizar_processo(const char *nome, const struct timespec *inicio, int codigo)
    __attribute__((noreturn));

#endif
