# Coletor de Dados (tarefa 6)

Processo em C++ que registra, **em disco**, tudo o que acontece na frota, para
rastreabilidade: quais robôs carregaram cada carga (baia e destino) e o
histórico de eventos de cada robô, sempre com *timestamp* (data, hora e ms).
Também envia uma cópia em tempo real ao Organizador, que a repassa à Tela de
Operação (MQTT, Etapa 2).

```
 processos dos robôs                    Coletor de Dados (processo)
 ───────────────────                    ──────────────────────────────────────────────────────
 shm_evento_publicar() ──▶ buffer circular ──▶ thread COLETA ──▶ fila interna ──▶ thread GRAVAÇÃO
                           em /atr_frota        (sem_timedwait)    (mutex+condvar)       │
                                                                                         ├─▶ dados/historico_robo_<id>.log
                                                                                         ├─▶ dados/cargas.csv
                                                                                         └─▶ fila POSIX /atr_coletor_organizador
```

## Threads e sincronização

| Elemento | Mecanismo | Função |
|---|---|---|
| Thread principal | `pthread_sigmask` + `sigwait` | Trata SIGINT/SIGTERM de forma síncrona e chama `encerrar()`. Antes de iniciar, espera o Gestor criar a memória (`sigtimedwait` a cada 200 ms) |
| Thread de coleta | Semáforo `cheios` do buffer compartilhado (`sem_timedwait`, 100 ms) | Consumidora do buffer dos robôs. Só tira o evento de lá e o põe na fila interna: **nunca faz E/S em disco** |
| Thread de gravação | `_mtx` + `_cv_fila` (`pthread_cond_wait`) | Pega a fila inteira de uma vez (`swap`), solta o mutex e grava; a coleta não espera o disco |
| `_mtx` | `pthread_mutex_t` | Protege a fila interna e o contador de uso máximo |
| Buffer compartilhado | Produtor/consumidor com semáforos `vazios`/`cheios` + mutex robusto (já existente em `comum/ipc_robo.hpp`) | Os robôs usam `sem_trywait` e descartam se o buffer enche; por isso a coleta precisa ser rápida |

**Por que duas threads?** O buffer compartilhado tem só 64 posições e os robôs
não esperam por ele (tempo real): se o consumidor travasse num `write` lento,
eventos seriam descartados. Separando a coleta (rápida, sem disco) da gravação
(lenta), o disco só aumenta a fila interna (4096 eventos), nunca a perda no
buffer dos robôs. A fila interna também nunca bloqueia a coleta: se enchesse,
o evento seria descartado e contado.

Encerramento ordenado: a coleta esvazia o que restou no buffer, a gravação
esvazia a fila interna, as cargas ainda não entregues são gravadas como
`incompleta` e as estatísticas são impressas.

## Arquivos gerados (pasta `dados/`, ou `--dir`)

Os arquivos são abertos em modo *append*: uma nova execução **acrescenta** ao
que já existe. Cada linha é uma única chamada `write` e os arquivos recebem
`fdatasync` (a cada carga em `cargas.csv`, e a cada lote nos históricos), então
os dados sobrevivem a `kill -9` do Coletor.

**`historico_robo_<id>.log`**: um arquivo por robô, todos os eventos em ordem:

```
[2026-10-20 06:01:45.568] CARGA: pegou carga na Baia 3 (tarefa 42)
[2026-10-20 06:01:50.752] CARGA: entregou carga na Zona C (tarefa 42)
[2026-10-20 06:02:10.314] DEFEITO: defeito presente: falha no hardware em (50, 105)
[2026-10-20 06:02:12.001] RECARGA: em recarga na Recarga 2 (bateria 14%)
```

**`cargas.csv`**: rastreabilidade, uma linha por carga (e não por robô), `;` como separador:

```
data_hora_entrega;tarefa_id;baia;destino;robos;data_hora_coleta;situacao
2026-10-20 06:01:50.752;42;Baia 3;Zona C;8+9;2026-10-20 06:01:45.568;entregue
-;55;Baia 4;-;13;2026-10-20 06:02:19.815;incompleta
```

A baia, o destino e os robôs vêm dos eventos `CARGA` dos robôs ([coletor/rastreio.cpp](rastreio.cpp)):
a carga é fechada quando **todos** os robôs que a pegaram a entregaram. `situacao`:
`entregue`, `incompleta` (pegou e não entregou, por exemplo se um robô morreu) ou
`entregue_sem_registro_de_coleta` (o evento de coleta foi descartado).

## Interface com o Organizador

Fila POSIX `/atr_coletor_organizador` ([comum/ipc_coletor.hpp](../comum/ipc_coletor.hpp)),
com `struct RegistroColetor`: `REG_EVENTO` (um evento de robô) e `REG_CARGA` (carga
fechada). O envio **não bloqueia**: sem leitor e com a fila cheia, a cópia em tempo
real é perdida (contada nas estatísticas), mas o registro continua salvo em disco.
O Organizador consome com `mq_coletor_abrir_leitura()` + `mq_coletor_receber()`
(exemplo em [ferramentas/escuta_coletor.cpp](../ferramentas/escuta_coletor.cpp)) e,
na Etapa 2, publica no tópico MQTT. O histórico completo está sempre nos arquivos.

## Estatísticas (análise de escalonabilidade, Etapa 2)

Ao encerrar, o Coletor imprime: eventos gravados, cargas registradas, perdas (buffer
compartilhado, fila interna, fila do Organizador), erros de gravação, **uso máximo da
fila interna** e a **latência máxima** entre a publicação do evento pelo robô e a sua
gravação em disco.

## Compilar e testar

```sh
make                    # gera bin/coletor e bin/escuta_coletor
make teste-coletor      # teste de integração (testes/teste_coletor.sh)
```

Manualmente, a partir da raiz do projeto:

```sh
bin/gestor_robos &                # cria a memória compartilhada
bin/coletor &                     # grava em dados/
bin/escuta_coletor &              # faz o papel do Organizador (opcional)
bin/novo_robo 1 0 0
bin/comando_robo 1 ir 20 10
cat dados/historico_robo_1.log
cat dados/cargas.csv
```

**Atenção:** o Coletor é o **único consumidor** do buffer de eventos. Não rode
`bin/monitor_frota eventos` ao mesmo tempo: os dois dividiriam os eventos entre si.
Se o Coletor for iniciado antes do Gestor, ele espera o Gestor aparecer.
Se a memória `/atr_frota` for removida com o Gestor rodando (`rm /dev/shm/atr_frota`),
reinicie o Coletor.
