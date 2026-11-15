"""Física dos robôs: o "hardware embarcado" simulado (Tabela 1 do enunciado).

Cada robô anda em linha reta até o setpoint de posição, gasta bateria ao se
mover, recarrega parado numa estação de recarga e leva um tempo para acoplar
à carga. O modelo é o mesmo do HardwareSimulado do C++ (robo/hardware_robo.cpp),
só que agora quem o executa é o simulador, e as variáveis chegam e saem por
MQTT. Nada aqui depende de rede nem de interface gráfica.
"""

import math
import threading
import time
from dataclasses import dataclass

from simulador import config


def arredondar(v):
    """Como std::lround para v >= 0 (round() do Python arredonda 0.5 para o par)."""
    return int(math.floor(v + 0.5))


@dataclass
class RoboSim:
    id: int
    x: float
    y: float
    sp_x: int
    sp_y: int
    bateria: float = 100.0
    falha: bool = False
    acoplar: bool = False           # o_acoplar_e_erguer
    atracada: bool = False          # i_carga_atracada
    t_acoplando: float = 0.0
    movendo: bool = False           # só para exibição
    recarregando: bool = False      # só para exibição
    criado_em: float = 0.0

    def entradas(self):
        """Payload de atr/robo/<id>/entradas: "x y atracada bateria falha"."""
        return "%d %d %d %d %d" % (arredondar(self.x), arredondar(self.y), int(self.atracada),
                                   math.ceil(self.bateria), int(self.falha))


def passo_robo(r, dt, em_recarga):
    """Avança o robô dt segundos. em_recarga(x, y) diz se o ponto é uma estação."""
    dx, dy = r.sp_x - r.x, r.sp_y - r.y
    dist = math.hypot(dx, dy)

    pode_mover = (not r.falha) and r.bateria > 0.0
    moveu = False

    if pode_mover and dist > 0.0:
        passo = config.VELOCIDADE_ROBO * dt

        if dist <= passo:
            r.x, r.y = float(r.sp_x), float(r.sp_y)
        else:
            r.x += dx / dist * passo
            r.y += dy / dist * passo

        moveu = True

    r.bateria -= (config.DRENO_BATERIA_MOVENDO if moveu else config.DRENO_BATERIA_PARADO) * dt

    # Parado numa estação de recarga: carrega (o simulador conhece o mapa)
    r.recarregando = (not moveu) and (not r.falha) and em_recarga(arredondar(r.x), arredondar(r.y))

    if r.recarregando:
        r.bateria += config.TAXA_RECARGA * dt

    r.bateria = min(100.0, max(0.0, r.bateria))
    r.movendo = moveu

    if r.acoplar and not r.atracada and not r.falha:
        r.t_acoplando += dt

        if r.t_acoplando >= config.TEMPO_ACOPLAR_S:
            r.atracada = True
    elif not r.acoplar:
        r.atracada = False
        r.t_acoplando = 0.0


