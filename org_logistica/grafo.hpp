#ifndef GRAFO_HPP
#define GRAFO_HPP

#include "pthread.h"
#include "math.h"
#include <vector>
#include <iostream>
#include <mutex>
#include <vector>
#include <unordered_map>
#include <queue>
#include <string>
#include <semaphore.h>

class Node {

    protected:

        int _id;
        float _pos_x;
        float _pos_y;

        std::vector<Node*> _ngb; // vizinhos

        bool locked;

        std::mutex m;

    public:

        Node(int id, float pos_x, float pos_y);

        int get_id(void);

        std::pair<float,float> get_location(void);

        std::vector<Node*>* get_ngb(void);

        int add_ngb(Node* node);

        int rmv_ngb(Node* node);
        
        bool is_on_x_pos(float pos_x);

        bool is_on_y_pos(float pos_y);

        bool is_locked(void);

        void set_locked(void);

        void set_free(void);

        float get_pos_x(void);

        float get_pos_y(void);

};

std::vector<Node*> dijkstra(Node* origem, Node* destino);

#endif