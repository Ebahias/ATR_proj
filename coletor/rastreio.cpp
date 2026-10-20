#include "coletor/rastreio.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

static const char PREFIXO_PEGOU[]   = "pegou carga na ";
static const char PREFIXO_ENTREGOU[] = "entregou carga na ";
static const char SEPARADOR_TAREFA[] = " (tarefa ";

bool Rastreio::interpretar(const char* texto, bool& coleta, std::string& local, int& tarefa) {

    const char* inicio;

    if(strncmp(texto, PREFIXO_PEGOU, sizeof(PREFIXO_PEGOU) - 1) == 0) {
        coleta = true;
        inicio = texto + sizeof(PREFIXO_PEGOU) - 1;
    } else if(strncmp(texto, PREFIXO_ENTREGOU, sizeof(PREFIXO_ENTREGOU) - 1) == 0) {
        coleta = false;
        inicio = texto + sizeof(PREFIXO_ENTREGOU) - 1;
    } else {
        return false;
    }

    // O local pode ser "Baia 3" ou, fora de uma área do mapa, "(15, 15)"
    const char* fim = strstr(inicio, SEPARADOR_TAREFA);

    if(!fim || fim == inicio)
        return false;

    char* resto;
    long id = strtol(fim + sizeof(SEPARADOR_TAREFA) - 1, &resto, 10);

    if(resto == fim + sizeof(SEPARADOR_TAREFA) - 1 || *resto != ')')
        return false;

    local.assign(inicio, fim - inicio);
    tarefa = (int)id;

    return true;
}

static bool contem(const std::vector<int>& v, int x) {
    return std::find(v.begin(), v.end(), x) != v.end();
}

bool Rastreio::processar(const EventoRobo& ev, RegistroCarga& pronto) {

    if(ev.tipo != EV_CARGA)
        return false;

    bool coleta;
    std::string local;
    int tarefa;

    if(!interpretar(ev.texto, coleta, local, tarefa))
        return false;

    auto it = _cargas.find(tarefa);

    if(coleta) {

        if(it == _cargas.end()) {
            Carga c;
            c.r.tarefa_id = tarefa;
            c.r.baia = local;
            c.r.t_coleta = ev.timestamp;
            memset(&c.r.t_entrega, 0, sizeof(c.r.t_entrega));
            c.r.coleta_registrada = true;
            c.r.completa = false;
            it = _cargas.emplace(tarefa, c).first;
        }

        if(!contem(it->second.r.robos, ev.robo_id))
            it->second.r.robos.push_back(ev.robo_id);

        return false;
    }

    // Entrega. Se o evento de coleta foi perdido (buffer cheio), registra mesmo assim.
    if(it == _cargas.end()) {
        Carga c;
        c.r.tarefa_id = tarefa;
        c.r.baia = "?";
        memset(&c.r.t_coleta, 0, sizeof(c.r.t_coleta));
        c.r.coleta_registrada = false;
        c.r.completa = false;
        c.r.robos.push_back(ev.robo_id);
        it = _cargas.emplace(tarefa, c).first;
    } else if(!contem(it->second.r.robos, ev.robo_id)) {
        it->second.r.robos.push_back(ev.robo_id);   // coleta deste robô foi perdida
    }

    Carga& c = it->second;

    if(!contem(c.entregues, ev.robo_id))
        c.entregues.push_back(ev.robo_id);

    c.r.destino = local;
    c.r.t_entrega = ev.timestamp;

    if(c.entregues.size() < c.r.robos.size())
        return false;

    c.r.completa = true;
    pronto = c.r;
    std::sort(pronto.robos.begin(), pronto.robos.end());    // ordem crescente de id
    _cargas.erase(it);

    return true;
}

std::vector<RegistroCarga> Rastreio::pendentes(void) const {

    std::vector<RegistroCarga> v;

    for(const auto& par : _cargas) {
        v.push_back(par.second.r);
        std::sort(v.back().robos.begin(), v.back().robos.end());
    }

    return v;
}
