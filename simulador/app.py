"""Aplicação: junta mapa, física, rede, controle e interface, e roda o laço principal."""

import argparse
import logging
import os
import signal
import sys
import time

from simulador import config
from simulador.controle import Controle
from simulador.fisica import MotorFisica, Mundo
from simulador.mapa import ErroMapa, carregar_mapa
from simulador.operacao import EstadoOperacao
from simulador.rede import Rede

FPS = 30


class App:
    """Tudo o que existe independente de haver janela: dá para rodar sem ela."""

    def __init__(self, host, porta, caminho_mapa=None, cliente=None):
        self.mapa = carregar_mapa(caminho_mapa)
        self.mundo = Mundo(self.mapa)
        self.operacao = EstadoOperacao()
        self.rede = Rede(self.mundo, self.operacao, host, porta, cliente=cliente)
        self.motor = MotorFisica(self.mundo, self.rede.publicar_entradas)
        self.controle = Controle(self.mapa, self.mundo, self.operacao, self.rede)
        self.mundo.ao_violacao = lambda texto: self.operacao.registrar_local(texto, "ALERTA")

    def iniciar(self):
        self.rede.iniciar()
        self.motor.start()

    def parar(self):
        self.motor.parar()
        self.motor.join(timeout=1.0)
        self.rede.parar()


class Janela:
    """Janela redimensionável: o desenho usa sempre LARGURA x ALTURA e é escalado para caber."""

    def __init__(self, largura, altura):
        import pygame
        self.pg = pygame
        self.largura, self.altura = largura, altura
        pygame.display.set_caption("ATR 2026/2 — Simulador e Tela de Operação")

        try:
            info = pygame.display.Info()
            escala = min(1.0, info.current_w * 0.96 / largura, info.current_h * 0.90 / altura)
        except pygame.error:
            escala = 1.0

        escala = max(escala, 0.4)
        self.tela = pygame.display.set_mode((int(largura * escala), int(altura * escala)), pygame.RESIZABLE)
        self.canvas = pygame.Surface((largura, altura))
        self._area = self._calcular_area()

    def _calcular_area(self):
        w, h = self.pg.display.get_surface().get_size()
        esc = min(w / self.largura, h / self.altura)
        tw, th = int(self.largura * esc), int(self.altura * esc)
        return self.pg.Rect((w - tw) // 2, (h - th) // 2, tw, th), esc

    def logico(self, pos):
        area, esc = self._calcular_area()
        return (int((pos[0] - area.x) / esc), int((pos[1] - area.y) / esc))

    def apresentar(self):
        tela = self.pg.display.get_surface()
        area, _ = self._calcular_area()

        if area.size == (self.largura, self.altura):
            tela.blit(self.canvas, area.topleft)
        else:
            tela.fill((18, 20, 26))
            tela.blit(self.pg.transform.smoothscale(self.canvas, area.size), area.topleft)

        self.pg.display.flip()

    def tela_cheia(self):
        self.pg.display.toggle_fullscreen()


def executar_com_janela(app, capturar=None, capturar_apos=5.0):
    import pygame
    from simulador.gui import ALTURA, LARGURA, Interface

    pygame.init()
    janela = Janela(LARGURA, ALTURA)
    ui = Interface(app.controle, app.motor)
    relogio = pygame.time.Clock()
    inicio = time.monotonic()
    rodando = True

    while rodando:
        for ev in pygame.event.get():
            if ev.type == pygame.QUIT:
                rodando = False
            elif ev.type == pygame.MOUSEMOTION:
                ui.mover(janela.logico(ev.pos))
            elif ev.type == pygame.MOUSEBUTTONDOWN and ev.button in (1, 3):
                ui.clique(janela.logico(ev.pos), ev.button)
            elif ev.type == pygame.MOUSEWHEEL:
                ui.roda(janela.logico(pygame.mouse.get_pos()), ev.y)
            elif ev.type == pygame.KEYDOWN:
                if ev.key == pygame.K_F11:
                    janela.tela_cheia()
                elif ev.key == pygame.K_F12:
                    salvar_captura(janela.canvas)
                else:
                    ui.tecla(ev)

        app.controle.atualizar()
        ui.desenhar(janela.canvas)
        janela.apresentar()
        ui.fps = relogio.get_fps()
        relogio.tick(FPS)

        if capturar and time.monotonic() - inicio >= capturar_apos:
            pygame.image.save(janela.canvas, capturar)
            print(f"captura salva em {capturar}", flush=True)
            rodando = False

    pygame.quit()


def salvar_captura(canvas, pasta="capturas"):
    import pygame
    os.makedirs(pasta, exist_ok=True)
    caminho = os.path.join(pasta, time.strftime("sim_%Y%m%d_%H%M%S.png"))
    pygame.image.save(canvas, caminho)
    print(f"captura salva em {caminho}", flush=True)


def executar_sem_janela(app):
    """Só física + MQTT (servidores sem tela, testes, medições de escalabilidade)."""
    parar = {"sim": False}
    signal.signal(signal.SIGINT, lambda *a: parar.update(sim=True))
    signal.signal(signal.SIGTERM, lambda *a: parar.update(sim=True))
    print("simulador sem janela: Ctrl+C para encerrar", flush=True)
    ultimo = 0.0

    while not parar["sim"]:
        time.sleep(0.2)

        if time.monotonic() - ultimo >= 5.0:
            ultimo = time.monotonic()
            m = app.motor
            print(f"robôs={len(app.mundo.ids())} ciclos={m.ciclos} atraso_max={m.atraso_max_s * 1000:.1f}ms "
                  f"exec_max={m.exec_max_s * 1000:.2f}ms publicadas={app.rede.publicadas} "
                  f"violações={app.mundo.violacoes} broker={'ok' if app.rede.conectado else 'desconectado'}", flush=True)


def ler_argumentos(argv=None):
    ap = argparse.ArgumentParser(prog="simulador", description="Simulador do galpão + Tela de Operação (ATR 2026/2)")
    ap.add_argument("--host", default=os.environ.get(config.ENV_HOST, config.HOST_PADRAO),
                    help=f"broker MQTT (padrão: ${config.ENV_HOST} ou {config.HOST_PADRAO})")
    ap.add_argument("--porta", type=int, default=int(os.environ.get(config.ENV_PORTA, config.PORTA_PADRAO)),
                    help=f"porta do broker (padrão: ${config.ENV_PORTA} ou {config.PORTA_PADRAO})")
    ap.add_argument("--mapa", default=None, help="JSON do mapa (padrão: bin/mapa.json, gerado por bin/mapa json)")
    ap.add_argument("--sem-janela", action="store_true", help="só física e MQTT, sem interface gráfica")
    ap.add_argument("--capturar", metavar="ARQUIVO.png", help="salva uma captura da tela e encerra")
    ap.add_argument("--capturar-apos", type=float, default=5.0, help="segundos até a captura (padrão: 5)")
    ap.add_argument("-v", "--verboso", action="store_true", help="log da rede no terminal")
    return ap.parse_args(argv)


def main(argv=None):
    args = ler_argumentos(argv)
    logging.basicConfig(level=logging.INFO if args.verboso else logging.WARNING,
                        format="%(asctime)s [%(name)s] %(message)s")

    try:
        app = App(args.host, args.porta, args.mapa)
    except ErroMapa as e:
        print(f"erro: {e}", file=sys.stderr)
        return 1

    app.iniciar()

    try:
        if args.sem_janela:
            executar_sem_janela(app)
        else:
            executar_com_janela(app, args.capturar, args.capturar_apos)
    finally:
        app.parar()

    return 0
