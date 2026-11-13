#include "robo.hpp"
#include "despacho.hpp"
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
int Robo::send_path(std::vector<Node*>) {

    return 0;
}

float Robo::get_pos_x(void) {
    return _pos_x;
}

float Robo::get_pos_y(void) {
    return _pos_y;
}


// Liberar a carga: o robô só acopla se estiver em FASE_AGUARDANDO_EQUIPE
int Robo::lift(void) {
    return despacho_acoplar(_id) ? 0 : -1;
}

// Soltar a carga e sair da tarefa (CMD_SOLTAR_CARGA). Não precisa ser
// chamado ao fim de uma missão: com CMD_TAREFA o robô solta a carga sozinho
// quando a equipe toda chega à entrega. Serve para abortar uma missão com
// a carga atracada (como faz abortar_robos) ou para comando direto.
int Robo::release(void) {
    return despacho_soltar(_id) ? 0 : -1;
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

// Atualiza a cópia local a partir da /atr_frota. O despacho mantém a
// memória aberta desde o início do processo: abrir e fechar a cada leitura
// custava um shm_open + mmap + munmap por robô por chamada.
int Robo::update_status(void) {

    DadosStatus d;

    if(!despacho_ler_status(_id, d)) {
        _ativo = 0;
        return -1;
    }

    _pos_x = d.pos_x;
    _pos_y = d.pos_y;
    _modo = d.modo;
    _ativo = d.ativo;
    _fase = d.fase;
    _bateria = d.bateria;
    _estado = d.estado;

    log_msg("ROBO", "ROBO %d -> X: %.0f Y: %.0f FASE: %s BATERIA: %d ESTADO: %s",
            _id,
            _pos_x,
            _pos_y,
            fase_tarefa_nome(_fase),
            _bateria,
            estado_robo_nome(_estado));

    return 0;
}

int Robo::get_fase(void) {


    return _fase;
}

