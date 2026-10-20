#ifndef Z_RECARGA_THREADS_HPP
#define Z_RECARGA_THREADS_HPP

#include "z_recarga.hpp"

typedef struct {

    Zona_recarga* zona;
    Node** grafo;
    int grafo_size;

}Zona_recarga_arg_t;

void Zona_recarga_receive_robot_thread(void* arg);

void init_recarga_thread(Zona_recarga* zona, Node** grafo, int grafo_size);

#endif