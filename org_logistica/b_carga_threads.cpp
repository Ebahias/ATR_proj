#include "b_carga_threads.hpp"

#include "comum/log.hpp"

#include <time.h>

// Intervalo entre a chegada de cargas novas numa baia
#define PERIODO_NOVA_CARGA_S    5

/**
 * Thread para simular a chegada de uma carga nova na Baia de carga
 */
void* Baia_carga_load_thread(void* arg) {

    Baia_carga* baia = (Baia_carga*)arg;

    // nanosleep no lugar do steady_timer do Boost: o Boost não está
    // instalado em todas as máquinas do grupo e só servia para dormir.
    struct timespec intervalo = { PERIODO_NOVA_CARGA_S, 0 };

    while(true) {

        nanosleep(&intervalo, NULL);

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