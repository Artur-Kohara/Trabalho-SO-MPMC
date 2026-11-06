#include "registro.h"
#include "shm_utils.h"
#include <dirent.h>
#include <fcntl.h>

/* Registro dos recursos IPC criados por esta execução do mpmc, em
 * REGISTRO_DIR/mpmc_<pid>.ipc. Cada linha é escrita assim que o recurso é
 * criado; se o mpmc for interrompido sem shutdown, a próxima execução encontra
 * o arquivo de um processo que não existe mais e remove os recursos listados. */

static int fd_registro = -1;
static char caminho_registro[256];

/* Remove o recurso se ele ainda existir e pertencer a este usuário */
static int remover_recurso(char tipo, int id) {
    uid_t uid = getuid();

    if (tipo == REG_FILA) {
        struct msqid_ds ds;
        if (msgctl(id, IPC_STAT, &ds) == 0 && ds.msg_perm.uid == uid) {
            return msgctl(id, IPC_RMID, NULL) == 0;
        }
    } else if (tipo == REG_SHM) {
        struct shmid_ds ds;
        if (shmctl(id, IPC_STAT, &ds) == 0 && ds.shm_perm.uid == uid) {
            return shmctl(id, IPC_RMID, NULL) == 0;
        }
    } else if (tipo == REG_SEM) {
        struct semid_ds ds;
        union semun arg;
        arg.buf = &ds;
        if (semctl(id, 0, IPC_STAT, arg) == 0 && ds.sem_perm.uid == uid) {
            return semctl(id, 0, IPC_RMID) == 0;
        }
    }
    return 0;
}

/* Remove os recursos registrados por execuções do mpmc que já não estão
 * rodando, e os respectivos arquivos de registro */
void limpar_execucoes_anteriores(void) {
    DIR *dir = opendir(REGISTRO_DIR);
    struct dirent *entrada;
    int removidos = 0;

    if (dir == NULL) {
        return;
    }
    while ((entrada = readdir(dir)) != NULL) {
        char esperado[64], caminho[512];
        int pid, id;
        char tipo;
        FILE *f;

        if (sscanf(entrada->d_name, "mpmc_%d", &pid) != 1) {
            continue;
        }
        snprintf(esperado, sizeof(esperado), "mpmc_%d.ipc", pid);
        if (strcmp(esperado, entrada->d_name) != 0) {
            continue;
        }
        /* Execução ainda ativa (ou de outro usuário): não mexer */
        if (pid != getpid() && (kill(pid, 0) == 0 || errno != ESRCH)) {
            continue;
        }
        snprintf(caminho, sizeof(caminho), "%s/%s", REGISTRO_DIR, entrada->d_name);
        f = fopen(caminho, "r");
        if (f == NULL) {
            continue;
        }
        while (fscanf(f, " %c %d", &tipo, &id) == 2) {
            removidos += remover_recurso(tipo, id);
        }
        fclose(f);
        unlink(caminho);
    }
    closedir(dir);
    if (removidos > 0) {
        printf("Removidos %d recursos IPC deixados por execuções anteriores interrompidas.\n", removidos);
    }
}

void iniciar_registro(void) {
    snprintf(caminho_registro, sizeof(caminho_registro), "%s/mpmc_%d.ipc", REGISTRO_DIR, (int)getpid());
    fd_registro = open(caminho_registro, O_WRONLY | O_CREAT | O_EXCL | O_APPEND | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd_registro == -1) {
        perror("Aviso: registro dos recursos IPC indisponível");
    }
}

void registrar_ipc(char tipo, int id) {
    char linha[32];
    int n;

    if (fd_registro == -1 || id == -1) {
        return;
    }
    n = snprintf(linha, sizeof(linha), "%c %d\n", tipo, id);
    if (write(fd_registro, linha, (size_t)n) != n) {
        perror("Aviso: falha ao atualizar o registro dos recursos IPC");
    }
}

/* Chamado depois que todos os recursos foram removidos */
void encerrar_registro(void) {
    if (fd_registro != -1) {
        close(fd_registro);
        unlink(caminho_registro);
        fd_registro = -1;
    }
}
