#ifndef ROBO_HPP 
#define ROBO_HPP


#include "carga.hpp"
#include "grafo.hpp"
#include "comum/ipc_robo.hpp"

typedef enum {

    AVAIABLE,
    GOING_TO_BAIA_CARGA,
    GOING_TO_BAIA_DESCARGA,
    GOING_TO_ZONA_RECARGA,
    CHARGING

}robo_status_enum;

class Robo {

    protected:

        int _id;
        
        float _pos_x;
        float _pos_y;

        sem_t proceed_sem;

        std::mutex m;

        int _estado;

        int _modo;

        int _ativo;

        int _bateria;

        int _fase;

    public:

        robo_status_enum _status;

        pid_t _pid;

        // Reservado para uma missão (ou à espera de começar uma). Escrito e
        // lido só com o mutex do Robo_manager: é o que impede que a mesma
        // instância entre em duas equipes ao mesmo tempo.
        bool _reservado = false;

        // O Gestor avisou que o processo do robô terminou (ROBO_REMOVIDO).
        // A instância não é apagada porque uma missão ainda pode ter o
        // ponteiro; só deixa de ser escolhida. Protegido pelo mesmo mutex.
        bool _removido = false;

        Robo(void);

        Robo(int id, float initial_x, float initial_y);

        Robo(int id, float initial_x, float initial_y, int pid);

        robo_status_enum get_status(void);

        Node* wait_for_arrival(void);

        int send_path(std::vector<Node*> path);

        int go_to_pos(Node* i_node, Node* f_node);

        int get_id(void);

        float get_pos_x(void);

        float get_pos_y(void);

        int lift(void);

        // Função que atualiza estados a partir do IPC
        int update_status(void);

        int get_fase(void);
};

#endif