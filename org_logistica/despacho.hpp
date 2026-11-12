#ifndef DESPACHO_HPP
#define DESPACHO_HPP

#include "comum/ipc_robo.hpp"

#include <stdint.h>
#include <vector>

/*
 * Despacho de tarefas aos robôs: a única parte do Organizador que fala com
 * as filas de comando ("/atr_robo_<id>_cmd") e lê a tabela de status da
 * /atr_frota. O resto do Organizador só trabalha com ids de robôs.
 *
 * Uma missão de transporte, do ponto de vista do Organizador:
 *   1. despacho_tarefa() para cada robô da equipe, todos com o mesmo
 *      tarefa_id e a mesma máscara de equipe, cada um com o seu ponto de
 *      coleta na baia e o seu ponto de entrega na zona;
 *   2. despacho_esperar_fase(..., FASE_AGUARDANDO_EQUIPE, ...): a equipe
 *      toda chegou à baia;
 *   3. despacho_acoplar() para cada robô: libera a carga;
 *   4. despacho_esperar_fase(..., FASE_CONCLUIDA, ...).
 * O robô faz o resto sozinho (rota, barreira da equipe, reserva conjunta das
 * passagens estreitas), em robo/navegador.cpp.
 *
 * Todas as funções podem ser chamadas de várias threads ao mesmo tempo
 * (uma por missão): o ponteiro da memória só é escrito em despacho_iniciar(),
 * antes de as threads existirem, e cada slot da tabela tem o seu próprio
 * mutex entre processos (shm_slot_ler devolve uma cópia consistente).
 */

// Abre a /atr_frota (criada pelo Gestor). Chamar uma vez no início do main,
// antes de criar as threads. Retorna false se o Gestor ainda não a criou.
bool despacho_iniciar(void);

void despacho_encerrar(void);

// Slot da tabela ocupado pelo robô, ou -1 se ele não está ativo.
int despacho_slot_do_robo(int robo_id);

// Cópia do status do robô. Retorna false se ele não está ativo.
bool despacho_ler_status(int robo_id, DadosStatus& d);

// Cópia do status de todos os robôs ativos, em ordem de slot (para a ponte
// MQTT publicar a frota). Retorna quantos são.
int despacho_ler_frota(std::vector<DadosStatus>& ativos);

// Ids de robôs -> máscara de slots (bit i = robô do slot i), o formato que o
// robô espera em ComandoRobo::equipe. Retorna 0 se algum robô não está ativo
// (uma equipe válida nunca tem máscara 0).
uint32_t despacho_mascara_equipe(const std::vector<int>& ids);

// Envia um comando ao robô, com a origem dada e o timestamp atual. Não
// bloqueia: falha se a fila do robô não existe ou está cheia. Os comandos
// da Tela de Operação usam ORIGEM_OPERADOR: em modo manual o robô só aceita
// movimento vindo do operador.
bool despacho_enviar(int robo_id, const ComandoRobo& cmd, int origem = ORIGEM_ORGANIZADOR);

// CMD_TAREFA: coleta em (cx, cy), entrega em (ex, ey). tarefa_id deve ser > 0.
bool despacho_tarefa(int robo_id, int tarefa_id, uint32_t equipe, int cx, int cy, int ex, int ey);

// CMD_ACOPLAR_E_ERGUER: libera o robô para pegar a carga.
bool despacho_acoplar(int robo_id);

// Usados para abortar uma missão. CMD_PARAR cancela a tarefa e para o robô
// (dentro de uma passagem estreita ele termina a travessia antes);
// CMD_SOLTAR_CARGA cancela a tarefa e solta a carga, se estiver atracada.
bool despacho_parar(int robo_id);

bool despacho_soltar(int robo_id);

// Espera até todos os robôs de ids estarem na tarefa tarefa_id e na fase
// pedida. Retorna false no timeout, se algum robô deixou de estar ativo, se
// algum entrou em FASE_FALHOU nesta tarefa, se algum largou a tarefa (ex.:
// o operador o passou para manual, o que cancela a tarefa no robô) ou se
// despacho_interromper() foi chamado. Bloqueia a thread que chama.
bool despacho_esperar_fase(const std::vector<int>& ids, int tarefa_id, int fase, int timeout_ms);

// Encerramento do Organizador: faz todas as esperas em andamento (e as
// futuras) retornarem false em até um período de leitura, para as threads
// das missões liberarem o que reservaram e terminarem.
void despacho_interromper(void);

#endif
