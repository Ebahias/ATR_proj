#include "org_logistica_init.hpp"


int org_logistica_init_gestor(void) {

    pid_t pid_gestor = fork();

    if(pid_gestor < 0) 
        return -1;

    if(pid_gestor == 0) {

          execlp("gnome-terminal",
           "gnome-terminal",
           "--",
           "./bin/gestor_robos",
           (char*)NULL);

     }

    // TO DO : Conseguir fazer com que o gestor encerre com o fim do org_logistica

     return 0;
}