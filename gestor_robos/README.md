# Gestor de Robôs (tarefa 1)

Processo em C++ que recebe os anúncios de "novo robô disponível", cria um
**processo** para cada robô e avisa o Organizador da Logística de forma
**assíncrona**.

```
 simulador / novo_robo                 Gestor de Robôs (processo)                 Organizador
 ─────────────────────                 ───────────────────────────                 ───────────
 "NOVO_ROBO 3 10 20" ──FIFO──▶ thread receptora ──fork+exec──▶ processo robo 3 10 20
                               │        │                              │
                               │   tabela de robôs ◀── thread monitora ◀┘ waitpid()
                               │   (mutex + condvar)        │
                               └────────────┬───────────────┘
                                            ▼
                               fila POSIX /atr_gestor_organizador ─────▶ ROBO_NOVO / ROBO_REMOVIDO
```

## Threads e sincronização

| Elemento | Mecanismo | Função |
|---|---|---|
| Thread principal | `pthread_sigmask` + `sigwait` | Trata SIGINT/SIGTERM de forma síncrona e chama `encerrar()` |
| Thread receptora | `poll` no FIFO + `eventfd` | Bloqueia até chegar um anúncio; o `eventfd` a acorda no encerramento |
| Thread monitora | `waitpid(-1)` | Detecta a morte de um robô (falha, `kill`) e libera a vaga |
| `_mtx` | `pthread_mutex_t` | Protege a tabela `_robos[MAX_ROBOS]` e `_n_ativos` |
| `_cv_tem_filhos` | `pthread_cond_t` | A monitora dorme enquanto não há filhos (senão `waitpid` daria `ECHILD` em loop) |
| `_cv_sem_filhos` | `pthread_cond_timedwait` | `encerrar()` espera até 2 s os robôs saírem após SIGTERM, depois manda SIGKILL |
| `_mtx_envio` | Passagem de bastão `_mtx` → `_mtx_envio` | Os eventos chegam na fila na mesma ordem das mudanças na tabela |

Detalhes:

- O `fork` acontece com `_mtx` travado, então o filho está na tabela antes de a
  monitora poder recolhê-lo.
- No filho, entre o `fork` e o `exec`, só são chamadas funções async-signal-safe
  (o `argv` é montado antes). A máscara de sinais é restaurada, porque o `exec` a herda.
- `PR_SET_PDEATHSIG`: se o Gestor morrer (até com `kill -9`), os robôs recebem SIGTERM.
- O FIFO é aberto com `O_RDWR` (nunca dá EOF) e `O_CLOEXEC` (os robôs não herdam o descritor).
- O envio para o Organizador usa `mq_timedsend` com prazo de 100 ms: se o
  Organizador travar, o Gestor não trava junto.

## Interfaces

**Entrada** ([fonte_novos_robos.hpp](fonte_novos_robos.hpp)): uma linha `NOVO_ROBO <id> [<x> <y>]`.
Sem posição, o robô nasce em `POS_INICIAL_X/Y`. Na Etapa 1 a fonte é o FIFO
`run/novo_robo.fifo`. Na Etapa 2 basta criar uma `FonteMqtt : FonteNovosRobos`
inscrita em `atr/robos/novo` e reaproveitar `parse_msg_novo_robo`.

**Saída** ([comum/ipc_gestor.hpp](../comum/ipc_gestor.hpp)): `struct EventoGestor` na fila
`/atr_gestor_organizador`. O Organizador consome com `mq_gestor_abrir_leitura()` +
`mq_gestor_receber()` (exemplo em [ferramentas/escuta_gestor.cpp](../ferramentas/escuta_gestor.cpp)).

**Processo do robô**: o Gestor executa `bin/robo <id> <x> <y>` e espera que ele
termine com SIGTERM. Hoje `bin/robo` é um stub ([robo/robo_stub.cpp](../robo/robo_stub.cpp))
até as tarefas 2–4 serem implementadas.

## Compilar e testar

```sh
make                 # gera bin/gestor_robos, bin/robo, bin/novo_robo, bin/escuta_gestor
make teste-gestor    # teste de integração (testes/teste_gestor.sh)
```

Manualmente, em três terminais, a partir da raiz do projeto:

```sh
bin/escuta_gestor             # faz o papel do Organizador
bin/gestor_robos
bin/novo_robo 1 10 20         # faz o papel do simulador
```

Se a fila ficar com eventos antigos de uma execução anterior: `rm /dev/mqueue/atr_gestor_organizador`.
