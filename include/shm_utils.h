#ifndef SHM_UTILS_H
#define SHM_UTILS_H

#include "common.h"

/* Estrutura do cabeçalho do buffer em memória compartilhada */
typedef struct {
    int cabeca;                /* índice para próxima escrita */
    int cauda;                 /* índice para próxima leitura */
    int count;                 /* número de mensagens no buffer */
    unsigned long total_produzidas;
    unsigned long total_consumidas;
    int capacidade;            /* capacidade máxima de mensagens */
    int iniciado;              /* flag para inicialização */
} shm_header_t;

/* Operações com semáforos */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

int criar_segmento_shm(int index);
int anexar_segmento_shm(int shmid, void **addr);
void desanexar_segmento_shm(void *addr);
void remover_segmento_shm(int shmid);

int criar_semaforos_shm(int index);
void inicializar_semaforos(int semid, int capacidade);
void remover_semaforos_shm(int semid);

/* Operações P/V (wait/signal) */
void sem_p(int semid, int sem_num);
void sem_v(int semid, int sem_num);

#endif
