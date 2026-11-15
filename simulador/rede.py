"""Comunicação MQTT do simulador / Tela de Operação (paho-mqtt).

Lado simulador (hardware embarcado dos robôs):
  publica   atr/robo/<id>/entradas   "x y atracada bateria falha"   a cada 50 ms
  assina    atr/robo/+/saidas        "sp_x sp_y acoplar"
  assina    atr/robo/+/vivo          "online" / "offline"            (retida; will do robô)
  assina    atr/sim/cmd              "FALHA <id> 0|1", "BATERIA <id> <pct>"

Lado tela de operação:
  publica   atr/robos/novo           "NOVO_ROBO <id> [x y]"          (Gestor de Robôs)
  publica   atr/operacao/cmd         "MODO|IR|PARAR|CAIXA ..."       (Organizador)
  assina    atr/frota/status, atr/cargas, atr/eventos                (JSON do Organizador)

Todos os callbacks do paho rodam numa única thread de rede e só atualizam
estado (Mundo / EstadoOperacao): nada bloqueia nem desenha aqui.
"""

import logging

import paho.mqtt.client as mqtt

from simulador import config

log = logging.getLogger("rede")


def criar_cliente(client_id):
    """Cliente paho compatível com as versões 1.x e 2.x da biblioteca."""
    try:
        return mqtt.Client(mqtt.CallbackAPIVersion.VERSION2, client_id=client_id)
    except AttributeError:
        return mqtt.Client(client_id=client_id)


