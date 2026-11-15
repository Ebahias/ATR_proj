"""Mapa do galpão, lido do JSON exportado por "bin/mapa json".

O C++ (comum/mapa.cpp) é a fonte única da verdade sobre o mapa: o simulador
só o lê, para que Organizador, robôs e tela nunca discordem da geometria.
"""

import json
import math
import os
import subprocess

from simulador import config

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


class ErroMapa(Exception):
    pass


class Mapa:
    """Vista superior do galpão: nós, trechos, passagens estreitas e áreas."""

    def __init__(self, dados):
        try:
            self.largura = int(dados["largura"])
            self.altura = int(dados["altura"])
            self.nos = list(dados["nos"])
            self.arestas = list(dados["arestas"])
            self.passagens = list(dados["passagens"])
            self.areas = list(dados["areas"])
            self.vagas_recarga = list(dados["vagas_recarga"])
        except (KeyError, TypeError, ValueError) as e:
            raise ErroMapa(f"JSON do mapa incompleto: {e}") from e

    # ------------------------------------------------------------ áreas
    def area_do_ponto(self, x, y):
        """Área que contém o ponto (bordas incluídas, como mapa_area_do_ponto), ou None."""
        for a in self.areas:
            if a["x0"] <= x <= a["x1"] and a["y0"] <= y <= a["y1"]:
                return a
        return None

    def em_recarga(self, x, y):
        a = self.area_do_ponto(x, y)
        return a is not None and a["tipo"] == "recarga"

    def buscar_area(self, tipo, numero):
        for a in self.areas:
            if a["tipo"] == tipo and a["numero"] == numero:
                return a
        return None

    def areas_do_tipo(self, tipo):
        return [a for a in self.areas if a["tipo"] == tipo]

    def dentro(self, x, y):
        return 0 <= x <= self.largura and 0 <= y <= self.altura

    # ------------------------------------------------------ passagens
    def segmento_passagem(self, i):
        """((x0, y0), (x1, y1)) do corredor estreito i."""
        p = self.passagens[i]
        a, b = self.nos[p["a"]], self.nos[p["b"]]
        return (a["x"], a["y"]), (b["x"], b["y"])

    def dentro_da_passagem(self, i, x, y):
        """O ponto está fisicamente DENTRO da passagem estreita i?

        "Dentro" = sobre o eixo do corredor e afastado dos nós das pontas:
        quem espera parado no nó de entrada ainda não está na passagem.
        """
        (x0, y0), (x1, y1) = self.segmento_passagem(i)
        comp = math.hypot(x1 - x0, y1 - y0)

        if comp == 0:
            return False

        ux, uy = (x1 - x0) / comp, (y1 - y0) / comp
        ao_longo = (x - x0) * ux + (y - y0) * uy
        lateral = abs(-(x - x0) * uy + (y - y0) * ux)

        return (config.PASSAGEM_BORDA < ao_longo < comp - config.PASSAGEM_BORDA
                and lateral <= config.PASSAGEM_LARGURA)

    def perto_de_passagem(self, x, y, folga=5.0):
        """O ponto está dentro ou a menos de 'folga' unidades de uma passagem estreita?"""
        for i in range(len(self.passagens)):
            (x0, y0), (x1, y1) = self.segmento_passagem(i)
            comp = math.hypot(x1 - x0, y1 - y0)

            if comp == 0:
                continue

            ux, uy = (x1 - x0) / comp, (y1 - y0) / comp
            ao_longo = (x - x0) * ux + (y - y0) * uy
            lateral = abs(-(x - x0) * uy + (y - y0) * ux)

            if -folga < ao_longo < comp + folga and lateral <= folga:
                return True

        return False

    def ocupacao_passagens(self, posicoes):
        """Quantos robôs estão dentro de cada passagem estreita (posicoes = [(x, y)]).
        Serve para o simulador conferir que o sistema respeita a capacidade."""
        return [sum(1 for (x, y) in posicoes if self.dentro_da_passagem(i, x, y))
                for i in range(len(self.passagens))]


def _gerar_json(caminho_json):
    """Roda "bin/mapa json" e grava o resultado em caminho_json."""
    exe = os.path.join(RAIZ, "bin", "mapa")

    if not os.path.exists(exe):
        raise ErroMapa(
            f"mapa não encontrado: nem {caminho_json} nem {exe}.\n"
            "Compile o projeto na raiz (make) e rode:  bin/mapa json > bin/mapa.json")

    try:
        saida = subprocess.run([exe, "json"], capture_output=True, text=True, check=True).stdout
    except (OSError, subprocess.CalledProcessError) as e:
        raise ErroMapa(f"falha ao executar {exe} json: {e}") from e

    try:
        with open(caminho_json, "w", encoding="utf-8") as f:
            f.write(saida)
    except OSError:
        pass            # sem permissão de escrita: usa só em memória

    return saida


def carregar_mapa(caminho=None):
    """Carrega o mapa de caminho (padrão: bin/mapa.json na raiz do projeto).

    Se o arquivo padrão não existir, gera-o com bin/mapa json.
    """
    padrao = caminho is None

    if padrao:
        caminho = os.path.join(RAIZ, "bin", config.NOME_ARQUIVO_MAPA)

    if os.path.exists(caminho):
        with open(caminho, encoding="utf-8") as f:
            texto = f.read()
    elif padrao:
        texto = _gerar_json(caminho)
    else:
        raise ErroMapa(f"arquivo de mapa não encontrado: {caminho}")

    try:
        return Mapa(json.loads(texto))
    except json.JSONDecodeError as e:
        raise ErroMapa(f"{caminho} não é um JSON válido: {e}") from e
