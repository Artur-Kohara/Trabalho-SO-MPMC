# Plano de Desenvolvimento - Múltiplos Produtores / Múltiplos Consumidores (MPMC)

**Disciplina:** Sistemas Operacionais (UnB)  
**Período:** 02/2026  
**Professora:** Alba Melo  

## 1. Objetivo

Desenvolver um sistema com múltiplos produtores e múltiplos consumidores (MPMC) com dois paradigmas de comunicação entre processos:

- **Paradigma 1:** Memória Compartilhada (`shm` + `sem`)
- **Paradigma 2:** Troca de Mensagens (`msg` - filas de mensagens)

O sistema deve ser controlado pelo processo `mpmc`, que funciona como um shell interativo aguardando comandos com `>>`. Processos `produz` e `consome` são criados sob demanda para executar as operações.

## 2. Entendimento Geral

### 2.1. Execução do programa principal

```bash
mpmc p paradigma &
# ou
mpmc p paradigma
```

- `p`: número de produtores/estruturas (filas de mensagem ou memórias compartilhadas)
- `paradigma`: `1` para memória compartilhada, `2` para troca de mensagens
- Se `paradigma == 1`: deve-se usar IPC com `shm` e `sem`. Tamanho do segmento de memória compartilhada deve ser **2048 bytes**. Serão criadas **p** memórias compartilhadas.
- Se `paradigma == 2`: deve-se usar IPC com `msg`. Serão criadas **p** filas de mensagens.
- Processo `mpmc` fica aguardando comandos no shell com `>>`

### 2.2. Comando para produzir

```bash
>> produz p_prod n_msgs ack &
```

- `p_prod`: número do produtor/estrutura alvo (1-based ou conforme definido). Se `p_prod == 0`, as `n_msgs` devem ser distribuídas no padrão **striped**: `(1,2,...,p,1,2,...,p,...)`
- `n_msgs`: número de mensagens a produzir (1 a 1024)
- `ack`: `1` = produtor deve esperar confirmação de que **todas** as mensagens foram consumidas; `2` = não espera
- Cria processo `produz`. Ele produz mensagens uma a uma e coloca na fila/memória compartilhada correspondente. Operação concorrente com `mpmc` (que volta a aceitar comandos com `>>`)
- Se a estrutura `p_prod` não existir (ex.: `p=2` mas tenta produzir para `3`), deve retornar **erro** (exemplo 3.2: `>> retorna erro – fila 3 nao existe`)

### 2.3. Comando para consumir

```bash
>> consome p_prod n_msgs ack &
```

- `p_prod`: produtor/estrutura do qual consumir
- `n_msgs`: número de mensagens a consumir (1 a 1024). Um consumidor pode consumir mais de uma mensagem no mesmo comando
- `ack`: `1` = bloqueado até consumir todas as `n_msgs` solicitadas (ou até que mensagens saiam do buffer). Se houver menos mensagens disponíveis e `ack == 1`, o consumidor fica **bloqueado**. Se `ack == 2` e houver menos mensagens que `n_msgs`, deve retornar **erro** informando o número de mensagens que foram consumidas.
- Cria processo `consome`. Consome mensagens uma a uma da fila/memória compartilhada. Opera concorrentemente com `mpmc`.

### 2.4. Comando para finalizar

```bash
>> shutdown
```

- Termina processos produtores e consumidores, bem como o processo `mpmc`
- Imprime o **tempo de execução de cada processo**
- Deleta os mecanismos de comunicação (IPC)
- Imprime o **número total de mensagens consumidas em cada estrutura `p`** e o **tempo de operação do `mpmc`**

## 3. Análise dos Exemplos

### Exemplo 3.1: Quatro Produtores em memória compartilhada
```bash
> mpmc 4 1 &
>> produz 3 2 1 &
>> consome 2 2 1 &
>> produz 2 5 1 &
>> shutdown
```
- `p=4, paradigma=1` → 4 memórias compartilhadas de 2048 bytes (com semáforos)
- Produtor 3 produz 2 mensagens na memória 2, fica bloqueado esperando confirmação (ack=1) de que foram consumidas
- Consumidor consome 2 mensagens da memória 2, fica bloqueado até saírem do buffer
- Produtor 2 produz 5 mensagens na memória 2 e é bloqueado
- Quando 2 mensagens produzidas são consumidas, o consumidor termina; execução finaliza com produtor 2 bloqueado
- Imprimem-se: número de mensagens consumidas por estrutura e tempo de operação do `mpmc`

