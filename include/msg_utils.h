#ifndef MSG_UTILS_H
#define MSG_UTILS_H

#include "common.h"

int criar_fila_mensagem(int index);
int obter_fila_mensagem(int index);
void remover_fila_mensagem(int msgid);

int criar_fila_ack(int index);
int obter_fila_ack(int index);
void remover_fila_ack(int msgid);

#endif
