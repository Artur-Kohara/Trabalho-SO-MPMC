#include "msg_utils.h"

/* Cria uma fila de mensagens privada; o identificador é repassado aos filhos */
int criar_fila(void) {
    int msgid = msgget(IPC_PRIVATE, IPC_CREAT | IPC_PERMS);
    if (msgid == -1) {
        perror("msgget");
    }
    return msgid;
}

void remover_fila(int msgid) {
    if (msgid != -1) {
        if (msgctl(msgid, IPC_RMID, NULL) == -1) {
            perror("msgctl IPC_RMID");
        }
    }
}
