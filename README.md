# Trabalho Prático 02 - Múltiplos Produtores / Múltiplos Consumidores (MPMC)

**Disciplina:** Sistemas Operacionais - Universidade de Brasília (UnB)  
**Professor:** Alba Melo  

## Descrição

Implementação de um sistema com múltiplos produtores e múltiplos consumidores (MPMC) utilizando dois paradigmas de comunicação entre processos: 

- **Paradigma 1 - Memória Compartilhada:** Utilizando segmentos `shm` e semáforos `sem`. Cada estrutura possui um segmento de memória compartilhada de **2048 bytes**.
- **Paradigma 2 - Troca de Mensagens:** Utilizando filas de mensagens `msg` (`msgget`, `msgsnd`, `msgrcv`).

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
./mpmc <p> <paradigma> [&]
```

- `p` - Número de produtores/estruturas (memórias compartilhadas ou filas de mensagens).
- `paradigma` - `1` para memória compartilhada (`shm` + `sem`) ou `2` para troca de mensagens (`msg`).
- O `&` é opcional para execução em segundo plano.

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

- Encerrar todos os processos `produz` e `consome` ativos.
- Imprimir o tempo de execução de cada processo.
- Remover todos os mecanismos IPC utilizados (`shm`, `sem` ou `msg`).
- Imprimir o número total de mensagens consumidas por estrutura.
- Imprimir o tempo total de operação do `mpmc`.

## Exemplos de Uso

### Exemplo com troca de mensagens (paradigma 2)
```bash
./mpmc 2 2 &
>> produz 2 5 2 &
>> consome 2 2 1 &
>> consome 2 2 1 &
>> shutdown
```

### Exemplo com distribuição striped
```bash
./mpmc 5 2 &
>> produz 0 15 2 &
>> consome 2 3 1 &
>> shutdown
```

## Organização do Projeto

```
Trabalho/
├── include/                        # Arquivos de cabeçalho (.h)
│   ├── config.h
│   ├── common.h
│   ├── shm_utils.h
│   └── msg_utils.h
├── src/                            # Código fonte (.c)
│   ├── mpmc.c
│   ├── produz.c
│   ├── consome.c
│   ├── common.c
│   ├── shm_utils.c
│   └── msg_utils.c
├── Makefile                        # Regras de compilação
├── plano_desenvolvimento_mpmc.md   # Plano detalhado para implementação
├── trabalho_pratico_02_2026_so.txt # Enunciado do trabalho
└── README.md                       # Este arquivo
```

## Status da Implementação

- [x] **Etapa 1:** Estrutura base, headers, Makefile
- [x] **Etapa 2:** Shell mpmc, parser de comandos, fork/exec
- [x] **Etapa 3:** Suporte a filas de mensagens (paradigma 2) - básico funcional
- [ ] **Etapa 4:** Suporte a memória compartilhada (paradigma 1)
- [ ] **Etapa 5:** Sincronização e mecanismo ACK completo
- [ ] **Etapa 6:** Distribuição striped e validações refinadas
- [ ] **Etapa 7:** Shutdown completo e estatísticas
- [ ] **Etapa 8:** Testes com exemplos

## Observações

- Todo o código deve ser desenvolvido em **C** para ambientes **Unix/Linux**.
- É necessário utilizar chamadas de sistema para IPC, processos (`fork`, `exec`, `wait`), sinais Unix e mecanismos de sincronização.
- O sistema deve manter concorrência entre os processos `mpmc`, `produz` e `consome` (todos executando em paralelo).
- Em caso de tentativa de acesso a uma estrutura inexistente, o sistema deve retornar um erro apropriado.
- Para paradigma 2 (filas de mensagens), o sistema já suporta criação de filas, envio/recebimento de mensagens com a estrutura obrigatória (pid_produtor, seq, tempo_envio).
- Distribuição striped implementada para `p_prod == 0`.
- Consumidor com `ack=1` fica bloqueado aguardando mensagens. Com `ack=2` retorna erro informando quantas mensagens foram consumidas caso não haja suficientes.
