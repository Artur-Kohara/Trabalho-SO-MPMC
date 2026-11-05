#include "shm_utils.h"

/* Cria um segmento privado; o identificador é repassado aos filhos */
int criar_segmento_shm(size_t tamanho) {
    int shmid = shmget(IPC_PRIVATE, tamanho, IPC_CREAT | IPC_PERMS);
    if (shmid == -1) {
        perror("shmget");
    }
    return shmid;
}

int anexar_segmento_shm(int shmid, void **addr) {
    *addr = shmat(shmid, NULL, 0);
    if (*addr == (void *) -1) {
        perror("shmat");
        return -1;
    }
    return 0;
}

void desanexar_segmento_shm(void *addr) {
    if (addr != NULL && addr != (void *) -1) {
        if (shmdt(addr) == -1) {
            perror("shmdt");
        }
    }
}

void remover_segmento_shm(int shmid) {
    if (shmid != -1) {
        if (shmctl(shmid, IPC_RMID, NULL) == -1) {
            perror("shmctl IPC_RMID");
        }
    }
}

/* Cria um conjunto privado de nsems semáforos */
int criar_semaforos(int nsems) {
    int semid = semget(IPC_PRIVATE, nsems, IPC_CREAT | IPC_PERMS);
    if (semid == -1) {
        perror("semget");
    }
    return semid;
}

void inicializar_semaforos(int semid, int capacidade) {
    union semun arg;
    unsigned short valores[3];
    
    if (semid == -1) return;
    
    /* 0 = mutex (inicialmente 1), 1 = vazio (capacidade), 2 = cheio (0) */
    valores[0] = 1;
    valores[1] = capacidade;
    valores[2] = 0;
    
    arg.array = valores;
    if (semctl(semid, 0, SETALL, arg) == -1) {
        perror("semctl SETALL");
    }
}

void remover_semaforos(int semid) {
    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1) {
            perror("semctl IPC_RMID");
        }
    }
}

/* Executa as operações de forma atômica, repetindo se interrompida por um sinal
 * que não seja de término. Em caso de erro, errno indica a causa (EAGAIN quando
 * uma operação com IPC_NOWAIT não pôde ser feita). */
int sem_ops(int semid, struct sembuf *ops, size_t n) {
    while (semop(semid, ops, n) == -1) {
        if (errno == EINTR && !terminar_processo) continue;
        return -1;
    }
    return 0;
}
