#include "robo_manager_threads.hpp"
#include "despacho.hpp"
#include "comum/log.hpp"
#include "comum/mapa.hpp"

#include <string>
#include <time.h>

/*
 * Threads de decisão do Organizador da Logística.
 *
 * Uma organizador_de_logistica_thread por baia: espera uma carga ficar
 * disponível na baia, reserva a equipe (um robô por unidade de tamanho da
 * carga) e cria uma Robo_manager_assign_robo_thread para a missão. Várias
 * missões andam ao mesmo tempo, inclusive da mesma baia.
 *
 * A missão não move os robôs ponto a ponto: manda uma única tarefa por robô
 * (coleta e entrega juntas) e o processo do robô faz a rota, a espera pela
 * equipe e a travessia conjunta das passagens (robo/navegador.cpp). O
 * Organizador só decide quando a carga é liberada (ACOPLAR, depois que a
 * equipe toda está na baia) e acompanha a fase pela /atr_frota.
 */

// Intervalo entre tentativas de reservar a equipe de uma carga
#define ESPERA_ROBOS_MS         500

// Intervalo entre tentativas de reservar pontos numa zona de entrega
#define ESPERA_ASS_B_DESCARGA   1

// Generosos de propósito: uma equipe de 4 pode esperar a vez numa passagem
// estreita, e um robô pode precisar recarregar antes de chegar.
#define TIMEOUT_CHEGADA_BAIA_MS 120000
#define TIMEOUT_ENTREGA_MS      300000

static const char* nome_area(TipoArea tipo, int indice) {

    const AreaMapa* a = mapa_buscar_area(tipo, indice + 1);

    return a ? a->nome : "?";
}

static double agora_s(void) {

    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);

    return t.tv_sec + t.tv_nsec / 1e9;
}

static std::string lista_ids(const std::vector<int>& ids) {

    std::string s;

    for(size_t i=0;i<ids.size();i++) {
        if(i > 0)
            s += "+";
        s += std::to_string(ids[i]);
    }

    return s;
}

// Missão abortada: tira da tarefa os robôs ainda vivos, para não ficarem
// esperando um ACOPLAR ou um colega que nunca vêm. Quem está com a carga
// atracada a solta (senão continuaria "carregando" sem tarefa). No
// encerramento do Organizador (soltar_carga = false) quem já está com a
// carga termina a entrega sozinho, pois o robô não depende mais do
// Organizador depois do ACOPLAR.
static void abortar_robos(const std::vector<int>& ids, int tarefa, bool soltar_carga) {

    for(int id : ids) {

        DadosStatus d;

        if(!despacho_ler_status(id, d))
            continue;

        if(d.carga_atracada) {
            if(soltar_carga)
                despacho_soltar(id);
            continue;
        }

        if(d.tarefa_id == tarefa && (d.fase == FASE_IR_COLETA || d.fase == FASE_AGUARDANDO_EQUIPE))
            despacho_parar(id);
    }
}

void* organizador_de_logistica_thread(void* arg) {

    Robo_manager_arg_t* manager_arg = (Robo_manager_arg_t*) arg;

    Robo_manager* manager = manager_arg->manager;
    Baia_carga* b_carga = manager_arg->b_carga;

    // Referência para escolher os robôs mais próximos da baia
    const AreaMapa* area = mapa_buscar_area(AREA_BAIA, b_carga->get_id() + 1);
    float ref_x = area ? area->cx : -1;
    float ref_y = area ? area->cy : -1;

    struct timespec espera_robos = { 0, ESPERA_ROBOS_MS * 1000000L };

    std::vector<Carga> rcv_carga;

    while(!manager->encerrando()) {

        Carga carga = b_carga->cargo_request_unload();

        log_msg("ORGANIZADOR", "CARGA DE TAMANHO %d CHEGOU NA BAIA %d", carga.get_type(), b_carga->get_id());

        rcv_carga.push_back(carga);

        // Faz ponteiro dinamico da carga
        Carga* p_carga = new Carga(carga.get_type());

        for(auto c : carga._node) {
            p_carga->_node.push_back(c);
        }

        // ================================

        Robo_assign_arg_t* assign_arg = new Robo_assign_arg_t;

        int n = p_carga->get_type();
        bool avisou = false;

        // Tudo ou nada (ver Robo_manager): não segura parte da equipe
        // enquanto espera o resto. O pedido registrado faz as cargas menores
        // das outras baias cederem a vez a esta, se ela for maior.
        manager->registrar_pedido(n);

        while(!manager->reservar_equipe(n, ref_x, ref_y, assign_arg->assign_robo)) {

            if(manager->encerrando())
                break;

            if(!avisou) {
                log_msg("ORGANIZADOR", "BAIA %d: AGUARDANDO %d ROBÔS DISPONÍVEIS (FROTA COM %d)",
                        b_carga->get_id(), n, manager->num_robos_ativos());
                avisou = true;
            }

            nanosleep(&espera_robos, NULL);
        }

        manager->retirar_pedido(n);

        if(assign_arg->assign_robo.empty()) {
            b_carga->free_ocupied_nodes(p_carga->_node);
            delete p_carga;
            delete assign_arg;
            break;
        }

        pthread_t thread_assign;

        assign_arg->carga = p_carga;
        assign_arg->manager = manager;
        assign_arg->b_carga = b_carga;

        manager->missao_iniciou();

        if(pthread_create( &thread_assign,
                           NULL,
                           Robo_manager_assign_robo_thread,
                           assign_arg) != 0) {

            log_msg("ORGANIZADOR", "BAIA %d: NÃO FOI POSSÍVEL CRIAR A THREAD DA MISSÃO", b_carga->get_id());

            for(auto r : assign_arg->assign_robo)
                manager->liberar_robo(r);

            b_carga->free_ocupied_nodes(p_carga->_node);
            b_carga->add_cargo(Carga(n));
            delete p_carga;
            delete assign_arg;
            manager->missao_terminou(false);
            continue;
        }

        // Ninguém faz join: a missão avisa o fim por missao_terminou()
        pthread_detach(thread_assign);
    }

    return nullptr;
}

