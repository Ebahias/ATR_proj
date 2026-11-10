#include <iostream>
#include "carga.hpp"
#include "grafo.hpp"
#include "b_carga.hpp"
#include "b_descarga.hpp"
#include "z_recarga.hpp"
#include "robo_manager.hpp"
#include "robo.hpp"
#include "grafo_armazem.hpp"
#include <unistd.h>

#include "robo_manager_threads.hpp"
#include "robo_threads.hpp"
#include "b_carga_threads.hpp"

#include "comum/mapa.hpp"

int main(void) {

    pid_t pid_gestor = fork();

    if(pid_gestor == 0) {
        
          execlp("gnome-terminal",
           "gnome-terminal",
           "--",
           "./bin/gestor_robos",
           (char*)NULL);

    }

    
    // Criação do Grafo do Armazem

    std::vector<Node*> grafo = get_grafo_armazem("coordenadas.txt");

    // ===========================

    std::vector<Node*> nodes_b1;

    nodes_b1.push_back(grafo[0]);
    nodes_b1.push_back(grafo[1]);
    nodes_b1.push_back(grafo[2]);
    nodes_b1.push_back(grafo[3]);

    Baia_carga b1(0, 4, nodes_b1);

    

    std::vector<Node*> nodes_b2;
    
    nodes_b2.push_back(grafo[4]);
    nodes_b2.push_back(grafo[5]);
    nodes_b2.push_back(grafo[6]);
    nodes_b2.push_back(grafo[7]);

    Baia_carga b2(1, 4, nodes_b2);

    

    std::vector<Node*> nodes_b3;
    
    nodes_b3.push_back(grafo[8]);
    nodes_b3.push_back(grafo[9]);
    nodes_b3.push_back(grafo[10]);
    nodes_b3.push_back(grafo[11]);

    Baia_carga b3(2, 4, nodes_b3);
    

    std::vector<Node*> nodes_b4;
    
    nodes_b4.push_back(grafo[12]);
    nodes_b4.push_back(grafo[13]);
    nodes_b4.push_back(grafo[14]);
    nodes_b4.push_back(grafo[15]);

    Baia_carga b4(3, 4, nodes_b4);
    

    std::vector<Node*> nodes_d1;

    nodes_d1.push_back(grafo[16]);
    nodes_d1.push_back(grafo[17]);
    nodes_d1.push_back(grafo[18]);
    nodes_d1.push_back(grafo[19]);

    Baia_descarga d1(0, 4, nodes_d1);
    

    std::vector<Node*> nodes_d2;
    
    nodes_d2.push_back(grafo[20]);
    nodes_d2.push_back(grafo[21]);
    nodes_d2.push_back(grafo[22]);
    nodes_d2.push_back(grafo[23]);

    Baia_descarga d2(1, 4, nodes_d2);
    

    std::vector<Node*> nodes_d3;
    
    nodes_d3.push_back(grafo[24]);
    nodes_d3.push_back(grafo[25]);
    nodes_d3.push_back(grafo[26]);
    nodes_d3.push_back(grafo[27]);

    Baia_descarga d3(2, 4, nodes_d3);
    

    std::vector<Node*> nodes_d4;
    
    nodes_d4.push_back(grafo[28]);
    nodes_d4.push_back(grafo[29]);
    nodes_d4.push_back(grafo[30]);
    nodes_d4.push_back(grafo[31]);

    Baia_descarga d4(3, 4, nodes_d4);
    

    std::vector<Baia_carga*> b_carga_list;

    b_carga_list.push_back(&b1);
    b_carga_list.push_back(&b2);
    b_carga_list.push_back(&b3);
    b_carga_list.push_back(&b4);

    std::vector<Baia_descarga*> b_descarga_list;

    b_descarga_list.push_back(&d1);
    b_descarga_list.push_back(&d2);
    b_descarga_list.push_back(&d3);
    b_descarga_list.push_back(&d4);

    std::vector<Zona_recarga*> z_recarga_list;

    Robo_manager manager(b_carga_list, b_descarga_list, z_recarga_list);

    pthread_t thread_novo_robo;

    pthread_create( &thread_novo_robo,
                    NULL,
                    Robo_manager_new_robo_thread,
                    &manager);
    

    init_baia_carga_threads(manager._b_carga[0]);

    for(int i=0;i<4;i++) {

        Robo_manager_arg_t arg = {.manager = &manager,
                                  .b_carga = manager._b_carga[i]
                                };

        init_Robo_manager_threads(&arg);


    }

    // Deleção do grafo
    for(auto v : grafo) {
        delete v;
    }
    
    return 0;    
}