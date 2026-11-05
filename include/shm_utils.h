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

/* Semáforos de cada segmento */
#define SEM_MUTEX 0   /* exclusão mútua no buffer */
#define SEM_VAZIO 1   /* posições livres */
#define SEM_CHEIO 2   /* mensagens disponíveis */

/* Operações com semáforos */
union semun {
    int val;
    struct semid_ds *buf;
    unsigned short *array;
    struct seminfo *__buf;
};

int criar_segmento_shm(size_t tamanho);
int anexar_segmento_shm(int shmid, void **addr);
void desanexar_segmento_shm(void *addr);
void remover_segmento_shm(int shmid);

int criar_semaforos(int nsems);
void inicializar_semaforos(int semid, int capacidade);
void remover_semaforos(int semid);

/* Operações sobre um conjunto de semáforos, executadas de forma atômica */
int sem_ops(int semid, struct sembuf *ops, size_t n);

#endif
