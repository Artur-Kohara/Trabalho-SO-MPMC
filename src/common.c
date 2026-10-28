#include "common.h"

void erro(char *msg) {
    perror(msg);
    exit(EXIT_FAILURE);
}

void imprimir_tempo(struct timeval *inicio, struct timeval *fim) {
    long segundos = fim->tv_sec - inicio->tv_sec;
    long microssegundos = fim->tv_usec - inicio->tv_usec;
    
    if (microssegundos < 0) {
        segundos--;
        microssegundos += 1000000;
    }
    
    printf("%ld.%06ld segundos\n", segundos, microssegundos);
}