void* Robo_manager_process_cargo_thread(void* arg);

/*
 * Missão de uma carga de tamanho n (n robôs já reservados):
 *   1. reserva n pontos de entrega numa zona;
 *   2. manda a cada robô um CMD_TAREFA com o seu ponto de coleta na baia e
 *      o seu ponto de entrega, todos com o mesmo tarefa_id e a mesma equipe;
 *   3. espera a equipe toda em FASE_AGUARDANDO_EQUIPE e libera a carga
 *      (ACOPLAR para cada robô);
 *   4. espera a equipe toda em FASE_CONCLUIDA.
 * Em qualquer falha (robô morreu, falhou ou não chegou a tempo) a missão é
 * abortada: os robôs vivos saem da tarefa, tudo o que foi reservado é
 * liberado e a carga volta para a fila da baia.
 */
void* Robo_manager_assign_robo_thread(void* arg) {

    Robo_assign_arg_t* assign_arg = (Robo_assign_arg_t*) arg;

    Baia_carga* baia = assign_arg->b_carga;

    std::vector<Robo*> ass_robo = assign_arg->assign_robo;

    Robo_manager* manager = assign_arg->manager;

    Carga* cargo = assign_arg->carga;

    const int n = cargo->get_type();

    const char* nome_baia = nome_area(AREA_BAIA, baia->get_id());

    int tarefa = manager->get_new_tarefa();

    double inicio = agora_s();

    std::vector<int> equipe;

    for(auto v : ass_robo) {

        equipe.push_back(v->get_id());

    }

    std::string nomes = lista_ids(equipe);

    std::vector<Node*> nodes_bd;

    Baia_descarga* b_d = nullptr;

    int zona = -1;

    bool entregue = false;

    const char* motivo = "";

    do {

        if((int)cargo->_node.size() < n || (int)ass_robo.size() < n) {
            motivo = "carga sem pontos de coleta suficientes";
            break;
        }

        uint32_t mascara = despacho_mascara_equipe(equipe);

        if(mascara == 0) {
            motivo = "robô da equipe deixou de estar ativo";
            break;
        }

        // Pontos de entrega antes de mandar a tarefa: o CMD_TAREFA leva
        // coleta e entrega juntas. Começa por uma zona diferente a cada
        // tarefa para espalhar as entregas, em vez de encher sempre a primeira.
        struct timespec itv_bd = { ESPERA_ASS_B_DESCARGA, 0 };
        bool avisou = false;
        size_t nz = manager->_b_descarga.size();

        while(b_d == nullptr && !manager->encerrando() && nz > 0) {

            for(size_t k=0;k<nz;k++) {

                int z = (int)((tarefa + k) % nz);

                nodes_bd = manager->_b_descarga[z]->get_avaiable_spots(n);

                if(!nodes_bd.empty()) {
                    b_d = manager->_b_descarga[z];
                    zona = z;
                    break;
                }
            }

            if(b_d == nullptr) {
                if(!avisou) {
                    log_msg("ORGANIZADOR", "TAREFA %d: AGUARDANDO %d PONTOS LIVRES NUMA ZONA DE ENTREGA", tarefa, n);
                    avisou = true;
                }
                nanosleep(&itv_bd, NULL);
            }
        }

        if(b_d == nullptr) {
            motivo = "Organizador encerrando";
            break;
        }

        log_msg("ORGANIZADOR", "TAREFA %d: CARGA DE TAMANHO %d, %s -> %s, EQUIPE %s",
                tarefa, n, nome_baia, nome_area(AREA_ZONA, zona), nomes.c_str());

        bool enviou = true;

        for(int i=0;i<n && enviou;i++) {
            enviou = despacho_tarefa(equipe[i], tarefa, mascara,
                                     (int)cargo->_node[i]->get_pos_x(), (int)cargo->_node[i]->get_pos_y(),
                                     (int)nodes_bd[i]->get_pos_x(), (int)nodes_bd[i]->get_pos_y());
        }

        if(!enviou) {
            motivo = "falha ao enviar a tarefa";
            break;
        }

        if(!despacho_esperar_fase(equipe, tarefa, FASE_AGUARDANDO_EQUIPE, TIMEOUT_CHEGADA_BAIA_MS)) {
            motivo = "a equipe não chegou à baia";
            break;
        }

        // Equipe toda em posição: libera a carga
        log_msg("ORGANIZADOR", "TAREFA %d: EQUIPE %s EM POSIÇÃO NA %s, LIBERANDO A CARGA (ACOPLAR)",
                tarefa, nomes.c_str(), nome_baia);

        bool acoplou = true;

        for(auto r : ass_robo) {
            if(r->lift() < 0)
                acoplou = false;
        }

        if(!acoplou) {
            motivo = "falha ao enviar ACOPLAR";
            break;
        }

        if(!despacho_esperar_fase(equipe, tarefa, FASE_CONCLUIDA, TIMEOUT_ENTREGA_MS)) {
            motivo = "a entrega não foi concluída";
            break;
        }

        entregue = true;

    } while(false);

    if(!entregue) {
        // A espera interrompida pelo encerramento também retorna false:
        // sem isto o motivo registrado seria o da etapa em que ela estava
        if(manager->encerrando())
            motivo = "Organizador encerrando";

        log_msg("ORGANIZADOR", "TAREFA %d ABORTADA (%s): CARGA DE TAMANHO %d DA %s, EQUIPE %s",
                tarefa, motivo, n, nome_baia, nomes.c_str());
        abortar_robos(equipe, tarefa, !manager->encerrando());
    }

    // Libera os nós da baia de carga e da zona de entrega
    baia->free_ocupied_nodes(cargo->_node);

    if(b_d != nullptr)
        b_d->free_occupied_spots(nodes_bd);

    for(auto r : ass_robo)
        manager->liberar_robo(r);

    if(entregue) {
        log_msg("ORGANIZADOR", "TAREFA %d CONCLUÍDA: CARGA DE TAMANHO %d, %s -> %s, EQUIPE %s, %.1f s",
                tarefa, n, nome_baia, nome_area(AREA_ZONA, zona), nomes.c_str(), agora_s() - inicio);
    } else if(!manager->encerrando()) {
        baia->add_cargo(Carga(n));
        log_msg("ORGANIZADOR", "TAREFA %d: CARGA DE TAMANHO %d DEVOLVIDA À FILA DA %s", tarefa, n, nome_baia);
    }

    delete assign_arg->carga;

    delete assign_arg;

    // Por último: depois disto o encerramento pode fechar a /atr_frota
    manager->missao_terminou(entregue);

    return nullptr;
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

            log_msg("NOVOS ROBOS", "NOVO ROBO %d EM %d x %d", ev.robo_id, ev.pos_x, ev.pos_y);

        } else {
            log_msg("ESCUTA", "%s robo=%d pid=%d %s=%d slot=%d t=%ld.%09ld",
                    evento_gestor_nome(ev.tipo), ev.robo_id, ev.pid,
                    WIFSIGNALED(ev.status_saida) ? "sinal" : "codigo",
                    WIFSIGNALED(ev.status_saida) ? WTERMSIG(ev.status_saida) : WEXITSTATUS(ev.status_saida),
                    ev.slot,
                    (long)ev.timestamp.tv_sec, ev.timestamp.tv_nsec);

            // Robô morto não pode ser escolhido de novo; uma missão que o
            // tinha na equipe percebe sozinha pela /atr_frota
            if(ev.tipo == EVENTO_ROBO_REMOVIDO) {
                manager->remover_robo(ev.robo_id);
                log_msg("NOVOS ROBOS", "ROBO %d REMOVIDO DA FROTA", ev.robo_id);
            }
        }
    }

    mq_close(mq);

    return NULL;
}

// Cria a thread de decisão da baia, sem esperar por ela (o join que havia
// aqui prendia o main na baia 1, e as outras nunca eram criadas)
pthread_t init_Robo_manager_threads(Robo_manager_arg_t* Manager) {

    pthread_t thread_manager;

    pthread_create( &thread_manager,
                    NULL,
                    organizador_de_logistica_thread,
                    Manager);

    return thread_manager;
}
