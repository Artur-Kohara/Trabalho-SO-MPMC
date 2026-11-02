# Trabalho Prático 02 - Múltiplos Produtores / Múltiplos Consumidores (MPMC)

**Disciplina:** Sistemas Operacionais - Universidade de Brasília (UnB)  
**Professor:** Alba Melo  
**Período:** 02/2026

## Descrição

Implementação de um sistema com múltiplos produtores e múltiplos consumidores (MPMC) utilizando dois paradigmas de comunicação entre processos:

- **Paradigma 1 - Memória Compartilhada:** segmentos `shm` e semáforos `sem`. Cada estrutura é um segmento de **2048 bytes** com um buffer circular, protegido por um conjunto de três semáforos (mutex, posições livres e mensagens disponíveis).
- **Paradigma 2 - Troca de Mensagens:** somente filas de mensagens `msg` (`msgget`, `msgsnd`, `msgrcv`): uma fila por estrutura, uma fila de confirmações (mecanismo ACK) e uma fila de estatísticas (mensagens consumidas).

Nos dois paradigmas também são usados sinais Unix, como o enunciado permite (término dos processos, aviso de vez aos consumidores e encerramento).

O processo `mpmc` funciona como um shell interativo com o prompt `>>`, sendo responsável por criar e gerenciar os processos `produz` e `consome`, além de realizar a limpeza dos recursos ao finalizar.

## Estrutura de Mensagens

Cada mensagem (`mensagem_t`, em `include/common.h`) contém:

- PID do processo produtor;
- número sequencial da mensagem para o processo emissor;
- tempo de envio;
- o modo `ack` do produtor e o destino da confirmação, usados pelo mecanismo ACK.

Com 48 bytes por mensagem, cada segmento de 2048 bytes comporta **41 mensagens** e cada fila (limite padrão `msgmnb` de 16 KB) cerca de **400 mensagens**. Acima disso o produtor bloqueia até haver consumo, mesmo com `ack=2`.

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

Para executar a bateria de testes automatizados:
```bash
make test
```

## Como Utilizar

### 1. Inicializar o sistema

```bash
./mpmc <p> <paradigma>
```

- `p` - Número de produtores/estruturas (memórias compartilhadas ou filas de mensagens), de 1 a 1024.
- `paradigma` - `1` para memória compartilhada (`shm` + `sem`) ou `2` para troca de mensagens (`msg`).

O `mpmc` deve ler os comandos do terminal em primeiro plano. Se for iniciado com `&` num shell interativo, o terminal o para (sinal `SIGTTIN`) assim que ele tenta ler o teclado; nesse caso ele avisa e basta usar `fg`.

### 2. Comandos disponíveis

#### Produzir mensagens
```bash
>> produz <p_prod> <n_msgs> <ack> &
```

- `p_prod` - Número da estrutura alvo. Caso seja `0`, as mensagens serão distribuídas no padrão *striped* (`1, 2, ..., p, 1, 2, ..., p, ...`).
- `n_msgs` - Quantidade de mensagens a produzir (entre 1 e 1024).
- `ack` - `1` para o produtor aguardar a confirmação de que **todas as suas** mensagens foram consumidas. `2` para não aguardar.

#### Consumir mensagens
```bash
>> consome <p_prod> <n_msgs> <ack> &
```

- `p_prod` - Número da estrutura da qual deseja consumir.
- `n_msgs` - Quantidade de mensagens a consumir (entre 1 e 1024). Um único consumidor pode requisitar mais de uma mensagem.
- `ack` - `1` para bloquear até consumir todas as mensagens solicitadas. `2` para não bloquear: o consumidor retira as mensagens disponíveis e, se forem menos que `n_msgs`, retorna erro informando quantas consumiu (código de saída 1).

O `&` é opcional e pode vir com ou sem espaço: `produz` e `consome` sempre executam de forma concorrente com o `mpmc`, que volta imediatamente ao prompt. Uma linha copiada junto com o prompt (`>> produz 3 2 1 &`) também é aceita.

#### Finalizar o sistema
```bash
>> shutdown
```

Ao executar o `shutdown`, o `mpmc`:

1. Espera (no máximo 2 segundos) que os processos ativos fiquem bloqueados. Assim, comandos colados ou lidos de um arquivo têm o mesmo efeito de comandos digitados um a um.
2. Envia `SIGTERM` aos processos `produz` e `consome` ativos. Eles terminam a operação em andamento, imprimem o próprio tempo de execução e encerram; quem não terminar em 500 ms recebe `SIGKILL`.
3. Imprime, para cada processo, o tempo de execução (relógio monotônico, do `fork` até o término), o tempo de CPU e como ele terminou (código de saída ou sinal).
4. Remove todos os mecanismos IPC criados.
5. Imprime o número total de mensagens consumidas em cada estrutura e o tempo total de operação do `mpmc`.

O mesmo procedimento é executado se a entrada terminar (EOF/Ctrl+D) ou se o `mpmc` receber `SIGINT` (Ctrl+C), `SIGTERM`, `SIGHUP` ou `SIGQUIT`.

