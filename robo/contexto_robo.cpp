#include "robo/contexto_robo.hpp"
#include "comum/log.hpp"

#include <cstdio>


ContextoRobo::ContextoRobo(int id, int slot, ShmFrota* shm, HardwareRobo* hw, mqd_t mq_comando)
    : id(id), slot(slot), shm(shm), hw(hw), mq_comando(mq_comando) {

    _encerrar = false;
    _estado = ESTADO_INICIANDO;
    _modo = MODO_AUTOMATICO;
    _tem_destino = false;
    _destino_x = 0;
    _destino_y = 0;
    _movimento_bloqueado = false;
    _tem_destino_forcado = false;
    _forcado_x = 0;
    _forcado_y = 0;
    _distancia_total = 0.0;
    _nav.fase = FASE_NENHUMA;
    _nav.tarefa_id = 0;
    _nav.progresso = 0;
    _nav.passagem = -1;
    _nav.aguardando_passagem = -1;
    _nav.estado_desejado = ESTADO_DISPONIVEL;

    pthread_mutex_init(&_mtx, NULL);
}

ContextoRobo::~ContextoRobo() {
    pthread_mutex_destroy(&_mtx);
}

bool ContextoRobo::deve_encerrar(void) {

    pthread_mutex_lock(&_mtx);
    bool r = _encerrar;
    pthread_mutex_unlock(&_mtx);

    return r;
}

void ContextoRobo::solicitar_encerramento(void) {

    pthread_mutex_lock(&_mtx);
    _encerrar = true;
    pthread_mutex_unlock(&_mtx);
}

InstantaneoRobo ContextoRobo::instantaneo(void) {

    InstantaneoRobo i;

    pthread_mutex_lock(&_mtx);

    i.estado = _estado;
    i.modo = _modo;
    i.tem_destino = _tem_destino;
    i.destino_x = _destino_x;
    i.destino_y = _destino_y;
    i.movimento_bloqueado = _movimento_bloqueado;
    i.tem_destino_forcado = _tem_destino_forcado;
    i.forcado_x = _forcado_x;
    i.forcado_y = _forcado_y;
    i.distancia_total = _distancia_total;
    i.nav = _nav;

    pthread_mutex_unlock(&_mtx);

    return i;
}

int ContextoRobo::estado(void) {

    pthread_mutex_lock(&_mtx);
    int e = _estado;
    pthread_mutex_unlock(&_mtx);

    return e;
}

static void registrar_mudanca(ContextoRobo* ctx, int antigo, int novo, const char* motivo);

void ContextoRobo::mudar_estado(int novo, const char* motivo) {

    pthread_mutex_lock(&_mtx);
    int antigo = _estado;
    _estado = novo;
    pthread_mutex_unlock(&_mtx);

    registrar_mudanca(this, antigo, novo, motivo);
}

bool ContextoRobo::mudar_estado_operacional(int novo, const char* motivo) {

    pthread_mutex_lock(&_mtx);
    int antigo = _estado;
    bool pode = (antigo != ESTADO_DEFEITO && antigo != ESTADO_RECARREGANDO);
    if(pode)
        _estado = novo;
    pthread_mutex_unlock(&_mtx);

    if(pode)
        registrar_mudanca(this, antigo, novo, motivo);

    return pode;
}

static void registrar_mudanca(ContextoRobo* ctx, int antigo, int novo, const char* motivo) {

    if(antigo == novo)
        return;

    ShmFrota* shm = ctx->shm;
    int id = ctx->id;

    // Evento fora do mutex do contexto: nada é feito com dois locks ao mesmo tempo
    shm_evento_publicar(shm, id, EV_ESTADO, "%s -> %s (%s)",
                        estado_robo_nome(antigo), estado_robo_nome(novo), motivo);

    char modulo[16];
    snprintf(modulo, sizeof(modulo), "ROBO %d", id);
    log_msg(modulo, "estado %s -> %s (%s)", estado_robo_nome(antigo), estado_robo_nome(novo), motivo);
}

int ContextoRobo::modo(void) {

    pthread_mutex_lock(&_mtx);
    int m = _modo;
    pthread_mutex_unlock(&_mtx);

    return m;
}

void ContextoRobo::mudar_modo(int novo) {

    pthread_mutex_lock(&_mtx);
    int antigo = _modo;
    _modo = novo;
    pthread_mutex_unlock(&_mtx);

    if(antigo != novo)
        shm_evento_publicar(shm, id, EV_MODO, "modo %s", modo_robo_nome(novo));
}

void ContextoRobo::definir_destino(int x, int y) {

    pthread_mutex_lock(&_mtx);
    _tem_destino = true;
    _destino_x = x;
    _destino_y = y;
    pthread_mutex_unlock(&_mtx);
}

void ContextoRobo::bloquear_movimento(bool bloquear) {

    pthread_mutex_lock(&_mtx);
    _movimento_bloqueado = bloquear;
    pthread_mutex_unlock(&_mtx);
}

void ContextoRobo::forcar_destino(int x, int y) {

    pthread_mutex_lock(&_mtx);
    _tem_destino_forcado = true;
    _forcado_x = x;
    _forcado_y = y;
    pthread_mutex_unlock(&_mtx);
}

void ContextoRobo::liberar_destino_forcado(void) {

    pthread_mutex_lock(&_mtx);
    _tem_destino_forcado = false;
    pthread_mutex_unlock(&_mtx);
}

void ContextoRobo::publicar_navegacao(const InfoNavegacao& info) {

    pthread_mutex_lock(&_mtx);
    _nav = info;
    pthread_mutex_unlock(&_mtx);
}

void ContextoRobo::definir_distancia_total(double distancia) {

    pthread_mutex_lock(&_mtx);
    _distancia_total = distancia;
    pthread_mutex_unlock(&_mtx);
}
