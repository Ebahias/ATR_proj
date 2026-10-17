#include "z_recarga_threads.hpp"

void Zona_recarga_receive_robot_thread(void* arg) {

    Zona_recarga_arg_t* zona = (Zona_recarga_arg_t*)arg;

    zona->wait_for_spot();

    int node_id = zona->zona->get_free_spot();

    zona->zona->reserve_spot(node_id);

    /* Espera o robô chegar lá*/

    zona->zona->free_spot(node_id);

    
    return;
}

void init_recarga_thread(Zona_recarga* zona, Node** grafo, int grafo_size) {

    pthread_t thread;

    Zona_recarga_arg_t zona_arg = {0};

    zona_arg.zona = zona;
    zona_arg.grafo = grafo;
    zona_arg.grafo_size = grafo_size;

    pthread_create( &thread,
                    NULL,
                    Zona_recarga_receive_robot_thread,
                    &zona_arg);
    
    return 
}
