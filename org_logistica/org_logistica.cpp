/*
 * Processo do Organizador da Logística.
 *
 * Uso: org_logistica [--fonte fifo|mqtt] [--fifo <caminho>]
 *                    [--sem-gestor] [--sem-coletor]
 *
 *   Executado da raiz do repositório (por causa de coordenadas.txt).
 *
 *   O Organizador sobe o Gestor de Robôs e o Coletor de Dados como
 *   processos filhos (org_logistica_init.cpp) e os encerra no fim:
 *   --fonte, --fifo    repassados ao gestor_robos: de onde chegam os
 *                      anúncios de robôs. Padrão mqtt (Etapa 2: o
 *                      simulador anuncia os robôs pelo broker); fifo para
 *                      testar sem broker, com bin/novo_robo
 *
 *   Ordem: limpa os restos de IPC de uma queda, sobe o Gestor, espera ele
 *   criar a /atr_frota e só então sobe o Coletor (que também a abre).
 *   --sem-gestor       não sobe o Gestor: usa um que já esteja rodando
 *   --sem-coletor      não sobe o Coletor: usa um que já esteja rodando
 *                      (ou roda sem ele)
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
#include "comum/ipc_coletor.hpp"
#include "comum/ipc_gestor.hpp"
#include "comum/log.hpp"
#include "comum/mapa.hpp"
#include "org_logistica_init.hpp"




// 4 baias de carga + 4 zonas de entrega, 4 pontos cada
#define NUM_PONTOS_ARMAZEM  32

// Tempo para as missões em andamento liberarem o que reservaram
#define ESPERA_MISSOES_MS   5000

// Tempo para o Gestor recém-iniciado criar a /atr_frota
#define ESPERA_GESTOR_MS    5000

// Fila pré-definida de caixas das baias (enunciado), na raiz do repositório
#define ARQUIVO_CARGAS      "cargas_iniciais.txt"

static void uso(const char* prog) {
    fprintf(stderr, "uso: %s [--fonte fifo|mqtt] [--fifo <caminho>] [--sem-gestor] [--sem-coletor]\n", prog);
}

int main(int argc, char** argv) {

    std::string fonte = "mqtt";
    std::vector<std::string> args_gestor;
    bool sobe_gestor = true;
    bool sobe_coletor = true;
    bool sobe_broker = true;
    bool sobe_tela = true;

    for(int i=1;i<argc;i++) {

        if(strcmp(argv[i], "--fonte") == 0 && i + 1 < argc
           && (strcmp(argv[i + 1], "fifo") == 0 || strcmp(argv[i + 1], "mqtt") == 0)) {
            fonte = argv[++i];
        } else if(strcmp(argv[i], "--fifo") == 0 && i + 1 < argc) {
            args_gestor.push_back(argv[i]);
            args_gestor.push_back(argv[++i]);
        } else if(strcmp(argv[i], "--sem-gestor") == 0) {
            sobe_gestor = false;
        } else if(strcmp(argv[i], "--sem-coletor") == 0) {
            sobe_coletor = false;
        } else {
            uso(argv[0]);
            return 2;
        }
    }

    args_gestor.insert(args_gestor.begin(), fonte);
    args_gestor.insert(args_gestor.begin(), "--fonte");

    if(!sobe_gestor && args_gestor.size() > 2) {
        log_msg("ORGANIZADOR", "--fonte/--fifo valem só para o Gestor que o Organizador sobe; ignorados com --sem-gestor");
    }

    // Restos de uma execução que terminou com kill -9 ou queda: filas e
    // memória POSIX têm persistência de kernel, e quem as removeria (o Gestor
    // e o Coletor, ao sair) não chegou a fazê-lo. Sem esta limpeza, a fila do
    // Gestor entregaria os ROBO_NOVO da execução anterior. Só remove o que
    // pertence aos processos que este Organizador vai subir: com --sem-gestor
    // ou --sem-coletor, os de fora podem estar rodando. A limpeza fica aqui, e
    // não no início do Gestor ou do Coletor, porque os testes sobem o leitor
    // antes deles: o Gestor apagaria a fila que o leitor já abriu.
    if(sobe_gestor) {
        if(mq_gestor_remover())
            log_msg("ORGANIZADOR", "removida fila %s de uma execução anterior", MQ_GESTOR_ORGANIZADOR);
        if(shm_frota_remover())
            log_msg("ORGANIZADOR", "removida memória %s de uma execução anterior", SHM_FROTA);

        int filas = mq_comando_remover_restos();
        if(filas > 0)
            log_msg("ORGANIZADOR", "removidas %d filas de comando de robôs de uma execução anterior", filas);
    }

    if(sobe_coletor && mq_coletor_remover())
        log_msg("ORGANIZADOR", "removida fila %s de uma execução anterior", MQ_COLETOR_ORGANIZADOR);

    // Os filhos zeram a máscara de sinais antes do exec (preparar_filho), então
    // a ordem em relação ao pthread_sigmask abaixo não importa para eles

    pid_t pid_tela = (pid_t)-1;

    pid_t pid_broker = (pid_t)-1;

    pid_t pid_gestor = (pid_t)-1;

    // O Coletor só sobe depois que o Gestor estiver pronto (ele também abre
    // a /atr_frota)
    pid_t pid_coletor = (pid_t)-1;


    // Em toda saída daqui para a frente: o Gestor primeiro (ele encerra os
    // robôs), depois o Coletor (que ainda registra esse encerramento)
    auto encerrar_filhos = [&]() {
        org_logistica_close_gestor(pid_gestor);
        org_logistica_close_coletor(pid_coletor);
        org_logistica_close_tela(pid_tela);
        org_logistica_close_broker_mqtt(pid_broker);

    };

        // Inicializa Broker Mqtt
    if(sobe_broker) {
        pid_broker = org_logistica_init_broker_mqtt();
        if(pid_broker < 0) {
            encerrar_filhos();
            return 1;
        }
    }
    
    // Inicializa tela
    if(sobe_tela) {
        pid_tela = org_logistica_init_tela();
        if(pid_tela < 0) {
            encerrar_filhos();
            return 1;
        }
    }

    // Inicializa o Gestor
    pid_gestor = sobe_gestor ? org_logistica_init_gestor(args_gestor) : (pid_t)-1;

    if(sobe_gestor && pid_gestor < 0) {
        encerrar_filhos();
        return 1;
    }

    // Inicializa o Mqtt
    // org_logistica_init_mqtt();


    // Bloqueado antes de criar qualquer thread (a máscara é herdada): só a
    // thread principal trata SIGINT/SIGTERM, de forma síncrona, com sigwait.
    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, NULL);

    // O Gestor acabou de ser criado e ainda não teve tempo de criar a
    // /atr_frota: abrir agora falharia
    if(sobe_gestor && !org_logistica_esperar_gestor(pid_gestor, ESPERA_GESTOR_MS)) {
        if(fonte == "mqtt")
            log_msg("ORGANIZADOR", "com --fonte mqtt o Gestor precisa do broker (ATR_MQTT_HOST/ATR_MQTT_PORT); "
                                   "sem broker, use --fonte fifo");
        encerrar_filhos();
        return 1;
    }

    // Inicializa o Coletor
    if(sobe_coletor) {
        pid_coletor = org_logistica_init_coletor();
        if(pid_coletor < 0 || !org_logistica_esperar_coletor(pid_coletor)) {
            encerrar_filhos();
            return 1;
        }
    }

    if(!despacho_iniciar()) {
        encerrar_filhos();
        return 1;
    }

    // Criação do Grafo do Armazem

    std::vector<Node*> grafo = get_grafo_armazem("coordenadas.txt");

    if((int)grafo.size() < NUM_PONTOS_ARMAZEM) {
        log_msg("ORGANIZADOR", "coordenadas.txt com %d pontos (esperado %d); execute da raiz do repositório",
                (int)grafo.size(), NUM_PONTOS_ARMAZEM);
        despacho_encerrar();
        encerrar_filhos();
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

    // Fila pré-definida de caixas: antes das threads e da ponte, para a
    // primeira publicação de atr/cargas já mostrar as caixas de cada baia
    org_logistica_carregar_cargas(ARQUIVO_CARGAS, b_carga_list);

    // Cada baia só separa caixas que a frota consegue atender
    for(auto b : b_carga_list)
        b->definir_capacidade_frota([&manager]() { return manager.capacidade_frota(); });

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
        encerrar_filhos();
        exit(0);
    }

    despacho_encerrar();

    log_msg("ORGANIZADOR", "encerrado: %d cargas entregues", manager.entregues());

    encerrar_filhos();

    // Deleção do grafo
    for(auto v : grafo) {
        delete v;
    }

    return 0;
}