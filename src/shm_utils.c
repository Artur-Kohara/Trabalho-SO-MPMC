#include "shm_utils.h"

int criar_segmento_shm(int index) {
    key_t key;
    int shmid;
    FILE *f;
    
    /* Cria arquivo para ftok se não existir */
    f = fopen(SHM_KEY_FILE, "a");
    if (f == NULL) {
        perror("fopen shm");
        return -1;
    }
    fclose(f);
    
    key = ftok(SHM_KEY_FILE, index);
    if (key == -1) {
        perror("ftok shm");
        return -1;
    }
    
    /* Cria segmento de memória compartilhada de 2048 bytes */
    shmid = shmget(key, SHM_SIZE, IPC_CREAT | IPC_EXCL | 0666);
    if (shmid == -1) {
        if (errno == EEXIST) {
            /* Se já existe, tenta obter */
            shmid = shmget(key, SHM_SIZE, 0666);
            if (shmid == -1) {
                perror("shmget existente");
                return -1;
            }
        } else {
            perror("shmget");
            return -1;
        }
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

int criar_semaforos_shm(int index) {
    key_t key;
    int semid;
    FILE *f;
    
    f = fopen(SEM_KEY_FILE, "a");
    if (f == NULL) {
        perror("fopen sem");
        return -1;
    }
    fclose(f);
    
    key = ftok(SEM_KEY_FILE, index);
    if (key == -1) {
        perror("ftok sem");
        return -1;
    }
    
    /* Cria conjunto de 3 semáforos: 0=mutex, 1=vazio, 2=cheio */
    semid = semget(key, 3, IPC_CREAT | IPC_EXCL | 0666);
    if (semid == -1) {
        if (errno == EEXIST) {
            semid = semget(key, 3, 0666);
            if (semid == -1) {
                perror("semget existente");
                return -1;
            }
        } else {
            perror("semget");
            return -1;
        }
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

void remover_semaforos_shm(int semid) {
    if (semid != -1) {
        if (semctl(semid, 0, IPC_RMID) == -1) {
            perror("semctl IPC_RMID");
        }
    }
}

void sem_p(int semid, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = -1;
    op.sem_flg = 0;
    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop P");
        break;
    }
}

void sem_v(int semid, int sem_num) {
    struct sembuf op;
    op.sem_num = sem_num;
    op.sem_op = 1;
    op.sem_flg = 0;
    while (semop(semid, &op, 1) == -1) {
        if (errno == EINTR) continue;
        perror("semop V");
        break;
    }
}
