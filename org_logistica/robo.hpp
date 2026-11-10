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

        robo_status_enum _status;

        sem_t proceed_sem;

        std::mutex m;

        int _estado;

        int _modo;

        int _ativo;

        int _bateria;

        int _fase;

    public:

        pid_t _pid;                  

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