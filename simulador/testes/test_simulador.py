"""Testes unitários do simulador / Tela de Operação (sem broker e sem janela).

    python3 -m unittest simulador.testes.test_simulador -v
"""

import json
import math
import os
import sys
import time
import unittest

os.environ.setdefault("SDL_VIDEODRIVER", "dummy")
os.environ.setdefault("SDL_AUDIODRIVER", "dummy")
sys.path.insert(0, os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))))

from simulador import config  # noqa: E402
from simulador.controle import Controle  # noqa: E402
from simulador.fisica import MotorFisica, Mundo, RoboSim, passo_robo  # noqa: E402
from simulador.mapa import Mapa, carregar_mapa  # noqa: E402
from simulador.operacao import EstadoOperacao, normalizar  # noqa: E402
from simulador.rede import Rede  # noqa: E402

MAPA = carregar_mapa()


class ClienteFalso:
    """Substitui o paho: guarda o que seria publicado e assinado."""

    def __init__(self):
        self.publicadas = []
        self.assinaturas = []

    def reconnect_delay_set(self, **kw):
        pass

    def subscribe(self, topico, qos=0):
        self.assinaturas.append((topico, qos))

    def publish(self, topico, payload, qos=0, retain=False):
        self.publicadas.append((topico, payload, qos, retain))

    def connect_async(self, *a):
        pass

    def loop_start(self):
        pass

    def loop_stop(self):
        pass

    def disconnect(self):
        pass


def fabrica():
    mundo = Mundo(MAPA)
    op = EstadoOperacao()
    cliente = ClienteFalso()
    rede = Rede(mundo, op, cliente=cliente)
    rede.conectado = True
    controle = Controle(MAPA, mundo, op, rede)
    return mundo, op, rede, cliente, controle


def status_json(*robos):
    base = {"estado": "disponível", "modo": "automático", "x": 0, "y": 0, "sp_x": 0, "sp_y": 0, "bateria": 100,
            "atracada": 0, "falha": 0, "distancia": 0.0, "fase": "-", "tarefa": 0, "progresso": 0,
            "passagem": -1, "aguardando_passagem": -1}
    return json.dumps({"t": 1, "robos": [dict(base, **r) for r in robos]})


