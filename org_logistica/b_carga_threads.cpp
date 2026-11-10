#include "b_carga_threads.hpp"

#include <boost/asio.hpp>
#include "comum/log.hpp"

/**
 * Thread para simular a chegada de uma carga nova na Baia de carga
 */
void* Baia_carga_load_thread(void* arg) {

    Baia_carga* baia = (Baia_carga*)arg;
        

    boost::asio::io_context io;

    while(true) {

        boost::asio::steady_timer tout(io, boost::asio::chrono::seconds(5));
        
        tout.wait();

        Carga carga_nova(1);

        baia->add_cargo(carga_nova);

        log_msg("CARGA THREAD", "CARGA ADICIONADA NA BAIA %d", baia->get_id());
        
    }


    return nullptr;
}

void init_baia_carga_threads(Baia_carga* Baia) {
   
    pthread_t thread_load;

    pthread_create( &thread_load,
                    NULL,
                    Baia_carga_load_thread,
                    Baia);
    
    
    return;
}