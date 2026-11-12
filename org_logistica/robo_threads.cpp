#include "robo_threads.hpp"

/*
 * send_robo_to_position e robo_thread foram substituídas pelo despacho
 * (despacho.hpp): em vez de criar um processo bin/comando_robo por robô e
 * uma thread por robô lendo a posição a cada segundo até ela bater com o
 * destino, a missão envia um CMD_TAREFA direto na fila de cada robô e
 * espera a fase da equipe na /atr_frota (Robo_manager_assign_robo_thread).
 */