# ------------------------------------------------------------------ física
class TestFisica(unittest.TestCase):

    def avancar(self, r, segundos, passo=0.02):
        for _ in range(int(round(segundos / passo))):
            passo_robo(r, passo, MAPA.em_recarga)

    def test_anda_ate_o_setpoint_na_velocidade_do_robo(self):
        r = RoboSim(1, 0, 0, 80, 0)
        self.avancar(r, 1.0)
        self.assertAlmostEqual(r.x, config.VELOCIDADE_ROBO, delta=0.5)
        self.assertEqual(r.y, 0)

    def test_chega_exatamente_sem_passar(self):
        r = RoboSim(1, 0, 0, 10, 10)
        self.avancar(r, 2.0)
        self.assertEqual((r.x, r.y), (10.0, 10.0))
        self.assertFalse(r.movendo)

    def test_gasta_bateria_movendo_e_parado(self):
        r = RoboSim(1, 0, 0, 1000, 0)
        self.avancar(r, 2.0)
        self.assertAlmostEqual(r.bateria, 100 - config.DRENO_BATERIA_MOVENDO * 2.0, delta=0.05)
        p = RoboSim(2, 0, 0, 0, 0)
        self.avancar(p, 10.0)
        self.assertAlmostEqual(p.bateria, 100 - config.DRENO_BATERIA_PARADO * 10.0, delta=0.05)

    def test_defeito_e_bateria_zero_travam_o_robo(self):
        r = RoboSim(1, 0, 0, 50, 0, falha=True)
        self.avancar(r, 1.0)
        self.assertEqual(r.x, 0)
        r = RoboSim(2, 0, 0, 50, 0, bateria=0.0)
        self.avancar(r, 1.0)
        self.assertEqual(r.x, 0)

    def test_recarrega_parado_na_estacao_e_satura_em_100(self):
        r = RoboSim(1, 100, 30, 100, 30, bateria=50.0)
        self.assertTrue(MAPA.em_recarga(100, 30))
        self.avancar(r, 1.0)
        self.assertTrue(r.recarregando)
        self.assertAlmostEqual(r.bateria, 50 + config.TAXA_RECARGA, delta=0.5)
        self.avancar(r, 10.0)
        self.assertEqual(r.bateria, 100.0)

    def test_nao_recarrega_fora_da_estacao_nem_com_defeito_nem_andando(self):
        fora = RoboSim(1, 100, 60, 100, 60, bateria=50.0)
        self.avancar(fora, 1.0)
        self.assertLess(fora.bateria, 50.0)
        defeito = RoboSim(2, 100, 30, 100, 30, bateria=50.0, falha=True)
        self.avancar(defeito, 1.0)
        self.assertLess(defeito.bateria, 50.0)
        andando = RoboSim(3, 100, 30, 100, 36, bateria=50.0)
        self.avancar(andando, 0.1)
        self.assertLess(andando.bateria, 50.0)

    def test_acoplamento_leva_o_tempo_configurado_e_solta_ao_desligar(self):
        r = RoboSim(1, 0, 0, 0, 0, acoplar=True)
        self.avancar(r, config.TEMPO_ACOPLAR_S - 0.06)
        self.assertFalse(r.atracada)
        self.avancar(r, 0.12)
        self.assertTrue(r.atracada)
        r.acoplar = False
        self.avancar(r, 0.02)
        self.assertFalse(r.atracada)
        self.assertEqual(r.t_acoplando, 0.0)

    def test_defeito_impede_acoplar(self):
        r = RoboSim(1, 0, 0, 0, 0, acoplar=True, falha=True)
        self.avancar(r, 2.0)
        self.assertFalse(r.atracada)

    def test_payload_das_entradas(self):
        r = RoboSim(7, 12.5, 30.4, 0, 0, bateria=87.2, falha=True, atracada=True)
        self.assertEqual(r.entradas(), "13 30 1 88 1")       # lround, ceil na bateria
        self.assertEqual(RoboSim(1, 0.5, 0, 0, 0).entradas().split()[0], "1")   # 0.5 -> 1 (round() do Python daria 0)

    def test_mundo_cria_robo_no_primeiro_setpoint_e_atualiza_depois(self):
        m = Mundo(MAPA)
        m.definir_saidas(4, 60, 100, False)
        r = m.snapshot()[0]
        self.assertEqual((r.id, r.x, r.y), (4, 60.0, 100.0))
        m.definir_saidas(4, 70, 50, True)
        r = m.snapshot()[0]
        self.assertEqual((r.sp_x, r.sp_y, r.acoplar, r.x, r.y), (70, 50, True, 60.0, 100.0))
        self.assertEqual(len(m.snapshot()), 1)

    def test_injecoes_do_simulador(self):
        m = Mundo(MAPA)
        m.adicionar(1, 10, 10)
        self.assertTrue(m.definir_falha(1, True))
        self.assertTrue(m.snapshot()[0].falha)
        m.definir_bateria(1, 150)
        self.assertEqual(m.snapshot()[0].bateria, 100.0)
        m.definir_bateria(1, 15)
        m.ajustar_bateria(1, -10)
        self.assertEqual(m.snapshot()[0].bateria, 5.0)
        m.ajustar_bateria(1, -50)
        self.assertEqual(m.snapshot()[0].bateria, 0.0)
        self.assertFalse(m.definir_falha(99, True))
        self.assertFalse(m.adicionar(1, 0, 0))

    def test_violacao_de_capacidade_conta_so_na_subida(self):
        m = Mundo(MAPA)
        avisos = []
        m.ao_violacao = avisos.append
        m.adicionar(1, 100, 45)             # Corredor central norte: capacidade 1
        m.verificar_passagens()
        self.assertEqual(m.violacoes, 0)
        m.adicionar(2, 110, 45)
        m.verificar_passagens()
        m.verificar_passagens()             # continua violando: não conta de novo
        self.assertEqual((m.violacoes, len(avisos)), (1, 1))
        self.assertIn("Corredor central norte", avisos[0])
        m.remover(2)
        m.verificar_passagens()
        self.assertEqual(m.passagens_violadas(), set())
        m.adicionar(3, 90, 45)
        m.verificar_passagens()
        self.assertEqual(m.violacoes, 2)

    def test_capacidade_dois_e_tres_sao_respeitadas(self):
        m = Mundo(MAPA)
        for i, x in enumerate((80, 90, 100)):
            m.adicionar(i + 1, x, 75)       # Corredor central sul: capacidade 2
        m.verificar_passagens()
        self.assertEqual(m.ocupacao[1], 3)
        self.assertEqual(m.violacoes, 1)
        m2 = Mundo(MAPA)
        for i, y in enumerate((50, 55, 60)):
            m2.adicionar(i + 1, 50, y)      # Corredor das baias: capacidade 3
        m2.verificar_passagens()
        self.assertEqual((m2.ocupacao[2], m2.violacoes), (3, 0))

    def test_motor_publica_entradas_a_cada_50ms_e_move_os_robos(self):
        m = Mundo(MAPA)
        m.adicionar(1, 100, 60)
        m.definir_saidas(1, 140, 60, False)
        recebido = []
        motor = MotorFisica(m, lambda rid, p: recebido.append((rid, p)))
        motor.aquecimento_s = 0
        motor.start()
        time.sleep(0.6)
        motor.parar()
        motor.join(1.0)
        self.assertTrue(8 <= len(recebido) <= 14, len(recebido))     # ~12 em 0,6 s
        self.assertGreater(m.snapshot()[0].x, 110)
        self.assertGreater(motor.ciclos, 15)
        x, y, atr, bat, falha = (int(v) for v in recebido[-1][1].split())
        self.assertEqual((y, atr, falha), (60, 0, 0))


