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

/* Estrutura de mensagem */
typedef struct {
    long mtype;
    pid_t pid_produtor;
    unsigned long seq;        /* número sequencial da mensagem para o processo emissor */
    struct timeval tempo_envio;
    char dados[64];
} mensagem_t;

/* Informações de cada estrutura (memória compartilhada ou fila de mensagens) */
typedef struct {
    int id;
    int tipo;
    int shmid;
    int semid;
    int msgid;
    void *shm_addr;
    unsigned long total_consumidas;
    unsigned long total_produzidas;
} estrutura_info_t;

/* Informações dos processos filhos */
typedef struct {
    pid_t pid;
    char tipo;                /* 'P' para produz, 'C' para consome */
    int estrutura;
    int n_msgs;
    int ack;
    struct timeval inicio;
    struct timeval fim;
    int finalizado;
} proc_info_t;

/* Funções utilitárias */
void erro(char *msg);
void imprimir_tempo(struct timeval *inicio, struct timeval *fim);

#endif
