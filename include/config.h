#ifndef CONFIG_H
#define CONFIG_H

#define SHM_SIZE 2048
#define MAX_MSGS 1024
#define MAX_PROD 1024
#define PROMPT ">> "

/* Os recursos IPC são criados com IPC_PRIVATE e seus identificadores são
 * repassados a produz e consome pela linha de comando. Cada execução do mpmc
 * registra os recursos que criou em REGISTRO_DIR/mpmc_<pid>.ipc, para que uma
 * execução posterior possa removê-los se o mpmc for interrompido sem shutdown
 * (por exemplo, com kill -9). */
#define REGISTRO_DIR "/tmp"

#define IPC_PERMS 0600

#define ACK_WAIT 1
#define ACK_NO_WAIT 2

#define PARADIGMA_SHM 1
#define PARADIGMA_MSG 2

#endif