### Exemplo 3.2: Dois Produtores em troca de mensagem
```bash
> mpmc 2 2 &
>> produz 3 2 1 &
>>>> retorna erro – fila 3 nao existe  (saída para o shell)
>> consome 2 2 1 &
>> consome 2 2 1 &
>> produz 2 5 2 &
>> shutdown
```
- `p=2, paradigma=2` → 2 filas de mensagens
- Tentar produzir para fila 3 quando só existem 2 → erro
- Dois consumidores na fila 2, ambos bloqueados (ack=1) até mensagens saírem
- Produtor 2 produz 5 mensagens (ack=2, não bloqueia)
- Duas vão para consumidor 1 (termina), duas para consumidor 2 (termina), fica com mensagens

### Exemplo 3.3: Cinco Produtores em troca de mensagem (striped)
```bash
> mpmc 5 2 &
>> produz 0 15 2 &
>> consome 2 3 1 &
>> consome 1 2 1 &
>> produz 3 2 1 &
>> shutdown
```
- `p=5, paradigma=2` → 5 filas
- `produz 0 15 2` → distribuição striped: 15 mensagens em 5 filas (3 em cada)
- Consumidores pedem de filas diferentes, bloqueados se ack=1
- Produtor 3 produz 2 na fila 3 e fica bloqueado (ack=1)
- Finaliza com produtor bloqueado

## 4. Estrutura de Dados

### 4.1. Mensagem (obrigatória)
Conforme item 2: *"A estrutura de dados da mensagem deve conter: pid do processo que a enviou, número sequencial da mensagem para o processo emissor, tempo do envio."*

Definir estrutura em C. Exemplos:

```c
#include <sys/time.h>
#include <sys/types.h>

typedef struct {
    long mtype;        // para msg (pode usar tipo = 1 ou baseado em produtor/consumidor)
    pid_t pid_produtor;
    unsigned long seq; // número sequencial da mensagem para o processo emissor
    struct timeval tempo_envio; // ou time_t + microsegundos
    // dados opcionais? (não especificado, mas pode ser buffer genérico)
    char dados[...];
} mensagem_t;
```

**Notas:**
- Para **memória compartilhada**: a estrutura pode ser armazenada em buffer circular (ou fila) dentro do segmento de 2048 bytes. É preciso gerenciar head/tail/contadores.
- Para **troca de mensagens**: usar `msgsnd`/`msgrcv` com essa estrutura (cuidar do `mtype`).

### 4.2. Estruturas por paradigma

**Paradigma 1 - Memória Compartilhada (shm + sem):**
- Para cada produtor `i` (1..p): criar um segmento de memória compartilhada de **2048 bytes**. 
- Precisa de semáforos para sincronização (mutex, vazio/cheio, ou semáforos clássicos produtor-consumidor). Também pode usar semáforos para "ack" (confirmação de consumo).
- Estrutura interna sugerida no buffer compartilhado:
  ```c
  typedef struct {
      int cabeca, cauda;
      int count; // mensagens no buffer
      int total_consumidas; // contador global de consumidas nesta estrutura
      // semáforos? ou usar sem_t externos (sem_open) associados
      mensagem_t buffer[N]; // alocação dentro dos 2048 bytes
  } shm_buf_t;
  ```
  - Calcular tamanho exato para caber em 2048 bytes.

**Paradigma 2 - Filas de Mensagens (msg):**
- Para cada produtor `i` (1..p): criar uma fila de mensagens com `msgget`
- Cada mensagem enviada via `msgsnd` seguindo `mensagem_t`
- Consumidores leem com `msgrcv` (podem filtrar por tipo se necessário)

### 4.3. Metadados do sistema (mantidos pelo `mpmc`)
- `p` e `paradigma`
- Lista/array com info de cada estrutura (shmid/msgid, semáforos, contadores)
- Lista de processos filhos ativos (PIDs de `produz`/`consome`) para terminar no `shutdown`
- Tempo de início/fim do `mpmc` (para imprimir tempo de operação)
- Tempos de execução de cada processo filho (coletar via `wait`/`getrusage` ou registrar timestamps)

## 5. Arquitetura do Sistema

O sistema terá **3 tipos de executáveis/processos**:

