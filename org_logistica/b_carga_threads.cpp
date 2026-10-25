#include "b_carga_threads.hpp"

#include <boost/asio.hpp>


/**
 * Thread para simular a chegada de uma carga nova na Baia de carga
 */
void* Baia_carga_load_thread(void* arg) {

    Baia_carga_arg_t* baia_arg = (Baia_carga_arg_t*)arg;
    
    Baia_carga* baia = baia_arg->baia;
    
    Node** grafo = baia_arg->grafo;
    
    int grafo_size = baia_arg->grafo_size;

    boost::asio::io_context io;

    while(true) {

        boost::asio::steady_timer tout(io, boost::asio::chrono::seconds(5));

        Carga carga_nova(1);

        baia->add_cargo(carga_nova);

        tout.wait();
        
    }


    return nullptr;
}

void init_baia_carga_threads(Baia_carga* Baia, Node** grafo, int grafo_size) {
   
    pthread_t thread_load;

    Baia_carga_arg_t baia_arg = {0};

    baia_arg.baia = Baia;
    baia_arg.grafo = grafo;
    baia_arg.grafo_size = grafo_size;

    pthread_create( &thread_load,
                    NULL,
                    Baia_carga_load_thread,
                    &baia_arg);
    
    
    return;
}