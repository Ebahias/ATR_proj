#include "robo/navegador.hpp"
#include "comum/log.hpp"
#include "comum/passagens.hpp"

#include <cstdio>


Navegador::Navegador(ContextoRobo* ctx) {

    _ctx = ctx;
    snprintf(_modulo, sizeof(_modulo), "ROBO %d NAV", ctx->id);

    _tem_rota = false;
    _idx = 0;
    _alvo_x = 0;
    _alvo_y = 0;
    _sincronizar = false;
    _passagem = -1;
    _aguardando = -1;

    _tem_destino = false;
    _destino_x = 0;
    _destino_y = 0;

    _fase = FASE_NENHUMA;
    _tarefa_id = 0;
    _coleta_x = _coleta_y = 0;
    _entrega_x = _entrega_y = 0;
    _equipe = 0;
    _progresso = 0;
    _soltou = false;
    _acoplado_manual = false;
}

const char* Navegador::nome_local(int x, int y, char* buf, size_t tam) {

    const AreaMapa* a = mapa_area_do_ponto(x, y);

    if(a)
        snprintf(buf, tam, "%s", a->nome);
    else
        snprintf(buf, tam, "(%d, %d)", x, y);

    return buf;
}

bool Navegador::tarefa_ativa(void) const {
    return _fase >= FASE_IR_COLETA && _fase <= FASE_ENTREGANDO;
}

void Navegador::publicar(void) {

    InfoNavegacao info;
    info.fase = _fase;
    info.tarefa_id = _tarefa_id;
    info.progresso = _progresso;
    info.passagem = _passagem;
    info.aguardando_passagem = _aguardando;

    if(_fase == FASE_IR_COLETA)
        info.estado_desejado = ESTADO_DESLOCANDO;
    else if(tarefa_ativa() || _acoplado_manual)
        info.estado_desejado = ESTADO_OPERANDO_CARGA;
    else if(_tem_destino)
        info.estado_desejado = ESTADO_DESLOCANDO;
    else
        info.estado_desejado = ESTADO_DISPONIVEL;

    _ctx->publicar_navegacao(info);
}


/* ------------------------------------------------------------------ */
/* Rotas e passagens                                                   */
/* ------------------------------------------------------------------ */

void Navegador::sair_da_fila(void) {

    if(_aguardando >= 0) {
        passagem_desistir(_ctx->shm, _aguardando, _ctx->slot);
        _aguardando = -1;
    }
}

void Navegador::segurar(const LeituraHardware& l) {
    _ctx->hw->escrever_setpoint(l.pos_x, l.pos_y);
}

// Garante que a rota em execução leva a (x, y); replaneja se não leva.
bool Navegador::rota_para(int x, int y, bool sincronizar) {

    if(_tem_rota && _alvo_x == x && _alvo_y == y && _sincronizar == sincronizar)
        return true;

    return planejar(x, y, sincronizar);
}

bool Navegador::planejar(int x, int y, bool sincronizar) {

    LeituraHardware l = _ctx->hw->ler();
    int tam_equipe = sincronizar ? __builtin_popcount(_equipe) : 1;

    std::vector<Waypoint> nova;

    bool atravessando = _passagem >= 0 && _tem_rota && _idx < _rota.size()
                        && _rota[_idx].passagem == _passagem;

    if(atravessando) {
        // Dentro de uma passagem estreita: termina a travessia antes de mudar
        // de rumo (continua dono da vaga até a saída)
        Waypoint saida = _rota[_idx];
        std::vector<Waypoint> resto;

        if(mapa_rota(saida.x, saida.y, x, y, tam_equipe, resto) < 0)
            return false;

        nova.push_back(saida);
        nova.insert(nova.end(), resto.begin(), resto.end());

    } else {

        if(_passagem >= 0) {
            passagem_sair(_ctx->shm, _passagem, _ctx->slot);
            _passagem = -1;
        }

        if(mapa_rota(l.pos_x, l.pos_y, x, y, tam_equipe, nova) < 0)
            return false;
    }

    sair_da_fila();

    _rota = nova;
    _idx = 0;
    _tem_rota = true;
    _alvo_x = x;
    _alvo_y = y;
    _sincronizar = sincronizar;

    _ctx->definir_destino(x, y);

    log_msg(_modulo, "rota até (%d, %d): %zu waypoints%s", x, y, _rota.size(),
            sincronizar ? " (em equipe)" : "");

    return true;
}

