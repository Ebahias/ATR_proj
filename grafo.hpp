#ifndef GRAFO_HPP
#define GRAFO_HPP

#include "pthread.h"
#include "math.h"
#include <vector>
#include <iostream>

class Node {

    protected:

        int _id;
        float _pos_x;
        float _pos_y;

        std::vector<Node*> _ngb; // vizinhos

    public:

        Node(int id, float pos_x, float pos_y);

        int get_id(void);

        std::pair<float,float> get_location(void);

        std::vector<Node*>* get_ngb(void);

        int add_ngb(Node* node);

        int rmv_ngb(Node* node);
        
        bool is_on_x_pos(float pos_x);

        bool is_on_y_pos(float pos_y);

};


#endif