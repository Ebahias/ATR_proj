#!/usr/bin/env python3
"""Teste de integração do simulador/Tela de Operação contra o sistema real.

Pré-requisitos (o testes/teste_simulador.sh cuida deles): broker MQTT no ar e
bin/org_logistica rodando (que sobe o Gestor com --fonte mqtt e o Coletor).

O teste não chama a lógica diretamente: ele CLICA nos botões da interface (os
mesmos objetos que o pygame aciona) e confere a reação do sistema real, e
salva capturas de tela em run/teste_simulador/ nos momentos principais.
"""

import os
import re
import sys
import time

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_AUDIODRIVER", "dummy")

sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

import pygame  # noqa: E402

from simulador import config  # noqa: E402
from simulador.app import App, ler_argumentos  # noqa: E402
from simulador.gui import ALTURA, LARGURA, R_MAPA, Interface  # noqa: E402

SAIDA = os.path.join("run", "teste_simulador")
falhas = 0


def ok(desc):
    print(f"  [OK]    {desc}", flush=True)


def falha(desc):
    global falhas
    falhas += 1
    print(f"  [FALHA] {desc}", flush=True)


class Teste:

    def __init__(self, app):
        pygame.init()
        self.app = app
        self.c = app.controle
        self.ui = Interface(app.controle, app.motor)
        self.canvas = pygame.Surface((LARGURA, ALTURA))
        os.makedirs(SAIDA, exist_ok=True)

    # ------------------------------------------------------------ apoio
    def quadro(self):
        """Um quadro da interface: manutenção + desenho (pega exceções de desenho com dados reais)."""
        self.c.atualizar()
        self.ui.desenhar(self.canvas)

    def esperar(self, desc, cond, timeout=10.0):
        fim = time.monotonic() + timeout

        while time.monotonic() < fim:
            self.quadro()

            if cond():
                ok(desc)
                return True

            time.sleep(0.1)

        falha(f"{desc} (timeout de {timeout:.0f} s)")
        return False

    def foto(self, nome):
        self.quadro()
        pygame.image.save(self.canvas, os.path.join(SAIDA, nome))

    def botao(self, rotulo, y=None, x=None):
        """Botão da interface pelo rótulo (e, se repetido, pela posição)."""
        for b in self.ui.botoes:
            r = b._val(b.rotulo)

            if r == rotulo and (y is None or b.rect.y == y) and (x is None or b.rect.x == x):
                return b

        raise KeyError(rotulo)

    def clicar(self, rotulo, **kw):
        b = self.botao(rotulo, **kw)
        self.ui.clique(b.rect.center, 1)

    def status(self, rid):
        return self.app.operacao.snapshot()["frota"].get(rid)

    def fisica(self, rid):
        return next((r for r in self.app.mundo.snapshot() if r.id == rid), None)

    def cargas(self):
        return self.app.operacao.snapshot()["cargas"]

    def selecionar_no_mapa(self, rid):
        r = self.fisica(rid)
        self.ui.clique(self.ui.px(r.x, r.y), 1)