| Processo | Função | Observações |
|---|---|---|
| `mpmc` | Processo principal (shell/interativo) | Cria IPCs (shm/sem/msg) conforme `paradigma`. Lê linha com prompt `>>`. Interpreta comandos `produz`, `consome`, `shutdown`. Cria processos filhos `produz`/`consome` via `fork+exec`. Controla finalização. |
| `produz` | Processo produtor | Recebe parâmetros (nº estrutura, n_msgs, ack, etc.). Gera mensagens com pid, seq (incremental por produtor), tempo_envio. Coloca na estrutura correspondente. Se ack==1, espera confirmação de **todas** mensagens consumidas. Opera concorrentemente. |
| `consome` | Processo consumidor | Recebe parâmetros. Consome n_msgs da estrutura indicada. Se ack==1 e houver menos mensagens: bloqueia até haver; se ack==2 e menos: retorna erro informando quantas consumiu. Atualiza contadores. Envia confirmação se necessário. |

### 5.1. Fluxo de inicialização

1. Usuário executa: `mpmc <p> <paradigma> [&]`
2. `mpmc` valida argumentos
3. Se `paradigma==1`: criar `p` segmentos shm (2048 bytes cada) + semáforos necessários
4. Se `paradigma==2`: criar `p` filas de mensagens (`msgget`)
5. Inicializar contadores (total consumidas por estrutura)
6. Registrar tempo de início do `mpmc`
7. Entrar no loop: exibir/ler `>>`, processar comandos

### 5.2. Fluxo do comando `produz`

1. `mpmc` parseia `>> produz p_prod n_msgs ack`
2. **Validar**: se `p_prod != 0` e (`p_prod < 1` ou `p_prod > p`) → **erro** "fila X nao existe" / "estrutura X nao existe" (usar a terminologia coerente com exemplo: "fila 3 nao existe" para msg)
3. Determinar lista de estruturas-alvo: se `p_prod==0` → distribuição **striped** em todas as `p` estruturas (ciclo 1..p)
4. Criar processo `produz` via `fork()` e `exec()`. Passar parâmetros via argumentos, variáveis de ambiente ou arquivo de configuração
5. `mpmc` volta imediatamente ao prompt `>>` (operação concorrente)
6. `produz` gera `n_msgs` no total (distribuindo por estrutura se striped). Para cada mensagem: `pid = getpid()`, `seq++` (por instância do produtor), `tempo_envio = gettimeofday()`
7. Sincronizar acesso (mutex) e respeitar capacidade do buffer/fila
8. Se `ack==1`: aguardar até que **todas** as mensagens enviadas por este produtor tenham sido **consumidas** (precisa de mecanismo de sincronização - pode ser semáforos, sinais, ou contadores compartilhados)
9. `produz` encerra e retorna/relata tempo de execução (coletado pelo `mpmc`)

### 5.3. Fluxo do comando `consome`

1. `mpmc` parseia `>> consome p_prod n_msgs ack`
2. Validar existência da estrutura
3. Criar processo `consome` via `fork+exec`
4. `mpmc` volta ao prompt `>>`
5. `consome` tenta consumir `n_msgs` da estrutura `p_prod`
6. Se `ack==1`:
   - Se houver mensagens suficientes: consumir até completar `n_msgs`, ficar bloqueado durante espera se necessário (bloquear até saírem/do buffer ter disponível conforme semântica)
   - Se houver **menos** mensagens do que `n_msgs`: **ficar bloqueado** até que mensagens sejam produzidas e consumidas até completar `n_msgs`? (Exemplo 3.1 e 3.2 mostram consumidores ficam bloqueados enquanto mensagens não tiverem saído do buffer; comportamento depende)
7. Se `ack==2`:
   - Se houver mensagens suficientes: consumir `n_msgs`
   - Se houver **menos**: **retornar erro** informando o **número de mensagens que foram consumidas** (ex.: "erro: foram consumidas apenas X mensagens")
8. Atualizar `total_consumidas` na estrutura
9. Enviar **confirmação** para produtores com `ack==1` (se aplicável) para que possam desbloquear
10. `consome` encerra

### 5.4. Fluxo do `shutdown`