# -------------------------------------------------------------------- mapa
class TestMapa(unittest.TestCase):

    def test_areas_e_bordas_inclusivas(self):
        self.assertEqual(MAPA.area_do_ponto(15, 15)["nome"], "Baia 1")
        self.assertEqual(MAPA.area_do_ponto(40, 30)["nome"], "Baia 1")      # borda: primeira da lista
        self.assertEqual(MAPA.area_do_ponto(185, 105)["nome"], "Zona D")
        self.assertIsNone(MAPA.area_do_ponto(100, 60))
        self.assertTrue(MAPA.em_recarga(94, 30))
        self.assertFalse(MAPA.em_recarga(15, 15))

    def test_tamanho_e_passagens(self):
        self.assertEqual((MAPA.largura, MAPA.altura), (200, 120))
        self.assertEqual(sorted(p["capacidade"] for p in MAPA.passagens), [1, 2, 3])
        self.assertEqual(len(MAPA.areas_do_tipo("baia")), 4)
        self.assertEqual(len(MAPA.areas_do_tipo("zona")), 4)

    def test_quem_espera_no_no_de_entrada_nao_esta_na_passagem(self):
        self.assertEqual(MAPA.ocupacao_passagens([(70, 45), (130, 45), (71, 45)]), [0, 0, 0])
        self.assertEqual(MAPA.ocupacao_passagens([(72, 45)])[0], 1)
        self.assertEqual(MAPA.ocupacao_passagens([(100, 52)])[0], 0)         # fora do eixo

    def test_json_incompleto_e_rejeitado(self):
        from simulador.mapa import ErroMapa
        with self.assertRaises(ErroMapa):
            Mapa({"largura": 1})


# --------------------------------------------------------------- operação
class TestOperacao(unittest.TestCase):

    def test_normalizar_acentos(self):
        self.assertEqual(normalizar("Disponível"), "disponivel")
        self.assertEqual(normalizar("automático"), "automatico")

    def test_frota_e_organizador_online(self):
        op = EstadoOperacao()
        self.assertFalse(op.organizador_online())
        self.assertTrue(op.atualizar_frota(status_json({"id": 1}, {"id": 2})))
        self.assertTrue(op.organizador_online())
        self.assertEqual(sorted(op.snapshot()["frota"]), [1, 2])
        self.assertFalse(op.atualizar_frota("{isto nao e json"))
        self.assertFalse(op.atualizar_frota('{"robos": 3}'))
        self.assertEqual(op.mensagens_invalidas, 1)                    # só o JSON quebrado conta

    def test_organizador_cai_depois_do_timeout(self):
        op = EstadoOperacao()
        op.atualizar_frota(status_json({"id": 1}))
        op.t_frota -= config.ORGANIZADOR_TIMEOUT_S + 0.1
        self.assertFalse(op.organizador_online())

    def test_cargas(self):
        op = EstadoOperacao()
        self.assertFalse(op.atualizar_cargas('{"x": 1}'))
        self.assertTrue(op.atualizar_cargas('{"t": 1, "baias": [{"baia": 1, "pendentes": [4, 1]}], "em_transporte": [], "entregues": 3}'))
        self.assertEqual(op.snapshot()["cargas"]["entregues"], 3)

    def test_eventos_de_robo_e_cargas_entregues(self):
        op = EstadoOperacao()
        op.registrar_evento(json.dumps({"tipo": "evento", "t": 1700000000000, "robo": 2, "evento": "CARGA", "texto": "pegou carga na Baia 1 (tarefa 3)"}))
        op.registrar_evento(json.dumps({"tipo": "carga", "t": 1700000005000, "tarefa": 3, "baia": "Baia 1", "destino": "Zona B", "robos": [2, 1], "t_coleta": 1, "completa": 1}))
        op.registrar_evento(json.dumps({"tipo": "carga", "t": 1700000009000, "tarefa": 4, "baia": "Baia 2", "destino": "", "robos": [5], "t_coleta": 1, "completa": 0}))
        s = op.snapshot()
        self.assertEqual([e["evento"] for e in s["eventos"]], ["CARGA", "ENTREGA", "INCOMPLETA"])
        self.assertEqual(s["por_zona"], {"Zona B": 1})
        self.assertEqual(len(s["entregues"]), 1)
        self.assertIn("robôs 1+2", s["eventos"][1]["texto"])
        self.assertFalse(op.registrar_evento("lixo"))
        self.assertFalse(op.registrar_evento('{"tipo": "desconhecido"}'))

    def test_log_limitado(self):
        op = EstadoOperacao()
        for i in range(700):
            op.registrar_local(f"linha {i}")
        self.assertEqual(len(op.snapshot()["eventos"]), 500)
        self.assertEqual(op.snapshot()["eventos"][-1]["texto"], "linha 699")


