#include "robo_manager_threads.hpp"
#include "comum/log.hpp"

#include <boost/asio.hpp>

void* organizador_de_logistica_thread(void* arg) {

    Robo_manager_arg_t* manager_arg = (Robo_manager_arg_t*) arg;

    Robo_manager* manager = manager_arg->manager;
    Baia_carga* b_carga = manager_arg->b_carga;

    std::vector<Carga> rcv_carga;

    while(true) {

        b_carga->pub_cargo();

        Carga carga = b_carga->cargo_request_unload();
        
        log_msg("ORGANIZADOR", "CARGA CHEGOU EM B CARGA DE TAMANHO %d em BAIA %d", carga.get_type(), b_carga->get_id());

        rcv_carga.push_back(carga);

        // Faz ponteiro dinamico da carga
        Carga* p_carga = new Carga(carga.get_type());

        for(auto c : carga._node) {
            p_carga->_node.push_back(c);
        }

        // ================================

        Robo_assign_arg_t* assign_arg = new Robo_assign_arg_t;

        boost::asio::io_context io;

        while(assign_arg->assign_robo.size() < p_carga->get_type()) {
        
            for(int i=0;i<p_carga->get_type();i++) {

                Robo* av_robo = manager->get_avaiable_robo();

                if(av_robo != nullptr) {
                    av_robo->_status  = GOING_TO_BAIA_CARGA;
                    assign_arg->assign_robo.push_back(av_robo);
                }
            }

            if(assign_arg->assign_robo.size() < p_carga->get_type()) {
                
                boost::asio::steady_timer tout(io, boost::asio::chrono::seconds(30));
                tout.wait();

            }
        }

        pthread_t thread_assign;

        assign_arg->carga = p_carga;
        assign_arg->manager = manager;
        assign_arg->b_carga = b_carga;

        pthread_create( &thread_assign,
                        NULL,
                        Robo_manager_assign_robo_thread,
                        assign_arg);

    }
}

void* Robo_manager_process_cargo_thread(void* arg);

void* Robo_manager_assign_robo_thread(void* arg) {

    Robo_assign_arg_t* assign_arg = (Robo_assign_arg_t*) arg;

    Baia_carga* baia = assign_arg->b_carga;

    std::vector<Robo*> ass_robo = assign_arg->assign_robo;

    Robo_manager* manager = assign_arg->manager;

    Carga* cargo = assign_arg->carga;

    std::cout<< ass_robo.size() << std::endl;

    int tarefa = manager->get_new_tarefa();

    sem_t sem;

    sem_init(&sem, 0, 0);

    std::vector<int> equipe;

    for(auto v : ass_robo) {

        equipe.push_back(v->get_id());

    }

    for(int i=0;i<cargo->get_type();i++) {
        send_robo_to_position(ass_robo[i], cargo->_node[i], &sem, equipe, tarefa);
    }

    for(int i=0;i<cargo->get_type();i++) {
        sem_wait(&sem);
    }

    std::cout<<"ok!\n";

    for(auto r : ass_robo) 
        r->lift();
    
    std::vector<Node*> nodes_bd;


    Baia_descarga* b_d = nullptr;

    for(int i=0;i<cargo->get_type();i++) {

        Node* node;

        for(auto bd : manager->_b_descarga) {
            
            node = bd->receive_cargo(cargo->get_type());
            
            if(node != nullptr) {

                nodes_bd.push_back(node);
                b_d = bd;
                break;
            }
        }
    }

    tarefa = manager->get_new_tarefa();

    for(int i=0;i<cargo->get_type();i++) {

        send_robo_to_position(ass_robo[i], nodes_bd[i], &sem, equipe, tarefa);

    }


    for(int i=0;i<cargo->get_type();i++) {
        sem_wait(&sem);
    }

    for(auto n : nodes_bd) {
        b_d->free_node(n);
    }

    delete[] assign_arg;
}

void* Robo_manager_new_robo_thread(void* arg) {

    Robo_manager* manager = (Robo_manager*) arg;

    mqd_t mq = mq_gestor_abrir_leitura();

    
    if(mq == (mqd_t)-1)
        return NULL;

    log_msg("ESCUTA", "aguardando eventos em %s", MQ_GESTOR_ORGANIZADOR);

    EventoGestor ev;

    while(mq_gestor_receber(mq, ev) == 0) {

        if(ev.tipo == EVENTO_ROBO_NOVO) {
            log_msg("ESCUTA", "%s robo=%d pid=%d pos=(%d, %d) slot=%d t=%ld.%09ld",
                    evento_gestor_nome(ev.tipo), ev.robo_id, ev.pid, ev.pos_x, ev.pos_y, ev.slot,
                    (long)ev.timestamp.tv_sec, ev.timestamp.tv_nsec);


            Robo* robo_novo = new Robo(ev.robo_id, ev.pos_x, ev.pos_y, ev.pid);

            manager->add_robo(robo_novo);
            
            std::cout << "[THREAD NOVOS ROBOS] -> Novo Robo" << ev.robo_id <<" "<< ev.pos_x <<" "<< ev.pos_y << std::endl;

        } else {
            log_msg("ESCUTA", "%s robo=%d pid=%d %s=%d slot=%d t=%ld.%09ld",
                    evento_gestor_nome(ev.tipo), ev.robo_id, ev.pid,
                    WIFSIGNALED(ev.status_saida) ? "sinal" : "codigo",
                    WIFSIGNALED(ev.status_saida) ? WTERMSIG(ev.status_saida) : WEXITSTATUS(ev.status_saida),
                    ev.slot,
                    (long)ev.timestamp.tv_sec, ev.timestamp.tv_nsec);
        }
    }

    mq_close(mq);
}

void init_Robo_manager_threads(Robo_manager_arg_t* Manager) {

    pthread_t thread_manager, thread_novo_robo;

    pthread_create( &thread_manager,
                    NULL,
                    organizador_de_logistica_thread,
                    Manager);

    // pthread_create( &thread_novo_robo,
                    // NULL,
                    // Robo_manager_new_robo_thread,
                    // Manager->manager);
    // 
    pthread_join(thread_manager, NULL);        

    return;
}
