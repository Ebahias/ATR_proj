#include "robo_manager_threads.hpp"

void organizador_de_logistica_thread(void* arg) {

    Robo_manager_arg_t* manager_arg = (Robo_manager_arg_t*) arg;

    Robo_manager* manager = manager_arg->manager;
    Baia_carga* b_carga = manager_arg->b_carga;


    while(true) {

        Carga carga = b_carga->cargo_request_unload();
        
        std::vector<Robo*> assign_robo;

        for(int i=0;i<carga.get_type();i++) {

            Robo* av_robo = manager->get_avaiable_robo();
            
            if(av_robo != nullptr) 
                assign_robo.push_back(av_robo);
        }

        pthread_t thread_assign;

        Robo_assign_arg_t assign_arg;

        assign_arg.assign_robo = assign_robo;
        assign_arg.carga = &carga;
        assign_arg.manager = manager;

        pthread_create( &thread_assign,
                        NULL,
                        Robo_manager_assign_robo_thread,
                        &assign_arg);


    }
}

void Robo_manager_process_cargo_thread(void* arg);

void Robo_manager_assign_robo_thread(void* arg) {

    Robo_assign_arg_t* assign_arg = (Robo_assign_arg_t*) arg;

    Baia_carga* baia = assign_arg->b_carga;

    std::vector<Robo*> ass_robo = assign_arg->assign_robo;

    Robo_manager* manager = assign_arg->manager;

    Carga cargo;

    memcpy(&cargo, assign_arg->carga, sizeof(Carga));

    sem_t sem;

    sem_init(&sem, 0, 0);

    for(int i=0;i<cargo.get_type();i++) {
        
        send_robo_to_position(ass_robo[i], manager->get_nearest_node(ass_robo[i]), cargo._node_id[i]);

    }

    for(int i=0;i<cargo.get_type();i++) {
        sem_wait(sem);
    }

    for(auto r : ass_robo) 
        r->lift();
    
    std::vector<Node*> nodes_bd;

    Baia_descarga* b_d = nullptr;

    for(int i=0;i<cargo.get_type();i++) {

        Node* node;

        for(auto bd : manager->_b_descarga) {
            
            node = bd->receive_cargo(cargo.get_type());
            
            if(node != nullptr) {

                nodes_bd.push_back(node);
                b_d = bd;
                break;
            }
        }
    }

     for(int i=0;i<cargo.get_type();i++) {
        
        send_robo_to_position(ass_robo[i], manager->get_nearest_node(ass_robo[i]), nodes_bd[i]);

    }


    for(int i=0;i<cargo.get_type();i++) {
        sem_wait(sem);
    }

    for(auto n : nodes_bd) {
        b_d->free_node(n);
    }


}

void init_Robo_manager_threads(Robo_manager_arg_t* Manager) {

    pthread_t thread_manager;

    pthread_create( &thread_manager,
                    NULL,
                    organizador_de_logistica_thread,
                    Manager);
    
    return;
}
