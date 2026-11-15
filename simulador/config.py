"""Constantes do simulador.

Os valores da física espelham robo/config_robo.hpp, e os tópicos e formatos
espelham comum/mqtt_topicos.hpp: se um deles mudar no C++, mude aqui também.
"""

# ---------------------------------------------------------------- física
# (robo/config_robo.hpp)
VELOCIDADE_ROBO = 40.0          # unidades de mapa por segundo
DRENO_BATERIA_MOVENDO = 0.5     # % por segundo
DRENO_BATERIA_PARADO = 0.05     # % por segundo
TEMPO_ACOPLAR_S = 0.5           # tempo entre o comando e i_carga_atracada
TAXA_RECARGA = 20.0             # % por segundo, parado numa área de recarga

PASSO_FISICA_S = 0.02           # integração da física (50 Hz)
PERIODO_ENTRADAS_S = 0.05       # publicação das entradas (MQTT_PERIODO_ENTRADAS_MS)
DT_MAXIMO_S = 0.1               # passo máximo: após um travamento, não "teletransporta"

MAX_ROBOS = 32                  # limite do Gestor de Robôs
POS_INICIAL_X = 100             # posição padrão do Gestor (fonte_novos_robos.hpp)
POS_INICIAL_Y = 30

# Ocupação das passagens estreitas (conferência do simulador, em unidades de mapa)
PASSAGEM_BORDA = 1.5            # perto dos nós das pontas o robô ainda está "esperando"
PASSAGEM_LARGURA = 2.5          # distância máxima ao eixo do corredor

# ------------------------------------------------------------------ MQTT
HOST_PADRAO = "localhost"
PORTA_PADRAO = 1883
ENV_HOST = "ATR_MQTT_HOST"
ENV_PORTA = "ATR_MQTT_PORT"
KEEPALIVE_S = 10

TOPICO_NOVO_ROBO = "atr/robos/novo"
TOPICO_FROTA_STATUS = "atr/frota/status"
TOPICO_EVENTOS = "atr/eventos"
TOPICO_CARGAS = "atr/cargas"
TOPICO_OPERACAO_CMD = "atr/operacao/cmd"
TOPICO_SIM_CMD = "atr/sim/cmd"      # FALHA <id> 0|1, BATERIA <id> <pct> (compatível com sim_falso.py)

FILTRO_SAIDAS = "atr/robo/+/saidas"
FILTRO_VIVO = "atr/robo/+/vivo"

QOS_NOVO_ROBO = 1
QOS_ENTRADAS = 0
QOS_SAIDAS = 1
QOS_VIVO = 1
QOS_FROTA_STATUS = 0
QOS_EVENTOS = 1
QOS_CARGAS = 1
QOS_OPERACAO_CMD = 1

VIVO_ONLINE = "online"
VIVO_OFFLINE = "offline"

ORGANIZADOR_TIMEOUT_S = 2.0     # sem atr/frota/status por tanto tempo = Organizador fora do ar
NOVO_ROBO_TIMEOUT_S = 5.0       # tempo para o Gestor criar o processo antes de avisar o operador

# ----------------------------------------------------------------- mapa
NOME_ARQUIVO_MAPA = "mapa.json"  # em bin/, gerado por "bin/mapa json"
