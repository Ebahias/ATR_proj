#include "org_logistica_init.hpp"
#include <fstream>

// Define para gerar um terminal separado para o gestor de robôs.
// Se for utilizar, retirar o comemntário abaixo
// #define GESTOR_NOVO_TERMINAL

pid_t org_logistica_init_gestor(void) {

    pid_t pid_gestor = fork();

    if(pid_gestor < 0) 
        return (pid_t)-1;

    #ifdef GESTOR_NOVO_TERMINAL

    if(pid_gestor == 0) {

          execlp(
                "gnome-terminal",
                "gnome-terminal",
                "--",
                "bash",
                "-c",
                "echo $$ > /tmp/gestor_robos.pid; exec ./bin/gestor_robos",
                (char*)NULL
        );


     }
    
    #endif

    #ifndef GESTOR_NOVO_TERMINAL
          execlp(
                "./bin/gestor_robos",
                "gestor",
                (char*)NULL
        );

    #endif

     return pid_gestor;
}

void org_logistica_close_gestor(pid_t pid_gestor) {

    #ifdef GESTOR_NOVO_TERMINAL

    std::ifstream file("/tmp/gestor_robos.pid");

    pid_t pid;

    if (!(file >> pid)) {
        return;
    }

    file.close();

    // Solicita encerramento do gestor
    kill(pid, SIGTERM);

    #endif

    #ifndef GESTOR_NOVO_TERMINAL
    
    kill(pid_gestor, SIGTERM);
    
    #endif

}
