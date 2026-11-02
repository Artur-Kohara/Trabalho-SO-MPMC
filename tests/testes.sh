#!/usr/bin/env bash
# Bateria de testes do MPMC. Uso: ./tests/testes.sh  (ou: make test)
#
# Cada execução do mpmc roda em uma sessão própria (setsid), com a entrada
# ligada a um FIFO. Ao fim de cada execução são verificados apenas os recursos
# daquela execução: objetos IPC que sobraram, processos remanescentes da sessão
# e arquivos deixados em /tmp. Assim a bateria não interfere em outros programas
# que usem IPC na mesma máquina, nem sofre interferência deles.

cd "$(dirname "$0")/.." || exit 1
make -s all || exit 1
BIN="$PWD"
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT
trap '' PIPE
PASS=0
FAIL=0
PROBLEMAS=()

ok()      { PASS=$((PASS + 1)); echo "   ok    - $1"; }
falha()   { FAIL=$((FAIL + 1)); echo "   FALHA - $1"; }
check()   { if "${@:2}"; then ok "$1"; else falha "$1"; fi; }
tem()     { grep -qE -- "$2" "$1"; }
nao_tem() { ! grep -qE -- "$2" "$1"; }
conta()   { grep -cE -- "$2" "$1"; }
igual()   { [[ "$1" == "$2" ]]; }

# Total de mensagens consumidas impresso no shutdown para a estrutura $2
total() { sed -nE "s/^Estrutura $2: ([0-9]+) mensagens consumidas/\1/p" "$1"; }

# Tempo de execução reportado pelo mpmc para o primeiro processo do tipo $2
tempo() { sed -nE "s/^Processo $2 \(PID [0-9]+\).*tempo de execução = ([0-9.]+).*/\1/p" "$1" | head -1; }
menor() { awk -v a="$1" -v b="$2" 'BEGIN { exit !(a != "" && a < b) }'; }

# Objetos IPC do usuário ("tipo id"), exceto segmentos de memória privados,
# que ambientes gráficos criam e removem o tempo todo
ids_ipc() {
    awk -v u="$(id -u)" 'FNR > 1 {
        if (FILENAME ~ /msg$/ && $8 == u) print "q " $2
        else if (FILENAME ~ /shm$/ && $8 == u && $1 != 0) print "m " $2
        else if (FILENAME ~ /sem$/ && $5 == u) print "s " $2
    }' /proc/sysvipc/msg /proc/sysvipc/shm /proc/sysvipc/sem | sort
}

# Verifica se o objeto IPC "tipo id" ainda existe
existe_ipc() {
    local arquivo
    case $1 in q) arquivo=msg ;; m) arquivo=shm ;; s) arquivo=sem ;; esac
    awk -v id="$2" 'FNR > 1 && $2 == id { achou = 1 } END { exit !achou }' "/proc/sysvipc/$arquivo"
}

MPMC_PID=
SAIDA=
IDS_ANTES=

# iniciar <saida> <p> <paradigma>: inicia o mpmc em uma sessão própria, com a
# entrada ligada a um FIFO aberto no descritor 3
iniciar() {
    SAIDA=$1
    IDS_ANTES=$(ids_ipc)
    rm -f "$TMP/.entrada_mpmc" "$SAIDA.ipc"
    mkfifo "$TMP/.entrada_mpmc"
    setsid "$BIN/mpmc" "$2" "$3" < "$TMP/.entrada_mpmc" > "$SAIDA" 2>&1 &
    MPMC_PID=$!
    exec 3> "$TMP/.entrada_mpmc"
}

# Copia o registro de recursos IPC da execução, para conferir depois que
# todos foram removidos
registrar_recursos() {
    [[ -e /tmp/mpmc_$MPMC_PID.ipc ]] && cp "/tmp/mpmc_$MPMC_PID.ipc" "$SAIDA.ipc"
}

