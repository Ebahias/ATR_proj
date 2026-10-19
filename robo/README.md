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
| Navegação ([navegacao.cpp](navegacao.cpp), [navegador.cpp](navegador.cpp)) | 50 ms | Comandos da fila, rota pelo mapa, passagens estreitas, tarefa de transporte |
| Status ([status.cpp](status.cpp)) | 100 ms | Lê o hardware; trata defeito, bateria/recarga e distância; publica o status na memória compartilhada |
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
| Vagas de recarga | Monitor em `/atr_frota` ([comum/recarga.hpp](../comum/recarga.hpp)) | Mutex robusto + dono de cada vaga; reserva sem bloquear; o Gestor libera a vaga de um robô morto. |
| Passagens estreitas | Monitor em `/atr_frota` ([comum/passagens.hpp](../comum/passagens.hpp)) | Ver "Comando de Navegação". |
| Navegação ↔ Status | `ContextoRobo` ([contexto_robo.hpp](contexto_robo.hpp)) | Mutex do processo. O Status bloqueia o movimento ou força um destino; a Navegação define estado, modo e destino. |
| Threads ↔ hardware | `HardwareRobo` | Mutex interno. As threads só conhecem a interface (Tabela 1). |

Nenhum código segura dois locks ao mesmo tempo, então não há como haver deadlock entre eles.

## Comandos ([comum/ipc_robo.hpp](../comum/ipc_robo.hpp), `ComandoRobo`)

`TAREFA`, `IR_PARA x y`, `PARAR`, `MODO`, `ACOPLAR_E_ERGUER`, `SOLTAR_CARGA`. Em modo
automático, só comandos com origem Organizador movem o robô; em manual, só os do
operador. `PARAR` e `MODO` são aceitos de qualquer origem. Ao trocar de modo, o
robô para e espera ordens de quem passa a comandar.
`SIM_FALHA`/`SIM_BATERIA` existem só na Etapa 1, no lugar da interface do simulador.

## Comando de Navegação (tarefa 3)

### Mapa ([comum/mapa.hpp](../comum/mapa.hpp))

Grafo do galpão de 200 x 120 unidades, com 4 baias a oeste, 4 zonas de entrega a
leste e 2 estações de recarga. Tem três **passagens estreitas**:

| Passagem | Capacidade | Trecho |
|---|---|---|
| P0 Corredor central norte | 1 robô | (70,45) – (130,45) |
| P1 Corredor central sul | 2 robôs | (70,75) – (130,75) |
| P2 Corredor das baias | 3 robôs | (50,45) – (50,75) |

A rota é calculada com Dijkstra (menor caminho). Uma equipe de k robôs
carregando uma caixa só usa passagens com capacidade ≥ k. Os corredores norte,
sul e leste são largos, então sempre existe rota para uma equipe de 4 robôs.
`bin/mapa json` exporta o mapa para o simulador/GUI da Etapa 2, e
`bin/mapa rota x0 y0 x1 y1 [equipe]` mostra a rota calculada.

### Passagens estreitas ([comum/passagens.hpp](../comum/passagens.hpp))

Cada passagem é um **monitor na memória compartilhada**: mutex robusto, máscara
dos robôs dentro, fila FIFO e reservas por equipe.

- **Exclusão com capacidade N:** no máximo N robôs dentro ou com vaga reservada.
- **Justiça:** a fila é FIFO, então ninguém fica esperando para sempre.
- **Equipe:** o primeiro membro a chegar reserva vagas para a equipe inteira,
  que atravessa junta.
- **Sem bloqueio:** a thread de navegação nunca dorme esperando uma vaga. O robô
  fica parado na entrada e tenta de novo a cada ciclo de 50 ms, então continua
  atendendo `PARAR` e troca de modo.
- **Sem deadlock:** não há duas passagens seguidas no mapa, e o robô sai de uma
  antes de pedir a próxima. Ele nunca segura uma vaga esperando outra, o que
  quebra a condição de "posse e espera".
- **Falhas:** se um robô morre dentro da passagem, o Gestor libera a vaga. Se o
  movimento é bloqueado pelo Status, o robô sai da fila para não travar quem está
  atrás. `PARAR` dentro de uma passagem termina a travessia antes de parar.

Escolhemos um monitor em vez de um semáforo contador porque o semáforo não
registra **quem** tem a vaga. Assim, se um robô morresse lá dentro, a vaga se
perderia para sempre; com a máscara por slot, o Gestor consegue devolvê-la.

### Tarefa de transporte (`CMD_TAREFA`)

O Organizador manda para cada robô da equipe o ponto de coleta dele (na baia), o
ponto de entrega dele (na zona), o `tarefa_id` e a equipe (máscara de slots):

