#!/usr/bin/env python3
"""Mede o aproveitamento da frota pelo Organizador (paralelismo das missões).

Cria N robôs em posições fixas e, durante --tempo segundos, amostra o status
publicado pelo Organizador a cada 100 ms:

  * robô ocioso   = disponível, em automático e sem missão em andamento;
  * "ociosidade com carga esperando" = tempo em que há robô ocioso E há caixa
    pendente (nas baias ou esperando equipe): robô parado sem necessidade;
  * missões simultâneas = quantas cargas estão em transporte ao mesmo tempo.

Requer broker e bin/org_logistica no ar (ver testes/teste_ocupacao.sh).
"""

import argparse
import os
import sys
import time

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from simulador.app import App, ler_argumentos  # noqa: E402

# Posições fixas, fora das passagens estreitas (resultados comparáveis entre execuções)
POSICOES = [(60, 100), (80, 105), (100, 100), (120, 105), (140, 100), (60, 15), (80, 10), (100, 15)]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--robos", type=int, default=5)
    ap.add_argument("--tempo", type=float, default=150.0)
    a = ap.parse_args()

    args = ler_argumentos([])
    app = App(args.host, args.porta)
    app.iniciar()
    c, op = app.controle, app.operacao

    try:
        t0 = time.monotonic()
        while not (app.rede.conectado and op.organizador_online()):
            if time.monotonic() - t0 > 15:
                print("Organizador/broker indisponível")
                return 2
            time.sleep(0.1)

        for i in range(a.robos):
            c.criar_robo(*POSICOES[i % len(POSICOES)])
            time.sleep(0.15)

        while len(app.mundo.ids()) < a.robos or any(op.snapshot()["frota"].get(i) is None for i in range(1, a.robos + 1)):
            if time.monotonic() - t0 > 30:
                print("robôs não apareceram")
                return 2
            time.sleep(0.1)

        inicio = time.monotonic()
        anterior = inicio
        ocioso_com_carga = ocioso_total = soma_missoes = evitavel = 0.0
        max_missoes = 0
        primeira = None
        caixas = None
        ultimo_print = 0.0
        ultimo_diag = 0.0
        fim = None

        while time.monotonic() - inicio < a.tempo:
            time.sleep(0.1)
            agora = time.monotonic()
            dt, anterior = agora - anterior, agora
            s = op.snapshot()
            cg = s["cargas"]

            if cg is None:
                continue

            pend = sum(len(b["pendentes"]) for b in cg["baias"])
            missoes = len(cg["em_transporte"])
            ociosos = sum(1 for r in s["frota"].values()
                          if r["estado"] == "disponível" and r["modo"].startswith("autom")
                          and r["fase"] in ("-", "concluida", "falhou") and not r["falha"])

            if caixas is None:
                caixas = pend + missoes + cg["entregues"]

            # Evitável = há robôs ociosos em número suficiente para a MENOR caixa pendente:
            # alguma equipe poderia estar trabalhando e não está
            tamanhos = [t for b in cg["baias"] for t in b["pendentes"]]
            if ociosos and tamanhos and ociosos >= min(tamanhos):
                evitavel += dt

                if os.environ.get('OCUPACAO_DIAG') and agora - ultimo_diag > 6:
                    ultimo_diag = agora
                    livres = [(r["id"], r["estado"], r["fase"], r["modo"][:4], round(r["bateria"])) for r in s["frota"].values()
                              if r["estado"] == "disponível" and r["modo"].startswith("autom") and r["fase"] in ("-", "concluida", "falhou") and not r["falha"]]
                    print(f"  AVOID t={agora - inicio:4.0f}s ociosos={livres} pend_por_baia={[b['pendentes'] for b in cg['baias']]} "
                          f"missoes={[(m['tarefa'], m['tamanho'], m['robos']) for m in cg['em_transporte']]}", flush=True)

            ocioso_total += dt * ociosos
            if ociosos and pend:
                ocioso_com_carga += dt * ociosos
            soma_missoes += dt * missoes
            max_missoes = max(max_missoes, missoes)

            if primeira is None and cg["entregues"] > 0:
                primeira = agora - inicio

            if caixas and cg["entregues"] >= caixas and fim is None:
                fim = agora - inicio
                break

            if agora - ultimo_print > 15:
                ultimo_print = agora
                print(f"  t={agora - inicio:5.0f}s entregues={cg['entregues']} pendentes={pend} missões={missoes} ociosos={ociosos}", flush=True)

        decorrido = time.monotonic() - inicio
        cg = op.snapshot()["cargas"]
        print()
        print(f"robôs={a.robos} tempo={decorrido:.0f}s caixas no início={caixas}")
        print(f"entregues: {cg['entregues']} de {caixas}  (primeira entrega em {primeira or 0:.0f}s)")
        print("TEMPO ATÉ ENTREGAR TUDO: " + (f"{fim:.0f} s" if fim else "não concluiu no tempo dado"))
        print(f"missões simultâneas: máx {max_missoes}, média {soma_missoes / decorrido:.2f}")
        print(f"robô-segundos ociosos: {ocioso_total:.0f} no total, "
              f"{ocioso_com_carga:.0f} COM CAIXA ESPERANDO ({100 * ocioso_com_carga / max(1.0, a.robos * decorrido):.0f}% do tempo da frota)")
        print(f"tempo com caixa atendível e robôs ociosos (EVITÁVEL): {evitavel:.0f} s ({100 * evitavel / decorrido:.0f}% do tempo)")
        print(f"MEDIDA entregues={cg['entregues']} missoes_media={soma_missoes / decorrido:.2f} max_missoes={max_missoes} "
              f"evitavel_s={evitavel:.0f} fim_s={fim or 0:.0f}")
    finally:
        app.parar()

    return 0


if __name__ == "__main__":
    sys.exit(main())
