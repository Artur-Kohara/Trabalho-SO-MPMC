#include "shm_utils.h"

int criar_segmento_shm(int index) {
    key_t key;
    int shmid;
    FILE *f;
    
    /* Cria arquivo para ftok se não existir */
    f = fopen(SHM_KEY_FILE, "a");
    if (f) fclose(f);
    
    key = ftok(SHM_KEY_FILE, index);
    if (key == -1) {
        erro("ftok shm");
    }
    
    /* Cria segmento de memória compartilhada de 2048 bytes */
    shmid = shmget(key, SHM_SIZE, IPC_CREAT | 0666);
    if (shmid == -1) {
        erro("shmget");
    }
    
    return shmid;
}

int anexar_segmento_shm(int shmid, void **addr) {
    *addr = shmat(shmid, NULL, 0);
    if (*addr == (void *) -1) {
        erro("shmat");
        return -1;
    }
    return 0;
}

void desanexar_segmento_shm(void *addr) {
    if (shmdt(addr) == -1) {
        perror("shmdt");
    }
}

void remover_segmento_shm(int shmid) {
    if (shmctl(shmid, IPC_RMID, NULL) == -1) {
        perror("shmctl IPC_RMID");
    }
}

int criar_semaforos_shm(int index) {
    key_t key;
    int semid;
    FILE *f;
    
    f = fopen(SEM_KEY_FILE, "a");
    if (f) fclose(f);
    
    key = ftok(SEM_KEY_FILE, index);
    if (key == -1) {
        erro("ftok sem");
    }
    
    /* Cria conjunto de semáforos */
    semid = semget(key, 3, IPC_CREAT | 0666); /* mutex, vazio, cheio */
    if (semid == -1) {
        erro("semget");
    }
    
    return semid;
}

void remover_semaforos_shm(int semid) {
    if (semctl(semid, 0, IPC_RMID) == -1) {
        perror("semctl IPC_RMID");
    }
}
