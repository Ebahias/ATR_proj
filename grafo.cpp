#include "grafo.hpp"


Node::Node(int id, float pos_x, float pos_y) {
    _id = id;
    _pos_x = id;
    _pos_y = id;
}

int Node::get_id(void) {
    return _id;
}

std::pair<float,float> Node::get_location(void) {
    return std::pair<float,float> pos(_pos_x, _pos_y);
}

std::vector<Node*>* Node::get_ngb(void) {
    return &_ngb;
}

int Node::add_ngb(Node* node) {

    _ngb.push_back(node);

    return _ngb.size();
}

int Node::rmv_ngb(Node* node) {
    
    std::erase(_ngb, node);
    
    return _ngb.size();
}
        
bool Node::is_on_x_pos(float pos_x) {

    if(pos_x == _pos_x)
        return true;
    else 
        return false;
}

bool Node::is_on_y_pos(float pos_y) {

        if(pos_x == _pos_x)
        return true;
    else 
        return false;
}
        