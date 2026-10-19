#include "comum/passagens.hpp"

#include <cstring>


static uint32_t bit(int slot) {
    return 1u << slot;
}

int passagem_ocupacao(const PassagemShm& p) {

    int n = __builtin_popcount(p.dentro);

    for(int i=0;i<MAX_RESERVAS;i++) {
        if(p.reservas[i].equipe_id != 0)
            n += __builtin_popcount(p.reservas[i].pendentes);
    }

    return n;
}

static void remover_da_fila(PassagemShm& p, int slot) {

    int j = 0;

    for(int i=0;i<p.n_fila;i++) {
        if(p.fila[i] != slot)
            p.fila[j++] = p.fila[i];
    }

    p.n_fila = j;
}

// Depois de recuperar o mutex de um processo morto: deixa a fila e as
// reservas válidas (sem slots repetidos ou fora da faixa).
static void reparar(PassagemShm& p) {

    if(p.n_fila < 0 || p.n_fila > MAX_ROBOS)
        p.n_fila = 0;

    uint32_t vistos = 0;
    int j = 0;

    for(int i=0;i<p.n_fila;i++) {
        int s = p.fila[i];
        if(s >= 0 && s < MAX_ROBOS && !(vistos & bit(s))) {
            vistos |= bit(s);
            p.fila[j++] = s;
        }
    }

    p.n_fila = j;

    for(int i=0;i<MAX_RESERVAS;i++) {
        if(p.reservas[i].pendentes == 0)
            p.reservas[i].equipe_id = 0;
    }
}

static void travar(PassagemShm& p) {

    if(shm_lock(&p.mtx) == 1)
        reparar(p);
}

int passagem_solicitar(ShmFrota* shm, int p, int slot, int equipe_id, uint32_t equipe) {

    PassagemShm& ps = shm->passagens[p];
    int admitido = 0;

    travar(ps);

    if(ps.dentro & bit(slot)) {
        admitido = 1;
        goto fim;
    }

    // Membro de uma equipe que já tem vaga reservada: entra direto
    for(int i=0;i<MAX_RESERVAS;i++) {

        ReservaPassagem& r = ps.reservas[i];

        if(r.equipe_id != 0 && (r.pendentes & bit(slot))) {
            r.pendentes &= ~bit(slot);
            if(r.pendentes == 0)
                r.equipe_id = 0;
            ps.dentro |= bit(slot);
            remover_da_fila(ps, slot);
            admitido = 1;
            goto fim;
        }
    }

    {
        bool na_fila = false;

        for(int i=0;i<ps.n_fila;i++) {
            if(ps.fila[i] == slot)
                na_fila = true;
        }

        if(!na_fila && ps.n_fila < MAX_ROBOS)
            ps.fila[ps.n_fila++] = slot;

        // FIFO: só o primeiro da fila pode ocupar vagas novas
        if(ps.fila[0] != slot)
            goto fim;

        equipe |= bit(slot);
        int necessarias = __builtin_popcount(equipe);

        if(ps.capacidade - passagem_ocupacao(ps) < necessarias)
            goto fim;

        // Equipe: reserva as vagas dos outros membros
        if(necessarias > 1) {

            int livre = -1;

            for(int i=0;i<MAX_RESERVAS;i++) {
                if(ps.reservas[i].equipe_id == 0) {
                    livre = i;
                    break;
                }
            }

            if(livre < 0)
                goto fim;

            ps.reservas[livre].equipe_id = equipe_id;
            ps.reservas[livre].pendentes = equipe & ~bit(slot);
        }

        remover_da_fila(ps, slot);
        ps.dentro |= bit(slot);
        admitido = 1;
    }

fim:
    shm_unlock(&ps.mtx);

    return admitido;
}

void passagem_sair(ShmFrota* shm, int p, int slot) {

    PassagemShm& ps = shm->passagens[p];

    travar(ps);
    ps.dentro &= ~bit(slot);
    shm_unlock(&ps.mtx);
}

void passagem_desistir(ShmFrota* shm, int p, int slot) {

    PassagemShm& ps = shm->passagens[p];

    travar(ps);
    remover_da_fila(ps, slot);
    shm_unlock(&ps.mtx);
}

void passagens_liberar_slot(ShmFrota* shm, int slot) {

    for(int p=0;p<shm->n_passagens;p++) {

        PassagemShm& ps = shm->passagens[p];

        travar(ps);

        ps.dentro &= ~bit(slot);
        remover_da_fila(ps, slot);

        for(int i=0;i<MAX_RESERVAS;i++) {
            ps.reservas[i].pendentes &= ~bit(slot);
            if(ps.reservas[i].pendentes == 0)
                ps.reservas[i].equipe_id = 0;
        }

        shm_unlock(&ps.mtx);
    }
}

void passagem_ler(ShmFrota* shm, int p, PassagemShm& copia) {

    PassagemShm& ps = shm->passagens[p];

    travar(ps);
    memcpy((void*)&copia, (const void*)&ps, sizeof(ps));
    shm_unlock(&ps.mtx);
}
