#ifndef REGISTRO_H
#define REGISTRO_H

#include "common.h"

/* Tipos de recurso no arquivo de registro (uma linha "tipo id" por recurso) */
#define REG_FILA 'q'
#define REG_SHM  'm'
#define REG_SEM  's'

void limpar_execucoes_anteriores(void);
void iniciar_registro(void);
void registrar_ipc(char tipo, int id);
void encerrar_registro(void);

#endif
