#ifndef COMMON_H
#define COMMON_H

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
    char dados[64];
} mensagem_t;

/* Informações de cada estrutura (memória compartilhada ou fila de mensagens) */
typedef struct {
    int id;                   /* índice 1-based */
    int tipo;                 /* PARADIGMA_SHM ou PARADIGMA_MSG */
    int shmid;                /* para shm */
    int semid;                /* para semáforos */
    int msgid;                /* para fila de mensagens */
    void *shm_addr;           /* endereço anexado */
    unsigned long total_consumidas;
    unsigned long total_produzidas;
} estrutura_info_t;

/* Informações dos processos filhos */
typedef struct {
    pid_t pid;
    char tipo;                /* 'P' para produz, 'C' para consome */
    int estrutura;            /* estrutura alvo ou 0 para striped */
    int n_msgs;
    int ack;
    struct timeval inicio;
    struct timeval fim;
    int finalizado;
} proc_info_t;

/* Funções utilitárias */
void erro(char *msg);
void imprimir_tempo(struct timeval *inicio, struct timeval *fim);
double tempo_em_segundos(struct timeval *inicio, struct timeval *fim);

#endif
