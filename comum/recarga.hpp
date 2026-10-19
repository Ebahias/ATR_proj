#ifndef COMUM_RECARGA_HPP
#define COMUM_RECARGA_HPP

#include "comum/ipc_robo.hpp"

/*
 * Vagas das estações de recarga (recurso compartilhado entre os processos
 * dos robôs). Monitor na memória compartilhada: mutex robusto + dono de cada
 * vaga. Como o dono fica registrado, o Gestor devolve a vaga de um robô que
 * morreu (recarga_liberar).
 */

// Reserva a vaga livre mais próxima de (x, y). Se o slot já tem uma vaga,
// devolve a mesma. Retorna o índice da vaga (mapa_vaga_recarga) ou -1 se
// todas estão ocupadas. Nunca bloqueia.
int recarga_reservar(ShmFrota* shm, int slot, int x, int y);

// Libera a vaga do slot, se houver.
void recarga_liberar(ShmFrota* shm, int slot);

// Cópia consistente das vagas (para telas e testes).
void recarga_ler(ShmFrota* shm, RecargaShm& copia);

#endif
