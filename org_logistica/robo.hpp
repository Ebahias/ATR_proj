#ifndef ROBO_HPP 
#define ROBO_HPP


#include "carga.hpp"
#include "grafo.hpp"

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

    public:

        Robo(int id, float initial_x, float initial_y);

        robo_status_enum get_status(void);

        Node* wait_for_arrival(void);

        int send_path(std::vector<Node*> path);

        int go_to_pos(Node* i_node, Node* f_node);

        int get_id(void);

        float get_pos_x(void);

        float get_pos_y(void);

        int lift(void);

};

#endif