"""Ações do operador e do simulador, independentes da interface gráfica.

A tela (gui.py) só desenha e traduz cliques em chamadas destes métodos; assim
as regras (só se manda IR a robô em manual, id novo sem repetir, limite de 32
robôs etc.) ficam num lugar só e podem ser testadas sem abrir janela.
"""

import random
import threading
import time

from simulador import config
from simulador.operacao import normalizar


class Controle:

    def __init__(self, mapa, mundo, operacao, rede, rng=None):
        self.mapa = mapa
        self.mundo = mundo
        self.operacao = operacao
        self.rede = rede
        self.rng = rng or random.Random()

        self.sel = None                 # id do robô selecionado
        self.posicionando = False       # esperando um clique no mapa para criar o robô
        self.pendentes = {}             # id -> {"x", "y", "t"}: anunciados ao Gestor, ainda sem processo
        self.avisos = []                # [(texto, expira_em)]
        self.baia_caixa = 1             # seleção do formulário "nova caixa"
        self.tamanho_caixa = 1
        self.ultimos_ids = 0
        self._lock = threading.Lock()

    # ------------------------------------------------------- avisos
    def aviso(self, texto, duracao=4.0):
        with self._lock:
            self.avisos.append((texto, time.monotonic() + duracao))
            del self.avisos[:-4]        # no máximo 4 na tela

    def avisos_ativos(self):
        agora = time.monotonic()

        with self._lock:
            self.avisos = [(t, e) for (t, e) in self.avisos if e > agora]
            return [t for (t, _) in self.avisos]

    # ------------------------------------------------- visão da frota
    def ids_conhecidos(self):
        ids = set(self.mundo.ids())
        ids.update(self.operacao.snapshot()["frota"])
        ids.update(self.pendentes)
        return sorted(ids)

    def proximo_id(self):
        conhecidos = self.ids_conhecidos()
        return max(max(conhecidos, default=0), self.ultimos_ids) + 1

    def visoes(self):
        """Uma linha por robô: a física do simulador (posição suave, bateria, defeito
        reais) juntada ao status que o Organizador publica (estado, modo, tarefa)."""
        op = self.operacao.snapshot()
        frota = op["frota"]
        fisica = {r.id: r for r in self.mundo.snapshot()}
        visoes = []

        for rid in sorted(set(fisica) | set(frota)):
            f, s = fisica.get(rid), frota.get(rid)

            v = {
                "id": rid,
                "estado": normalizar(s["estado"]) if s else "iniciando",
                "manual": bool(s) and normalizar(s.get("modo", "")) == "manual",
                "fase": normalizar(s.get("fase", "-")) if s else "-",
                "tarefa": s.get("tarefa", 0) if s else 0,
                "distancia": s.get("distancia", 0.0) if s else 0.0,
                "aguardando_passagem": s is not None and s.get("aguardando_passagem", -1) >= 0,
                "no_status": s is not None,
                "no_simulador": f is not None,
            }

            if f is not None:
                v.update(x=f.x, y=f.y, sp_x=f.sp_x, sp_y=f.sp_y, bateria=f.bateria,
                         falha=f.falha, atracada=f.atracada,
                         recarregando=f.recarregando and f.bateria < 99.5,   # cheia = só estacionada
                         movendo=f.movendo)
            else:       # só o Organizador a conhece (simulador reiniciado): usa o status
                v.update(x=float(s["x"]), y=float(s["y"]), sp_x=s["sp_x"], sp_y=s["sp_y"],
                         bateria=float(s["bateria"]), falha=bool(s["falha"]),
                         atracada=bool(s["atracada"]), recarregando=v["estado"] == "recarregando",
                         movendo=False)

            visoes.append(v)

        return visoes

    # ------------------------------------------------------ seleção
    def selecionar(self, rid):
        self.sel = rid

    def selecionar_proximo(self):
        ids = self.ids_conhecidos()

        if ids:
            i = ids.index(self.sel) + 1 if self.sel in ids else 0
            self.sel = ids[i % len(ids)]

    def atualizar(self):
        """Manutenção a cada quadro: expira pedidos de robô e mantém a seleção válida."""
        agora = time.monotonic()

        for rid in list(self.pendentes):
            if self.mundo.existe(rid):
                del self.pendentes[rid]
            elif agora - self.pendentes[rid]["t"] > config.NOVO_ROBO_TIMEOUT_S:
                del self.pendentes[rid]
                self.aviso(f"O Gestor não criou o robô {rid} em {config.NOVO_ROBO_TIMEOUT_S:.0f} s: "
                           "ele está rodando com --fonte mqtt?", 8)

        self._adotar_orfaos()

        ids = self.ids_conhecidos()

        if self.sel not in ids:
            self.sel = ids[0] if ids else None

    def _adotar_orfaos(self):
        """Simulador reiniciado com robôs ainda vivos: eles estão online (vivo) e no status do
        Organizador, mas não na física. Sem entradas eles declarariam defeito (timeout de 1 s),
        então o simulador os adota na posição e bateria que o Organizador lembra."""
        frota = self.operacao.snapshot()["frota"]

        for rid in list(self.rede.vivos):
            s = frota.get(rid)

            if s is not None and not self.mundo.existe(rid):
                self.mundo.adicionar(rid, s["x"], s["y"])
                self.mundo.definir_saidas(rid, s["sp_x"], s["sp_y"], bool(s.get("atracada")))
                self.mundo.definir_bateria(rid, s["bateria"])
                self.operacao.registrar_local(f"robô {rid} adotado em ({s['x']}, {s['y']}) com bateria {s['bateria']}%")

    # ------------------------------------------------------- robôs
    def _pode_publicar(self):
        if not self.rede.conectado:
            self.aviso("Sem conexão com o broker MQTT.")
            return False
        return True

    def criar_robo(self, x=None, y=None):
        """Anuncia um novo robô ao Gestor de Robôs. Sem (x, y), ele nasce na posição padrão."""
        if not self._pode_publicar():
            return None

        if len(self.ids_conhecidos()) >= config.MAX_ROBOS:
            self.aviso(f"Limite de {config.MAX_ROBOS} robôs atingido.")
            return None

        if x is not None and not self.mapa.dentro(x, y):
            self.aviso("Posição fora do mapa.")
            return None

        rid = self.proximo_id()
        self.ultimos_ids = rid

        if not self.rede.novo_robo(rid, x, y):
            self.aviso("Não foi possível anunciar o robô.")
            return None

        px, py = (config.POS_INICIAL_X, config.POS_INICIAL_Y) if x is None else (x, y)
        self.pendentes[rid] = {"x": px, "y": py, "t": time.monotonic()}
        self.operacao.registrar_local(f"novo robô {rid} anunciado em ({px}, {py})")
        return rid

    def posicao_livre_aleatoria(self):
        """Ponto arbitrário dos corredores, longe dos outros robôs e das passagens
        estreitas (largar dois robôs sorteados num corredor de capacidade 1 seria
        uma "violação" criada pelo próprio simulador)."""
        outros = [(r.x, r.y) for r in self.mundo.snapshot()]
        melhor = None

        for _ in range(30):
            p = (self.rng.randint(45, 155), self.rng.randint(10, 110))

            if self.mapa.perto_de_passagem(*p):
                continue

            dmin = min((((p[0] - ox) ** 2 + (p[1] - oy) ** 2) ** 0.5 for ox, oy in outros), default=99)
            melhor = melhor or p

            if dmin >= 8:
                return p

        return melhor or (config.POS_INICIAL_X, config.POS_INICIAL_Y)

    def criar_robo_aleatorio(self):
        x, y = self.posicao_livre_aleatoria()
        return self.criar_robo(x, y)

    def iniciar_posicionamento(self):
        self.posicionando = not self.posicionando

        if self.posicionando:
            self.aviso("Clique no mapa para posicionar o novo robô (Esc cancela).", 6)

    def clique_posicionar(self, x, y):
        self.posicionando = False
        return self.criar_robo(int(round(x)), int(round(y)))

    # ---------------------------------------------------- operação
    def _status(self, rid):
        return self.operacao.snapshot()["frota"].get(rid)

    def eh_manual(self, rid):
        s = self._status(rid)
        return s is not None and normalizar(s.get("modo", "")) == "manual"

    def alternar_modo(self, rid=None):
        rid = self.sel if rid is None else rid

        if rid is None or not self._pode_publicar():
            return False

        manual = not self.eh_manual(rid)
        return self.definir_modo(rid, manual)

    def definir_modo(self, rid, manual):
        if rid is None or not self._pode_publicar():
            return False

        self.rede.cmd_modo(rid, manual)
        self.operacao.registrar_local(f"operador: robô {rid} -> {'manual' if manual else 'automático'}")
        return True

    def ir_para(self, rid, x, y):
        """Setpoint do operador. O robô só o aceita em modo manual."""
        if rid is None:
            self.aviso("Selecione um robô.")
            return False

        if not self.mapa.dentro(x, y):
            self.aviso(f"({x}, {y}) está fora do mapa.")
            return False

        if self._status(rid) is None:
            self.aviso(f"O Organizador ainda não conhece o robô {rid}.")
            return False

        if not self.eh_manual(rid):
            self.aviso(f"Robô {rid} está em AUTOMÁTICO: troque para MANUAL antes de enviar o destino.")
            return False

        if not self._pode_publicar():
            return False

        self.rede.cmd_ir(rid, x, y)
        self.operacao.registrar_local(f"operador: robô {rid} -> ir para ({x}, {y})")
        return True

    def ir_para_area(self, rid, tipo, numero):
        area = self.mapa.buscar_area(tipo, numero)
        return area is not None and self.ir_para(rid, area["cx"], area["cy"])

    def parar(self, rid):
        if rid is None or not self._pode_publicar():
            return False

        self.rede.cmd_parar(rid)
        self.operacao.registrar_local(f"operador: robô {rid} -> parar")
        return True

    # ------------------------------------------ injeções (simulador)
    def alternar_falha(self, rid):
        r = next((r for r in self.mundo.snapshot() if r.id == rid), None)

        if r is None:
            return False

        self.mundo.definir_falha(rid, not r.falha)
        self.operacao.registrar_local(f"simulador: defeito {'removido do' if r.falha else 'gerado no'} robô {rid}")
        return True

    def baixar_bateria(self, rid, delta=-10.0):
        if self.mundo.ajustar_bateria(rid, delta):
            self.operacao.registrar_local(f"simulador: bateria do robô {rid} reduzida em {abs(delta):.0f}%")
            return True
        return False

    def bateria_para(self, rid, pct):
        if self.mundo.definir_bateria(rid, pct):
            self.operacao.registrar_local(f"simulador: bateria do robô {rid} ajustada para {pct:.0f}%")
            return True
        return False

    # ------------------------------------------------------ caixas
    def criar_caixa(self, baia=None, tamanho=None):
        baia = self.baia_caixa if baia is None else baia
        tamanho = self.tamanho_caixa if tamanho is None else tamanho

        if not (1 <= baia <= 4 and 1 <= tamanho <= 4):
            self.aviso("Baia e tamanho devem estar entre 1 e 4.")
            return False

        if not self._pode_publicar():
            return False

        self.rede.cmd_caixa(baia, tamanho)
        self.operacao.registrar_local(f"simulador: nova caixa de tamanho {tamanho} na Baia {baia}")
        return True

    def criar_caixa_aleatoria(self):
        return self.criar_caixa(self.rng.randint(1, 4), self.rng.randint(1, 4))