bool Navegador::seguir_rota(const LeituraHardware& l) {

    if(!_tem_rota)
        return false;

    if(_idx >= _rota.size())
        return true;

    const Waypoint w = _rota[_idx];

    // Equipe: só avança quando todos alcançaram o waypoint anterior
    if(_sincronizar && !equipe_alcancou((int)_idx)) {
        segurar(l);
        return false;
    }

    if(w.passagem >= 0 && _passagem != w.passagem) {

        uint32_t equipe = _sincronizar ? _equipe : (1u << _ctx->slot);
        int equipe_id = _sincronizar ? _tarefa_id : 0;

        if(!passagem_solicitar(_ctx->shm, w.passagem, _ctx->slot, equipe_id, equipe)) {

            if(_aguardando != w.passagem) {
                _aguardando = w.passagem;
                shm_evento_publicar(_ctx->shm, _ctx->id, EV_PASSAGEM, "aguardando vaga: %s",
                                    mapa_passagem(w.passagem).nome);
                log_msg(_modulo, "aguardando vaga em %s", mapa_passagem(w.passagem).nome);
            }

            segurar(l);
            return false;
        }

        _aguardando = -1;
        _passagem = w.passagem;
        shm_evento_publicar(_ctx->shm, _ctx->id, EV_PASSAGEM, "entrou: %s", mapa_passagem(w.passagem).nome);
    }

    _ctx->hw->escrever_setpoint(w.x, w.y);

    if(l.pos_x != w.x || l.pos_y != w.y)
        return false;

    // Alcançou o waypoint
    if(_passagem >= 0 && w.passagem == _passagem) {
        passagem_sair(_ctx->shm, _passagem, _ctx->slot);
        shm_evento_publicar(_ctx->shm, _ctx->id, EV_PASSAGEM, "saiu: %s", mapa_passagem(_passagem).nome);
        _passagem = -1;
    }

    _idx++;

    if(_sincronizar)
        _progresso = (int)_idx;

    return _idx >= _rota.size();
}

// Todos os colegas de equipe já alcançaram "progresso" waypoints?
bool Navegador::equipe_alcancou(int progresso) {

    for(int s=0;s<MAX_ROBOS;s++) {

        if(!(_equipe & (1u << s)) || s == _ctx->slot)
            continue;

        DadosStatus d;
        shm_slot_ler(_ctx->shm, s, d);

        if(!d.ativo || d.tarefa_id != _tarefa_id || d.progresso < progresso)
            return false;
    }

    return true;
}

bool Navegador::equipe_atracada(void) {

    for(int s=0;s<MAX_ROBOS;s++) {

        if(!(_equipe & (1u << s)) || s == _ctx->slot)
            continue;

        DadosStatus d;
        shm_slot_ler(_ctx->shm, s, d);

        if(!d.ativo || d.tarefa_id != _tarefa_id || !d.carga_atracada)
            return false;
    }

    return true;
}


/* ------------------------------------------------------------------ */
/* Tarefa de transporte                                                */
/* ------------------------------------------------------------------ */

void Navegador::mudar_fase(int fase, const char* detalhe) {

    _fase = fase;

    shm_evento_publicar(_ctx->shm, _ctx->id, EV_TAREFA, "tarefa %d: %s%s%s", _tarefa_id,
                        fase_tarefa_nome(fase), detalhe ? " - " : "", detalhe ? detalhe : "");
    log_msg(_modulo, "tarefa %d: %s%s%s", _tarefa_id, fase_tarefa_nome(fase),
            detalhe ? " - " : "", detalhe ? detalhe : "");

    publicar();
}

void Navegador::cancelar_tarefa(const char* motivo) {

    if(!tarefa_ativa())
        return;

    mudar_fase(FASE_NENHUMA, motivo);

    _tarefa_id = 0;
    _equipe = 0;
    _progresso = 0;
    _sincronizar = false;
}