# -------------------------------------------------------------------- rede
class TestRede(unittest.TestCase):

    def test_assina_tudo_ao_conectar(self):
        _, _, rede, cliente, _ = fabrica()
        rede._ao_conectar(cliente)
        topicos = {t for t, _ in cliente.assinaturas}
        self.assertEqual(topicos, {"atr/robo/+/saidas", "atr/robo/+/vivo", "atr/sim/cmd",
                                   "atr/frota/status", "atr/eventos", "atr/cargas"})

    def test_saidas_criam_e_atualizam_o_robo(self):
        mundo, op, rede, _, _ = fabrica()
        rede.tratar("atr/robo/3/saidas", "100 30 0")
        self.assertEqual(mundo.ids(), [3])
        rede.tratar("atr/robo/3/saidas", "20 40 1")
        r = mundo.snapshot()[0]
        self.assertEqual((r.sp_x, r.sp_y, r.acoplar), (20, 40, True))
        self.assertTrue(any("robô 3 apareceu" in e["texto"] for e in op.snapshot()["eventos"]))

    def test_saidas_invalidas_sao_contadas_e_ignoradas(self):
        mundo, _, rede, _, _ = fabrica()
        for p in ("", "1 2", "a b c", "1 2 x"):
            rede.tratar("atr/robo/3/saidas", p)
        rede.tratar("atr/robo/abc/saidas", "1 2 3")
        self.assertEqual(mundo.ids(), [])
        self.assertEqual(rede.invalidas, 4)

    def test_vivo_offline_remove_so_robo_existente(self):
        mundo, op, rede, _, _ = fabrica()
        rede.tratar("atr/robo/9/vivo", "offline")                 # retido de uma execução anterior
        self.assertEqual(op.snapshot()["eventos"], [])
        mundo.adicionar(9, 1, 1)
        rede.tratar("atr/robo/9/vivo", "online")
        self.assertEqual(mundo.ids(), [9])
        rede.tratar("atr/robo/9/vivo", "offline")
        self.assertEqual(mundo.ids(), [])

    def test_comandos_do_simulador_por_mqtt(self):
        mundo, _, rede, _, _ = fabrica()
        mundo.adicionar(1, 0, 0)
        rede.tratar("atr/sim/cmd", "FALHA 1 1")
        self.assertTrue(mundo.snapshot()[0].falha)
        rede.tratar("atr/sim/cmd", "BATERIA 1 15")
        self.assertEqual(mundo.snapshot()[0].bateria, 15.0)
        rede.tratar("atr/sim/cmd", "VOAR 1 1")
        self.assertEqual(rede.invalidas, 1)

    def test_status_cargas_e_eventos_vao_para_a_operacao(self):
        _, op, rede, _, _ = fabrica()
        rede.tratar("atr/frota/status", status_json({"id": 1}))
        rede.tratar("atr/cargas", '{"baias": [], "em_transporte": [], "entregues": 0}')
        rede.tratar("atr/eventos", '{"tipo": "evento", "t": 1, "robo": 1, "evento": "ESTADO", "texto": "x"}')
        s = op.snapshot()
        self.assertEqual((list(s["frota"]), s["cargas"] is not None, len(s["eventos"])), ([1], True, 1))

    def test_formatos_publicados_batem_com_o_cpp(self):
        _, _, rede, cliente, _ = fabrica()
        rede.publicar_entradas(2, "10 20 0 99 0")
        rede.novo_robo(5)
        rede.novo_robo(6, 60, 100)
        rede.cmd_modo(5, True)
        rede.cmd_modo(5, False)
        rede.cmd_ir(5, 120, 60)
        rede.cmd_parar(5)
        rede.cmd_caixa(2, 3)
        esperado = [
            ("atr/robo/2/entradas", "10 20 0 99 0", 0, False),
            ("atr/robos/novo", "NOVO_ROBO 5", 1, False),
            ("atr/robos/novo", "NOVO_ROBO 6 60 100", 1, False),
            ("atr/operacao/cmd", "MODO 5 manual", 1, False),
            ("atr/operacao/cmd", "MODO 5 auto", 1, False),
            ("atr/operacao/cmd", "IR 5 120 60", 1, False),
            ("atr/operacao/cmd", "PARAR 5", 1, False),
            ("atr/operacao/cmd", "CAIXA 2 3", 1, False),
        ]
        self.assertEqual(cliente.publicadas, esperado)

    def test_sem_conexao_nao_publica(self):
        _, _, rede, cliente, _ = fabrica()
        rede.conectado = False
        self.assertFalse(rede.cmd_ir(1, 2, 3))
        self.assertFalse(rede.publicar_entradas(1, "0 0 0 0 0"))
        self.assertEqual(cliente.publicadas, [])


