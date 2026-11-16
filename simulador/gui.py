"""Interface gráfica (pygame): simulador do galpão + Tela de Operação numa janela só.

A janela é desenhada numa superfície lógica fixa (LARGURA x ALTURA) que o
app.py escala para o tamanho real; todas as coordenadas daqui são lógicas.

    +-----------------------------------------------------------------+
    | barra: conexões, totais, cursor                                 |
    +--------------------------------------+--------------------------+
    |                                      | ROBÔS (lista, modo M/A)  |
    |  MAPA DO GALPÃO (200 x 120 un.)      +--------------------------+
    |  baias | corredores | zonas          | OPERAÇÃO (robô selec.)   |
    |  robôs, caixas, passagens estreitas  +--------------------------+
    +------------------+-------------------+ SIMULADOR (defeito,      |
    | CAIXAS           | EVENTOS           |  bateria, robô, caixa)   |
    +------------------+-------------------+--------------------------+
"""

import math
import time

import pygame

from simulador import config
from simulador.operacao import normalizar

LARGURA, ALTURA = 1440, 900
ESCALA = 5                                  # pixels lógicos por unidade do mapa

R_BARRA = pygame.Rect(0, 0, LARGURA, 38)
R_MAPA = pygame.Rect(10, 46, 200 * ESCALA, 120 * ESCALA)
COL_X, COL_W = 1022, 408
R_LISTA = pygame.Rect(COL_X, 46, COL_W, 322)
R_OPERACAO = pygame.Rect(COL_X, 374, COL_W, 188)
R_SIM = pygame.Rect(COL_X, 568, COL_W, 322)
R_CAIXAS = pygame.Rect(10, 656, 490, 234)
R_EVENTOS = pygame.Rect(508, 656, 502, 234)

ALTURA_CARTAO = 58
LINHA_EVENTO = 16

COR = {
    "fundo": (28, 32, 40),
    "barra": (36, 42, 54),
    "painel": (247, 248, 250),
    "painel_borda": (186, 192, 202),
    "titulo": (52, 60, 76),
    "texto": (34, 40, 50),
    "texto_fraco": (110, 118, 130),
    "branco": (255, 255, 255),
    "piso": (238, 241, 245),
    "grade": (226, 230, 236),
    "via": (206, 211, 220),
    "via_estreita": (160, 168, 184),
    "no": (120, 128, 142),
    "baia": (214, 232, 250),
    "baia_borda": (110, 156, 210),
    "zona": (216, 240, 222),
    "zona_borda": (104, 172, 128),
    "recarga": (255, 243, 205),
    "recarga_borda": (214, 168, 40),
    "selecao": (255, 196, 0),
    "ok": (46, 160, 67),
    "alerta": (232, 145, 20),
    "erro": (220, 53, 69),
    "info": (47, 129, 247),
    "manual": (124, 77, 255),
    "botao": (234, 238, 244),
    "botao_hover": (216, 224, 236),
    "botao_borda": (166, 176, 192),
    "botao_ativo": (47, 129, 247),
}

COR_ESTADO = {
    "disponivel": (46, 160, 67),
    "deslocando": (47, 129, 247),
    "operando carga": (232, 145, 20),
    "recarregando": (16, 163, 148),
    "defeito": (220, 53, 69),
    "iniciando": (140, 146, 156),
    "inativo": (140, 146, 156),
}
ROTULO_ESTADO = {
    "disponivel": "disponível", "deslocando": "deslocando", "operando carga": "operando carga",
    "recarregando": "recarregando", "defeito": "defeito", "iniciando": "iniciando", "inativo": "inativo",
}

# Fases em que a equipe ainda NÃO coletou a carga: a caixa continua na baia
FASES_ANTES_DA_COLETA = {"-", "ir_coleta", "aguardando_equipe", "acoplando"}

ANIM_TAXA = 12.0            # suavização (1/s) da entrada em formação e do deslize das caixas

COR_ZONAS = {1: (206, 236, 214), 2: (214, 234, 246), 3: (250, 236, 206), 4: (238, 220, 244)}

# Caixas de tamanho 1 a 4 (lado em pixels lógicos e cor)
LADO_CAIXA = {1: 20, 2: 26, 3: 32, 4: 38}
COR_CAIXA = {1: (142, 202, 230), 2: (144, 190, 109), 3: (249, 199, 79), 4: (248, 150, 30)}

COR_EVENTO = {
    "DEFEITO": (200, 40, 55), "RECARGA": (14, 140, 128), "CARGA": (36, 140, 60),
    "ENTREGA": (36, 140, 60), "INCOMPLETA": (200, 40, 55), "PASSAGEM": (96, 110, 140),
    "TAREFA": (47, 100, 210), "MODO": (124, 77, 255), "SIM": (196, 110, 10),
    "COMANDO": (120, 126, 138), "INICIADO": (34, 40, 50), "ENCERRADO": (200, 40, 55),
    "CHEGOU_DESTINO": (34, 40, 50), "ESTADO": (70, 78, 92), "ALERTA": (200, 40, 55),
}

AJUDA = [
    "Mouse",
    "  clique esquerdo no robô (mapa ou lista)  seleciona",
    "  clique direito no mapa                    envia destino ao robô selecionado (modo MANUAL)",
    "  roda do mouse                             rola a lista de robôs / o log de eventos",
    "Teclado",
    "  Tab    próximo robô          M    alterna manual/automático do selecionado",
    "  F      gera / remove defeito  B    bateria -10%       Espaço   para o robô",
    "  Esc    cancela posicionamento N    mostra/oculta nomes dos nós",
    "  F11    tela cheia             F12  salva captura em capturas/       H / F1   esta ajuda",
]


# ----------------------------------------------------------------------------
# Texto e desenho básico
# ----------------------------------------------------------------------------

class Fontes:
    """Fontes do sistema (DejaVu/Arial se existirem), senão a padrão do pygame."""

    def __init__(self):
        pygame.font.init()
        caminho = pygame.font.match_font("dejavusans,liberationsans,arial,freesans")

        def f(tam, negrito=False):
            if caminho:
                fonte = pygame.font.Font(caminho, tam)
                fonte.set_bold(negrito)
                return fonte
            fonte = pygame.font.Font(None, int(tam * 1.25))
            fonte.set_bold(negrito)
            return fonte

        self.mini = f(11)
        self.p = f(12)
        self.n = f(14)
        self.b = f(14, True)
        self.t = f(15, True)
        self.g = f(19, True)


class CacheTexto:
    def __init__(self):
        self._c = {}

    def render(self, fonte, texto, cor):
        chave = (id(fonte), texto, cor)
        s = self._c.get(chave)

        if s is None:
            if len(self._c) > 4000:
                self._c.clear()
            s = self._c[chave] = fonte.render(texto, True, cor)

        return s


def cortar(fonte, texto, largura):
    """Texto cortado com reticências para caber em largura pixels."""
    if fonte.size(texto)[0] <= largura:
        return texto

    while texto and fonte.size(texto + "…")[0] > largura:
        texto = texto[:-1]

    return texto + "…"


def linha_tracejada(surf, cor, a, b, largura=1, traco=6, vazio=4):
    dx, dy = b[0] - a[0], b[1] - a[1]
    d = math.hypot(dx, dy)

    if d < 1:
        return

    ux, uy = dx / d, dy / d
    pos = 0.0

    while pos < d:
        fim = min(pos + traco, d)
        pygame.draw.line(surf, cor, (a[0] + ux * pos, a[1] + uy * pos), (a[0] + ux * fim, a[1] + uy * fim), largura)
        pos += traco + vazio


