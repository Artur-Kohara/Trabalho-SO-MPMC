#include "msg_utils.h"

int criar_fila_mensagem(int index) {
    key_t key;
    int msgid;
    FILE *f;
    
    /* Cria arquivo para ftok se não existir */
    f = fopen(MSG_KEY_FILE, "a");
    if (f == NULL) {
        perror("fopen");
        return -1;
    }
    fclose(f);
    
    key = ftok(MSG_KEY_FILE, index);
    if (key == -1) {
        perror("ftok msg");
        return -1;
    }
    
    /* Cria fila de mensagens com permissões 0666 */
    msgid = msgget(key, IPC_CREAT | IPC_EXCL | 0666);
    if (msgid == -1) {
        if (errno == EEXIST) {
            /* Se já existe, tenta obter */
            msgid = msgget(key, 0666);
            if (msgid == -1) {
                perror("msgget existente");
                return -1;
            }
        } else {
            perror("msgget");
            return -1;
        }
    }
    
    return msgid;
}

int obter_fila_mensagem(int index) {
    key_t key;
    int msgid;
    FILE *f;
    
    f = fopen(MSG_KEY_FILE, "a");
    if (f == NULL) {
        perror("fopen");
        return -1;
    }
    fclose(f);
    
    key = ftok(MSG_KEY_FILE, index);
    if (key == -1) {
        perror("ftok msg");
        return -1;
    }
    
    msgid = msgget(key, 0666);
    if (msgid == -1) {
        return -1;
    }
    
    return msgid;
}

void remover_fila_mensagem(int msgid) {
    if (msgctl(msgid, IPC_RMID, NULL) == -1) {
        perror("msgctl IPC_RMID");
    }
}