class Mundo:
    """Conjunto dos robôs simulados. Thread-safe: a física, a rede e a tela o usam."""

    def __init__(self, mapa):
        self.mapa = mapa
        self.lock = threading.RLock()
        self.robos = {}

        # Conferência das passagens estreitas (o simulador sabe onde cada robô está de verdade)
        self.ocupacao = [0] * len(mapa.passagens)
        self.violacoes = 0                  # quantas vezes uma passagem passou da capacidade
        self.ao_violacao = None             # callback(texto) na subida de cada violação
        self._violando = set()

    # ------------------------------------------------------- robôs
    def adicionar(self, rid, x, y):
        """Cria o robô parado em (x, y). Retorna False se o id já existe."""
        with self.lock:
            if rid in self.robos:
                return False
            self.robos[rid] = RoboSim(rid, float(x), float(y), int(x), int(y),
                                      criado_em=time.monotonic())
            return True

    def remover(self, rid):
        with self.lock:
            return self.robos.pop(rid, None) is not None

    def existe(self, rid):
        with self.lock:
            return rid in self.robos

    def ids(self):
        with self.lock:
            return sorted(self.robos)

    # ---------------------------------- saídas (O) vindas do robô
    def definir_saidas(self, rid, sp_x, sp_y, acoplar):
        """Aplica o_sp_posicao_x/y e o_acoplar_e_erguer. O primeiro anúncio de um
        robô desconhecido o cria: o processo do robô começa com o setpoint igual
        à posição inicial pedida ao Gestor."""
        with self.lock:
            r = self.robos.get(rid)

            if r is None:
                self.adicionar(rid, sp_x, sp_y)
                r = self.robos[rid]

            r.sp_x, r.sp_y, r.acoplar = int(sp_x), int(sp_y), bool(acoplar)
            return r

    # ----------------------------- injeções do simulador (item 8)
    def definir_falha(self, rid, falha):
        with self.lock:
            r = self.robos.get(rid)

            if r is None:
                return False

            r.falha = bool(falha)
            return True

    def definir_bateria(self, rid, pct):
        with self.lock:
            r = self.robos.get(rid)

            if r is None:
                return False

            r.bateria = min(100.0, max(0.0, float(pct)))
            return True

    def ajustar_bateria(self, rid, delta):
        with self.lock:
            r = self.robos.get(rid)

            if r is None:
                return False

            r.bateria = min(100.0, max(0.0, r.bateria + delta))
            return True

    # ------------------------------------------------------ física
    def passo(self, dt):
        with self.lock:
            for r in self.robos.values():
                passo_robo(r, dt, self.mapa.em_recarga)

    def entradas_de_todos(self):
        """[(id, payload)] com as entradas atuais de todos os robôs."""
        with self.lock:
            return [(r.id, r.entradas()) for r in self.robos.values()]

    def snapshot(self):
        """Cópia dos robôs para a tela desenhar sem segurar o lock."""
        with self.lock:
            return [RoboSim(**{k: getattr(r, k) for k in RoboSim.__dataclass_fields__})
                    for r in self.robos.values()]

    def verificar_passagens(self):
        """Atualiza a ocupação real de cada passagem estreita e conta as violações
        de capacidade (mais robôs dentro do corredor do que ele comporta)."""
        with self.lock:
            self.ocupacao = self.mapa.ocupacao_passagens([(r.x, r.y) for r in self.robos.values()])

            for i, n in enumerate(self.ocupacao):
                p = self.mapa.passagens[i]

                if n > p["capacidade"]:
                    if i not in self._violando:
                        self._violando.add(i)
                        self.violacoes += 1

                        if self.ao_violacao:
                            quem = ", ".join(f"{r.id} ({r.x:.0f},{r.y:.0f})" for r in self.robos.values()
                                             if self.mapa.dentro_da_passagem(i, r.x, r.y))
                            self.ao_violacao(f"VIOLAÇÃO: {n} robôs em '{p['nome']}' (capacidade {p['capacidade']}): {quem}")
                else:
                    self._violando.discard(i)

            return list(self.ocupacao)

    def passagens_violadas(self):
        with self.lock:
            return set(self._violando)


class MotorFisica(threading.Thread):
    """Thread periódica: integra a física a 50 Hz e publica as entradas a cada 50 ms.

    Roda numa thread própria (e não no laço da tela) para que arrastar a janela
    ou um quadro lento de desenho nunca atrase os robôs: o processo do robô
    declara falha se ficar mais de 1 s sem receber entradas.
    """

    def __init__(self, mundo, publicar, passo=config.PASSO_FISICA_S,
                 periodo_entradas=config.PERIODO_ENTRADAS_S):
        super().__init__(name="fisica", daemon=True)
        self.mundo = mundo
        self.publicar = publicar            # publicar(id, payload)
        self.passo = passo
        self.periodo_entradas = periodo_entradas
        self._parar = threading.Event()

        # Estatísticas (relatório de escalonabilidade)
        self.ciclos = 0
        self.atrasos = 0                    # ciclos que começaram > 1 passo depois do previsto
        self.atraso_max_s = 0.0             # maior atraso de início de ciclo
        self.exec_max_s = 0.0               # maior tempo de execução de um ciclo
        self.publicacoes = 0
        self.aquecimento_s = 2.0            # os primeiros segundos (abrir a janela) não contam
        self._t0 = None

    def parar(self):
        self._parar.set()

    def run(self):
        proximo = time.monotonic()
        anterior = proximo
        ultima_pub = 0.0

        while not self._parar.is_set():
            inicio = time.monotonic()
            if self._t0 is None:
                self._t0 = inicio

            atraso = inicio - proximo
            medindo = inicio - self._t0 >= self.aquecimento_s

            if medindo:
                self.atraso_max_s = max(self.atraso_max_s, atraso)

                if atraso > self.passo:
                    self.atrasos += 1

            dt = min(inicio - anterior, config.DT_MAXIMO_S)
            anterior = inicio

            self.mundo.passo(dt)

            if inicio - ultima_pub >= self.periodo_entradas:
                ultima_pub = inicio

                for rid, payload in self.mundo.entradas_de_todos():
                    self.publicar(rid, payload)
                    self.publicacoes += 1

                self.mundo.verificar_passagens()

            self.ciclos += 1
            fim = time.monotonic()

            if medindo:
                self.exec_max_s = max(self.exec_max_s, fim - inicio)

            proximo += self.passo
            espera = proximo - fim

            if espera > 0:
                self._parar.wait(espera)
            elif espera < -0.5:
                proximo = time.monotonic()      # muito atrasado: recomeça o relógio
