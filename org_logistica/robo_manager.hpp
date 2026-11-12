#ifndef ROBO_MANAGER_HPP
#define ROBO_MANAGER_HPP

#include "robo.hpp"
#include "b_carga.hpp"
#include "b_descarga.hpp"
#include "z_recarga.hpp"

#include <atomic>
#include <pthread.h>


/*
 * Frota vista pelo Organizador e reserva de robôs para as missões.
 *
 * Um robô só é escolhido se a /atr_frota diz que ele pode aceitar uma
 * tarefa do Organizador agora (ativo, automático, disponível, sem tarefa em
 * andamento) e se nenhuma outra missão o reservou. A reserva de uma equipe é
 * tudo ou nada (reservar_equipe): se cada baia segurasse os robôs que já
 * conseguiu enquanto espera os que faltam, duas baias pedindo 3 robôs numa
 * frota de 4 ficariam esperando uma pela outra para sempre.
 *
 * Prioridade para cargas maiores: cada baia que espera robôs registra o
 * tamanho da sua carga (registrar_pedido). Enquanto houver um pedido maior
 * esperando, um menor não leva robôs, senão os robôs que vão ficando livres
 * seriam sempre pegos pelas cargas pequenas e a de 4 nunca juntaria a
 * equipe. Um pedido maior que a frota em modo automático não bloqueia os
 * outros (nunca seria atendido).
 */
class Robo_manager {

    protected:

        std::vector<Robo*> _robo;

        std::vector<Zona_recarga*> _z_recarga;

        std::vector<Node*> _grafo;

        int _num_tarefas;

        // Protege _robo, _num_tarefas e as reservas (_reservado/_removido)
        std::mutex m;

        // Missões em andamento: o encerramento espera elas liberarem o que
        // reservaram antes de fechar a /atr_frota.
        pthread_mutex_t _mtx_missoes;
        pthread_cond_t _cv_missoes;
        int _missoes_ativas;
        int _entregues;

        std::atomic<bool> _encerrando;

        // Tamanho da carga de cada baia esperando equipe
        std::vector<int> _pedidos;

        // Chamados com m travado
        bool pode_receber_tarefa(Robo* robo, DadosStatus& d);

        Robo* escolher_disponivel(float ref_x, float ref_y);

        int maior_pedido_atendivel(void);

    public:

        std::vector<Baia_carga*> _b_carga;

        std::vector<Baia_descarga*> _b_descarga;

        Robo_manager(std::vector<Baia_carga*>& b_carga, std::vector<Baia_descarga*>& b_descarga, std::vector<Zona_recarga*>& z_recarga);

        ~Robo_manager();

        // Reserva e devolve o robô disponível mais próximo de (ref_x, ref_y),
        // ou nullptr. Com ref_x < 0, qualquer robô disponível.
        Robo* get_avaiable_robo(float ref_x = -1, float ref_y = -1);

        // Reserva n robôs de uma vez, os mais próximos de (ref_x, ref_y), ou
        // nenhum. Retorna false (e equipe vazia) se não há n disponíveis ou
        // se há um pedido maior esperando.
        bool reservar_equipe(int n, float ref_x, float ref_y, std::vector<Robo*>& equipe);

        // Uma baia começa / para de esperar uma equipe de n robôs
        void registrar_pedido(int n);

        void retirar_pedido(int n);

        void liberar_robo(Robo* robo);

        // ROBO_REMOVIDO do Gestor: o robô deixa de ser escolhido.
        void remover_robo(int robo_id);

        // Robôs que o Gestor anunciou e ainda não removeu.
        int num_robos_ativos(void);

        void missao_iniciou(void);

        void missao_terminou(bool entregue);

        int entregues(void);

        // Espera até timeout_ms as missões em andamento terminarem.
        // Retorna false se ainda há alguma.
        bool esperar_missoes(int timeout_ms);

        void solicitar_encerramento(void);

        bool encerrando(void);


        int add_robo(Robo* robo);
        
        int set_robo_to_z_recarga(Robo* robo, Zona_recarga* z_recarga);

        int wait(void);

        void add_grafo(std::vector<Node*> grafo);

        Node* get_nearest_node(Robo* robo);

        int get_new_tarefa(void);
};

#endif