# enviar <comando|@segundos|!comando>...
#   comando   : enviado ao mpmc seguido de uma pausa de 0.3s
#   !comando  : enviado sem pausa
#   @segundos : pausa adicional
enviar() {
    local c pausa
    for c in "$@"; do
        pausa=0.3
        case $c in
            @*) sleep "${c#@}"; continue ;;
            !*) c=${c#!}; pausa= ;;
        esac
        [[ $c == shutdown* ]] && registrar_recursos
        echo "$c" >&3 2>/dev/null
        [[ -n $pausa ]] && sleep "$pausa"
    done
}

# Espera o mpmc terminar (no máximo 60s) e grava o código de saída em <saida>.rc
aguardar_mpmc() {
    local i
    for i in $(seq 600); do
        kill -0 "$MPMC_PID" 2>/dev/null || break
        sleep 0.1
    done
    if kill -0 "$MPMC_PID" 2>/dev/null; then
        PROBLEMAS+=("mpmc não terminou em 60s")
        kill -9 -- -"$MPMC_PID" 2>/dev/null
    fi
    wait "$MPMC_PID" 2>/dev/null
    echo $? > "$SAIDA.rc"
}

# Confere se a execução deixou objetos IPC, processos ou arquivos para trás
verificar_execucao() {
    local tipo id novos restantes
    sleep 0.2
    if [[ -s $SAIDA.ipc ]]; then
        while read -r tipo id; do
            existe_ipc "$tipo" "$id" && PROBLEMAS+=("IPC residual: $tipo $id")
        done < "$SAIDA.ipc"
    else
        novos=$(comm -13 <(echo "$IDS_ANTES") <(ids_ipc) | tr '\n' ' ')
        [[ -n $novos ]] && PROBLEMAS+=("IPC residual: $novos")
    fi
    restantes=$(pgrep -s "$MPMC_PID" | tr '\n' ' ')
    if [[ -n $restantes ]]; then
        PROBLEMAS+=("processos remanescentes: $restantes")
        kill -9 $restantes 2>/dev/null
    fi
    if compgen -G "/tmp/mpmc_${MPMC_PID}[._]*" > /dev/null; then
        PROBLEMAS+=("arquivos residuais em /tmp")
    fi
}

# encerrar_execucao: fecha a entrada do mpmc (EOF), espera seu término e
# verifica o que ficou para trás
encerrar_execucao() {
    registrar_recursos
    exec 3>&-
    aguardar_mpmc
    verificar_execucao
}

# executar <saida> <p> <paradigma> <comandos>...
executar() {
    local out=$1 p=$2 par=$3
    shift 3
    iniciar "$out" "$p" "$par"
    enviar "$@"
    encerrar_execucao
}

teste() {
    echo "== $1"
    PROBLEMAS=()
}

