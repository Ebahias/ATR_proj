#ifndef B_CARGA_THREADS_HPP
#define B_CARGA_THREADS_HPP

#include "b_carga.hpp"
#include "b_descarga.hpp"

typedef struct {

    Baia_carga* baia;
    Node** grafo;
    int grafo_size;

}Baia_carga_arg_t;

void Baia_carga_consume_thread(void* arg);

void* Baia_carga_process_thread(void* arg);

// Cria a thread de processamento de cargas da baia (reserva os pontos de
// cada caixa). Se "threads" não for nulo, acrescenta o id nele (o main
// encerra por ele).
void init_baia_carga_threads(Baia_carga* Baia, std::vector<pthread_t>* threads = nullptr);

#endif