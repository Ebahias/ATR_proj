#ifndef B_DESCARGA_THREADS_HPP
#define B_DESCARGA_THREADS_HPP

#include "b_carga.hpp"
#include "b_descarga.hpp"


void Baia_descarga_load_thread(void* arg);

void Baia_descarga_process_thread(void* arg);

void init_baia_descarga_threads(Baia_descarga* Baia);

#endif