# ---------------------------------------------------------------- controle
class TestControle(unittest.TestCase):

    def test_ir_para_so_em_modo_manual(self):
        mundo, op, rede, cliente, c = fabrica()
        op.atualizar_frota(status_json({"id": 1, "modo": "automático"}))
        self.assertFalse(c.ir_para(1, 50, 50))
        self.assertTrue(any("AUTOMÁTICO" in a for a in c.avisos_ativos()))
        self.assertEqual(cliente.publicadas, [])
        op.atualizar_frota(status_json({"id": 1, "modo": "manual"}))
        self.assertTrue(c.ir_para(1, 50, 50))
        self.assertEqual(cliente.publicadas[-1][:2], ("atr/operacao/cmd", "IR 1 50 50"))

    def test_ir_para_valida_destino_e_robo(self):
        _, op, _, cliente, c = fabrica()
        op.atualizar_frota(status_json({"id": 1, "modo": "manual"}))
        self.assertFalse(c.ir_para(1, 500, 10))
        self.assertFalse(c.ir_para(1, -1, 10))
        self.assertFalse(c.ir_para(7, 10, 10))
        self.assertFalse(c.ir_para(None, 10, 10))
        self.assertEqual(cliente.publicadas, [])

    def test_destino_por_area_usa_o_centro(self):
        _, op, _, cliente, c = fabrica()
        op.atualizar_frota(status_json({"id": 1, "modo": "manual"}))
        self.assertTrue(c.ir_para_area(1, "zona", 3))
        self.assertEqual(cliente.publicadas[-1][1], "IR 1 185 75")
        self.assertTrue(c.ir_para_area(1, "recarga", 2))
        self.assertEqual(cliente.publicadas[-1][1], "IR 1 100 90")
        self.assertTrue(c.ir_para_area(1, "baia", 4))
        self.assertEqual(cliente.publicadas[-1][1], "IR 1 15 105")

    def test_modo_e_parar(self):
        _, op, _, cliente, c = fabrica()
        op.atualizar_frota(status_json({"id": 1, "modo": "automático"}))
        c.alternar_modo(1)
        self.assertEqual(cliente.publicadas[-1][1], "MODO 1 manual")
        op.atualizar_frota(status_json({"id": 1, "modo": "manual"}))
        c.alternar_modo(1)
        self.assertEqual(cliente.publicadas[-1][1], "MODO 1 auto")
        c.parar(1)
        self.assertEqual(cliente.publicadas[-1][1], "PARAR 1")

    def test_criar_robo_ids_sequenciais_e_pendencia(self):
        mundo, op, _, cliente, c = fabrica()
        self.assertEqual(c.criar_robo(), 1)
        self.assertEqual(c.criar_robo(60, 100), 2)
        self.assertEqual([p[1] for p in cliente.publicadas], ["NOVO_ROBO 1", "NOVO_ROBO 2 60 100"])
        self.assertEqual(sorted(c.pendentes), [1, 2])
        mundo.adicionar(1, 100, 30)         # o Gestor criou o robô 1
        c.atualizar()
        self.assertEqual(sorted(c.pendentes), [2])
        self.assertEqual(c.proximo_id(), 3)

    def test_id_nao_repete_mesmo_depois_do_pedido_expirar(self):
        _, _, _, _, c = fabrica()
        self.assertEqual(c.criar_robo(), 1)
        c.pendentes.clear()
        self.assertEqual(c.criar_robo(), 2)

    def test_pedido_sem_resposta_do_gestor_avisa(self):
        _, _, _, _, c = fabrica()
        rid = c.criar_robo()
        c.pendentes[rid]["t"] -= config.NOVO_ROBO_TIMEOUT_S + 1
        c.atualizar()
        self.assertEqual(c.pendentes, {})
        self.assertTrue(any("--fonte mqtt" in a for a in c.avisos_ativos()))

    def test_limite_de_32_robos_e_fora_do_mapa(self):
        mundo, _, _, cliente, c = fabrica()
        for i in range(1, 33):
            mundo.adicionar(i, 10, 10)
        self.assertIsNone(c.criar_robo())
        self.assertEqual(cliente.publicadas, [])
        mundo.remover(32)
        self.assertIsNone(c.criar_robo(300, 5))
        self.assertEqual(cliente.publicadas, [])

    def test_criar_robo_sem_broker(self):
        _, _, rede, cliente, c = fabrica()
        rede.conectado = False
        self.assertIsNone(c.criar_robo())
        self.assertTrue(any("broker" in a for a in c.avisos_ativos()))

    def test_posicao_aleatoria_dentro_do_mapa_e_longe_dos_outros(self):
        mundo, _, _, _, c = fabrica()
        mundo.adicionar(1, 100, 60)
        for _ in range(50):
            x, y = c.posicao_livre_aleatoria()
            self.assertTrue(MAPA.dentro(x, y))
            self.assertGreaterEqual(math.hypot(x - 100, y - 60), 8)

    def test_posicao_aleatoria_nunca_cai_numa_passagem_estreita(self):
        mundo, _, _, _, c = fabrica()
        for _ in range(500):
            x, y = c.posicao_livre_aleatoria()
            self.assertFalse(MAPA.perto_de_passagem(x, y), (x, y))
        self.assertTrue(MAPA.perto_de_passagem(100, 45))
        self.assertTrue(MAPA.perto_de_passagem(50, 60))
        self.assertFalse(MAPA.perto_de_passagem(100, 60))

    def test_caixas(self):
        _, _, _, cliente, c = fabrica()
        self.assertTrue(c.criar_caixa(3, 2))
        self.assertEqual(cliente.publicadas[-1][1], "CAIXA 3 2")
        self.assertFalse(c.criar_caixa(5, 1))
        self.assertFalse(c.criar_caixa(1, 0))
        c.baia_caixa, c.tamanho_caixa = 4, 4
        c.criar_caixa()
        self.assertEqual(cliente.publicadas[-1][1], "CAIXA 4 4")
        self.assertTrue(c.criar_caixa_aleatoria())
        baia, tam = cliente.publicadas[-1][1].split()[1:]
        self.assertTrue(1 <= int(baia) <= 4 and 1 <= int(tam) <= 4)

    def test_defeito_e_bateria_agem_na_fisica(self):
        mundo, op, _, _, c = fabrica()
        mundo.adicionar(1, 10, 10)
        c.alternar_falha(1)
        self.assertTrue(mundo.snapshot()[0].falha)
        c.alternar_falha(1)
        self.assertFalse(mundo.snapshot()[0].falha)
        c.baixar_bateria(1, -10)
        self.assertEqual(mundo.snapshot()[0].bateria, 90.0)
        c.bateria_para(1, 15)
        self.assertEqual(mundo.snapshot()[0].bateria, 15.0)
        self.assertFalse(c.alternar_falha(99))
        self.assertTrue(any("defeito gerado" in e["texto"] for e in op.snapshot()["eventos"]))

    def test_visao_junta_fisica_e_status_e_adota_robo_so_do_status(self):
        mundo, op, _, _, c = fabrica()
        mundo.adicionar(1, 50.5, 20.5)
        mundo.definir_falha(1, True)
        op.atualizar_frota(status_json({"id": 1, "estado": "deslocando", "modo": "manual", "tarefa": 3, "fase": "ir_coleta"},
                                       {"id": 2, "x": 70, "y": 80, "sp_x": 90, "sp_y": 80, "bateria": 42}))
        v1, v2 = c.visoes()
        self.assertEqual((v1["x"], v1["y"], v1["falha"], v1["manual"], v1["estado"], v1["tarefa"]),
                         (50.5, 20.5, True, True, "deslocando", 3))
        self.assertEqual((v2["x"], v2["y"], v2["bateria"], v2["no_simulador"]), (70.0, 80.0, 42.0, False))

    def test_adota_robo_vivo_que_o_simulador_nao_conhece(self):
        mundo, op, rede, _, c = fabrica()
        op.atualizar_frota(status_json({"id": 1, "x": 70, "y": 80, "sp_x": 90, "sp_y": 80, "bateria": 42},
                                       {"id": 2, "x": 10, "y": 10}))
        c.atualizar()
        self.assertEqual(mundo.ids(), [])                       # nenhum está online: não adota
        rede.tratar("atr/robo/1/vivo", "online")
        c.atualizar()
        r = mundo.snapshot()[0]
        self.assertEqual((r.id, r.x, r.y, r.sp_x, r.sp_y, r.bateria), (1, 70.0, 80.0, 90, 80, 42.0))
        self.assertEqual(mundo.ids(), [1])
        c.atualizar()
        self.assertEqual(len(mundo.snapshot()), 1)              # não duplica
        rede.tratar("atr/robo/1/vivo", "offline")               # morreu: sai e não é readotado
        c.atualizar()
        self.assertEqual(mundo.ids(), [])

    def test_selecao_acompanha_os_robos_existentes(self):
        mundo, _, _, _, c = fabrica()
        c.atualizar()
        self.assertIsNone(c.sel)
        mundo.adicionar(4, 1, 1)
        mundo.adicionar(6, 1, 1)
        c.atualizar()
        self.assertEqual(c.sel, 4)
        c.selecionar_proximo()
        self.assertEqual(c.sel, 6)
        c.selecionar_proximo()
        self.assertEqual(c.sel, 4)
        mundo.remover(4)
        c.atualizar()
        self.assertEqual(c.sel, 6)


