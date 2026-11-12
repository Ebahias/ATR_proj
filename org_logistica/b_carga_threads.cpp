#include "b_carga_threads.hpp"

#include "ponte_mqtt.hpp"
#include "comum/log.hpp"

#include <stdlib.h>
#include <time.h>

// Intervalo entre a chegada de cargas novas numa baia. Com 5 s as 4 baias
// geravam uma carga a cada 1,25 s, e 6 robôs entregam uma a cada ~6 s: a
// fila crescia sem parar. Com 30 s chega uma carga a cada 7,5 s, abaixo do
// que a frota dá conta.
#define PERIODO_NOVA_CARGA_S    30

// Tamanho da carga = número de robôs da equipe (o robô aceita até 4)
#define TAMANHO_MAX_CARGA       4

/**
 * Thread para simular a chegada de uma carga nova na Baia de carga
 */
void* Baia_carga_load_thread(void* arg) {

    Baia_carga* baia = (Baia_carga*)arg;

    // nanosleep no lugar do steady_timer do Boost: o Boost não está
    // instalado em todas as máquinas do grupo e só servia para dormir.
    struct timespec intervalo = { PERIODO_NOVA_CARGA_S, 0 };

    // rand_r com semente própria: rand() tem estado global e não é
    // thread-safe, e cada baia tem a sua thread
    unsigned int semente = (unsigned int)time(NULL) ^ (unsigned int)(baia->get_id() * 7919);

    // A primeira carga chega logo, sem esperar um período inteiro
    while(true) {

        Carga carga_nova(1 + rand_r(&semente) % TAMANHO_MAX_CARGA);

        baia->add_cargo(carga_nova);

        ponte_mqtt_cargas_mudaram();

        log_msg("CARGA THREAD", "CARGA DE TAMANHO %d CRIADA NA BAIA %d", carga_nova.get_type(), baia->get_id());

        nanosleep(&intervalo, NULL);

    }


    return nullptr;
}

void* Baia_carga_process_thread(void* arg) {

    Baia_carga* baia = (Baia_carga*)arg;

    while(true) {

        baia->pub_cargo();

        log_msg("CARGA THREAD", "CARGA PROCESSADA NA BAIA %d", baia->get_id());

    }

    return nullptr;
}

void init_baia_carga_threads(Baia_carga* Baia, std::vector<pthread_t>* threads) {

    pthread_t thread_load, thread_pub;

    pthread_create( &thread_load,
                    NULL,
                    Baia_carga_load_thread,
                    Baia);

    pthread_create( &thread_pub,
                    NULL,
                    Baia_carga_process_thread,
                    Baia);

    if(threads != nullptr) {
        threads->push_back(thread_load);
        threads->push_back(thread_pub);
    }

    return;
}