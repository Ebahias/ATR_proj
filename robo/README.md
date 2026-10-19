# Processo do robô (tarefa 2)

Cada robô é um processo (`bin/robo <id> <slot> <x> <y>`) criado pelo Gestor de Robôs.

```
                          processo do robô
 Organizador ─┐        ┌───────────────────────────────────────────────────┐
 / operador   │  fila  │  thread Navegação (T1=50ms)                       │
 comando_robo ├──────▶ │   ├─ esvazia a fila de comandos ──┐               │
              │ POSIX  │   └─ escreve setpoint ───────────┐│               │
              │        │                                  ▼▼               │
              │        │  ContextoRobo (mutex) ◀──▶ HardwareRobo (Tabela 1)│
              │        │                                  ▲  └─ thread física (20ms, só Etapa 1)
              │        │  thread Status (T2=100ms)        │                │
              │        │   ├─ lê hardware ────────────────┘                │
              │        │   └─ publica status / eventos ──┐                 │
              │        └─────────────────────────────────┼─────────────────┘
              │                                          ▼
 Organizador ◀┴──────── memória compartilhada /atr_frota: tabela de status ── buffer de eventos ──▶ Coletor
```

## Threads

| Thread | Período | Função |
|---|---|---|
| principal | – | `sigwait(SIGINT/SIGTERM)` e encerramento ordenado (join de todas) |
| Navegação ([navegacao.cpp](navegacao.cpp)) | 50 ms | Comandos da fila, setpoint no hardware, chegada ao destino |
| Status ([status.cpp](status.cpp)) | 100 ms | Lê o hardware, avalia condições, publica o status na memória compartilhada |
| Física ([hardware_robo.cpp](hardware_robo.cpp)) | 20 ms | Simula o hardware (só na Etapa 1; na Etapa 2 vira o cliente MQTT) |

As threads periódicas usam `Periodo` ([comum/periodico.hpp](../comum/periodico.hpp)):
`clock_nanosleep` com tempo absoluto (sem deriva), além de medir o pior tempo de
execução e os prazos perdidos. Esses números são impressos no encerramento e
servem para a análise de escalonabilidade da Etapa 2.

## IPC e sincronização ([comum/ipc_robo.hpp](../comum/ipc_robo.hpp))

| Canal | Mecanismo | Detalhe |
|---|---|---|
| Organizador → robô | Fila POSIX `/atr_robo_<id>_cmd` | Criada e removida pelo Gestor. O robô lê sem bloquear (`O_NONBLOCK`) a cada ciclo. O Organizador envia sem bloquear: fila cheia gera `EAGAIN`, nunca trava. |
| Robô → Organizador | Memória compartilhada `/atr_frota`, tabela `robos[MAX_ROBOS]` | Um mutex por slot, `PTHREAD_PROCESS_SHARED` + `PTHREAD_MUTEX_ROBUST`. Se um robô morre com o lock, o próximo `lock` recebe `EOWNERDEAD` e recupera. |
| Robôs → Coletor | Buffer circular em `/atr_frota` | Produtor/consumidor com semáforos `vazios`/`cheios` (`sem_init` com `pshared=1`) + mutex robusto. O produtor usa `sem_trywait`: um robô nunca bloqueia esperando o Coletor; com buffer cheio, o evento é descartado e contado. |
| Navegação ↔ Status | `ContextoRobo` ([contexto_robo.hpp](contexto_robo.hpp)) | Mutex do processo. O Status bloqueia o movimento ou força um destino; a Navegação define estado, modo e destino. |
| Threads ↔ hardware | `HardwareRobo` | Mutex interno. As threads só conhecem a interface (Tabela 1). |

Nenhum código segura dois locks ao mesmo tempo, então não há como haver deadlock entre eles.

## Comandos ([comum/ipc_robo.hpp](../comum/ipc_robo.hpp), `ComandoRobo`)

`IR_PARA x y`, `PARAR`, `MODO`, `ACOPLAR_E_ERGUER`, `SOLTAR_CARGA`. Em modo
automático, só comandos com origem Organizador movem o robô; em manual, só os do
operador. `PARAR` e `MODO` são aceitos de qualquer origem.
`SIM_FALHA`/`SIM_BATERIA` existem só na Etapa 1, no lugar da interface do simulador.

## O que fica para as tarefas 3 e 4

Os pontos de extensão estão marcados com `TODO tarefa 3` / `TODO tarefa 4`:

- **Tarefa 3** (`navegacao.cpp`, `atualizar_movimento`): rota pelo mapa em vez de
  linha reta e passagem segura pelas passagens estreitas (sugestão: semáforo
  nomeado `sem_open("/atr_passagem_<n>")` com valor inicial igual à capacidade).
- **Tarefa 4** (`status.cpp`, `avaliar_condicoes`): falha → `DEFEITO` + bloquear
  movimento; bateria baixa → destino forçado para a recarga + `RECARREGANDO`;
  distância total percorrida.

## Testar

```sh
make teste-robo
```

Ou manualmente, a partir da raiz do projeto:

```sh
bin/gestor_robos &
bin/monitor_frota eventos &          # papel do Coletor
bin/novo_robo 1 0 0                  # papel do simulador
bin/comando_robo 1 ir 20 10          # papel do Organizador
bin/monitor_frota status             # tabela de status
bin/comando_robo 1 manual --operador
bin/comando_robo 1 ir 5 5 --operador
```