1. `mpmc` recebe comando `>> shutdown`
2. **Terminar** processos `produz` e `consome` ativos (enviar sinal `SIGTERM` ou `SIGKILL`; preferir terminar graciosamente)
3. Aguardar finalização de todos (usar `waitpid`)
4. **Imprimir tempo de execução de cada processo** (pode ser obtido via `wait4` com `struct rusage`, ou registrar `clock_gettime` antes/depois de `fork`/durante)
5. **Deletar mecanismos de comunicação**:
   - Paradigma 1: remover segmentos `shm` (`shmctl` com `IPC_RMID`) e semáforos (`semctl` com `IPC_RMID` ou `sem_unlink` se POSIX)
   - Paradigma 2: remover filas de mensagens (`msgctl` com `IPC_RMID`)
6. **Imprimir número total de mensagens consumidas em cada estrutura `p`**
7. **Imprimir tempo de operação do `mpmc`** (tempo total desde inicialização até shutdown)
8. Encerrar `mpmc`

## 6. Sincronização e Concorrência

### 6.1. Para memória compartilhada (paradigma 1)
Necessário gerenciar:
- **Exclusão mútua**: acesso ao buffer (múltiplos produtores/consumidores concorrentes)
- **Sincronização buffer cheio/vazio**: usar semáforos (System V `sem_init` via `semget` + `semop`, ou `sem_open`)
- **Confirmação (ack)**: quando `ack==1`, produtor precisa saber que **todas** suas mensagens foram **consumidas**. Abordagens:
  - Contador por produtor: `enviadas` vs `consumidas` (pode ser em memória compartilhada ou em estrutura controlada pelo `mpmc`)
  - Semáforos específicos por instância de produtor
  - Processo `produz` bloqueia até contador atingir `n_msgs` enviadas
- **Bloqueio de consumidor**: se `ack==1` e menos mensagens disponíveis, deve ficar bloqueado até haver mensagens suficientes (ou até saírem). Se `ack==2` e menos, erro.

### 6.2. Para filas de mensagens (paradigma 2)
- `msgsnd`/`msgrcv` são bloqueantes por padrão dependendo dos flags; pode usar `IPC_NOWAIT` para comportamento não-bloqueante
- Sincronização para "todas consumidas" (ack): pode usar mensagens de confirmação (consumidor envia mensagem de ack para um canal do produtor) ou semáforos/shm adicionais
- Controle de contadores (total consumidas por estrutura) - precisa de variável compartilhada ou ser atualizado pelos consumidores

### 6.3. Considerações gerais
- Usar `fork()` para criar `produz`/`consome`
- Comunicação de parâmetros: linha de comando via `exec` (`execl`, `execv`) ou variáveis de ambiente
- Sinais Unix podem ser usados (mencionado no item 2). Úteis para shutdown/coordenação
- Concorrência real (processos distintos) - não threads

## 7. Organização de Arquivos Sugerida

Criar estrutura limpa no diretório do trabalho:

```
Trabalho/
├── trabalho_pratico_02_2026_so.txt  (enunciado)
├── plano_desenvolvimento_mpmc.md    (este plano)
├── Makefile
├── include/
│   ├── common.h       # estruturas, macros, funções comuns
│   ├── shm_utils.h    # helpers para shm/sem
│   ├── msg_utils.h    # helpers para msg
│   └── config.h       # constantes (tamanho 2048, limites)
├── src/
│   ├── mpmc.c         # processo principal (shell)
│   ├── produz.c       # processo produtor
│   ├── consome.c      # processo consumidor
│   ├── shm_utils.c
│   ├── msg_utils.c
│   └── common.c
└── bin/               # executáveis gerados (ou deixar na raiz)
```

## 8. Constantes e Definições

```c
#define SHM_SIZE 2048
#define MAX_MSGS 1024
#define MAX_PROD 1024  // limite superior, mas p é passado
#define PROMPT ">> "
```

## 9. Plano de Implementação por Etapas

### Etapa 1: Preparação e estrutura base
- [ ] **Ler e analisar** completamente o enunciado (feito)
- [ ] **Criar estrutura de diretórios** (`include/`, `src/`, `bin/`)
- [ ] **Definir headers comuns**: estruturas de mensagem, constantes, protótipos
- [ ] **Criar Makefile** básico para compilar `mpmc`, `produz`, `consome`

**Critério de aceite:** Compilação sem erros com estrutura vazia

