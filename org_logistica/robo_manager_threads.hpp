#ifndef ROBO_MANAGER_THREAD_HPP
#define ROBO_MANAGER_THREAD_HPP


#include "robo_manager.hpp"
#include "robo_threads.hpp"


typedef struct {

    Robo_manager* manager;
    Baia_carga* b_carga;

}Robo_manager_arg_t;

typedef struct {

    Robo_manager* manager;
    std::vector<Robo*> assign_robo;
    Baia_carga* b_carga;
    Cargo* carga;

}Robo_assign_arg_t;

void organizador_de_logistica_thread(void* arg);

void Robo_manager_process_cargo_thread(void* arg);

void Robo_manager_recharge_thread(void* arg);

void Robo_manager_assign_robo_thread(void* arg);

void init_Robo_manager_threads(Robo_manager_arg_t* Manager);

#endif