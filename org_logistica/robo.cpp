#include "robo.hpp"
#include <stdlib.h>
#include "comum/log.hpp"


Robo::Robo(int id, float initial_x, float initial_y) {

    _id = id;
    _pos_x = initial_x;
    _pos_y = initial_y;

    _status = AVAIABLE;
}

Robo::Robo(int id, float initial_x, float initial_y, int pid) {

    _id = id;
    _pos_x = initial_x;
    _pos_y = initial_y;
    _pid = pid;

    _status = AVAIABLE;

}

Robo::Robo(void) {
}

robo_status_enum Robo::get_status(void) {

    return _status;
}

int Robo::get_id(void) {

    return _id;
}

// Implementado via IPC
Node* Robo::wait_for_arrival(void) {

    return nullptr;
}

// Implementado via IPC
int Robo::send_path(std::vector<Node*> path) {

    return 0;
}

float Robo::get_pos_x(void) {
    return _pos_x;
}

float Robo::get_pos_y(void) {
    return _pos_y;
}


int Robo::lift(void) {
    return 0;
}
int Robo::go_to_pos(Node* i_node, Node* f_node) {

    Node* anterior_Node = i_node;

    std::vector<Node*> path = dijkstra(i_node, f_node);

    this->send_path(path);

    while(true) {

        Node* current_node = this->wait_for_arrival();

        anterior_Node->set_free();

        anterior_Node = current_node;

        if(current_node == f_node)
            break;
    }
    
    return 0;
}

int Robo::update_status(void) {

    // Acessa memória da frota
    ShmFrota* shm = shm_frota_abrir();

    if(!shm)
        return -1;

    for(int s=0;s<MAX_ROBOS;s++) {
        DadosStatus d;
        shm_slot_ler(shm, s, d);
        
        if(d.robo_id == _id) {

            _pos_x = d.pos_x;
            _pos_y = d.pos_y;
            _modo = d.modo;
            _ativo = d.ativo;
            _fase = d.fase;
            _bateria = d.bateria;
            _estado = d.estado;


        }
    }

    shm_frota_fechar(shm);

    log_msg("ROBO", "ROBO %d -> X: %f Y: %f FASE: %d BATERIA: %d ESTADO: %D",
            _pos_x,
            _pos_y,
            _fase,
            _bateria,
            _estado);
            
    return 0;
}

int Robo::get_fase(void) {


    return _fase;
}

