"""Estado da operação: tudo o que o Organizador da Logística publica por MQTT.

  atr/frota/status  status de cada robô (200 ms)
  atr/cargas        filas das baias, missões em transporte, total entregue
  atr/eventos       eventos dos robôs e cargas entregues (do Coletor de Dados)

É escrito pela thread de rede do paho e lido pela thread da tela, por isso
tudo passa por um lock; a tela trabalha sobre cópias (snapshot).
"""

import json
import threading
import time
import unicodedata
from collections import Counter, deque

from simulador import config

MAX_EVENTOS = 500


def normalizar(texto):
    """Minúsculas e sem acento: "disponível" -> "disponivel" (robusto a mudanças de grafia)."""
    s = unicodedata.normalize("NFKD", str(texto))
    return "".join(c for c in s if not unicodedata.combining(c)).lower().strip()


class EstadoOperacao:

    def __init__(self):
        self.lock = threading.Lock()
        self.frota = {}                     # id -> dict do atr/frota/status
        self.t_frota = None                 # time.monotonic() do último status
        self.cargas = None                  # último atr/cargas, ou None
        self.eventos = deque(maxlen=MAX_EVENTOS)
        self.entregues = []                 # cargas entregues vistas nesta sessão
        self.entregues_por_zona = Counter()
        self.mensagens_invalidas = 0
        self.mensagens = Counter()          # por tipo, para estatística

    # ------------------------------------------------------ entrada
    def _json(self, payload):
        try:
            dados = json.loads(payload)
        except (ValueError, TypeError):
            self.mensagens_invalidas += 1
            return None

        if not isinstance(dados, dict):
            self.mensagens_invalidas += 1
            return None

        return dados

    def atualizar_frota(self, payload):
        dados = self._json(payload)

        if dados is None or not isinstance(dados.get("robos"), list):
            return False

        nova = {}

        for r in dados["robos"]:
            if isinstance(r, dict) and isinstance(r.get("id"), int):
                nova[r["id"]] = r

        with self.lock:
            self.frota = nova
            self.t_frota = time.monotonic()
            self.mensagens["frota"] += 1

        return True

    def atualizar_cargas(self, payload):
        dados = self._json(payload)

        if dados is None or not isinstance(dados.get("baias"), list):
            return False

        with self.lock:
            self.cargas = dados
            self.mensagens["cargas"] += 1

        return True

    def registrar_evento(self, payload):
        dados = self._json(payload)

        if dados is None:
            return False

        tipo = dados.get("tipo")

        with self.lock:
            self.mensagens["eventos"] += 1

            if tipo == "evento":
                self.eventos.append({
                    "t": dados.get("t", 0) / 1000.0,
                    "robo": dados.get("robo"),
                    "evento": str(dados.get("evento", "?")),
                    "texto": str(dados.get("texto", "")),
                })
                return True

            if tipo == "carga":
                carga = {
                    "t": dados.get("t", 0) / 1000.0,
                    "tarefa": dados.get("tarefa"),
                    "baia": str(dados.get("baia", "?")),
                    "destino": str(dados.get("destino", "")),
                    "robos": [r for r in dados.get("robos", []) if isinstance(r, int)],
                    "completa": bool(dados.get("completa")),
                }
                robos = "+".join(str(r) for r in sorted(carga["robos"]))

                if carga["completa"]:
                    self.entregues.append(carga)
                    self.entregues_por_zona[carga["destino"]] += 1
                    texto = f"carga {carga['tarefa']}: {carga['baia']} -> {carga['destino']} (robôs {robos})"
                    evento = "ENTREGA"
                else:
                    texto = f"carga {carga['tarefa']} INCOMPLETA em {carga['baia']} (robôs {robos})"
                    evento = "INCOMPLETA"

                self.eventos.append({"t": carga["t"], "robo": None, "evento": evento, "texto": texto})
                return True

        with self.lock:
            self.mensagens_invalidas += 1

        return False

    def registrar_local(self, texto, evento="SIM"):
        """Linha do próprio simulador no registro de eventos (defeito injetado etc.)."""
        with self.lock:
            self.eventos.append({"t": time.time(), "robo": None, "evento": evento, "texto": texto})

    # ------------------------------------------------------ consulta
    def organizador_online(self):
        with self.lock:
            return self.t_frota is not None and (time.monotonic() - self.t_frota) < config.ORGANIZADOR_TIMEOUT_S

    def snapshot(self):
        with self.lock:
            return {
                "frota": dict(self.frota),
                "cargas": self.cargas,
                "eventos": list(self.eventos),
                "entregues": list(self.entregues),
                "por_zona": dict(self.entregues_por_zona),
            }