### Etapa 2: Implementação do processo `mpmc` (shell)
- [ ] **Parser de linha**: ler input, split por espaços
- [ ] **Comando `produz`**: parsear `p_prod n_msgs ack`, validar
- [ ] **Comando `consome`**: parsear `p_prod n_msgs ack`, validar
- [ ] **Comando `shutdown`**: implementar lógica
- [ ] **Criação de filhos**: `fork()` + `exec()` para `produz`/`consome`
- [ ] **Loop interativo** com prompt `>>`

**Critério de aceite:** Shell reconhece comandos, cria processos (ainda sem IPC funcional)

### Etapa 3: Suporte a IPC - Paradigma 2 (Troca de Mensagens) - mais simples inicialmente
Recomendado começar por **troca de mensagens** (menos complicado para sincronização inicial).

- [ ] **Criar filas**: `msgget` com chave única por estrutura
- [ ] **Definir estrutura `mensagem_t`** com `mtype`, `pid_produtor`, `seq`, `tempo_envio`
- [ ] **Implementar `produz.c`**: enviar mensagens com `msgsnd`
- [ ] **Implementar `consome.c`**: receber mensagens with `msgrcv`
- [ ] **Distribuição striped** quando `p_prod == 0`
- [ ] **Tratamento de erros**: estrutura inexistente

**Critério de aceite:** `mpmc 2 2`, `produz 2 5 2`, `consome 2 2 1`, funcionar (sem ack complexo ainda)

### Etapa 4: Suporte a IPC - Paradigma 1 (Memória Compartilhada)
- [ ] **Criar segmentos shm**: `shmget` + `shmat`, tamanho 2048 bytes
- [ ] **Criar semáforos**: System V semaphores para mutex, vazio/cheio
- [ ] **Implementar buffer circular** dentro do segmento
- [tag] [ ] **Adaptar `produz.c` / `consome.c` para shm**
- [ ] **Calcular espaço** necessário dentro de 2048 bytes

**Critério de aceite:** Funcionar com paradigma 1 para casos simples

### Etapa 5: Sincronização e mecanismo de ACK
- [ ] **ACK=2 (não bloqueante)**: comportamento conforme exemplos
- [ ] **ACK=1 (bloqueante): produtor espera todas consumidas**
  - Para **msg**: criar canal de confirmação (fila adicional ou mensagem de ack). Consumidor envia confirmação após consumir mensagem (ou após completar lote)
  - Para **shm**: usar semáforos ou contador compartilhado + variáveis condicionais/semáforos
- [ ] **Consumidor bloqueado**: se ack==1 e menos mensagens, ficar bloqueado. se ack==2, erro e retornar número consumidas
- [ ] **Confirmação de consumo**: garantir produtor só desbloqueia quando **todas** mensagens produzidas foram consumidas

**Critério de aceite:** Exemplos 3.1, 3.2, 3.3 atendem semântica de ack

### Etapa 6: Distribuição striped e validações
- [ ] **Striped**: `produz 0 15 2` → distribuir 15 mensagens para 5 estruturas (3 por fila), ciclo 1..p
- [ ] **Validação de estrutura inexistente**: retornar erro "fila 3 nao existe" (ou similar)
- [tag] [ ] **Limites**: n_msgs 1-1024

**Critério de aceite:** Exemplo 3.3 funciona

