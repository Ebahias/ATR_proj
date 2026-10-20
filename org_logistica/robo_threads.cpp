#include "robo_threads.hpp"

void robo_thread(void* arg) {

    robo_thread_arg_t* robo_arg = (robo_thread_arg_t*)arg;
    
    Node* anterior_Node = robo_arg->init_pos;

    std::vector<Node*> path = dijkstra(robo_arg->init_pos, robo_arg->final_pos);

    robo_arg->robo->send_path(path);

    while(true) {

        Node* current_node = robo_arg->robo->wait_for_arrival();

        anterior_Node->set_free();

        anterior_Node = current_node;

        if(current_node == robo_arg->final_pos)
            break;
    }
    
    sem_post(&robo_arg->sem);
}

void send_robo_to_position(Robo* robo, Node* i_node, Node* f_node, sem_t* sem) {

    pthread_t thread_robo;

    robo_thread_arg_t robo_arg = {0};

    robo_arg.robo = robo;
    robo_arg.init_pos = i_node;
    robo_arg.final_pos = f_node;
    robo_arg.sem = sem;

    pthread_create( &thread_robo,
                    NULL,
                    robo_thread,
                    &robo_arg);
}
