# Trabalho Prático 02 - Múltiplos Produtores / Múltiplos Consumidores (MPMC)

**Disciplina:** Sistemas Operacionais - Universidade de Brasília (UnB)  
**Professor:** Alba Melo  
**Período:** 02/2026

## Descrição

Implementação de um sistema com múltiplos produtores e múltiplos consumidores (MPMC) utilizando dois paradigmas de comunicação entre processos: 

- **Paradigma 1 - Memória Compartilhada:** Utilizando segmentos `shm` e semáforos `sem`. Cada estrutura possui um segmento de memória compartilhada de **2048 bytes** com buffer circular.
- **Paradigma 2 - Troca de Mensagens:** Utilizando filas de mensagens `msg` (`msgget`, `msgsnd`, `msgrcv`) com fila de confirmação for mecanismo ACK.

O processo `mpmc` funciona como um shell interativo com o prompt `>>`, sendo responsável por criar e gerenciar os processos `produz` e `consome`, além de realizar a limpeza dos recursos ao finalizar.

## Estrutura de Mensagens

Toda mensagem trocada deve conter obrigatoriamente: 

- PID do processo produtor
- Número sequencial da mensagem para o processo emissor
- Tempo de envio

## Compilação

Para compilar o projeto, basta utilizar o `Makefile`:

```bash
make
```

Os executáveis `mpmc`, `produz` e `consome` serão gerados no diretório raiz.

Para limpar os arquivos objeto e executáveis:
```bash
make clean
```

## Como Utilizar

### 1. Inicializar o sistema

```bash
./mpmc <p> <paradigma>
```

- `p` - Número de produtores/estruturas (memórias compartilhadas ou filas de mensagens).
- `paradigma` - `1` para memória compartilhada (`shm` + `sem`) ou `2` para troca de mensagens (`msg`).

### 2. Comandos disponíveis

#### Produzir mensagens
```bash
>> produz <p_prod> <n_msgs> <ack> &
```

- `p_prod` - Número da estrutura alvo. Caso seja `0`, as mensagens serão distribuídas no padrão *striped* (`1, 2, ..., p, 1, 2, ..., p, ...`).
- `n_msgs` - Quantidade de mensagens a produzir (entre 1 e 1024).
- `ack` - `1` para o produtor aguardar a confirmação de que **todas** as mensagens foram consumidas. `2` para não aguardar.

#### Consumir mensagens
```bash
>> consome <p_prod> <n_msgs> <ack> &
```

- `p_prod` - Número da estrutura da qual deseja consumir.
- `n_msgs` - Quantidade de mensagens a consumir (entre 1 e 1024). Um único consumidor pode requisitar mais de uma mensagem.
- `ack` - `1` para bloquear até consumir todas as mensagens solicitadas. `2` para não bloquear, retornando um erro com o número efetivamente consumido caso haja menos mensagens disponíveis.

#### Finalizar o sistema
```bash
>> shutdown
```

Ao executar o `shutdown`, o `mpmc` irá: 

- Encerrar todos os processos `produz` e `consome` ativos (enviando SIGTERM e, se necessário, SIGKILL).
- Imprimir o tempo de execução de cada processo (com informações detalhadas).
- Remover todos os mecanismos IPC utilizados (`shm`, `sem` ou `msg`).
- Remover arquivos de chave IPC.
- Imprimir o número total de mensagens consumidas em cada estrutura.
- Imprimir o tempo total de operação do `mpmc`.

## Status da Implementação

- [x] **Etapa 1:** Estrutura base, headers, Makefile
- [x] **Etapa 2:** Shell mpmc, parser de comandos, fork/exec
- [x] **Etapa 3:** Suporte a filas de mensagens (paradigma 2)
- [x] **Etapa 4:** Suporte a memória compartilhada (paradigma 1) com shm+sem e buffer circular (2048 bytes)
- [x] **Etapa 5:** Sincronização e mecanismo ACK completo
- [x] **Etapa 6:** Distribuição striped e validações
- [x] **Etapa 7:** Shutdown completo com limpeza de IPCs e estatísticas
- [ ] **Etapa 8:** Testes com exemplos

## Observações

- Todo o código deve ser desenvolvido em **C** para ambientes **Unix/Linux**.
- É necessário utilizar chamadas de sistema para IPC, processos (`fork`, `exec`, `wait`), sinais Unix e mecanismos de sincronização.
- O sistema deve manter concorrência entre os processos `mpmc`, `produz` e `consome`.
- Em caso de tentativa de acesso a uma estrutura inexistente, o sistema deve retornar um erro apropriado.
- Tamanho do segmento de memória compartilhada é **2048 bytes**.
- Para paradigma 1: utiliza semáforos System V para exclusão mútua, controle de buffer vazio/cheio e buffer circular.
- Para paradigma 2: utiliza filas de mensagens com fila de confirmação para mecanismo ACK.
- Distribuição striped implementada para `p_prod == 0`.
- Consumidor com `ack=1` fica bloqueado aguardando mensagens. Com `ack=2` retorna erro informando quantas mensagens foram consumidas caso não haja suficientes.
- Produtor com `ack=1` aguarda confirmação de que todas as mensagens foram consumidas.
- `shutdown` termina todos os processos, imprime tempos de execução individuais e tempo total do mpmc, remove todos os IPCs e limpa arquivos de chave.
