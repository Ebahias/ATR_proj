#!/usr/bin/env python3
import json
import math
import os
import subprocess
import sys
import threading
import time

import paho.mqtt.client as mqtt

VELOCIDADE_ROBO = 40.0
DRENO_BATERIA_MOVENDO = 0.5
DRENO_BATERIA_PARADO = 0.05
TEMPO_ACOPLAR_S = 0.5
TAXA_RECARGA = 20.0
PERIODO_S = 0.05

raiz = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
mapa = json.loads(subprocess.run([os.path.join(raiz, "bin", "mapa"), "json"], capture_output=True, text=True, check=True).stdout)
recargas = [a for a in mapa["areas"] if a["tipo"] == "recarga"]

robos = {}
trava = threading.Lock()


def em_recarga(x, y):
    xi, yi = round(x), round(y)
    return any(a["x0"] <= xi <= a["x1"] and a["y0"] <= yi <= a["y1"] for a in recargas)


def novo_cliente():
    try:
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id="sim_falso")
    except AttributeError:
        return mqtt.Client(client_id="sim_falso")


def ao_conectar(cliente, *args):
    cliente.subscribe("atr/robo/+/saidas", 1)
    cliente.subscribe("atr/robo/+/vivo", 1)
    cliente.subscribe("atr/sim/cmd", 1)


def ao_receber(cliente, dados, msg):
    partes = msg.topic.split("/")
    texto = msg.payload.decode(errors="replace").strip()
    with trava:
        if msg.topic == "atr/sim/cmd":
            campos = texto.split()
            if len(campos) == 3 and int(campos[1]) in robos:
                r = robos[int(campos[1])]
                if campos[0] == "FALHA":
                    r["falha"] = campos[2] == "1"
                elif campos[0] == "BATERIA":
                    r["bateria"] = float(campos[2])
            return
        rid = int(partes[2])
        if partes[3] == "vivo":
            if texto == "offline" and rid in robos:
                del robos[rid]
                print(f"robô {rid} removido", flush=True)
            return
        campos = texto.split()
        if len(campos) < 3:
            return
        sp_x, sp_y, acoplar = int(campos[0]), int(campos[1]), campos[2] == "1"
        if rid not in robos:
            robos[rid] = {"x": float(sp_x), "y": float(sp_y), "bateria": 100.0, "falha": False,
                          "acoplar": False, "atracada": False, "t_acoplando": 0.0}
            print(f"robô {rid} adicionado em ({sp_x}, {sp_y})", flush=True)
        r = robos[rid]
        r["sp_x"], r["sp_y"], r["acoplar"] = sp_x, sp_y, acoplar


def passo(r, dt):
    dx, dy = r["sp_x"] - r["x"], r["sp_y"] - r["y"]
    dist = math.hypot(dx, dy)
    moveu = False
    if not r["falha"] and r["bateria"] > 0 and dist > 0:
        p = VELOCIDADE_ROBO * dt
        if dist <= p:
            r["x"], r["y"] = float(r["sp_x"]), float(r["sp_y"])
        else:
            r["x"] += dx / dist * p
            r["y"] += dy / dist * p
        moveu = True
    r["bateria"] -= (DRENO_BATERIA_MOVENDO if moveu else DRENO_BATERIA_PARADO) * dt
    if not moveu and not r["falha"] and em_recarga(r["x"], r["y"]):
        r["bateria"] += TAXA_RECARGA * dt
    r["bateria"] = min(100.0, max(0.0, r["bateria"]))
    if r["acoplar"] and not r["atracada"] and not r["falha"]:
        r["t_acoplando"] += dt
        if r["t_acoplando"] >= TEMPO_ACOPLAR_S:
            r["atracada"] = True
    elif not r["acoplar"]:
        r["atracada"] = False
        r["t_acoplando"] = 0.0


def main():
    cliente = novo_cliente()
    cliente.on_connect = ao_conectar
    cliente.on_message = ao_receber
    host = os.environ.get("ATR_MQTT_HOST", "localhost")
    porta = int(os.environ.get("ATR_MQTT_PORT", "1883"))
    cliente.connect(host, porta, 10)
    cliente.loop_start()
    print(f"sim_falso conectado em {host}:{porta}", flush=True)
    proximo = time.monotonic()
    try:
        while True:
            with trava:
                for rid, r in robos.items():
                    passo(r, PERIODO_S)
                    payload = f'{round(r["x"])} {round(r["y"])} {int(r["atracada"])} {int(r["bateria"])} {int(r["falha"])}'
                    cliente.publish(f"atr/robo/{rid}/entradas", payload, 0)
            proximo += PERIODO_S
            time.sleep(max(0.0, proximo - time.monotonic()))
    except KeyboardInterrupt:
        pass
    cliente.loop_stop()
    cliente.disconnect()


if __name__ == "__main__":
    sys.exit(main())
