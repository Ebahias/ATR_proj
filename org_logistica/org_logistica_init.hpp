#ifndef ORG_LOGISTICA_INIT_HPP
#define ORG_LOGISTICA_INIT_HPP


#include "robo.hpp"
#include "robo_manager.hpp"
#include "robo_manager_threads.hpp"

/**
 * @brief Inicia outro processo para o gestor de robôs.
 * 
 * @return status da operação
*/
pid_t org_logistica_init_gestor(void);

/**
 * @brief Encerra o processo para o gestor de robôs.
 * 
 * @return status da operação
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


#endif