bool Navegador::iniciar_tarefa(const ComandoRobo& cmd) {

    uint32_t equipe = cmd.equipe | (1u << _ctx->slot);

    if(cmd.tarefa_id <= 0 || __builtin_popcount(equipe) > 4
       || !mapa_dentro(cmd.x, cmd.y) || !mapa_dentro(cmd.x2, cmd.y2)) {
        log_msg(_modulo, "tarefa inválida (id %d, equipe de %d)", cmd.tarefa_id, __builtin_popcount(equipe));
        return false;
    }

    // A equipe precisa de uma rota da coleta até a entrega
    std::vector<Waypoint> teste;

    if(mapa_rota(cmd.x, cmd.y, cmd.x2, cmd.y2, __builtin_popcount(equipe), teste) < 0) {
        log_msg(_modulo, "tarefa %d recusada: sem rota para equipe de %d", cmd.tarefa_id,
                __builtin_popcount(equipe));
        return false;
    }

    cancelar_tarefa("substituída por nova tarefa");
    _tem_destino = false;

    _tarefa_id = cmd.tarefa_id;
    _coleta_x = cmd.x;
    _coleta_y = cmd.y;
    _entrega_x = cmd.x2;
    _entrega_y = cmd.y2;
    _equipe = equipe;
    _progresso = 0;
    _soltou = false;

    char origem[32], destino[32], detalhe[96];
    snprintf(detalhe, sizeof(detalhe), "%s -> %s, equipe de %d",
             nome_local(_coleta_x, _coleta_y, origem, sizeof(origem)),
             nome_local(_entrega_x, _entrega_y, destino, sizeof(destino)),
             __builtin_popcount(_equipe));

    mudar_fase(FASE_IR_COLETA, detalhe);
    _ctx->mudar_estado_operacional(ESTADO_DESLOCANDO, "tarefa: indo para a coleta");

    return true;
}

void Navegador::acoplar(void) {

    if(_fase == FASE_AGUARDANDO_EQUIPE) {
        _ctx->hw->acoplar_e_erguer(true);
        mudar_fase(FASE_ACOPLANDO, "liberado pelo Organizador");
        return;
    }

    if(tarefa_ativa()) {
        log_msg(_modulo, "ACOPLAR ignorado na fase %s", fase_tarefa_nome(_fase));
        return;
    }

    // Sem tarefa (ex.: operação manual): comando direto ao hardware
    _ctx->hw->acoplar_e_erguer(true);
    _acoplado_manual = true;
    _ctx->mudar_estado_operacional(ESTADO_OPERANDO_CARGA, "comando ACOPLAR_E_ERGUER");
}

void Navegador::soltar(void) {

    cancelar_tarefa("comando SOLTAR_CARGA");

    _ctx->hw->acoplar_e_erguer(false);
    _acoplado_manual = false;
    _ctx->mudar_estado_operacional(ESTADO_DISPONIVEL, "comando SOLTAR_CARGA");
}

bool Navegador::ir_para(int x, int y) {

    if(!mapa_dentro(x, y)) {
        log_msg(_modulo, "IR_PARA (%d, %d) fora do mapa", x, y);
        return false;
    }

    cancelar_tarefa("comando IR_PARA");

    if(!tarefa_ativa()) {
        _fase = FASE_NENHUMA;
        _tarefa_id = 0;
    }

    _tem_destino = true;
    _destino_x = x;
    _destino_y = y;

    _ctx->mudar_estado_operacional(ESTADO_DESLOCANDO, "comando IR_PARA");

    return true;
}

void Navegador::parar(void) {

    cancelar_tarefa("comando PARAR");

    if(_passagem >= 0 && _tem_rota && _idx < _rota.size()) {
        // Não para dentro de uma passagem estreita: termina a travessia
        _tem_destino = true;
        _destino_x = _rota[_idx].x;
        _destino_y = _rota[_idx].y;
        return;
    }

    _tem_destino = false;
    _tem_rota = false;
    sair_da_fila();

    LeituraHardware l = _ctx->hw->ler();
    segurar(l);
    _ctx->definir_destino(l.pos_x, l.pos_y);

    if(_ctx->estado() == ESTADO_DESLOCANDO)
        _ctx->mudar_estado_operacional(ESTADO_DISPONIVEL, "comando PARAR");
}

void Navegador::liberar(void) {

    passagens_liberar_slot(_ctx->shm, _ctx->slot);
    _passagem = -1;
    _aguardando = -1;
    publicar();
}