def cor_bateria(pct):
    return COR["ok"] if pct > 50 else COR["alerta"] if pct > 20 else COR["erro"]


class Botao:
    """Botão retangular. rotulo/ativo/habilitado podem ser valores ou funções (estado dinâmico)."""

    def __init__(self, rect, rotulo, acao, ativo=None, habilitado=None, perigo=False, fonte="n"):
        self.rect = pygame.Rect(rect)
        self.rotulo = rotulo
        self.acao = acao
        self.ativo = ativo
        self.habilitado = habilitado
        self.perigo = perigo
        self.fonte = fonte

    def _val(self, v):
        return v() if callable(v) else v

    def esta_habilitado(self):
        return True if self.habilitado is None else bool(self._val(self.habilitado))

    def esta_ativo(self):
        return False if self.ativo is None else bool(self._val(self.ativo))

    def clicar(self):
        if self.esta_habilitado():
            self.acao()
            return True
        return False


class CampoTexto:
    """Campo de uma linha para "x y" (aceita dígitos, espaço, vírgula e sinal)."""

    def __init__(self, rect, dica=""):
        self.rect = pygame.Rect(rect)
        self.texto = ""
        self.dica = dica
        self.foco = False

    def tecla(self, ev):
        if ev.key == pygame.K_BACKSPACE:
            self.texto = self.texto[:-1]
        elif ev.unicode and ev.unicode in "0123456789 ,-" and len(self.texto) < 12:
            self.texto += ev.unicode

    def valores(self):
        """(x, y) ou None se o texto não tem exatamente dois inteiros."""
        partes = self.texto.replace(",", " ").split()

        try:
            if len(partes) == 2:
                return int(partes[0]), int(partes[1])
        except ValueError:
            pass

        return None


# ----------------------------------------------------------------------------
# Interface
# ----------------------------------------------------------------------------