class Rede:

    def __init__(self, mundo, operacao, host=config.HOST_PADRAO, porta=config.PORTA_PADRAO,
                 client_id="simulador", cliente=None):
        self.mundo = mundo
        self.operacao = operacao
        self.host = host
        self.porta = porta
        self.cliente = cliente if cliente is not None else criar_cliente(client_id)
        self.conectado = False
        self.vivos = set()                  # robôs com atr/robo/<id>/vivo = online (retido no broker)
        self.publicadas = 0
        self.recebidas = 0
        self.invalidas = 0

        self.cliente.on_connect = self._ao_conectar
        self.cliente.on_disconnect = self._ao_desconectar
        self.cliente.on_message = self._ao_receber
        self.cliente.reconnect_delay_set(min_delay=1, max_delay=5)

    # ----------------------------------------------------- conexão
    def iniciar(self):
        """Conecta em segundo plano: sem broker a tela abre assim mesmo e o paho
        tenta de novo até conseguir."""
        self.cliente.connect_async(self.host, self.porta, config.KEEPALIVE_S)
        self.cliente.loop_start()

    def parar(self):
        try:
            self.cliente.disconnect()
        finally:
            self.cliente.loop_stop()

    def _ao_conectar(self, cliente, *args):
        self.conectado = True
        log.info("conectado ao broker %s:%d", self.host, self.porta)

        # Assinaturas refeitas a cada conexão (o broker pode ter reiniciado)
        for topico, qos in ((config.FILTRO_SAIDAS, config.QOS_SAIDAS),
                            (config.FILTRO_VIVO, config.QOS_VIVO),
                            (config.TOPICO_SIM_CMD, 1),
                            (config.TOPICO_FROTA_STATUS, config.QOS_FROTA_STATUS),
                            (config.TOPICO_EVENTOS, config.QOS_EVENTOS),
                            (config.TOPICO_CARGAS, config.QOS_CARGAS)):
            cliente.subscribe(topico, qos)

    def _ao_desconectar(self, *args):
        self.conectado = False
        log.warning("desconectado do broker")

    # ------------------------------------------------------ entrada
    def _ao_receber(self, cliente, dados, msg):
        self.recebidas += 1

        try:
            texto = msg.payload.decode("utf-8", errors="replace")
        except AttributeError:
            return

        self.tratar(msg.topic, texto)

    def tratar(self, topico, texto):
        """Despacha uma mensagem recebida (separado do paho para poder ser testado)."""
        if topico == config.TOPICO_FROTA_STATUS:
            self.operacao.atualizar_frota(texto)
        elif topico == config.TOPICO_CARGAS:
            self.operacao.atualizar_cargas(texto)
        elif topico == config.TOPICO_EVENTOS:
            self.operacao.registrar_evento(texto)
        elif topico == config.TOPICO_SIM_CMD:
            self._comando_sim(texto)
        else:
            partes = topico.split("/")

            if len(partes) == 4 and partes[0] == "atr" and partes[1] == "robo":
                try:
                    rid = int(partes[2])
                except ValueError:
                    return

                if partes[3] == "saidas":
                    self._saidas(rid, texto)
                elif partes[3] == "vivo":
                    self._vivo(rid, texto)

    def _saidas(self, rid, texto):
        campos = texto.split()

        try:
            if len(campos) < 3:
                raise ValueError
            sp_x, sp_y, acoplar = int(campos[0]), int(campos[1]), int(campos[2]) != 0
        except ValueError:
            self.invalidas += 1
            return

        novo = not self.mundo.existe(rid)
        self.mundo.definir_saidas(rid, sp_x, sp_y, acoplar)

        if novo:
            self.operacao.registrar_local(f"robô {rid} apareceu no galpão em ({sp_x}, {sp_y})")

    def _vivo(self, rid, texto):
        if texto.strip() == config.VIVO_ONLINE:
            self.vivos.add(rid)
            return

        self.vivos.discard(rid)

        # "offline" retido de uma execução anterior chega ao assinar: só vale para robôs que existem
        if texto.strip() == config.VIVO_OFFLINE and self.mundo.remover(rid):
            self.operacao.registrar_local(f"robô {rid} saiu do galpão (processo encerrado)")

    def _comando_sim(self, texto):
        campos = texto.split()

        try:
            if len(campos) != 3:
                raise ValueError
            rid = int(campos[1])

            if campos[0] == "FALHA":
                self.mundo.definir_falha(rid, campos[2] == "1")
            elif campos[0] == "BATERIA":
                self.mundo.definir_bateria(rid, float(campos[2]))
            else:
                raise ValueError
        except ValueError:
            self.invalidas += 1

    # ------------------------------------------------------- saída
    def _publicar(self, topico, payload, qos=0, retain=False):
        if not self.conectado:
            return False

        self.cliente.publish(topico, payload, qos, retain)
        self.publicadas += 1
        return True

    def publicar_entradas(self, rid, payload):
        """Chamado pela thread de física a cada 50 ms, por robô."""
        return self._publicar(f"atr/robo/{rid}/entradas", payload, config.QOS_ENTRADAS)

    def novo_robo(self, rid, x=None, y=None):
        """Anuncia um robô ao Gestor. Sem posição, ele nasce na posição padrão do Gestor."""
        msg = f"NOVO_ROBO {rid}" if x is None else f"NOVO_ROBO {rid} {int(x)} {int(y)}"
        return self._publicar(config.TOPICO_NOVO_ROBO, msg, config.QOS_NOVO_ROBO)

    def cmd_modo(self, rid, manual):
        modo = "manual" if manual else "auto"
        return self._publicar(config.TOPICO_OPERACAO_CMD, f"MODO {rid} {modo}", config.QOS_OPERACAO_CMD)

    def cmd_ir(self, rid, x, y):
        return self._publicar(config.TOPICO_OPERACAO_CMD, f"IR {rid} {int(x)} {int(y)}", config.QOS_OPERACAO_CMD)

    def cmd_parar(self, rid):
        return self._publicar(config.TOPICO_OPERACAO_CMD, f"PARAR {rid}", config.QOS_OPERACAO_CMD)

    def cmd_caixa(self, baia, tamanho):
        return self._publicar(config.TOPICO_OPERACAO_CMD, f"CAIXA {int(baia)} {int(tamanho)}", config.QOS_OPERACAO_CMD)
