#ifndef ROBO_THREADS_HPP
#define ROBO_THREADS_HPP

#include "robo.hpp"


typedef struct {

    Robo* robo;
    
    Node* init_pos;
    Node* final_pos;

    sem_t* sem;
}robo_thread_arg_t;

void robo_thread(void* arg);

void send_robo_to_position(Robo* robo, Node* i_node, Node* f_node, sem_t* sem);

#endif