def cenario(t):
    c, app = t.c, t.app

    print("1. Conexão")
    t.esperar("broker MQTT conectado", lambda: app.rede.conectado, 10)
    t.esperar("Organizador publicando atr/frota/status", app.operacao.organizador_online, 10)
    t.esperar("atr/cargas recebido (filas das baias)", lambda: t.cargas() is not None, 10)

    print("2. Criar robôs pelos botões (padrão, aleatória, clicar no mapa)")
    t.clicar("Posição padrão")
    t.esperar("robô 1 apareceu no simulador", lambda: t.fisica(1) is not None, 10)
    r1 = t.fisica(1)
    (ok if (round(r1.x), round(r1.y)) == (config.POS_INICIAL_X, config.POS_INICIAL_Y) else falha)(
        f"robô 1 nasceu na posição padrão ({round(r1.x)}, {round(r1.y)})")

    t.clicar("Aleatória", y=t.botao("Aleatória", x=None).rect.y)
    t.esperar("robô 2 apareceu", lambda: t.fisica(2) is not None, 10)

    t.clicar("Clicar no mapa")
    (ok if c.posicionando else falha)("modo de posicionamento ativado")
    t.ui.mover(t.ui.px(60, 100))
    t.foto("01_posicionando.png")
    t.ui.clique(t.ui.px(60, 100), 1)
    t.esperar("robô 3 apareceu no ponto clicado", lambda: t.fisica(3) is not None, 10)
    r3 = t.fisica(3)
    (ok if abs(r3.x - 60) <= 1 and abs(r3.y - 100) <= 1 else falha)(f"robô 3 em ({r3.x:.0f}, {r3.y:.0f}), esperado (60, 100)")

    for _ in range(2):
        t.clicar("Aleatória")
    t.esperar("robôs 4 e 5 apareceram", lambda: t.fisica(4) and t.fisica(5), 12)
    t.esperar("Organizador registrou os 5 robôs", lambda: all(t.status(i) for i in range(1, 6)), 10)

    print("3. Selecionar robô no mapa e na lista")
    t.selecionar_no_mapa(3)
    (ok if c.sel == 3 else falha)(f"clique no robô do mapa selecionou o robô 3 (sel={c.sel})")
    t.quadro()
    rect, rid, _ = next(x for x in t.ui.cartoes if x[1] == 5)
    t.ui.clique((rect.x + 100, rect.y + 6), 1)
    (ok if c.sel == 5 else falha)(f"clique no cartão da lista selecionou o robô 5 (sel={c.sel})")

    print("4. Nova caixa")
    antes = t.cargas()["t"]
    for b in t.ui.botoes:       # baia 2, tamanho 1 (segmentos do formulário)
        pass
    sim_y = 568
    t.clicar("2", y=sim_y + 172)
    t.clicar("1", y=sim_y + 204)
    (ok if (c.baia_caixa, c.tamanho_caixa) == (2, 1) else falha)("formulário em Baia 2, tamanho 1")
    t.clicar("Criar caixa")
    t.esperar("Organizador republicou atr/cargas após CAIXA", lambda: t.cargas()["t"] != antes, 8)
    t.clicar("Caixa aleatória")

    print("5. Transporte colaborativo e entrega (automático)")
    visto = {"transporte": False}

    def em_transporte():
        cg = t.cargas()
        if cg and any(any(t.fisica(r) and t.fisica(r).atracada for r in m["robos"]) for m in cg["em_transporte"]):
            if not visto["transporte"]:
                t.foto("02_transportando.png")
                visto["transporte"] = True
            return True
        return False

    t.esperar("há robôs com carga atracada (caixa no mapa)", em_transporte, 45)
    t.esperar("ao menos 2 cargas entregues (atr/cargas)", lambda: (t.cargas() or {}).get("entregues", 0) >= 2, 90)
    t.esperar("evento de entrega chegou pelo Coletor", lambda: bool(app.operacao.snapshot()["entregues"]), 10)
    t.esperar("entregas contadas por zona", lambda: sum(app.operacao.snapshot()["por_zona"].values()) >= 1, 5)
    t.foto("03_entregas.png")

    print("6. Modo manual e setpoint arbitrário")
    # Livre = disponível e sem missão em andamento (a tarefa concluída continua no status)
    livre = lambda rid: (t.status(rid) or {}).get("estado") == "disponível" and (t.status(rid) or {}).get("fase") in ("-", "concluida", "falhou")
    t.esperar("algum robô livre", lambda: any(livre(i) for i in range(1, 6)), 90)
    rid = next(i for i in range(1, 6) if livre(i))
    c.selecionar(rid)

    # Em automático o clique direito é recusado com aviso
    t.ui.clique(t.ui.px(120, 60), 3)
    (ok if any("AUTOMÁTICO" in a for a in c.avisos_ativos()) else falha)("destino recusado com aviso em modo automático")

    t.clicar("MANUAL")
    t.esperar(f"robô {rid} ficou MANUAL no Organizador", lambda: t.status(rid) and t.status(rid)["modo"] == "manual", 8)
    t.ui.clique(t.ui.px(120, 60), 3)
    t.esperar(f"robô {rid} chegou em (120, 60)", lambda: t.fisica(rid) and abs(t.fisica(rid).x - 120) < 1 and abs(t.fisica(rid).y - 60) < 1, 25)
    t.foto("04_manual.png")

    t.clicar("Baia 3")
    t.esperar(f"botão 'Baia 3' levou o robô {rid} ao centro da baia", lambda: t.fisica(rid) and abs(t.fisica(rid).x - 15) < 1 and abs(t.fisica(rid).y - 75) < 1, 30)

    t.ui.campo.foco = True
    for ch in "90 105":
        t.ui.tecla(pygame.event.Event(pygame.KEYDOWN, key=ord(ch) if ch != " " else pygame.K_SPACE, unicode=ch))
    t.ui.campo.foco = False
    t.clicar("Ir")
    t.esperar(f"campo \"x y\" levou o robô {rid} a (90, 105)", lambda: abs(t.fisica(rid).x - 90) < 1 and abs(t.fisica(rid).y - 105) < 1, 30)

    t.ui.clique(t.ui.px(30, 20), 3)
    time.sleep(0.5)
    t.clicar("Parar")
    sp = t.fisica(rid)
    t.esperar("Parar: setpoint passou a ser a posição atual", lambda: (round(t.fisica(rid).sp_x), round(t.fisica(rid).sp_y)) == (round(t.fisica(rid).x), round(t.fisica(rid).y)), 5)

    print("7. Defeito injetado pelo simulador")
    t.clicar("Gerar defeito")
    t.esperar(f"robô {rid} em defeito no Organizador", lambda: t.status(rid) and t.status(rid)["falha"] == 1, 5)
    t.foto("05_defeito.png")
    t.clicar("Reparar defeito")
    t.esperar(f"defeito removido do robô {rid}", lambda: t.status(rid) and t.status(rid)["falha"] == 0, 8)

    print("8. Volta ao automático e bateria baixa leva à recarga")
    t.clicar("AUTOMÁTICO")
    t.esperar("robô voltou ao automático", lambda: t.status(rid)["modo"].startswith("autom"), 8)
    t.clicar("Bateria → 15%")
    t.esperar("robô foi para a recarga (estado recarregando)", lambda: t.status(rid)["estado"] == "recarregando" or (t.fisica(rid) and t.fisica(rid).recarregando), 40)
    t.foto("06_recarga.png")
    t.esperar("bateria recarregou acima de 90%", lambda: t.fisica(rid).bateria > 90, 30)

    print("9. Processo do robô morre: o simulador remove o robô")
    texto = next((e["texto"] for e in app.operacao.snapshot()["eventos"] if e["robo"] == 5 and e["evento"] == "INICIADO"), "")
    m = re.search(r"processo (\d+)", texto)
    if m:
        os.kill(int(m.group(1)), 9)
        t.esperar("robô 5 removido do simulador (vivo=offline)", lambda: t.fisica(5) is None, 8)
    else:
        falha("pid do robô 5 não encontrado nos eventos")

    print("10. Simulador reiniciado com os robôs vivos: adoção")
    vivos = [i for i in range(1, 5) if t.fisica(i) is not None]
    antes = {i: (t.fisica(i).x, t.fisica(i).y) for i in vivos}
    violacoes_antes = app.mundo.violacoes
    invalidas_antes = app.rede.invalidas + app.operacao.mensagens_invalidas
    t.foto("07_antes_de_reiniciar.png")
    for e in app.operacao.snapshot()["eventos"]:
        if e["evento"] == "ALERTA":
            print(f"  [ALERTA antes] {time.strftime('%H:%M:%S', time.localtime(e['t']))} {e['texto']}", flush=True)
    app.parar()
    time.sleep(2.5)         # sem entradas por mais de 1 s: os robôs declaram defeito (timeout)
    args = ler_argumentos([])
    app = App(args.host, args.porta)
    app.iniciar()
    t = Teste(app)
    t.esperar("novo simulador adotou os robôs 1 a 4", lambda: all(t.fisica(i) is not None for i in vivos), 10)
    t.esperar("robô morto (robô 5) não foi readotado", lambda: t.fisica(5) is None, 3)
    for i in vivos:
        x, y = antes[i]
        f = t.fisica(i)
        (ok if f and abs(f.x - x) <= 40 and abs(f.y - y) <= 40 else falha)(f"robô {i} adotado perto de onde estava ({f.x:.0f},{f.y:.0f}) ~ ({x:.0f},{y:.0f})")
    t.esperar("nenhum robô ficou em defeito depois da adoção", lambda: all(t.status(i) and t.status(i)["falha"] == 0 for i in vivos), 20)
    t.foto("08_depois_de_reiniciar.png")

    print("11. Conferências finais")
    for e in app.operacao.snapshot()["eventos"]:
        if e["evento"] == "ALERTA":
            print(f"  [ALERTA] {e['texto']}", flush=True)
    print(f"  violações: antes do reinício = {violacoes_antes}, depois = {app.mundo.violacoes}", flush=True)
    total_viol = violacoes_antes + app.mundo.violacoes
    (ok if total_viol == 0 else falha)(f"nenhuma violação de capacidade das passagens ({total_viol})")
    invalidas = invalidas_antes + app.rede.invalidas + app.operacao.mensagens_invalidas
    (ok if invalidas == 0 else falha)(f"nenhuma mensagem inválida ({invalidas})")
    m = app.motor
    print(f"  física: {m.ciclos} ciclos, atraso máx {m.atraso_max_s * 1000:.1f} ms, exec máx {m.exec_max_s * 1000:.2f} ms, "
          f"{m.publicacoes} entradas publicadas", flush=True)
    t.foto("09_final.png")
    return app


def main():
    args = ler_argumentos([])
    app = App(args.host, args.porta)
    app.iniciar()
    ultimo = app

    try:
        ultimo = cenario(Teste(app)) or app
    finally:
        ultimo.parar()

    print()
    print("TODOS OS TESTES PASSARAM" if falhas == 0 else f"{falhas} TESTE(S) FALHARAM")
    return falhas


if __name__ == "__main__":
    sys.exit(main())
