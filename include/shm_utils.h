#ifndef SHM_UTILS_H
#define SHM_UTILS_H

#include "common.h"

/* Estrutura para buffer em memória compartilhada */
typedef struct {
    int cabeca;
    int cauda;
    int count;
    unsigned long total_consumidas;
    unsigned long total_produzidas;
    int capacidade;
    mensagem_t buffer[];
} shm_buffer_t;

int criar_segmento_shm(int index);
int anexar_segmento_shm(int shmid, void **addr);
void desanexar_segmento_shm(void *addr);
void remover_segmento_shm(int shmid);
int criar_semaforos_shm(int index);
void remover_semaforos_shm(int semid);

#endif
