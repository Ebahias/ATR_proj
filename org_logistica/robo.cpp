#include "robo.hpp"
#include <stdlib.h>
Robo::Robo(int id, float initial_x, float initial_y) {

    _id = id;
    _pos_x = initial_x;
    _pos_y = initial_y;

    _status = AVAIABLE;
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
