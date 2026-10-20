#include "b_carga_threads.hpp"


/**
 * Thread para simular a chegada de uma carga nova na Baia de carga
 */
void Baia_carga_load_thread(void* arg) {

    Baia_carga_arg_t* baia_arg = (Baia_carga_arg_t*)arg;
    
    Baia_carga* baia = baia_arg->baia;
    
    Node** grafo = baia_arg->grafo;
    
    int grafo_size = baia_arg->grafo_size;


    while(true) {

        Carga carga_nova(1);

        baia->add_cargo(carga_nova);

        sleep(4);
    }


    return;
}

void Baia_carga_process_thread(void* arg) {

    return;
}

void Baia_carga_consume_thread(void* arg) {

    return;
}

void init_baia_carga_threads(Baia_carga_arg_t* Baia, Node** grafo, int grafo_size) {
   
    pthread_t thread_load, thread_process, thread_consume;

    Baia_recarga_arg_t baia_arg = {0};

    baia_arg.baia = baia;
    baia_arg.grafo = grafo;
    baia_arg.grafo_size = grafo_size;

    pthread_create( &thread_load,
                    NULL,
                    Baia_carga_load_thread,
                    &baia_arg);
    
    
    return;
}