### Etapa 7: Shutdown, tempo de execução e limpeza
- [ ] **Coletar tempo de execução de processos**: registrar timestamps antes/durante/finalização. Usar `waitpid` + opcional `gettimeofday`/`clock_gettime`, também pode trazer info via arquivo temporário
- [ ] **Terminar processos ativos**: enviar `SIGTERM` (e eventualmente `SIGKILL`)
- [ ] **Deletar IPCs**: `shmctl(IPC_RMID)`, `semctl(IPC_RMID), `msgctl(IPC_RMID)`
- [ ] **Imprimir contadores por estrutura**: "número total de mensagens consumidas em cada estrutura p"
- [ ] **Imprimir tempo de operação do `mpmc`**: calcular diferença entre início e shutdown
- [ ] **Limpeza de recursos**

**Critério de aceite:** `shutdown` limpa tudo corretamente, imprime informações solicitadas

### Etapa 8: Testes, debug e ajustes
- [ ] **Testar Exemplo 3.1** (shm)
- [ ] **Testar Exemplo 3.2** (msg)
- [ ] **Testar Exemplo 3.3** (msg, striped)
- [ ] **Casos extremos**: n_msgs=1, ack=2 com menos mensagens, múltiplos consumidores simultâneos
- [ ] **Concorrência**: verificar que `mpmc` volta ao prompt `>>` imediatamente após criar filhos
- [ ] **Validação de memória**: garantir segmento 2048 bytes para shm

**Critério de aceite:** Todos os exemplos rodam conforme descrito

## 10. Dicas de Implementação

### 10.1. Como passar parâmetros para `produz`/`consome`
Opções:
- **Argumentos**: `execl("./produz", "produz", "paradigma", "p", "p_prod", "n_msgs", "ack", ..., NULL)` - simples
- **Variáveis de ambiente**: definir variáveis e ler com `getenv()`
- **Arquivo temporário**: passar caminho via argumento

Recomendo **argumentos da linha de comando** por simplicidade.

### 10.2. Chaves IPC
Para evitar colisões, usar `ftok()` com um arquivo único (pode ser o próprio executável ou `/tmp/mpmc_key`).

### 10.3. Tempo de execução
- Para **processos filhos**: gravar `clock_gettime(CLOCK_MONOTONIC)` no início/fim de `produz.c`/`consome.c` e imprimir no término. Ou usar `gettimeofday`.
- Para **mpmc**: gravar no início e imprimir no `shutdown`.

### 10.4. Gerenciamento de PIDs
Manter lista dinâmica de PIDs filhos em `mpmc` (array) para terminar no shutdown.

### 10.5. Prompt
O `mpmc` deve mostrar `>> ` (conforme enunciado: "O processo mpmc fica esperando comandos no shell com '>>'"). Em alguns exemplos, entrada aparece como `> mpmc ...` e depois `>> ...`

### 10.6. Saída de erro
Exemplo 3.2: `>>>> retorna erro – fila 3 nao existe` (o `>>` aparece múltiplos; pode ser formatação do shell, mas a mensagem de erro deve ser clara)

## 11. Checklist Final

- [ ] Compila com `make` sem warnings críticos
- [ ] Suporta ambos paradigmas (`1` e `2`)
- [ ] Comandos `produz`, `consome`, `shutdown` funcionam
- [ ] Distribuição striped com `p_prod == 0`
- [ ] Validação de estrutura inexistente
- [ ] Ack=1 bloqueia corretamente, ack=2 não bloqueia
- [ ] Consumidor com ack=2 e menos mensagens retorna erro com número consumido
- [ ] Produtor com ack=1 espera todas consumidas
- [ ] Tamanho shm = 2048 bytes
- [ ] Usa shm+sem no paradigma 1, msg no paradigma 2
- [ ] Sinais Unix podem ser usados quando apropriado
- [ ] Shutdown termina tudo, limpa IPCs, imprime tempos e contadores
- [ ] Exemplos 3.1, 3.2, 3.3 reproduzíveis

## 12. Cronograma Sugerido

| Dia | Foco | Meta |
|---|---|---|
| **Dia 1** | Estrutura, Makefile, `mpmc.c` shell básico | Shell funcional parseando comandos |
| **Dia 2** | Paradigma 2 (msg) - `produz`/`consome` básicos | Troca de mensagens sem ack complexo |
| **Dia 3** | Sincronização ack e bloqueios | Comportamento correto para ack=1/2 |
| **Dia 4** | Striped, validações, erros | Exemplo 3.2 e 3.3 |
| **Dia 5** | Paradigma 1 (shm+sem) | Memória compartilhada funcional |
| **Dia 6** | Shutdown, tempos, limpeza | Finalização completa |
| **Dia 7** | Testes com exemplos, ajustes | Todos os exemplos OK |

## 13. Referências Úteis

- **System V IPC:** `shmget`, `shmat`, `shmdt`, `shmctl`, `semget`, `semop`, `semctl`, `msgget`, `msgsnd`, `msgrcv`, `msgctl`
- **Processos:** `fork`, `exec`, `wait`, `waitpid`, `exit`
- **Sinais:** `signal`, `kill`
- **Tempo:** `gettimeofday`, `clock_gettime`
- **Man pages:** `man shm_overview`, `man sem_overview`, `man msg_overview`

**Dica final:** Comece pelo paradigma 2 (filas de mensagens) pois é mais direto. Depois implemente o paradigma 1. Foque primeiro em fazer os exemplos funcionarem, depois polir detalhes. A complexidade maior estará em sincronização e no mecanismo de ack - planeje essas partes com cuidado.
