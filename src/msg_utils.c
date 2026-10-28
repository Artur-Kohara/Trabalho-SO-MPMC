#include "msg_utils.h"

int criar_fila_mensagem(int index) {
    key_t key;
    int msgid;
    FILE *f;
    
    /* Cria arquivo para ftok se não existir */
    f = fopen(MSG_KEY_FILE, "a");
    if (f) fclose(f);
    
    key = ftok(MSG_KEY_FILE, index);
    if (key == -1) {
        erro("ftok msg");
    }
    
    /* Cria fila de mensagens */
    msgid = msgget(key, IPC_CREAT | 0666);
    if (msgid == -1) {
        erro("msgget");
    }
    
    return msgid;
}

int obter_fila_mensagem(int index) {
    key_t key;
    int msgid;
    FILE *f;
    
    f = fopen(MSG_KEY_FILE, "a");
    if (f) fclose(f);
    
    key = ftok(MSG_KEY_FILE, index);
    if (key == -1) {
        erro("ftok msg");
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
