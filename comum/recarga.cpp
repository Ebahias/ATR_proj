#include "comum/recarga.hpp"

#include <cmath>
#include <cstring>

int recarga_reservar(ShmFrota* shm, int slot, int x, int y) {

    RecargaShm& r = shm->recarga;
    int escolhida = -1;

    shm_lock(&r.mtx);

    for(int i=0;i<r.n_vagas;i++) {
        if(r.ocupante[i] == slot) {
            escolhida = i;
            break;
        }
    }

    if(escolhida < 0) {

        double melhor = 0.0;

        for(int i=0;i<r.n_vagas;i++) {

            if(r.ocupante[i] >= 0)
                continue;

            const VagaRecargaMapa& v = mapa_vaga_recarga(i);
            double d = std::hypot((double)(v.x - x), (double)(v.y - y));

            if(escolhida < 0 || d < melhor) {
                escolhida = i;
                melhor = d;
            }
        }

        if(escolhida >= 0)
            r.ocupante[escolhida] = slot;
    }

    shm_unlock(&r.mtx);

    return escolhida;
}

void recarga_liberar(ShmFrota* shm, int slot) {

    RecargaShm& r = shm->recarga;

    shm_lock(&r.mtx);

    for(int i=0;i<r.n_vagas;i++) {
        if(r.ocupante[i] == slot)
            r.ocupante[i] = -1;
    }

    shm_unlock(&r.mtx);
}

void recarga_ler(ShmFrota* shm, RecargaShm& copia) {

    RecargaShm& r = shm->recarga;

    shm_lock(&r.mtx);
    memcpy((void*)&copia, (const void*)&r, sizeof(r));
    shm_unlock(&r.mtx);
}
