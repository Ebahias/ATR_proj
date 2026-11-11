#include "robo_threads.hpp"

#include <time.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>

#include "comum/log.hpp"

#define PERIODO_STATUS_ROBO_S 1

void* robo_thread(void* arg) {

    robo_thread_arg_t* robo_arg = (robo_thread_arg_t*)arg;

    Robo* robo = robo_arg->robo;

    // nanosleep no lugar do steady_timer do Boost: o Boost não está
    // instalado em todas as máquinas do grupo e só servia para dormir.
    struct timespec intervalo = { PERIODO_STATUS_ROBO_S, 0 };

    log_msg("ROBO THREAD", "ROBO %d SE DIRIGINDO ATE %f %f", robo->get_id(), robo_arg->final_pos->get_pos_x(), robo_arg->final_pos->get_pos_y());
    
    while(true) {

        nanosleep(&intervalo, NULL);

        robo->update_status();

        if(robo->get_pos_x() == robo_arg->final_pos->get_pos_x() && robo->get_pos_y() == robo_arg->final_pos->get_pos_y()) {
            break;
        }
    }
    
    sem_post(robo_arg->sem);
    
    delete robo_arg;

    return nullptr;
}

void send_robo_to_position(Robo* robo, Node* f_node, sem_t* sem, std::vector<int>& equipe, int tarefa) {

    pthread_t thread_robo;

    // cria o ponteiro (lembrar de deletar!!!!)
    robo_thread_arg_t* robo_arg = new robo_thread_arg_t;

    robo_arg->robo = robo;
    robo_arg->sem = sem;
    robo_arg->final_pos = f_node;
    
    //Manda o comando tarefa <tarefa_id> <cx> <cy> <ex> <ey> [--equipe <id>,<id>,...] 

    std::string id_s = std::to_string(robo->get_id());
    std::string tarefa_s = std::to_string(tarefa);
    
    std::string i_x = std::to_string((int)robo->get_pos_x());
    std::string i_y = std::to_string((int)robo->get_pos_y());

    std::string f_x = std::to_string((int)f_node->get_pos_x());
    std::string f_y = std::to_string((int)f_node->get_pos_y());

    // -----------
    pid_t pid = fork();

    if(pid < 0) {
        log_msg("SEND ROBO", "FORK COM ERRO");
    }
    if(pid == 0) {

        execlp("./bin/comando_robo",
        "comando",
        id_s.c_str(),
        "ir",
        // tarefa_s.c_str(),
        // i_x.c_str(),
        // i_y.c_str(),
        f_x.c_str(),
        f_y.c_str(),
        // "--equipe",
        // equipe_s,
        (char*)NULL);

        log_msg("SEND ROBO", "COMANDO CRIADO");

        _exit(1);
    }

    int status;
    waitpid(pid, &status, 0);

    pthread_create( &thread_robo,
                    NULL,
                    robo_thread,
                    robo_arg);
}