```
IR_COLETA ─chegou─▶ AGUARDANDO_EQUIPE ─ACOPLAR (Organizador libera quando a
equipe toda chegou)─▶ ACOPLANDO ─equipe toda atracada─▶ TRANSPORTANDO ─chegou─▶
ENTREGANDO ─equipe toda chegou, solta─▶ CONCLUIDA
```

O transporte em equipe usa uma **barreira por waypoint**. Cada robô publica
quantos waypoints já alcançou (`progresso`) e só segue para o próximo quando
todos os colegas chegaram ao anterior. Essa barreira é lida do status dos colegas
na memória compartilhada, sem ocupar o Organizador. Do mesmo jeito, a equipe só
começa a andar quando todos estão com a carga atracada, e só solta a carga quando
todos chegaram. A fase, o `tarefa_id`, o progresso e a passagem aparecem no
status de cada robô. Os eventos "pegou carga na Baia 3 (tarefa 42)" e "entregou
carga na Zona C (tarefa 42)" vão para o Coletor.

**Interação com o Status:** quando o movimento está bloqueado, o robô para. Um
destino forçado (ex.: recarga) tem prioridade sobre a tarefa, que continua depois.
A navegação nunca tira o robô dos estados `defeito`/`recarregando`, que são
decididos pelo Status.

## Status do Robô (tarefa 4)

A cada 100 ms, a thread de Status lê o hardware, avalia as condições abaixo e
publica o status na memória compartilhada.

| Condição | Reação |
|---|---|
| `i_falha` ou bateria 0% | Estado `defeito`, `bloquear_movimento(true)` (a navegação para e sai das filas) e evento "defeito presente". Comandos de movimento são recusados. Quando o defeito some, o robô retoma o que fazia. |
| Bateria ≤ 20% | Estado `recarregando` (indisponível). Reserva a **vaga de recarga** livre mais próxima e força a navegação até ela. Sem vaga livre, espera na entrada da estação mais próxima e tenta de novo a cada ciclo. |
| Na vaga, bateria ≥ 95% | Libera a vaga e o destino forçado, e o robô retoma o que fazia (inclusive um comando recebido durante a recarga). Os limites de 20% e 95% formam uma histerese, para o robô não oscilar entre recarregar e sair. |
| Bateria baixa carregando caixa | Recarga **adiada** até a entrega: o robô não abandona a equipe no meio do transporte. |
| Bateria baixa em modo manual | Só avisa (evento). Quem decide é o operador. |
| Sempre | Acumula a **distância total percorrida** (publicada em `distancia_total`). |

Ao sair de um defeito ou de uma recarga, o estado volta para o que a navegação
quer agora (`InfoNavegacao::estado_desejado`), e não para uma cópia antiga. A
navegação, por sua vez, nunca tira o robô de `defeito`/`recarregando`
(`mudar_estado_operacional`).

**Vagas de recarga** ([comum/recarga.hpp](../comum/recarga.hpp)): há 3 vagas na
Recarga 1 e 3 na Recarga 2. Como são um recurso disputado por vários processos,
usamos outro monitor na memória compartilhada, com mutex robusto e o dono de cada
vaga registrado. A reserva nunca bloqueia, e o Gestor devolve a vaga de um robô
que morreu. No simulador da Etapa 1, a bateria carrega quando o robô está parado
numa área de recarga.

Eventos para o Coletor: "defeito presente: falha no hardware", "defeito
removido", "bateria 15%: indo para Recarga 2", "em recarga na Recarga 2",
"recarga concluída (bateria 96%)", "recarga adiada até entregar a carga".

## Testar

```sh
make teste-robo         # processo do robô (tarefa 2)
make teste-navegacao    # rotas, passagens e transporte em equipe (tarefa 3)
make teste-status       # defeito, bateria/recarga e distância (tarefa 4)
make teste              # todos
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

# Transporte em equipe: robôs 2 e 3 levam uma caixa da Baia 3 para a Zona C
bin/novo_robo 2 && bin/novo_robo 3
bin/comando_robo 2 tarefa 42 12 75 182 75 --equipe 2,3
bin/comando_robo 3 tarefa 42 18 75 188 75 --equipe 2,3
bin/monitor_frota status             # esperar fase aguardando_equipe nos dois
bin/comando_robo 2 acoplar && bin/comando_robo 3 acoplar
bin/monitor_frota passagens          # ocupação das passagens estreitas

# Bateria e defeito (no lugar do simulador)
bin/comando_robo 2 bateria 15        # vai recarregar
bin/monitor_frota recarga            # vagas de recarga
bin/comando_robo 3 falha 1           # defeito: para e recusa movimento
```