/* ------------------------------------------------------------------ */
/* Ciclo periódico                                                     */
/* ------------------------------------------------------------------ */

void Navegador::ciclo(void) {

    LeituraHardware l = _ctx->hw->ler();
    InstantaneoRobo s = _ctx->instantaneo();

    // Status bloqueou o movimento (falha, içamento...): parado e fora das filas,
    // para não segurar a vez de quem está atrás. Vagas já ocupadas continuam.
    if(s.movimento_bloqueado) {
        segurar(l);
        sair_da_fila();
        publicar();
        return;
    }

    // Destino forçado pelo Status (ex.: recarga) tem prioridade
    if(s.tem_destino_forcado) {
        if(_fase == FASE_AGUARDANDO_EQUIPE)
            mudar_fase(FASE_IR_COLETA, "desviado pelo Status; volta para a coleta depois");

        if(rota_para(s.forcado_x, s.forcado_y, false))
            seguir_rota(l);
        publicar();
        return;
    }

    char local[32];

    switch(_fase) {

        case FASE_IR_COLETA:
            if(!rota_para(_coleta_x, _coleta_y, false)) {
                mudar_fase(FASE_FALHOU, "sem rota até a coleta");
                _ctx->mudar_estado_operacional(ESTADO_DISPONIVEL, "tarefa falhou");
            } else if(seguir_rota(l)) {
                mudar_fase(FASE_AGUARDANDO_EQUIPE, nome_local(_coleta_x, _coleta_y, local, sizeof(local)));
                _ctx->mudar_estado_operacional(ESTADO_OPERANDO_CARGA, "na coleta, aguardando equipe");
            }
            break;

        case FASE_AGUARDANDO_EQUIPE:
            segurar(l);     // espera o CMD_ACOPLAR_E_ERGUER do Organizador
            break;

        case FASE_ACOPLANDO:
            segurar(l);
            if(l.carga_atracada && equipe_atracada()) {
                shm_evento_publicar(_ctx->shm, _ctx->id, EV_CARGA, "pegou carga na %s (tarefa %d)",
                                    nome_local(_coleta_x, _coleta_y, local, sizeof(local)), _tarefa_id);
                _progresso = 0;
                mudar_fase(FASE_TRANSPORTANDO, "equipe toda atracada");
            }
            break;

        case FASE_TRANSPORTANDO:
            if(!rota_para(_entrega_x, _entrega_y, true)) {
                mudar_fase(FASE_FALHOU, "sem rota para a equipe até a entrega");
            } else if(seguir_rota(l)) {
                mudar_fase(FASE_ENTREGANDO, nome_local(_entrega_x, _entrega_y, local, sizeof(local)));
            }
            break;

        case FASE_ENTREGANDO:
            segurar(l);
            if(!_soltou && equipe_alcancou(_progresso)) {
                _ctx->hw->acoplar_e_erguer(false);
                _soltou = true;
            }
            if(_soltou && !l.carga_atracada) {
                shm_evento_publicar(_ctx->shm, _ctx->id, EV_CARGA, "entregou carga na %s (tarefa %d)",
                                    nome_local(_entrega_x, _entrega_y, local, sizeof(local)), _tarefa_id);
                mudar_fase(FASE_CONCLUIDA, NULL);
                _ctx->mudar_estado_operacional(ESTADO_DISPONIVEL, "tarefa concluída");
            }
            break;

        default:    // sem tarefa: destino simples (IR_PARA)
            if(!_tem_destino)
                break;

            if(!rota_para(_destino_x, _destino_y, false)) {
                log_msg(_modulo, "sem rota até (%d, %d)", _destino_x, _destino_y);
                _tem_destino = false;
                _ctx->mudar_estado_operacional(ESTADO_DISPONIVEL, "sem rota");
            } else if(seguir_rota(l)) {
                _tem_destino = false;
                shm_evento_publicar(_ctx->shm, _ctx->id, EV_CHEGOU_DESTINO, "chegou em (%d, %d)",
                                    _destino_x, _destino_y);
                if(_ctx->estado() == ESTADO_DESLOCANDO)
                    _ctx->mudar_estado_operacional(ESTADO_DISPONIVEL, "chegou ao destino");
            }
            break;
    }

    publicar();
}
