#ifndef ORG_LOGISTICA_INIT_HPP
#define ORG_LOGISTICA_INIT_HPP


#include "robo.hpp"
#include "robo_manager.hpp"
#include "robo_manager_threads.hpp"

#include <string>
#include <vector>
#include <sys/types.h>

/*
 * O Organizador sobe o Gestor de Robôs e o Coletor de Dados como processos
 * filhos e os encerra no fim. Os filhos ficam num grupo de processos próprio
 * (o Ctrl+C do terminal chega só ao Organizador, que encerra tudo na ordem
 * certa) e recebem SIGTERM se o Organizador morrer sem encerrá-los.
 */

/**
 * @brief Inicia outro processo para o gestor de robôs.
 *
 * @param args argumentos repassados ao gestor_robos (ex.: --fonte mqtt)
 * @return Pid do processo, ou -1 em erro
*/
pid_t org_logistica_init_gestor(const std::vector<std::string>& args);

/**
 * @brief Espera o gestor de robôs criar a /atr_frota.
 *
 * O Organizador só pode abrir a memória da frota depois que o Gestor a
 * criou; sem esta espera ele tentava logo depois do fork e saía com erro.
 *
 * @return false no timeout ou se o gestor terminou antes (ex.: sem broker
 * em --fonte mqtt)
*/
bool org_logistica_esperar_gestor(pid_t pid_gestor, int timeout_ms);

/**
 * @brief Encerra o processo para o gestor de robôs.
 *
 * Manda SIGTERM e espera ele terminar (o Gestor encerra os robôs antes).
 * Não faz nada com pid <= 0.
*/
void org_logistica_close_gestor(pid_t pid_gestor);

/**
 * @brief Inicia o mqtt para o organizador de logística.
 *
 * @return status da operação
*/
int org_logistica_init_mqtt(void);

/**
 * @brief Encerra o mqtt no organizador de logística.
 *
 * @return status da operação
*/
int org_logistica_close_mqtt(void);

/**
 * @brief Inicia outro processo para o coletor de dados.
 *
 * Os dados vão para data/data_on_<data>_<hora>/, uma pasta por execução.
 *
 * @return Pid do processo, ou -1 em erro
*/
pid_t org_logistica_init_coletor(void);

/**
 * @brief Encerra o processo para o coletor de dados.
 *
 * Chamar depois de org_logistica_close_gestor: assim o Coletor ainda
 * registra o encerramento dos robôs. Não faz nada com pid <= 0.
*/
void org_logistica_close_coletor(pid_t pid_coletor);


#endif
