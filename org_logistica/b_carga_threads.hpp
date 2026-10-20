#ifndef B_CARGA_THREADS_HPP
#define B_CARGA_THREADS_HPP

#include "b_carga.hpp"
#include "b_descarga.hpp"

typedef struct {

    Baia_carga* baia;
    Node** grafo;
    int grafo_size;

}Baia_carga_arg_t;

void Baia_carga_load_thread(void* arg);

void Baia_carga_consume_thread(void* arg);

void Baia_carga_process_thread(void* arg);

void init_baia_carga_threads(Baia_carga_arg_t* Baia, Node** grafo, int grafo_size);

#endif