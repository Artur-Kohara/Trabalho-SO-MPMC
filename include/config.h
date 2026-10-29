#ifndef CONFIG_H
#define CONFIG_H

#define SHM_SIZE 2048
#define MAX_MSGS 1024
#define MAX_PROD 1024
#define PROMPT ">> "

#define MSG_KEY_FILE "/tmp/mpmc_msg_key"
#define SHM_KEY_FILE "/tmp/mpmc_shm_key"
#define SEM_KEY_FILE "/tmp/mpmc_sem_key"
#define ACK_KEY_FILE "/tmp/mpmc_ack_key"

#define ACK_WAIT 1
#define ACK_NO_WAIT 2

#define PARADIGMA_SHM 1
#define PARADIGMA_MSG 2

#endif