class Interface:

    def __init__(self, controle, motor=None):
        self.c = controle
        self.mapa = controle.mapa
        self.motor = motor
        self.fontes = Fontes()
        self.cache = CacheTexto()

        self.mouse = (0, 0)
        self.lista_scroll = 0
        self._sel_visto = None
        self._t_ant = None
        self._exib = {}                     # id -> (px, py) onde o robô é DESENHADO (formação em torno da caixa)
        self._delta = {}                    # id -> deslocamento suavizado entre a posição real e a desenhada
        self._equipes = []                  # caixas em transporte: [{tarefa, tam, centro, robos}]
        self._reservadas = {}               # baia -> [(tarefa, tam)]: caixas com equipe a caminho, ainda na baia
        self._estado_missao = {}            # tarefa -> "buscando" | "transportando"
        self._slot_reservada = {}           # tarefa -> (px, py) do último lugar da caixa na baia
        self._delta_caixa = {}              # tarefa -> deslocamento do deslize baia -> equipe
        self.ev_scroll = 0                  # linhas acima do fim do log (0 = acompanha)
        self.filtro_selecionado = False
        self.mostrar_nos = False
        self.ajuda = False
        self.cartoes = []                   # [(rect, id, rect_do_modo)] do último desenho
        self.campo = CampoTexto((COL_X + 52, R_OPERACAO.y + 150, 112, 26))
        self.fps = 0.0

        self._mapa_estatico = self._montar_mapa_estatico()
        self.botoes = self._montar_botoes()

    # ------------------------------------------------------- utilidades
    def px(self, x, y):
        return (int(R_MAPA.x + x * ESCALA), int(R_MAPA.y + y * ESCALA))

    def unidades(self, pos):
        return ((pos[0] - R_MAPA.x) / ESCALA, (pos[1] - R_MAPA.y) / ESCALA)

    def txt(self, surf, fonte, s, pos, cor=None, ancora="tl"):
        img = self.cache.render(fonte, s, cor or COR["texto"])
        r = img.get_rect()

        if ancora == "tr":
            r.topright = pos
        elif ancora == "tc":
            r.midtop = pos
        elif ancora == "mc":
            r.center = pos
        elif ancora == "ml":
            r.midleft = pos
        elif ancora == "mr":
            r.midright = pos
        else:
            r.topleft = pos

        surf.blit(img, r)
        return r

    def painel(self, surf, rect, titulo):
        pygame.draw.rect(surf, COR["painel"], rect, border_radius=6)
        pygame.draw.rect(surf, COR["painel_borda"], rect, 1, border_radius=6)
        pygame.draw.rect(surf, COR["titulo"], (rect.x, rect.y, rect.w, 24), border_top_left_radius=6, border_top_right_radius=6)
        self.txt(surf, self.fontes.t, titulo, (rect.x + 10, rect.y + 4), COR["branco"])

    # ------------------------------------------------- botões e layout
    def _montar_botoes(self):
        c = self.c
        sel = lambda: c.sel
        tem_sel = lambda: c.sel is not None
        b = []

        # ---- OPERAÇÃO (robô selecionado)
        y0 = R_OPERACAO.y
        b.append(Botao((COL_X + 64, y0 + 32, 150, 26), "AUTOMÁTICO",
                       lambda: c.definir_modo(sel(), False),
                       ativo=lambda: tem_sel() and not c.eh_manual(sel()), habilitado=tem_sel))
        b.append(Botao((COL_X + 220, y0 + 32, 150, 26), "MANUAL",
                       lambda: c.definir_modo(sel(), True),
                       ativo=lambda: tem_sel() and c.eh_manual(sel()), habilitado=tem_sel))

        destinos = [("baia", 1, "Baia 1"), ("baia", 2, "Baia 2"), ("baia", 3, "Baia 3"), ("baia", 4, "Baia 4"),
                    ("recarga", 1, "Recarga 1"),
                    ("zona", 1, "Zona A"), ("zona", 2, "Zona B"), ("zona", 3, "Zona C"), ("zona", 4, "Zona D"),
                    ("recarga", 2, "Recarga 2")]

        for i, (tipo, num, nome) in enumerate(destinos):
            col, lin = i % 5, i // 5
            b.append(Botao((COL_X + 12 + col * 78, y0 + 86 + lin * 30, 72, 26), nome,
                           (lambda t=tipo, n=num: c.ir_para_area(sel(), t, n)), habilitado=tem_sel, fonte="p"))

        b.append(Botao((COL_X + 172, y0 + 150, 52, 26), "Ir", self._ir_campo, habilitado=tem_sel))
        b.append(Botao((COL_X + 232, y0 + 150, 90, 26), "Parar", lambda: c.parar(sel()), habilitado=tem_sel))
        b.append(Botao((COL_X + 330, y0 + 150, 66, 26), "Auto/Man", lambda: c.alternar_modo(sel()), habilitado=tem_sel, fonte="p"))

        # ---- SIMULADOR
        y0 = R_SIM.y
        b.append(Botao((COL_X + 12, y0 + 52, 128, 26),
                       lambda: "Reparar defeito" if self._falha_sel() else "Gerar defeito",
                       lambda: c.alternar_falha(sel()), perigo=True, habilitado=tem_sel))
        b.append(Botao((COL_X + 146, y0 + 52, 120, 26), "Bateria −10%",
                       lambda: c.baixar_bateria(sel(), -10.0), habilitado=tem_sel))
        b.append(Botao((COL_X + 272, y0 + 52, 124, 26), "Bateria → 15%",
                       lambda: c.bateria_para(sel(), 15.0), habilitado=tem_sel))

        b.append(Botao((COL_X + 12, y0 + 112, 120, 26), "Posição padrão", lambda: c.criar_robo()))
        b.append(Botao((COL_X + 138, y0 + 112, 120, 26), "Aleatória", c.criar_robo_aleatorio))
        b.append(Botao((COL_X + 264, y0 + 112, 132, 26), "Clicar no mapa", c.iniciar_posicionamento,
                       ativo=lambda: c.posicionando))

        for i in range(4):
            b.append(Botao((COL_X + 76 + i * 44, y0 + 172, 40, 26), str(i + 1),
                           (lambda n=i + 1: setattr(c, "baia_caixa", n)),
                           ativo=(lambda n=i + 1: c.baia_caixa == n)))
            b.append(Botao((COL_X + 76 + i * 44, y0 + 204, 40, 26), str(i + 1),
                           (lambda n=i + 1: setattr(c, "tamanho_caixa", n)),
                           ativo=(lambda n=i + 1: c.tamanho_caixa == n)))

        b.append(Botao((COL_X + 262, y0 + 172, 134, 26), "Criar caixa", c.criar_caixa))
        b.append(Botao((COL_X + 262, y0 + 204, 134, 26), "Caixa aleatória", c.criar_caixa_aleatoria))

        # ---- EVENTOS (filtro)
        b.append(Botao((R_EVENTOS.right - 168, R_EVENTOS.y + 2, 76, 20), "Todos",
                       lambda: setattr(self, "filtro_selecionado", False),
                       ativo=lambda: not self.filtro_selecionado, fonte="p"))
        b.append(Botao((R_EVENTOS.right - 88, R_EVENTOS.y + 2, 80, 20), "Selecionado",
                       lambda: setattr(self, "filtro_selecionado", True),
                       ativo=lambda: self.filtro_selecionado, fonte="p"))
        return b

    def _falha_sel(self):
        for v in self.c.visoes():
            if v["id"] == self.c.sel:
                return v["falha"]
        return False

    def _ir_campo(self):
        v = self.campo.valores()

        if v is None:
            self.c.aviso("Digite o destino como \"x y\" (ex.: 120 60).")
        else:
            self.c.ir_para(self.c.sel, *v)

    # ============================================================ ENTRADA
    def mover(self, pos):
        self.mouse = pos

    def clique(self, pos, botao):
        """Clique em coordenadas lógicas. botao: 1 = esquerdo, 3 = direito."""
        self.mouse = pos

        if self.ajuda:
            self.ajuda = False
            return

        if botao == 1:
            self.campo.foco = self.campo.rect.collidepoint(pos)

            for bt in self.botoes:
                if bt.rect.collidepoint(pos):
                    bt.clicar()
                    return

            for rect, rid, rect_modo in self.cartoes:
                if rect.collidepoint(pos):
                    if rect_modo.collidepoint(pos):
                        self.c.alternar_modo(rid)
                    self.c.selecionar(rid)
                    return

            if R_MAPA.collidepoint(pos):
                x, y = self.unidades(pos)

                if self.c.posicionando:
                    self.c.clique_posicionar(x, y)
                else:
                    alvo = self._robo_proximo(pos)

                    if alvo is not None:
                        self.c.selecionar(alvo)

        elif botao == 3 and R_MAPA.collidepoint(pos):
            x, y = self.unidades(pos)
            self.c.ir_para(self.c.sel, int(round(x)), int(round(y)))

    def roda(self, pos, dy):
        if R_LISTA.collidepoint(pos):
            self.lista_scroll = max(0, self.lista_scroll - dy * 40)
        elif R_EVENTOS.collidepoint(pos):
            self.ev_scroll = max(0, self.ev_scroll + dy * 3)

    def tecla(self, ev):
        if self.campo.foco:
            if ev.key in (pygame.K_RETURN, pygame.K_KP_ENTER):
                self._ir_campo()
            elif ev.key == pygame.K_ESCAPE:
                self.campo.foco = False
            else:
                self.campo.tecla(ev)
            return

        k, c = ev.key, self.c

        if k == pygame.K_ESCAPE:
            c.posicionando = False
            self.ajuda = False
        elif k == pygame.K_TAB:
            c.selecionar_proximo()
        elif k == pygame.K_m:
            c.alternar_modo(c.sel)
        elif k == pygame.K_f:
            c.alternar_falha(c.sel)
        elif k == pygame.K_b:
            c.baixar_bateria(c.sel, -10.0)
        elif k == pygame.K_SPACE:
            c.parar(c.sel)
        elif k == pygame.K_n:
            self.mostrar_nos = not self.mostrar_nos
        elif k in (pygame.K_h, pygame.K_F1):
            self.ajuda = not self.ajuda

    def pos_exibida(self, v):
        """Onde o robô é desenhado (na formação em torno da caixa, se está carregando)."""
        return self._exib.get(v["id"]) or self.px(v["x"], v["y"])

    def _robo_proximo(self, pos, raio=14):
        melhor, dmin = None, raio

        for v in self.c.visoes():
            ex, ey = self.pos_exibida(v)
            d = math.hypot(ex - pos[0], ey - pos[1])

            if d <= dmin:
                melhor, dmin = v["id"], d

        return melhor

    @staticmethod
    def slots_formacao(n, lado):
        """Posições (px, relativas ao centro da caixa) dos n robôs que a carregam:
        1 e 2 robôs nos lados, 3 em T, 4 nos cantos. Assim a caixa continua visível
        mesmo que a navegação coloque a equipe toda no mesmo ponto."""
        s = lado / 2 + 13       # lado a lado com a caixa (raio do robô 11 + folga)
        c = lado / 2 + 3        # cantos

        if n <= 1:
            return [(-s, 0)]
        if n == 2:
            return [(-s, 0), (s, 0)]
        if n == 3:
            return [(-s, 0), (s, 0), (0, -s)]

        return [(-c, -c), (c, -c), (-c, c), (c, c)]

    def _preparar_equipes(self, visoes, op, dt):
        """Decide, por missão, onde está a caixa e onde cada robô é desenhado.

        * Antes da coleta (equipe indo, esperando ou acoplando) a caixa continua
          NA BAIA (o Organizador a tira da fila ao reservar a equipe, mas ela só
          sai do lugar quando os robôs a pegam).
        * Depois, a caixa desliza da baia até o centro da equipe e os robôs se
          distribuem nos lados/cantos dela, acompanhando o movimento real.
        """
        f = 1.0 - math.exp(-ANIM_TAXA * dt)
        por_id = {v["id"]: v for v in visoes}
        reais = {v["id"]: self.px(v["x"], v["y"]) for v in visoes}
        alvos = {rid: (0.0, 0.0) for rid in reais}
        cargas = op["cargas"]
        ativas = set()

        self._equipes = []
        self._reservadas = {}
        self._estado_missao = {}

        for m in (cargas["em_transporte"] if cargas else []):
            tarefa = m["tarefa"]
            tam = m["tamanho"] if m["tamanho"] in LADO_CAIXA else 1
            membros = [por_id[r] for r in sorted(m["robos"]) if r in por_id]

            if not membros:
                continue

            ativas.add(tarefa)

            coletada = (any(v["atracada"] for v in membros)
                        or any(v["tarefa"] == tarefa and v["fase"] not in FASES_ANTES_DA_COLETA for v in membros))

            if not coletada:
                self._reservadas.setdefault(m["baia"], []).append((tarefa, tam))
                self._estado_missao[tarefa] = "buscando"
                continue

            self._estado_missao[tarefa] = "transportando"
            cx = sum(reais[v["id"]][0] for v in membros) / len(membros)
            cy = sum(reais[v["id"]][1] for v in membros) / len(membros)
            slots = self.slots_formacao(len(m["robos"]), LADO_CAIXA[tam])

            for i, rid in enumerate(sorted(m["robos"])):
                v = por_id.get(rid)

                if v is not None and v["atracada"]:
                    sx, sy = slots[i % len(slots)]
                    alvos[rid] = (cx + sx - reais[rid][0], cy + sy - reais[rid][1])

            # A caixa desliza do seu lugar na baia até a equipe (e depois acompanha sem atraso)
            if tarefa not in self._delta_caixa:
                ini = self._slot_reservada.pop(tarefa, None)
                self._delta_caixa[tarefa] = (ini[0] - cx, ini[1] - cy) if ini else (0.0, 0.0)

            dx, dy = self._delta_caixa[tarefa]
            dx, dy = dx * (1.0 - f), dy * (1.0 - f)
            self._delta_caixa[tarefa] = (dx, dy)
            self._equipes.append({"tarefa": tarefa, "tam": tam, "centro": (cx + dx, cy + dy),
                                  "robos": [rid for rid in sorted(m["robos"]) if rid in por_id and por_id[rid]["atracada"]]})

        for t in [t for t in self._delta_caixa if t not in ativas]:
            del self._delta_caixa[t]

        for t in [t for t in self._slot_reservada if t not in ativas]:
            del self._slot_reservada[t]

        # Robôs: o deslocamento suaviza até o alvo (formação ao acoplar, volta ao soltar)
        for rid, (tx, ty) in alvos.items():
            dx, dy = self._delta.get(rid, (0.0, 0.0))
            dx, dy = dx + (tx - dx) * f, dy + (ty - dy) * f
            self._delta[rid] = (dx, dy)
            self._exib[rid] = (reais[rid][0] + dx, reais[rid][1] + dy)

        for rid in [r for r in self._delta if r not in reais]:
            del self._delta[rid]
            self._exib.pop(rid, None)

    # ============================================================ DESENHO
    def desenhar(self, surf):
        visoes = self.c.visoes()
        op = self.c.operacao.snapshot()
        ocupacao = self.c.mundo.ocupacao
        violadas = self.c.mundo.passagens_violadas()

        agora = time.monotonic()
        dt = 0.033 if self._t_ant is None else min(0.1, max(0.0, agora - self._t_ant))
        self._t_ant = agora
        self._preparar_equipes(visoes, op, dt)

        surf.fill(COR["fundo"])
        self._barra(surf, visoes, op)
        self._mapa(surf, visoes, op, ocupacao, violadas)
        self._lista(surf, visoes)
        self._operacao(surf)
        self._simulador(surf, visoes)
        self._caixas(surf, op)
        self._eventos(surf, op)
        self._avisos(surf)

        if self.c.posicionando:
            self._fantasma(surf)

        if self.ajuda:
            self._tela_ajuda(surf)

    # ------------------------------------------------------------ barra
    def _led(self, surf, x, y, cor, rotulo):
        pygame.draw.circle(surf, cor, (x, y), 6)
        pygame.draw.circle(surf, (255, 255, 255), (x, y), 6, 1)
        return self.txt(surf, self.fontes.n, rotulo, (x + 12, y), COR["branco"], "ml").right

    def _barra(self, surf, visoes, op):
        pygame.draw.rect(surf, COR["barra"], R_BARRA)
        self.txt(surf, self.fontes.g, "Simulador e Tela de Operação", (12, 8), COR["branco"])

        x = 330
        rede = self.c.rede
        x = self._led(surf, x, 19, COR["ok"] if rede.conectado else COR["erro"],
                      f"Broker {rede.host}:{rede.porta}" if rede.conectado else "Broker desconectado") + 22
        online = self.c.operacao.organizador_online()
        x = self._led(surf, x, 19, COR["ok"] if online else COR["alerta"],
                      "Organizador" if online else "Organizador (sem status)") + 22

        entregues = op["cargas"]["entregues"] if op["cargas"] else 0
        defeitos = sum(1 for v in visoes if v["falha"])
        x = self._led(surf, x, 19, COR["info"], f"{len(visoes)} robôs") + 18
        x = self._led(surf, x, 19, COR["erro"] if defeitos else COR["texto_fraco"], f"{defeitos} defeito(s)") + 18
        viol = self.c.mundo.violacoes
        self._led(surf, x, 19, COR["erro"] if viol else COR["ok"],
                  f"{viol} violação(ões) de passagem" if viol else "passagens OK")

        # Cursor sobre o mapa
        if R_MAPA.collidepoint(self.mouse):
            ux, uy = self.unidades(self.mouse)
            a = self.mapa.area_do_ponto(int(round(ux)), int(round(uy)))
            info = f"({ux:.0f}, {uy:.0f})" + (f"  {a['nome']}" if a else "")
            self.txt(surf, self.fontes.n, info, (LARGURA - 150, 19), (190, 200, 215), "mr")

        self.txt(surf, self.fontes.p, "H = ajuda", (LARGURA - 10, 19), (150, 160, 178), "mr")

    # ------------------------------------------------------------ mapa
    def _montar_mapa_estatico(self):
        """Piso, grade, áreas e vias: não mudam, então são desenhados uma vez."""
        s = pygame.Surface(R_MAPA.size)
        s.fill(COR["piso"])
        fontes = self.fontes
        cache = self.cache
        m = self.mapa

        for u in range(0, m.largura + 1, 10):
            pygame.draw.line(s, COR["grade"], (u * ESCALA, 0), (u * ESCALA, m.altura * ESCALA), 1)
        for u in range(0, m.altura + 1, 10):
            pygame.draw.line(s, COR["grade"], (0, u * ESCALA), (m.largura * ESCALA, u * ESCALA), 1)

        def loc(x, y):
            return (int(x * ESCALA), int(y * ESCALA))

        # Áreas
        for a in m.areas:
            r = pygame.Rect(loc(a["x0"], a["y0"]), (int((a["x1"] - a["x0"]) * ESCALA), int((a["y1"] - a["y0"]) * ESCALA)))

            if a["tipo"] == "baia":
                fundo, borda = COR["baia"], COR["baia_borda"]
            elif a["tipo"] == "zona":
                fundo, borda = COR_ZONAS.get(a["numero"], COR["zona"]), COR["zona_borda"]
            else:
                fundo, borda = COR["recarga"], COR["recarga_borda"]

            pygame.draw.rect(s, fundo, r)
            pygame.draw.rect(s, borda, r, 2)
            if a["tipo"] == "recarga":
                s.blit(cache.render(fontes.p, a["nome"], (150, 110, 10)), (r.x + 4, r.bottom - 15))
            else:
                s.blit(cache.render(fontes.b, a["nome"].upper(), borda), (r.x + 6, r.y + 4))

        # Vias (os trechos de passagem estreita recebem destaque dinâmico depois)
        for ar in m.arestas:
            a, b = m.nos[ar["a"]], m.nos[ar["b"]]
            cor = COR["via_estreita"] if ar["passagem"] >= 0 else COR["via"]
            pygame.draw.line(s, cor, loc(a["x"], a["y"]), loc(b["x"], b["y"]), 12)
            pygame.draw.circle(s, cor, loc(a["x"], a["y"]), 6)
            pygame.draw.circle(s, cor, loc(b["x"], b["y"]), 6)

        for n in m.nos:
            pygame.draw.circle(s, COR["no"], loc(n["x"], n["y"]), 3)

        # Escala nas bordas
        for u in range(0, m.largura + 1, 50):
            s.blit(cache.render(fontes.mini, str(u), COR["texto_fraco"]), (u * ESCALA + 2, m.altura * ESCALA - 13))

        return s

    def _mapa(self, surf, visoes, op, ocupacao, violadas):
        surf.blit(self._mapa_estatico, R_MAPA.topleft)
        pygame.draw.rect(surf, COR["painel_borda"], R_MAPA.inflate(2, 2), 1)
        surf.set_clip(R_MAPA)
        m = self.mapa

        if self.mostrar_nos:
            for n in m.nos:
                self.txt(surf, self.fontes.mini, n["nome"], (self.px(n["x"], n["y"])[0] + 4, self.px(n["x"], n["y"])[1] - 14),
                         COR["texto_fraco"])

        # Passagens estreitas: ocupação real (n/capacidade) e violação
        piscar = int(time.monotonic() * 3) % 2 == 0

        for i, p in enumerate(m.passagens):
            (x0, y0), (x1, y1) = m.segmento_passagem(i)
            n = ocupacao[i] if i < len(ocupacao) else 0
            cap = p["capacidade"]

            if i in violadas:
                cor = COR["erro"] if piscar else (255, 150, 160)
            elif n >= cap:
                cor = COR["alerta"]
            elif n > 0:
                cor = (250, 205, 120)
            else:
                cor = (140, 200, 150)

            pygame.draw.line(surf, cor, self.px(x0, y0), self.px(x1, y1), 6)
            mx, my = self.px((x0 + x1) / 2, (y0 + y1) / 2)
            rotulo = f"{p['nome']}  {n}/{cap}"
            larg = self.fontes.p.size(rotulo)[0] + 10
            caixa = pygame.Rect(0, 0, larg, 17)
            caixa.midbottom = (mx, my - 9) if y0 == y1 else (mx + larg // 2 + 12, my + 9)
            pygame.draw.rect(surf, COR["branco"], caixa, border_radius=4)
            pygame.draw.rect(surf, cor, caixa, 2, border_radius=4)
            self.txt(surf, self.fontes.p, rotulo, caixa.center, COR["texto"], "mc")

        # Vagas das estações de recarga
        for vaga in m.vagas_recarga:
            ocupada = any(abs(v["x"] - vaga["x"]) <= 2 and abs(v["y"] - vaga["y"]) <= 2 for v in visoes)
            cx, cy = self.px(vaga["x"], vaga["y"])
            r = pygame.Rect(cx - 13, cy - 9, 26, 18)
            pygame.draw.rect(surf, (255, 226, 140) if ocupada else (255, 252, 235), r, border_radius=4)
            pygame.draw.rect(surf, COR["recarga_borda"], r, 1, border_radius=4)

        self._caixas_do_mapa(surf, op)

        # Destinos (setpoints): tracejado fraco para todos, forte para o selecionado
        for v in visoes:
            if (round(v["sp_x"]), round(v["sp_y"])) != (round(v["x"]), round(v["y"])):
                a, b = self.pos_exibida(v), self.px(v["sp_x"], v["sp_y"])
                sel = v["id"] == self.c.sel
                cor = COR["selecao"] if sel else (150, 158, 172)
                linha_tracejada(surf, cor, a, b, 2 if sel else 1)
                pygame.draw.circle(surf, cor, b, 4 if sel else 3, 1)

        self._caixas_em_transporte(surf, visoes, op)

        for v in visoes:
            self._robo(surf, v)

        surf.set_clip(None)

    def _caixa(self, surf, x, y, lado, tam, texto=None, reservada=False):
        r = pygame.Rect(x, y, lado, lado)
        pygame.draw.rect(surf, COR_CAIXA[tam], r, border_radius=3)

        if reservada:       # equipe já designada e a caminho: a caixa ainda está na baia
            pygame.draw.rect(surf, COR["info"], r.inflate(4, 4), 2, border_radius=4)
        else:
            pygame.draw.rect(surf, (60, 66, 80), r, 1, border_radius=3)
        self.txt(surf, self.fontes.b if lado >= 26 else self.fontes.p, texto or str(tam), r.center, (40, 44, 54), "mc")

    def _fluxo(self, surf, rect, itens, desenhar, lado_de):
        """Distribui itens em linhas dentro de rect; devolve quantos não couberam."""
        x, y, alt = rect.x, rect.y, 0

        for i, it in enumerate(itens):
            lado = lado_de(it)

            if x + lado > rect.right:
                x, y, alt = rect.x, y + alt + 4, 0

            if y + lado > rect.bottom:
                return len(itens) - i

            desenhar(x, y, it)
            x += lado + 4
            alt = max(alt, lado)

        return 0

    def _caixas_do_mapa(self, surf, op):
        """Caixas pendentes dentro das baias e caixas já entregues dentro das zonas."""
        cargas = op["cargas"]

        for a in self.mapa.areas_do_tipo("baia"):
            pend = []

            if cargas:
                pend = next((b["pendentes"] for b in cargas["baias"] if b["baia"] == a["numero"]), [])

            # (tamanho, tarefa): tarefa None = ainda sem equipe; com tarefa = equipe a caminho
            itens = [(t, None) for t in pend if t in LADO_CAIXA]
            itens += [(tam, tarefa) for (tarefa, tam) in self._reservadas.get(a["numero"], [])]
            x0, y0 = self.px(a["x0"], a["y0"])
            rect = pygame.Rect(x0 + 6, y0 + 26, 27 * ESCALA, (a["y1"] - a["y0"]) * ESCALA - 32)

            def desenhar_item(x, y, it):
                tam, tarefa = it
                self._caixa(surf, x, y, LADO_CAIXA[tam], tam, reservada=tarefa is not None)

                if tarefa is not None:      # lembra onde a caixa estava, para deslizar até a equipe
                    self._slot_reservada[tarefa] = (x + LADO_CAIXA[tam] / 2, y + LADO_CAIXA[tam] / 2)

            resto = self._fluxo(surf, rect, itens, desenhar_item, lambda it: LADO_CAIXA[it[0]])

            if resto:
                self.txt(surf, self.fontes.b, f"+{resto}", (rect.right - 4, rect.bottom - 2), COR["texto"], "tr")

        for a in self.mapa.areas_do_tipo("zona"):
            n = op["por_zona"].get(a["nome"], 0)
            x0, y0 = self.px(a["x0"], a["y0"])
            rect = pygame.Rect(x0 + 6, y0 + 26, 17 * ESCALA, (a["y1"] - a["y0"]) * ESCALA - 32)
            lista = list(range(n))

            def entregue(x, y, _):
                r = pygame.Rect(x, y, 18, 18)
                pygame.draw.rect(surf, (200, 232, 205), r, border_radius=3)
                pygame.draw.rect(surf, COR["ok"], r, 1, border_radius=3)
                pygame.draw.lines(surf, COR["ok"], False, [(x + 4, y + 9), (x + 8, y + 13), (x + 14, y + 5)], 2)

            resto = self._fluxo(surf, rect, lista, entregue, lambda _: 18)

            if resto:
                self.txt(surf, self.fontes.b, f"+{resto}", (rect.right - 4, rect.bottom - 2), COR["texto"], "tr")

    def _caixas_em_transporte(self, surf, visoes, op):
        """A caixa entre os robôs que a carregam (que ficam nos lados/cantos dela)."""
        for e in self._equipes:
            if not e["robos"]:
                continue

            centro = (int(e["centro"][0]), int(e["centro"][1]))

            for rid in e["robos"]:
                if rid in self._exib:
                    pygame.draw.line(surf, (70, 76, 90), centro, (int(self._exib[rid][0]), int(self._exib[rid][1])), 2)

            lado = LADO_CAIXA[e["tam"]]
            self._caixa(surf, centro[0] - lado // 2, centro[1] - lado // 2, lado, e["tam"])

    # ------------------------------------------------------------ robô
    def estado_efetivo(self, v):
        return "defeito" if v["falha"] else v["estado"]

    def _bateria_mini(self, surf, x, y, larg, alt, pct):
        pygame.draw.rect(surf, (60, 66, 80), (x, y, larg, alt), border_radius=2)
        pygame.draw.rect(surf, cor_bateria(pct), (x + 1, y + 1, max(0, int((larg - 2) * pct / 100.0)), alt - 2), border_radius=2)

    def _icone_alerta(self, surf, cx, cy, s=7):
        pygame.draw.polygon(surf, COR["erro"], [(cx, cy - s), (cx + s, cy + s - 1), (cx - s, cy + s - 1)])
        pygame.draw.polygon(surf, COR["branco"], [(cx, cy - s), (cx + s, cy + s - 1), (cx - s, cy + s - 1)], 1)
        pygame.draw.line(surf, COR["branco"], (cx, cy - 2), (cx, cy + 2), 2)
        pygame.draw.circle(surf, COR["branco"], (cx, cy + 5), 1)

    def _icone_raio(self, surf, cx, cy, s=7):
        pts = [(cx + 2, cy - s), (cx - s + 2, cy + 1), (cx, cy + 1), (cx - 2, cy + s), (cx + s - 2, cy - 2), (cx + 1, cy - 2)]
        pygame.draw.polygon(surf, (255, 214, 10), pts)
        pygame.draw.polygon(surf, (120, 90, 0), pts, 1)

    def _icone_ampulheta(self, surf, cx, cy, s=6):
        pygame.draw.polygon(surf, COR["alerta"], [(cx - s, cy - s), (cx + s, cy - s), (cx, cy)])
        pygame.draw.polygon(surf, COR["alerta"], [(cx - s, cy + s), (cx + s, cy + s), (cx, cy)])
        pygame.draw.polygon(surf, (110, 70, 0), [(cx - s, cy - s), (cx + s, cy - s), (cx, cy), (cx + s, cy + s), (cx - s, cy + s), (cx, cy)], 1)

    def _robo(self, surf, v):
        cx, cy = (int(c) for c in self.pos_exibida(v))
        estado = self.estado_efetivo(v)
        cor = COR_ESTADO.get(estado, COR_ESTADO["iniciando"])
        sel = v["id"] == self.c.sel

        if not v["no_simulador"]:
            cor = (170, 174, 182)

        pygame.draw.circle(surf, (0, 0, 0, 0) if False else (90, 96, 110), (cx + 2, cy + 3), 11)
        pygame.draw.circle(surf, cor, (cx, cy), 11)
        pygame.draw.circle(surf, COR["branco"], (cx, cy), 11, 2)

        if sel:
            pygame.draw.circle(surf, COR["selecao"], (cx, cy), 16, 3)

        self.txt(surf, self.fontes.b, str(v["id"]), (cx, cy), COR["branco"], "mc")
        self._bateria_mini(surf, cx - 13, cy + 15, 26, 5, v["bateria"])

        if v["manual"]:
            pygame.draw.circle(surf, COR["manual"], (cx - 12, cy - 12), 7)
            pygame.draw.circle(surf, COR["branco"], (cx - 12, cy - 12), 7, 1)
            self.txt(surf, self.fontes.mini, "M", (cx - 12, cy - 12), COR["branco"], "mc")

        if v["falha"]:
            self._icone_alerta(surf, cx + 13, cy - 13)
        elif v["recarregando"]:
            self._icone_raio(surf, cx + 13, cy - 13)
        elif v["aguardando_passagem"]:
            self._icone_ampulheta(surf, cx + 13, cy - 13)

    # ----------------------------------------------------------- lista
    def _lista(self, surf, visoes):
        self.painel(surf, R_LISTA, f"ROBÔS ({len(visoes)})")
        area = pygame.Rect(R_LISTA.x + 4, R_LISTA.y + 28, R_LISTA.w - 8, R_LISTA.h - 32)
        pend = [rid for rid in self.c.pendentes if rid not in {v["id"] for v in visoes}]
        total = len(visoes) + len(pend)
        maximo = max(0, total * ALTURA_CARTAO - area.h)
        self.lista_scroll = min(self.lista_scroll, maximo)

        # Rola até o robô selecionado só quando a seleção MUDA (Tab, clique no mapa);
        # senão a lista voltaria sozinha a cada vez que o operador rolasse para longe dele
        if self.c.sel is not None and self.c.sel != self._sel_visto:
            ids = [v["id"] for v in visoes]

            if self.c.sel in ids:
                topo = ids.index(self.c.sel) * ALTURA_CARTAO

                if topo < self.lista_scroll:
                    self.lista_scroll = topo
                elif topo + ALTURA_CARTAO > self.lista_scroll + area.h:
                    self.lista_scroll = topo + ALTURA_CARTAO - area.h

        self._sel_visto = self.c.sel

        surf.set_clip(area)
        self.cartoes = []
        y = area.y - self.lista_scroll

        if total == 0:
            self.txt(surf, self.fontes.n, "Nenhum robô no galpão.", (area.centerx, area.y + 60), COR["texto_fraco"], "tc")
            self.txt(surf, self.fontes.p, "Use \"Novo robô\" no painel Simulador.", (area.centerx, area.y + 84), COR["texto_fraco"], "tc")

        for v in visoes:
            if y + ALTURA_CARTAO > area.y and y < area.bottom:
                self._cartao(surf, v, pygame.Rect(area.x, y, area.w - 8, ALTURA_CARTAO - 4))
            y += ALTURA_CARTAO

        for rid in pend:
            r = pygame.Rect(area.x, y, area.w - 8, ALTURA_CARTAO - 4)
            pygame.draw.rect(surf, (240, 241, 244), r, border_radius=5)
            pygame.draw.rect(surf, COR["painel_borda"], r, 1, border_radius=5)
            self.txt(surf, self.fontes.n, f"Robô {rid} — aguardando o Gestor criar o processo…", (r.x + 10, r.centery), COR["texto_fraco"], "ml")
            y += ALTURA_CARTAO

        surf.set_clip(None)

        if maximo > 0:                      # barra de rolagem
            h = max(24, int(area.h * area.h / (area.h + maximo)))
            ty = area.y + int((area.h - h) * self.lista_scroll / maximo)
            pygame.draw.rect(surf, (190, 196, 208), (R_LISTA.right - 8, ty, 4, h), border_radius=2)

    def _cartao(self, surf, v, r):
        sel = v["id"] == self.c.sel
        estado = self.estado_efetivo(v)
        cor = COR_ESTADO.get(estado, COR_ESTADO["iniciando"])

        pygame.draw.rect(surf, (255, 248, 218) if sel else COR["branco"], r, border_radius=5)
        pygame.draw.rect(surf, COR["selecao"] if sel else COR["painel_borda"], r, 2 if sel else 1, border_radius=5)

        ic = (r.x + 24, r.centery)
        pygame.draw.circle(surf, cor, ic, 17)
        pygame.draw.circle(surf, COR["branco"], ic, 17, 2)
        self.txt(surf, self.fontes.g, str(v["id"]), ic, COR["branco"], "mc")

        if v["falha"]:
            self._icone_alerta(surf, r.x + 40, r.y + 10, 6)
        elif v["recarregando"]:
            self._icone_raio(surf, r.x + 40, r.y + 10, 6)
        elif v["aguardando_passagem"]:
            self._icone_ampulheta(surf, r.x + 40, r.y + 10, 5)

        x = r.x + 52
        t = self.txt(surf, self.fontes.b, f"Robô {v['id']}", (x, r.y + 5))
        self.txt(surf, self.fontes.n, ROTULO_ESTADO.get(estado, estado), (t.right + 8, r.y + 5), cor)

        pct = max(0, min(100, int(math.ceil(v["bateria"]))))
        self._bateria_mini(surf, x, r.y + 27, 64, 10, pct)
        self.txt(surf, self.fontes.p, f"{pct}%", (x + 70, r.y + 32), cor_bateria(pct), "ml")
        pos = f"({v['x']:.0f},{v['y']:.0f}) → ({v['sp_x']},{v['sp_y']})"
        self.txt(surf, self.fontes.p, pos, (x + 104, r.y + 32), COR["texto_fraco"], "ml")

        if v["tarefa"]:
            detalhe = f"tarefa {v['tarefa']}: {v['fase'].replace('_', ' ')}"
        elif not v["no_status"]:
            detalhe = "ainda não registrado no Organizador"
        else:
            detalhe = f"percorreu {v['distancia']:.0f} un."

        self.txt(surf, self.fontes.p, cortar(self.fontes.p, detalhe, r.w - 60), (x, r.y + 42), COR["texto_fraco"])

        modo = pygame.Rect(r.right - 84, r.y + 6, 76, 22)
        cor_modo = COR["manual"] if v["manual"] else (92, 120, 100)
        pygame.draw.rect(surf, cor_modo, modo, border_radius=11)
        self.txt(surf, self.fontes.p, "MANUAL" if v["manual"] else "AUTO", modo.center, COR["branco"], "mc")
        self.cartoes.append((r, v["id"], modo))

    # ----------------------------------------------------------- botões
    def _desenhar_botao(self, surf, b):
        hover = b.rect.collidepoint(self.mouse)
        hab = b.esta_habilitado()
        ativo = b.esta_ativo()
        fonte = getattr(self.fontes, b.fonte)

        if ativo:
            fundo, texto, borda = COR["botao_ativo"], COR["branco"], (30, 90, 190)
        elif b.perigo and hab:
            fundo, texto, borda = (252, 228, 230) if not hover else (248, 205, 210), (150, 20, 35), (210, 120, 130)
        else:
            fundo = COR["botao_hover"] if (hover and hab) else COR["botao"]
            texto = COR["texto"] if hab else (160, 166, 176)
            borda = COR["botao_borda"]

        pygame.draw.rect(surf, fundo, b.rect, border_radius=5)
        pygame.draw.rect(surf, borda, b.rect, 1, border_radius=5)
        rotulo = b._val(b.rotulo)
        self.txt(surf, fonte, rotulo, b.rect.center, texto, "mc")

    def _botoes_do_painel(self, surf, rect):
        for b in self.botoes:
            if rect.contains(b.rect):
                self._desenhar_botao(surf, b)

    # -------------------------------------------------------- operação
    def _operacao(self, surf):
        sel = self.c.sel
        titulo = f"OPERAÇÃO — Robô {sel}" if sel is not None else "OPERAÇÃO — nenhum robô selecionado"
        self.painel(surf, R_OPERACAO, titulo)
        y0 = R_OPERACAO.y
        self.txt(surf, self.fontes.n, "Modo", (COL_X + 12, y0 + 37))
        self.txt(surf, self.fontes.n, "Destino (modo manual)", (COL_X + 12, y0 + 64))
        self.txt(surf, self.fontes.p, "botão direito = destino livre", (R_OPERACAO.right - 12, y0 + 66), COR["texto_fraco"], "tr")
        self.txt(surf, self.fontes.n, "x y", (COL_X + 12, y0 + 156))

        c = self.campo
        pygame.draw.rect(surf, COR["branco"], c.rect, border_radius=4)
        pygame.draw.rect(surf, COR["botao_ativo"] if c.foco else COR["botao_borda"], c.rect, 2 if c.foco else 1, border_radius=4)
        if c.texto or c.foco:
            self.txt(surf, self.fontes.n, c.texto + ("|" if c.foco and int(time.monotonic() * 2) % 2 == 0 else ""), (c.rect.x + 6, c.rect.centery), COR["texto"], "ml")
        else:
            self.txt(surf, self.fontes.n, "ex.: 120 60", (c.rect.x + 6, c.rect.centery), (170, 176, 186), "ml")

        self._botoes_do_painel(surf, R_OPERACAO)

    # -------------------------------------------------------- simulador
    def _simulador(self, surf, visoes):
        self.painel(surf, R_SIM, "SIMULADOR")
        y0 = R_SIM.y
        sel = self.c.sel
        self.txt(surf, self.fontes.n, f"Robô selecionado: {sel if sel is not None else '—'}", (COL_X + 12, y0 + 32), COR["texto"])
        self.txt(surf, self.fontes.n, "Novo robô (anunciado ao Gestor)", (COL_X + 12, y0 + 92))
        self.txt(surf, self.fontes.n, "Nova caixa (enviada ao Organizador)", (COL_X + 12, y0 + 150))
        self.txt(surf, self.fontes.n, "Baia", (COL_X + 12, y0 + 178))
        self.txt(surf, self.fontes.n, "Tamanho", (COL_X + 12, y0 + 210))
        self._botoes_do_painel(surf, R_SIM)

        # Estatísticas da simulação (insumo da análise de escalonabilidade)
        m = self.motor
        rede = self.c.rede
        y = y0 + 246
        pygame.draw.line(surf, COR["painel_borda"], (COL_X + 10, y - 4), (R_SIM.right - 10, y - 4))

        if m is not None:
            linhas = [
                f"Física {1.0 / m.passo:.0f} Hz · {m.ciclos} ciclos · atraso máx {m.atraso_max_s * 1000:.1f} ms · exec máx {m.exec_max_s * 1000:.2f} ms",
                f"Entradas publicadas: {m.publicacoes} · atrasos de ciclo: {m.atrasos}",
            ]
        else:
            linhas = ["Física: desligada"]

        linhas.append(f"MQTT: {rede.publicadas} publicadas · {rede.recebidas} recebidas · {rede.invalidas + self.c.operacao.mensagens_invalidas} inválidas")

        for i, ln in enumerate(linhas):
            self.txt(surf, self.fontes.p, cortar(self.fontes.p, ln, COL_W - 24), (COL_X + 12, y + i * 17), COR["texto_fraco"])

    # ----------------------------------------------------------- caixas
    def _caixas(self, surf, op):
        self.painel(surf, R_CAIXAS, "CAIXAS")
        cargas = op["cargas"]
        x0, y = R_CAIXAS.x + 10, R_CAIXAS.y + 30
        self.txt(surf, self.fontes.b, "Pendentes nas baias", (x0, y))
        y += 20

        for numero in range(1, 5):
            pend = []

            if cargas:
                pend = next((b["pendentes"] for b in cargas["baias"] if b["baia"] == numero), [])

            # Com a equipe já designada a caixa continua na baia (borda azul) até ser coletada
            itens = [(t, False) for t in pend if t in LADO_CAIXA]
            itens += [(tam, True) for (_, tam) in self._reservadas.get(numero, [])]

            self.txt(surf, self.fontes.n, f"Baia {numero}", (x0, y + 1))
            x = x0 + 62

            for t, reservada in itens[:14]:
                self._caixa(surf, x, y, 18, t, reservada=reservada)
                x += 22

            if len(itens) > 14:
                self.txt(surf, self.fontes.p, f"+{len(itens) - 14}", (x, y + 3))

            if not itens:
                self.txt(surf, self.fontes.p, "vazia", (x0 + 62, y + 3), COR["texto_fraco"])

            y += 21

        y += 4
        self.txt(surf, self.fontes.b, "Em transporte", (x0, y))
        y += 19

        if not cargas or not cargas["em_transporte"]:
            self.txt(surf, self.fontes.p, "nenhuma", (x0 + 8, y), COR["texto_fraco"])
            y += 16
        else:
            for m in cargas["em_transporte"][:3]:
                destino = m["destino"] or "destino a definir"
                robos = "+".join(str(r) for r in sorted(m["robos"]))
                estado = self._estado_missao.get(m["tarefa"], "")
                self.txt(surf, self.fontes.p, cortar(self.fontes.p, f"#{m['tarefa']}  Baia {m['baia']} → {destino}  ·  tam. {m['tamanho']}  ·  robôs {robos}  ·  {estado}", R_CAIXAS.w - 24), (x0 + 4, y))
                y += 16

        y = R_CAIXAS.bottom - 24
        total = cargas["entregues"] if cargas else 0
        zonas = op["por_zona"]
        partes = "  ".join(f"{n}:{zonas.get('Zona ' + n, 0)}" for n in "ABCD")
        self.txt(surf, self.fontes.b, f"Entregues: {total}", (x0, y), COR["ok"])
        self.txt(surf, self.fontes.p, f"nesta sessão por zona  {partes}", (x0 + 120, y + 2), COR["texto_fraco"])

    # ----------------------------------------------------------- eventos
    def _eventos(self, surf, op):
        self.painel(surf, R_EVENTOS, "EVENTOS")
        eventos = op["eventos"]

        if self.filtro_selecionado:
            eventos = [e for e in eventos if e["robo"] == self.c.sel]

        area = pygame.Rect(R_EVENTOS.x + 6, R_EVENTOS.y + 28, R_EVENTOS.w - 12, R_EVENTOS.h - 32)
        linhas = area.h // LINHA_EVENTO
        self.ev_scroll = max(0, min(self.ev_scroll, max(0, len(eventos) - linhas)))
        fim = len(eventos) - self.ev_scroll
        visiveis = eventos[max(0, fim - linhas):fim]
        surf.set_clip(area)

        for i, e in enumerate(visiveis):
            y = area.y + i * LINHA_EVENTO
            hora = time.strftime("%H:%M:%S", time.localtime(e["t"])) if e["t"] else "--:--:--"
            quem = f"R{e['robo']}" if e["robo"] is not None else "  "
            cor = COR_EVENTO.get(e["evento"], COR["texto"])
            self.txt(surf, self.fontes.mini, hora, (area.x, y + 2), COR["texto_fraco"])
            self.txt(surf, self.fontes.mini, quem, (area.x + 54, y + 2), COR["texto_fraco"])
            self.txt(surf, self.fontes.mini, e["evento"], (area.x + 80, y + 2), cor)
            self.txt(surf, self.fontes.mini, cortar(self.fontes.mini, e["texto"], area.w - 172), (area.x + 168, y + 2), COR["texto"])

        if not visiveis:
            self.txt(surf, self.fontes.p, "Sem eventos ainda.", (area.centerx, area.y + 40), COR["texto_fraco"], "tc")

        surf.set_clip(None)

        if self.ev_scroll:
            self.txt(surf, self.fontes.p, f"↑ {self.ev_scroll} linhas acima do fim (role para baixo)", (R_EVENTOS.right - 10, R_EVENTOS.bottom - 6), COR["alerta"], "mr")

        self._botoes_do_painel(surf, R_EVENTOS)

    # ----------------------------------------------------- sobreposições
    def _avisos(self, surf):
        y = R_MAPA.bottom - 8

        for texto in reversed(self.c.avisos_ativos()):
            larg = self.fontes.n.size(texto)[0] + 24
            r = pygame.Rect(0, 0, larg, 26)
            r.midbottom = (R_MAPA.centerx, y)
            fundo = pygame.Surface(r.size, pygame.SRCALPHA)
            pygame.draw.rect(fundo, (30, 34, 44, 235), fundo.get_rect(), border_radius=6)
            surf.blit(fundo, r)
            pygame.draw.rect(surf, COR["alerta"], r, 1, border_radius=6)
            self.txt(surf, self.fontes.n, texto, r.center, COR["branco"], "mc")
            y -= 32

    def _fantasma(self, surf):
        if not R_MAPA.collidepoint(self.mouse):
            return

        ux, uy = self.unidades(self.mouse)
        p = self.px(round(ux), round(uy))
        pygame.draw.circle(surf, COR["info"], p, 12, 2)
        pygame.draw.line(surf, COR["info"], (p[0] - 18, p[1]), (p[0] + 18, p[1]), 1)
        pygame.draw.line(surf, COR["info"], (p[0], p[1] - 18), (p[0], p[1] + 18), 1)
        self.txt(surf, self.fontes.p, f"novo robô em ({round(ux)}, {round(uy)})", (p[0] + 16, p[1] - 22), COR["info"])

    def _tela_ajuda(self, surf):
        veu = pygame.Surface((LARGURA, ALTURA), pygame.SRCALPHA)
        veu.fill((10, 14, 22, 215))
        surf.blit(veu, (0, 0))
        r = pygame.Rect(0, 0, 820, 40 + len(AJUDA) * 24)
        r.center = (LARGURA // 2, ALTURA // 2)
        pygame.draw.rect(surf, COR["painel"], r, border_radius=8)
        self.txt(surf, self.fontes.g, "Atalhos e comandos", (r.x + 20, r.y + 12))

        for i, ln in enumerate(AJUDA):
            negrito = not ln.startswith(" ")
            self.txt(surf, self.fontes.b if negrito else self.fontes.n, ln, (r.x + 20, r.y + 48 + i * 24),
                     COR["titulo"] if negrito else COR["texto"])