# --------------------------------------------------------------- interface
class TestInterface(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        import pygame
        pygame.init()
        cls.pygame = pygame

    def montar(self):
        from simulador.gui import ALTURA, LARGURA, Interface
        mundo, op, rede, cliente, c = fabrica()
        motor = MotorFisica(mundo, lambda *a: None)
        ui = Interface(c, motor)
        canvas = self.pygame.Surface((LARGURA, ALTURA))
        return mundo, op, cliente, c, ui, canvas

    def popular(self, mundo, op):
        mundo.adicionar(1, 30, 95)
        mundo.adicionar(2, 100, 45)
        mundo.adicionar(3, 100, 30)
        mundo.definir_bateria(3, 40)
        op.atualizar_frota(status_json(
            {"id": 1, "estado": "operando carga", "tarefa": 1, "fase": "transportando", "atracada": 1},
            {"id": 2, "estado": "deslocando", "modo": "manual", "aguardando_passagem": 0},
            {"id": 3, "estado": "recarregando"}))
        op.atualizar_cargas(json.dumps({"t": 1, "baias": [{"baia": 1, "pendentes": [4, 1, 2]}, {"baia": 2, "pendentes": []},
                                                          {"baia": 3, "pendentes": [3] * 30}, {"baia": 4, "pendentes": [1]}],
                                        "em_transporte": [{"tarefa": 1, "baia": 4, "destino": "Zona B", "tamanho": 2, "robos": [1, 3]}],
                                        "entregues": 5}))
        for i in range(8):
            op.registrar_evento(json.dumps({"tipo": "carga", "t": 1700000000000 + i, "tarefa": i, "baia": "Baia 1", "destino": "Zona B", "robos": [1], "completa": 1}))
        mundo.verificar_passagens()

    def test_desenha_um_quadro_com_dados_sem_erro(self):
        mundo, op, _, c, ui, canvas = self.montar()
        self.popular(mundo, op)
        c.atualizar()
        mundo.adicionar(4, 100, 45)         # 2 robôs na passagem de capacidade 1: violação visível
        mundo.verificar_passagens()
        ui.mostrar_nos = True
        ui.ajuda = True
        ui.desenhar(canvas)
        ui.ajuda = False
        c.posicionando = True
        ui.mover(ui.px(50, 50))
        ui.desenhar(canvas)
        self.pygame.image.save(canvas, "/tmp/teste_gui_unitario.png")

    def test_desenha_sem_nenhum_robo_e_sem_dados(self):
        _, _, _, _, ui, canvas = self.montar()
        ui.desenhar(canvas)

    def test_clique_no_cartao_e_no_robo_do_mapa_seleciona(self):
        mundo, op, _, c, ui, canvas = self.montar()
        self.popular(mundo, op)
        c.atualizar()
        ui.desenhar(canvas)
        rect, rid, _ = next(x for x in ui.cartoes if x[1] == 3)
        ui.clique((rect.x + 120, rect.y + 6), 1)
        self.assertEqual(c.sel, 3)
        ui.clique(ui.px(30, 95), 1)
        self.assertEqual(c.sel, 1)

    def test_pilula_de_modo_do_cartao_alterna_o_modo(self):
        mundo, op, cliente, c, ui, canvas = self.montar()
        self.popular(mundo, op)
        c.atualizar()
        ui.desenhar(canvas)
        _, _, modo = next(x for x in ui.cartoes if x[1] == 2)       # robô 2 está em manual
        ui.clique(modo.center, 1)
        self.assertEqual(cliente.publicadas[-1][1], "MODO 2 auto")

    def test_botoes_de_operacao_e_simulador(self):
        mundo, op, cliente, c, ui, canvas = self.montar()
        self.popular(mundo, op)
        c.atualizar()
        c.selecionar(2)
        ui.desenhar(canvas)

        def botao(rotulo, y=None):
            return next(b for b in ui.botoes if b._val(b.rotulo) == rotulo and (y is None or b.rect.y == y))

        ui.clique(botao("Zona C").rect.center, 1)
        self.assertEqual(cliente.publicadas[-1][1], "IR 2 185 75")
        ui.clique(botao("Recarga 1").rect.center, 1)
        self.assertEqual(cliente.publicadas[-1][1], "IR 2 100 30")
        ui.clique(botao("Parar").rect.center, 1)
        self.assertEqual(cliente.publicadas[-1][1], "PARAR 2")
        ui.clique(botao("AUTOMÁTICO").rect.center, 1)
        self.assertEqual(cliente.publicadas[-1][1], "MODO 2 auto")
        ui.clique(botao("MANUAL").rect.center, 1)
        self.assertEqual(cliente.publicadas[-1][1], "MODO 2 manual")

        ui.clique(botao("Gerar defeito").rect.center, 1)
        self.assertTrue(next(r for r in mundo.snapshot() if r.id == 2).falha)
        self.assertEqual(botao("Reparar defeito").rect, botao("Reparar defeito").rect)   # o rótulo mudou
        ui.clique(botao("Reparar defeito").rect.center, 1)
        self.assertFalse(next(r for r in mundo.snapshot() if r.id == 2).falha)
        ui.clique(botao("Bateria −10%").rect.center, 1)
        self.assertEqual(next(r for r in mundo.snapshot() if r.id == 2).bateria, 90.0)
        ui.clique(botao("Bateria → 15%").rect.center, 1)
        self.assertEqual(next(r for r in mundo.snapshot() if r.id == 2).bateria, 15.0)

        ui.clique(botao("Posição padrão").rect.center, 1)
        self.assertTrue(cliente.publicadas[-1][1].startswith("NOVO_ROBO "))
        n = len(cliente.publicadas)
        ui.clique(botao("Aleatória").rect.center, 1)
        self.assertEqual(len(cliente.publicadas), n + 1)

        ui.clique(botao("3", y=R_SIM_Y + 172).rect.center, 1)
        ui.clique(botao("4", y=R_SIM_Y + 204).rect.center, 1)
        ui.clique(botao("Criar caixa").rect.center, 1)
        self.assertEqual(cliente.publicadas[-1][1], "CAIXA 3 4")

    def test_botoes_sem_robo_selecionado_ficam_desabilitados(self):
        _, _, cliente, c, ui, canvas = self.montar()
        ui.desenhar(canvas)
        b = next(b for b in ui.botoes if b._val(b.rotulo) == "Parar")
        self.assertFalse(b.esta_habilitado())
        ui.clique(b.rect.center, 1)
        self.assertEqual(cliente.publicadas, [])

    def test_botao_direito_no_mapa_envia_destino_arbitrario(self):
        mundo, op, cliente, c, ui, canvas = self.montar()
        self.popular(mundo, op)
        c.atualizar()
        c.selecionar(2)
        ui.clique(ui.px(120, 60), 3)
        self.assertEqual(cliente.publicadas[-1][1], "IR 2 120 60")
        c.selecionar(1)                       # automático: recusa com aviso
        n = len(cliente.publicadas)
        ui.clique(ui.px(120, 60), 3)
        self.assertEqual(len(cliente.publicadas), n)
        self.assertTrue(c.avisos_ativos())

    def test_clicar_no_mapa_posiciona_novo_robo(self):
        _, _, cliente, c, ui, canvas = self.montar()
        botao = next(b for b in ui.botoes if b._val(b.rotulo) == "Clicar no mapa")
        ui.clique(botao.rect.center, 1)
        self.assertTrue(c.posicionando)
        ui.clique(ui.px(75, 62), 1)
        self.assertFalse(c.posicionando)
        self.assertEqual(cliente.publicadas[-1][1], "NOVO_ROBO 1 75 62")

    def test_campo_de_texto_x_y(self):
        mundo, op, cliente, c, ui, canvas = self.montar()
        self.popular(mundo, op)
        c.atualizar()
        c.selecionar(2)
        ui.clique(ui.campo.rect.center, 1)
        self.assertTrue(ui.campo.foco)
        for ch in "120,60abc":
            ui.tecla(self.pygame.event.Event(self.pygame.KEYDOWN, key=ord(ch), unicode=ch))
        self.assertEqual(ui.campo.texto, "120,60")           # letras ignoradas
        ui.tecla(self.pygame.event.Event(self.pygame.KEYDOWN, key=self.pygame.K_RETURN, unicode="\r"))
        self.assertEqual(cliente.publicadas[-1][1], "IR 2 120 60")
        ui.campo.texto = "12"
        ui.tecla(self.pygame.event.Event(self.pygame.KEYDOWN, key=self.pygame.K_RETURN, unicode="\r"))
        self.assertTrue(any("Digite o destino" in a for a in c.avisos_ativos()))

    def test_atalhos_de_teclado(self):
        mundo, op, cliente, c, ui, canvas = self.montar()
        self.popular(mundo, op)
        c.atualizar()
        ev = lambda k: self.pygame.event.Event(self.pygame.KEYDOWN, key=k, unicode="")
        c.selecionar(1)
        ui.tecla(ev(self.pygame.K_TAB))
        self.assertEqual(c.sel, 2)
        ui.tecla(ev(self.pygame.K_f))
        self.assertTrue(next(r for r in mundo.snapshot() if r.id == 2).falha)
        ui.tecla(ev(self.pygame.K_b))
        self.assertEqual(next(r for r in mundo.snapshot() if r.id == 2).bateria, 90.0)
        ui.tecla(ev(self.pygame.K_m))
        self.assertEqual(cliente.publicadas[-1][1], "MODO 2 auto")      # já estava manual
        ui.tecla(ev(self.pygame.K_SPACE))
        self.assertEqual(cliente.publicadas[-1][1], "PARAR 2")
        ui.tecla(ev(self.pygame.K_h))
        self.assertTrue(ui.ajuda)
        ui.tecla(ev(self.pygame.K_ESCAPE))
        self.assertFalse(ui.ajuda)

    def test_rolagem_da_lista_e_do_log(self):
        from simulador.gui import R_EVENTOS, R_LISTA
        mundo, op, _, c, ui, canvas = self.montar()
        for i in range(1, 13):
            mundo.adicionar(i, 10 + i, 10)
        for i in range(60):
            op.registrar_local(f"linha {i}")
        c.atualizar()
        ui.desenhar(canvas)
        ui.roda(R_LISTA.center, -3)
        ui.desenhar(canvas)
        self.assertGreater(ui.lista_scroll, 0)
        ui.roda(R_EVENTOS.center, 2)
        ui.desenhar(canvas)
        self.assertEqual(ui.ev_scroll, 6)
        ui.roda(R_EVENTOS.center, -5)
        self.assertEqual(ui.ev_scroll, 0)


R_SIM_Y = 568


if __name__ == "__main__":
    unittest.main(verbosity=2)