fim_teste() {
    local p
    if [[ ${#PROBLEMAS[@]} -eq 0 ]]; then
        ok "nenhum IPC residual nem processo remanescente"
    else
        for p in "${PROBLEMAS[@]}"; do falha "$p"; done
    fi
}

# ---------------------------------------------------------------------------
teste "Exemplo 3.1 (4 produtores, memória compartilhada)"
O=$TMP/ex31
executar "$O" 4 1 "produz 3 2 1 &" "consome 2 2 1 &" "produz 2 5 1 &" @0.5 "shutdown"
check "estrutura 2 consumiu 2 mensagens" igual "$(total "$O" 2)" 2
check "estruturas 1, 3 e 4 sem consumo" igual "$(total "$O" 1)$(total "$O" 3)$(total "$O" 4)" 000
check "consumidor terminou" tem "$O" "CONSOME.*Todas as 2 mensagens"
check "produtores ficaram bloqueados" nao_tem "$O" "Confirmação completa"
check "tempo do mpmc impresso" tem "$O" "Tempo total: [0-9.]+ segundos"
fim_teste

teste "Exemplo 3.2 (2 produtores, troca de mensagens)"
O=$TMP/ex32
executar "$O" 2 2 "produz 3 2 1 &" "consome 2 2 1 &" "consome 2 2 1 &" "produz 2 5 2 &" @0.5 "shutdown"
check "erro para fila 3 inexistente" tem "$O" "fila 3 nao existe"
check "os dois consumidores terminaram" igual "$(conta "$O" "CONSOME.*Todas as 2 mensagens")" 2
check "estrutura 2 consumiu 4 mensagens" igual "$(total "$O" 2)" 4
check "produtor ack=2 não aguardou" tem "$O" "ack=2: não aguardando"
fim_teste

teste "Exemplo 3.3 (5 produtores, striped)"
O=$TMP/ex33
executar "$O" 5 2 "produz 0 15 2 &" "consome 2 3 1 &" "consome 1 2 1 &" "produz 3 2 1 &" @0.5 "shutdown"
check "15 mensagens enviadas em striped" tem "$O" "Total de 15 mensagens enviadas com distribuição striped"
check "3 mensagens em cada fila" igual "$(conta "$O" "PRODUZ.*enviada para fila 4")" 3
check "estrutura 1 = 2, estrutura 2 = 3" igual "$(total "$O" 1)-$(total "$O" 2)" 2-3
check "produtor da fila 3 ficou bloqueado" nao_tem "$O" "Confirmação completa"
fim_teste

teste "Exemplos do enunciado colados de uma vez (sem pausas)"
O=$TMP/colado_31
executar "$O" 4 1 "!produz 3 2 1 &" "!consome 2 2 1 &" "!produz 2 5 1 &" "!shutdown"
check "3.1: estrutura 2 consumiu 2" igual "$(total "$O" 2)" 2
check "3.1: produtores bloqueados" nao_tem "$O" "Confirmação completa"
O=$TMP/colado_32
executar "$O" 2 2 "!produz 3 2 1 &" "!consome 2 2 1 &" "!consome 2 2 1 &" "!produz 2 5 2 &" "!shutdown"
check "3.2: estrutura 2 consumiu 4" igual "$(total "$O" 2)" 4
O=$TMP/colado_33
executar "$O" 5 2 "!produz 0 15 2 &" "!consome 2 3 1 &" "!consome 1 2 1 &" "!produz 3 2 1 &" "!shutdown"
check "3.3: estruturas 1 e 2 consumiram 2 e 3" igual "$(total "$O" 1)-$(total "$O" 2)" 2-3
check "nenhum processo precisou de SIGKILL" nao_tem "$TMP/colado_31" "Forçando"
fim_teste

# PIDs dos consumidores na ordem em que foram criados, e as mensagens (seq) que cada um consumiu
pids_consumidores() { sed -nE 's/.*Processo consome \(PID ([0-9]+)\) criado\./\1/p' "$1"; }
seqs_de() { sed -nE "s/.*CONSOME PID $2\] Mensagem ([0-9]+) consumida.*/\1/p" "$1" | tr -d '\n'; }

for par in 2 1; do
    teste "Consumidores bloqueantes da mesma estrutura atendidos por ordem de chegada (paradigma $par)"
    O=$TMP/ordem_$par
    executar "$O" 2 $par "consome 2 2 1 &" "consome 2 2 1 &" "produz 2 5 2 &" @0.5 "shutdown"
    pids=($(pids_consumidores "$O"))
    check "primeiro consumidor recebeu as mensagens 1 e 2" igual "$(seqs_de "$O" "${pids[0]}")" 12
    check "segundo consumidor recebeu as mensagens 3 e 4" igual "$(seqs_de "$O" "${pids[1]}")" 34
    check "estrutura 2 consumiu 4" igual "$(total "$O" 2)" 4
    fim_teste
done

teste "Vez repassada quando o consumidor da frente é morto"
O=$TMP/vez_kill
iniciar "$O" 1 2
enviar "consome 1 1 1 &" "consome 1 1 1 &"
pids=($(pids_consumidores "$O"))
kill -9 "${pids[0]}"
enviar @0.3 "produz 1 1 2 &" @0.5 "shutdown"
encerrar_execucao
check "o segundo consumidor recebe a vez e consome" igual "$(seqs_de "$O" "${pids[1]}")" 1
fim_teste

teste "Consumidor ack=2 não espera a vez"
O=$TMP/ack2_sem_vez
executar "$O" 1 2 "consome 1 5 1 &" "consome 1 1 2 &" @0.3 "shutdown"
check "retorna na hora informando que não há mensagens" tem "$O" "CONSOME PID [0-9]+\] Erro: não há mensagens disponíveis"
fim_teste

teste "FIFO e campos da mensagem (paradigma 1)"
O=$TMP/fifo
executar "$O" 1 1 "produz 1 5 2 &" "consome 1 5 1 &" @0.3 "shutdown"
check "ordem seq=1..5" igual "$(sed -nE 's/.*consumida da memória 1 \| pid_produtor=[0-9]+ \| seq=([0-9]+) .*/\1/p' "$O" | tr -d '\n')" 12345
check "pid e tempo de envio presentes" tem "$O" "pid_produtor=[1-9][0-9]* \| seq=1 \| tempo_envio=[0-9]+\.[0-9]{6}"
fim_teste

teste "Striped em memória compartilhada"
O=$TMP/striped_shm
executar "$O" 3 1 "produz 0 6 2 &" "consome 1 2 2 &" "consome 2 2 2 &" "consome 3 2 2 &" @0.3 "shutdown"
check "2 mensagens em cada estrutura" igual "$(total "$O" 1)$(total "$O" 2)$(total "$O" 3)" 222
fim_teste

teste "Entrada: & colado, prompt copiado e quantidade de argumentos"
O=$TMP/entrada_shell
executar "$O" 1 2 "produz 1 2 2&" ">> consome 1 2 2 &" "produz 1 2 2 9" "consome 1 1" "shutdown &"
check "comandos com & colado e com prompt são aceitos" igual "$(conta "$O" "Processo .* criado")" 2
check "argumentos a mais ou a menos são rejeitados" igual "$(conta "$O" "Erro: uso correto")" 2
check "shutdown seguido de & é aceito" tem "$O" "SHUTDOWN"
check "estrutura 1 consumiu 2" igual "$(total "$O" 1)" 2
fim_teste

teste "Linha longa demais"
O=$TMP/linha_longa
longa=$(printf 'produz 1 1 2 %0600d' 0)
executar "$O" 1 2 "$longa" "consome 1 1 2 &" "shutdown"
check "linha longa é rejeitada" tem "$O" "linha muito longa"
check "o resto da linha não vira outro comando" nao_tem "$O" "Comando desconhecido"
check "o comando seguinte funciona" tem "$O" "Processo consome .* criado"
fim_teste

teste "Capacidade das estruturas"
O=$TMP/capacidade_shm
executar "$O" 1 1 "produz 1 41 2 &" @0.5 "shutdown"
check "segmento de 2048 bytes comporta 41 mensagens" tem "$O" "capacidade de 41 mensagens por estrutura"
check "produtor ack=2 com 41 mensagens não bloqueia" tem "$O" "PRODUZ PID [0-9]+\] Finalizado"
O=$TMP/capacidade_msg
executar "$O" 1 2 "produz 1 400 2 &" @0.5 "shutdown"
check "fila comporta 400 mensagens sem bloquear o produtor ack=2" tem "$O" "PRODUZ PID [0-9]+\] Finalizado"
fim_teste

teste "Validação de comandos"
O=$TMP/validacao
executar "$O" 2 2 "produz abc 2 1" "produz 1 0 1" "produz 1 1025 1" "produz 1 2 3" "produz 1 2" \
    "consome 0 1 1" "consome 3 1 1" "consome 1 x 1" "foo" "shutdown"
check "nenhum processo criado" igual "$(conta "$O" "Processo .* criado")" 0
check "comando desconhecido" tem "$O" "Comando desconhecido: 'foo'"
check "erro de n_msgs" igual "$(conta "$O" "n_msgs deve estar entre 1 e 1024")" 2
check "erro de fila inexistente" igual "$(conta "$O" "fila [03] nao existe")" 2
fim_teste

teste "ACK: produtor não é confirmado pelo consumo de mensagens alheias (paradigma 1)"
O=$TMP/ack_alheio
executar "$O" 1 1 "produz 1 3 2 &" "consome 1 3 1 &" @0.3 "produz 1 2 1 &" @0.5 "shutdown"
check "produtor não confirmado sem consumo próprio" nao_tem "$O" "Confirmação completa"
check "estrutura 1 consumiu 3" igual "$(total "$O" 1)" 3
O=$TMP/ack_alheio_b
executar "$O" 1 1 "produz 1 3 2 &" "consome 1 3 1 &" @0.3 "produz 1 2 1 &" "consome 1 2 1 &" @0.5 "shutdown"
check "produtor confirmado após consumo próprio" igual "$(conta "$O" "Confirmação completa")" 1
fim_teste

teste "ACK: striped com ack=1 (paradigma 2)"
O=$TMP/ack_striped_msg
executar "$O" 3 2 "produz 0 3 1 &" "consome 1 1 1 &" "consome 2 1 1 &" @0.5 "shutdown"
check "não confirma enquanto falta a fila 3" nao_tem "$O" "Confirmação completa"
O=$TMP/ack_striped_msg_b
executar "$O" 3 2 "produz 0 3 1 &" "consome 1 1 1 &" "consome 2 1 1 &" "consome 3 1 1 &" @0.5 "shutdown"
check "confirma após consumo em todas as filas" tem "$O" "Confirmação completa"
fim_teste

teste "ACK: striped com ack=1 (paradigma 1)"
O=$TMP/ack_striped_shm
executar "$O" 2 1 "produz 0 4 1 &" @0.5 "shutdown"
check "produtor aguarda confirmação" nao_tem "$O" "PRODUZ.*Finalizado"
O=$TMP/ack_striped_shm_b
executar "$O" 2 1 "produz 0 4 1 &" "consome 1 2 1 &" "consome 2 2 1 &" @0.5 "shutdown"
check "confirma após consumo em todas as estruturas" tem "$O" "Confirmação completa"
fim_teste

for par in 2 1; do
    teste "ACK: confirmação vai para o produtor certo (paradigma $par)"
    O=$TMP/ack_destino_$par
    executar "$O" 1 $par "produz 1 1 1 &" "produz 1 1 1 &" "consome 1 2 1 &" @0.5 "shutdown"
    check "os dois produtores confirmados" igual "$(conta "$O" "Confirmação completa")" 2
    O=$TMP/ack_destino_b_$par
    executar "$O" 1 $par "produz 1 1 1 &" "consome 1 1 2 &" @0.5 "shutdown"
    check "consumidor ack=2 também confirma" igual "$(conta "$O" "Confirmação completa")" 1
    fim_teste
done

teste "Tempo de execução de processos já terminados"
O=$TMP/tempo
executar "$O" 1 2 "consome 1 1 2 &" @1.5 "shutdown"
check "tempo do consumidor < 0.5s (reportado: $(tempo "$O" CONSOME))" menor "$(tempo "$O" CONSOME)" 0.5
check "processo terminado não é forçado" nao_tem "$O" "Forçando"
fim_teste

teste "Limpeza no fim da entrada (EOF)"
O=$TMP/eof
executar "$O" 2 2 "produz 1 3 1 &" "consome 2 1 1 &"
check "EOF executa o shutdown" tem "$O" "SHUTDOWN"
fim_teste

for sig in INT TERM HUP QUIT; do
    teste "Limpeza ao receber SIG$sig"
    O=$TMP/sig_$sig
    iniciar "$O" 2 1
    enviar "produz 1 3 1 &" @0.5
    registrar_recursos
    kill -$sig "$MPMC_PID"
    aguardar_mpmc
    exec 3>&-
    verificar_execucao
    check "SIG$sig executa o shutdown" tem "$O" "SHUTDOWN"
    fim_teste
done

teste "Ctrl+C no terminal (SIGINT para todo o grupo de processos)"
O=$TMP/ctrl_c
iniciar "$O" 2 2
enviar "produz 1 3 1 &" "consome 2 5 1 &" @0.3
registrar_recursos
kill -INT -- -"$MPMC_PID"
aguardar_mpmc
exec 3>&-
verificar_execucao
check "mpmc executa o shutdown" tem "$O" "Sinal 2 .* recebido"
check "filhos terminados pelo SIGINT" igual "$(conta "$O" "terminado pelo sinal 2")" 2
fim_teste

teste "Shutdown logo após um comando não precisa de SIGKILL"
forcados=0
for rodada in 1 2 3 4 5; do
    O=$TMP/shutdown_imediato_$rodada
    executar "$O" 1 2 "!consome 1 1 1 &" "!shutdown"
    tem "$O" "Forçando" && forcados=$((forcados + 1))
done
check "nenhuma das 5 rodadas precisou de SIGKILL ($forcados)" igual "$forcados" 0
fim_teste

teste "Relatório do shutdown"
O=$TMP/relatorio
executar "$O" 1 1 "produz 1 3 2 &" "consome 1 3 1 &" @0.3 "shutdown"
check "tempo de CPU de cada processo" igual "$(conta "$O" "tempo de execução = [0-9.]+ segundos \(CPU: [0-9.]+ segundos\)")" 2
fim_teste

teste "Muitos processos filhos"
O=$TMP/muitos_filhos
cmds=()
for _ in $(seq 100); do cmds+=("!consome 1 1 2 &"); done
executar "$O" 1 2 "${cmds[@]}" @1.5 "shutdown"
check "os 100 consumidores aparecem no relatório" igual "$(conta "$O" "^Processo CONSOME")" 100
fim_teste

teste "Saída para um pipe fechado (SIGPIPE)"
IDS_ANTES=$(ids_ipc)
rm -f "$TMP/entrada_pipe"
mkfifo "$TMP/entrada_pipe"
( setsid "$BIN/mpmc" 1 2 < "$TMP/entrada_pipe" | head -c 1 > /dev/null ) &
exec 4> "$TMP/entrada_pipe"
echo "produz 1 2 2 &" >&4
sleep 0.5
echo "shutdown" >&4
exec 4>&-
wait $!
sleep 0.3
check "nenhum IPC residual" igual "$(comm -13 <(echo "$IDS_ANTES") <(ids_ipc) | wc -l)" 0
mortos=0
for f in /tmp/mpmc_*.ipc; do
    [[ -e $f ]] || continue
    pid=${f#/tmp/mpmc_}; pid=${pid%.ipc}
    kill -0 "$pid" 2>/dev/null || mortos=$((mortos + 1))
done
check "nenhum registro abandonado em /tmp" igual "$mortos" 0
fim_teste

teste "Limites de p"
O=$TMP/p1025
executar "$O" 1025 2 "shutdown"
check "p=1025 rejeitado" igual "$(cat "$O.rc")" 1
for par in 1 2; do
    O=$TMP/p1024_$par
    executar "$O" 1024 $par "produz 0 1024 2 &" "consome 1024 1 1 &" @1 "shutdown"
    check "p=1024 paradigma $par sem erros de IPC" nao_tem "$O" "Invalid argument|shmget|msgget|semget|semop|msgsnd"
    check "p=1024 paradigma $par: estrutura 1024 consumiu 1" igual "$(total "$O" 1024)" 1
done
fim_teste

for par in 1 2; do
    teste "Mecanismos IPC usados pelo paradigma $par"
    O=$TMP/mecanismos_$par
    iniciar "$O" 2 $par
    enviar "produz 1 2 1 &" "consome 1 2 1 &"
    criados=$(cut -d' ' -f1 "/tmp/mpmc_$MPMC_PID.ipc" | sort -u | tr -d '\n')
    enviar "shutdown"
    encerrar_execucao
    if [[ $par == 1 ]]; then
        check "apenas memória compartilhada e semáforos (criados: $criados)" igual "$criados" ms
    else
        check "apenas filas de mensagens (criados: $criados)" igual "$criados" q
    fi
    check "estrutura 1 consumiu 2" igual "$(total "$O" 1)" 2
    fim_teste
done

teste "Contador com consumidores concorrentes (paradigma 2)"
O=$TMP/contador
cmds=("!produz 1 1000 2 &")
for _ in $(seq 10); do cmds+=("!consome 1 100 1 &"); done
executar "$O" 1 2 "${cmds[@]}" @3 "shutdown"
check "1000 mensagens contabilizadas" igual "$(total "$O" 1)" 1000
fim_teste

for par in 1 2; do
    teste "Carga: 1024 mensagens com ack=1 (paradigma $par)"
    O=$TMP/carga_$par
    executar "$O" 1 $par "produz 1 1024 1 &" "consome 1 1024 1 &" @3 "shutdown"
    check "produtor confirmado" tem "$O" "Confirmação completa"
    check "1024 consumidas" igual "$(total "$O" 1)" 1024
    fim_teste
done

for par in 2 1; do
    teste "ACK: vários produtores grandes na mesma estrutura não travam (paradigma $par)"
    O=$TMP/ack_volume_$par
    executar "$O" 1 $par "!produz 1 1024 1 &" "!produz 1 1024 1 &" "!produz 1 1024 1 &" \
        "!consome 1 1024 1 &" "!consome 1 1024 1 &" "!consome 1 1024 1 &" @6 "shutdown"
    check "os três produtores confirmados" igual "$(conta "$O" "Confirmação completa")" 3
    check "3072 consumidas" igual "$(total "$O" 1)" 3072
    fim_teste
done

teste "Execução a partir de outro diretório"
O=$TMP/outro_dir
pushd "$TMP" > /dev/null
executar "$O" 1 2 "produz 1 1 2 &" "consome 1 1 2 &" @0.3 "shutdown"
popd > /dev/null
check "filhos executados" igual "$(total "$O" 1)" 1
fim_teste

teste "Consumidor ack=2 com mensagens insuficientes"
O=$TMP/parcial
executar "$O" 1 2 "produz 1 2 2 &" "consome 1 5 2 &" @0.3 "shutdown"
check "informa quantas foram consumidas" tem "$O" "foram consumidas apenas 2"
check "status de erro reportado no shutdown" tem "$O" "CONSOME.*código 1"
fim_teste

teste "Recursos de um mpmc morto com kill -9 são removidos na execução seguinte"
O=$TMP/kill9
iniciar "$O" 2 1
enviar "produz 1 3 1 &" @0.3
registrar_recursos
morto=$MPMC_PID
kill -9 "$morto"
aguardar_mpmc
exec 3>&-
sleep 0.5
check "filhos terminam junto com o mpmc" [ -z "$(pgrep -s "$morto")" ]
pkill -9 -s "$morto" 2>/dev/null
sobras=0
while read -r tipo id; do existe_ipc "$tipo" "$id" && sobras=$((sobras + 1)); done < "$O.ipc"
check "kill -9 deixa recursos para trás ($sobras)" [ "$sobras" -gt 0 ]
O2=$TMP/kill9_limpeza
executar "$O2" 1 2 "shutdown"
check "a execução seguinte remove as sobras" tem "$O2" "Removidos [0-9]+ recursos IPC"
restantes=0
while read -r tipo id; do existe_ipc "$tipo" "$id" && restantes=$((restantes + 1)); done < "$O.ipc"
check "nenhum recurso da execução morta resta" igual "$restantes" 0
check "registro da execução morta removido" [ ! -e "/tmp/mpmc_$morto.ipc" ]
fim_teste

teste "Produtor sai com erro quando o envio falha"
"$BIN/produz" 2 1 1 2 1 -1 -1 > "$TMP/produz_erro" 2>&1
check "código de saída 1" igual "$?" 1
check "erro informado e nenhum sucesso anunciado" tem "$TMP/produz_erro" "msgsnd: Invalid argument"
check "nenhuma mensagem contada como enviada" tem "$TMP/produz_erro" "Total de 0 mensagens"
fim_teste

teste "Encerramento gracioso dos filhos no shutdown"
O=$TMP/gracioso
executar "$O" 2 2 "produz 2 3 1 &" "consome 1 5 1 &" "produz 1 2 2 &" @0.3 "shutdown"
check "filhos bloqueados imprimem o próprio tempo" igual "$(conta "$O" "(PRODUZ|CONSOME) PID [0-9]+\] Encerrado pelo sinal 15")" 2
check "mpmc registra o término pelo sinal" igual "$(conta "$O" "terminado pelo sinal 15")" 2
check "consumidor informa quantas consumiu" tem "$O" "Interrompido após consumir 2 de 5"
check "nenhum filho precisou de SIGKILL" nao_tem "$O" "Forçando"
fim_teste

echo
echo "Resultado: $PASS ok, $FAIL falhas"
[[ $FAIL -eq 0 ]]