## Mecanismo de confirmação (ACK)

Cada mensagem carrega o modo `ack` do produtor. Quando o produtor usa `ack=1`, cada mensagem dele retirada por qualquer consumidor (com `ack=1` ou `ack=2`) é confirmada a ele:

- **Paradigma 1:** o `mpmc` cria um semáforo privado (valor inicial 0) para o produtor e repassa seu identificador, que vai em cada mensagem. O consumidor incrementa esse semáforo a cada mensagem do produtor que retira do buffer, e o produtor aguarda com uma única operação `semop(-n)`. O semáforo é removido assim que o produtor termina.
- **Paradigma 2:** a fila de confirmações guarda um contador por produtor, com `mtype` igual ao número do produtor naquela execução. Para confirmar uma mensagem, o consumidor retira o contador do produtor que estiver na fila (sem bloquear), soma 1 e o devolve. O produtor, inclusive no modo striped, soma os contadores que recebe até atingir o total enviado. Como a fila guarda poucos contadores por produtor, ela não enche, qualquer que seja o volume de mensagens.

## Contagem de mensagens consumidas

- **Paradigma 1:** o consumidor incrementa um contador no cabeçalho do segmento, na mesma região crítica em que retira a mensagem.
- **Paradigma 2:** ao terminar (inclusive quando encerrado pelo `shutdown`), cada consumidor envia à fila de estatísticas quantas mensagens retirou de cada estrutura, e o `mpmc` soma esses valores.

## Ordem de atendimento dos consumidores

Consumidores bloqueantes (`ack=1`) de uma mesma estrutura são atendidos por ordem de chegada, como no exemplo 3.2 do enunciado: enquanto um deles não termina, o seguinte espera. O `mpmc` avisa o próximo com `SIGUSR1` quando o anterior termina, por qualquer motivo. Consumidores com `ack=2` nunca esperam: retiram imediatamente as mensagens disponíveis.

## Sincronização e sinais

- No paradigma 1, produtores e consumidores entram na região crítica com uma única operação atômica sobre dois semáforos (posição livre ou mensagem disponível, mais o mutex). O mutex usa `SEM_UNDO`: se um processo morrer dentro da região crítica, o kernel o libera e a estrutura não trava.
- No `mpmc`, os sinais tratados ficam bloqueados e só são liberados enquanto ele espera um comando (`pselect`). Os tratadores apenas registram o evento, que é processado no laço principal. Assim, nenhum sinal se perde entre uma verificação e o início da espera, e nenhum comando, nem o próprio `shutdown`, é interrompido no meio.
- Os filhos terminam junto com o `mpmc` (`PR_SET_PDEATHSIG`), mesmo que ele seja morto com `SIGKILL`.

## Recursos IPC

- Todos os recursos são criados com `IPC_PRIVATE` e permissão `0600`, e seus identificadores são repassados a `produz` e `consome` pela linha de comando. Não há arquivos de chave nem colisões entre execuções simultâneas.
- Cada execução registra os recursos que criou em `/tmp/mpmc_<pid>.ipc`. Se um `mpmc` for interrompido sem `shutdown` (por exemplo, com `kill -9`), a próxima execução remove os recursos que ficaram para trás e informa quantos removeu.

## Testes

A bateria `tests/testes.sh` (`make test`) cobre:

- os três exemplos do enunciado, digitados com pausas e colados de uma vez;
- o mecanismo ACK, inclusive com vários produtores grandes na mesma estrutura;
- a ordem de atendimento dos consumidores e os limites de `p`, `n_msgs` e capacidade;
- a entrada do shell, os sinais e o encerramento;
- a limpeza dos recursos após `kill -9`.

Cada execução do `mpmc` roda numa sessão própria, e a bateria confere se ela deixou objetos IPC, processos ou arquivos para trás.

## Status da Implementação

- [x] **Etapa 1:** Estrutura base, headers, Makefile
- [x] **Etapa 2:** Shell mpmc, parser de comandos, fork/exec
- [x] **Etapa 3:** Suporte a filas de mensagens (paradigma 2)
- [x] **Etapa 4:** Suporte a memória compartilhada (paradigma 1) com shm+sem e buffer circular (2048 bytes)
- [x] **Etapa 5:** Sincronização e mecanismo ACK completo
- [x] **Etapa 6:** Distribuição striped e validações
- [x] **Etapa 7:** Shutdown completo com limpeza de IPCs e estatísticas
- [x] **Etapa 8:** Testes, debug e ajustes

## Observações

- Código em **C** para **Linux** (usa `/proc` e `prctl`).
- O tempo de envio é registrado imediatamente antes de a mensagem ser gravada no buffer (paradigma 1) ou de cada tentativa de `msgsnd` (paradigma 2); com a fila cheia, o `msgsnd` espera por espaço.
- Estruturas inexistentes são rejeitadas pelo `mpmc` antes de criar o processo (ex.: `Erro - fila 3 nao existe`).
