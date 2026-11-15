"""Simulador do galpão + Tela de Operação (Etapa 2 do trabalho final de ATR).

Uma única aplicação Python (pygame + paho-mqtt) que faz dois papéis:

  * Simulador: emula o hardware embarcado de todos os robôs (posição, bateria,
    acoplamento) e o mapa do galpão, trocando as variáveis da Tabela 1 do
    enunciado com os processos dos robôs via MQTT.
  * Tela de Operação: mostra a frota, as caixas e os eventos recebidos do
    Organizador da Logística e envia a ele os comandos do operador.
"""
