#include "b_carga_threads.hpp"

#include "comum/log.hpp"

/*
 * Cargas das baias: não há mais geração periódica. O enunciado pede um
 * número fixo de caixas por baia no início, tiradas de uma fila
 * pré-definida (cargas_iniciais.txt, carregada pelo main), e caixas novas
 * só quando o simulador as incluir (comando CAIXA, na ponte MQTT).
 */

void* Baia_carga_process_thread(void* arg) {

    Baia_carga* baia = (Baia_carga*)arg;

    while(true) {

        baia->pub_cargo();

        log_msg("CARGA THREAD", "CARGA PROCESSADA NA BAIA %d", baia->get_id());

    }

    return nullptr;
}

void init_baia_carga_threads(Baia_carga* Baia, std::vector<pthread_t>* threads) {

    pthread_t thread_pub;

    pthread_create( &thread_pub,
                    NULL,
                    Baia_carga_process_thread,
                    Baia);

    if(threads != nullptr)
        threads->push_back(thread_pub);

    return;
}