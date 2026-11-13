/*
 * Processo do Organizador da Logística.
 *
 * Uso: org_logistica   (executado da raiz do repositório, por causa de
 *                       coordenadas.txt)
 *
 * Pré-requisito: o Gestor de Robôs já rodando (ele cria a /atr_frota e a
 * fila de eventos "/atr_gestor_organizador"). Quem sobe os processos na
 * ordem certa é o script de execução do sistema.
 *
 * Threads:
 *   - principal:     trata SIGINT/SIGTERM (sigwait) e coordena o encerramento
 *   - novos robôs:   eventos ROBO_NOVO/ROBO_REMOVIDO do Gestor
 *   - por baia (4):  chegada de cargas, processamento das cargas (reserva
 *                    dos pontos da baia) e decisão (reserva da equipe)
 *   - por missão:    uma Robo_manager_assign_robo_thread por carga em
 *                    transporte (robo_manager_threads.cpp)
 *
 * Os comandos para os robôs saem pelo despacho (despacho.hpp), direto nas
 * filas POSIX de cada robô. A ponte MQTT (ponte_mqtt.hpp) publica o estado
 * para a Tela de Operação e recebe os comandos dela; sem broker, o
 * Organizador funciona igual. Broker em ATR_MQTT_HOST/ATR_MQTT_PORT (padrão
 * localhost:1883).
 */

#include <iostream>
#include "carga.hpp"
#include "grafo.hpp"
#include "b_carga.hpp"
#include "b_descarga.hpp"
#include "z_recarga.hpp"
#include "robo_manager.hpp"
#include "robo.hpp"
#include "grafo_armazem.hpp"
#include <csignal>
#include <cstring>
#include <unistd.h>

#include "robo_manager_threads.hpp"
#include "robo_threads.hpp"
#include "b_carga_threads.hpp"
#include "despacho.hpp"
#include "ponte_mqtt.hpp"
#include "comum/log.hpp"
#include "comum/mapa.hpp"
#include "org_logistica_init.hpp"




// 4 baias de carga + 4 zonas de entrega, 4 pontos cada
#define NUM_PONTOS_ARMAZEM  32

// Tempo para as missões em andamento liberarem o que reservaram
#define ESPERA_MISSOES_MS   5000

int main(void) {

    // Inicializa o Gestor
    pid_t pid_gestor = org_logistica_init_gestor();

    // Inicializa o Coletor
    pid_t pid_coletor =  org_logistica_init_coletor();

    // Inicializa o Mqtt
    // org_logistica_init_mqtt();


    // Bloqueado antes de criar qualquer thread (a máscara é herdada): só a
    // thread principal trata SIGINT/SIGTERM, de forma síncrona, com sigwait.
    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, NULL);

    if(!despacho_iniciar())
        return 1;

    // Criação do Grafo do Armazem

    std::vector<Node*> grafo = get_grafo_armazem("coordenadas.txt");

    if((int)grafo.size() < NUM_PONTOS_ARMAZEM) {
        log_msg("ORGANIZADOR", "coordenadas.txt com %d pontos (esperado %d); execute da raiz do repositório",
                (int)grafo.size(), NUM_PONTOS_ARMAZEM);
        despacho_encerrar();
        return 1;
    }

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

    // Antes das threads das baias: as primeiras cargas já saem publicadas.
    // Sem broker a ponte só registra e segue tentando em segundo plano.
    ponte_mqtt_iniciar(&manager);

    // Threads que rodam até o encerramento (as das missões terminam sozinhas)
    std::vector<pthread_t> threads;

    pthread_t thread_novo_robo;

    pthread_create( &thread_novo_robo,
                    NULL,
                    Robo_manager_new_robo_thread,
                    &manager);

    threads.push_back(thread_novo_robo);

    // Argumentos vivem até o fim do main: as threads guardam o ponteiro
    std::vector<Robo_manager_arg_t> args(manager._b_carga.size());

    for(size_t i=0;i<manager._b_carga.size();i++) {

        init_baia_carga_threads(manager._b_carga[i], &threads);

        args[i].manager = &manager;
        args[i].b_carga = manager._b_carga[i];

        threads.push_back(init_Robo_manager_threads(&args[i]));
    }

    log_msg("ORGANIZADOR", "iniciado: %d baias de carga, %d zonas de entrega",
            (int)b_carga_list.size(), (int)b_descarga_list.size());

    int sinal;
    sigwait(&sinais, &sinal);

    log_msg("ORGANIZADOR", "recebido %s, encerrando", strsignal(sinal));

    // 1. Nenhuma missão nova, e as esperas das missões em andamento
    //    retornam já (despacho_interromper)
    manager.solicitar_encerramento();
    despacho_interromper();

    // 2. As threads das baias e a dos novos robôs ficam bloqueadas em
    //    semáforos, na fila do Gestor ou em nanosleep, todos pontos de
    //    cancelamento. Nenhuma delas faz chamada bloqueante segurando mutex.
    for(auto t : threads)
        pthread_cancel(t);

    for(auto t : threads)
        pthread_join(t, NULL);

    // 3. As missões abortam, liberam robôs e pontos e avisam o manager
    bool missoes_terminaram = manager.esperar_missoes(ESPERA_MISSOES_MS);

    // 4. A ponte lê a /atr_frota e o manager: para antes de fechar a memória
    ponte_mqtt_encerrar();

    if(!missoes_terminaram) {
        // Uma missão ainda usa a /atr_frota e os objetos do main: sai sem
        // destruí-los (exit não chama destrutores de variáveis locais)
        log_msg("ORGANIZADOR", "missões ainda em andamento após %d ms; saindo assim mesmo", ESPERA_MISSOES_MS);
        exit(0);
    }

    despacho_encerrar();

    log_msg("ORGANIZADOR", "encerrado: %d cargas entregues", manager.entregues());

    org_logistica_close_gestor(pid_gestor);

    org_logistica_close_coletor(pid_coletor);

    // Deleção do grafo
    for(auto v : grafo) {
        delete v;
    }

    return 0;
}