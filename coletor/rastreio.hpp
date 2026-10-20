#ifndef COLETOR_RASTREIO_HPP
#define COLETOR_RASTREIO_HPP

#include "comum/ipc_robo.hpp"

#include <map>
#include <string>
#include <time.h>
#include <vector>

/*
 * Rastreabilidade das cargas: a partir dos eventos EV_CARGA dos robôs
 * ("pegou carga na Baia 3 (tarefa 42)" / "entregou carga na Zona C (tarefa 42)"),
 * descobre qual baia, qual destino e quais robôs carregaram cada carga.
 *
 * Classe pura (sem I/O e sem threads): só a thread de gravação do Coletor a usa.
 */

struct RegistroCarga {
    int tarefa_id;
    std::string baia;               // "?" se o evento de coleta foi perdido
    std::string destino;
    std::vector<int> robos;         // quem carregou, na ordem em que pegou
    struct timespec t_coleta;       // zerado se o evento de coleta foi perdido
    struct timespec t_entrega;      // zerado se ainda não foi entregue
    bool coleta_registrada;
    bool completa;                  // todos os robôs que pegaram já entregaram
};

class Rastreio {

    private:

        struct Carga {
            RegistroCarga r;
            std::vector<int> entregues;     // robôs que já soltaram a carga no destino
        };

        std::map<int, Carga> _cargas;       // por tarefa_id, só as ainda em transporte

    public:

        // Interpreta o texto de um EV_CARGA. Retorna false se não for reconhecido.
        static bool interpretar(const char* texto, bool& coleta, std::string& local, int& tarefa);

        // Processa um evento. Retorna true e preenche 'pronto' quando a entrega
        // da carga se completou (e a remove do rastreio).
        bool processar(const EventoRobo& ev, RegistroCarga& pronto);

        // Cargas pegas que ainda não foram entregues (ex.: robô morreu no caminho).
        std::vector<RegistroCarga> pendentes(void) const;
};

#endif
