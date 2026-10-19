#ifndef GESTOR_ROBOS_HPP
#define GESTOR_ROBOS_HPP

#include "comum/ipc_gestor.hpp"
#include "comum/ipc_robo.hpp"
#include "gestor_robos/fonte_novos_robos.hpp"

#include <pthread.h>
#include <string>

/*
 * Gestor de Robôs (tarefa 1).
 *
 *  - thread receptora: espera anúncios na FonteNovosRobos e, para cada robô
 *    novo, cria um processo (fork + exec do executável do robô);
 *  - thread monitora: espera (waitpid) o término dos processos dos robôs e
 *    libera a vaga na tabela;
 *  - ambas avisam o Organizador da Logística, de forma assíncrona, pela fila
 *    POSIX MQ_GESTOR_ORGANIZADOR.
 *
 * O Gestor também é dono do ciclo de vida dos recursos de IPC de cada robô:
 * cria a memória da frota (SHM_FROTA), reserva o slot de status e cria a
 * fila de comandos do robô antes do fork, e libera ambos quando o robô termina.
 */

struct RegistroRobo {
    bool ativo;
    int id;
    pid_t pid;
    int pos_x;
    int pos_y;
    struct timespec criado_em;
};

class GestorRobos {

    protected:

        FonteNovosRobos* _fonte;

        std::string _exe_robo;

        mqd_t _mq;

        ShmFrota* _shm;

        pthread_t _th_receptor;

        pthread_t _th_monitor;

        bool _threads_ativas;

        // Protege _robos, _n_ativos e _encerrando
        pthread_mutex_t _mtx;

        // Sinaliza para a thread monitora que existe pelo menos um filho
        pthread_cond_t _cv_tem_filhos;

        // Sinaliza para encerrar() que todos os filhos terminaram
        pthread_cond_t _cv_sem_filhos;

        // Garante que os eventos chegam ao Organizador na mesma ordem em que
        // a tabela foi alterada (um ROBO_REMOVIDO nunca antes do ROBO_NOVO)
        pthread_mutex_t _mtx_envio;

        RegistroRobo _robos[MAX_ROBOS];

        int _n_ativos;

        bool _encerrando;

        static void* thread_receptor(void* arg);

        static void* thread_monitor(void* arg);

        void loop_receptor(void);

        void loop_monitor(void);

        int criar_robo(const MsgNovoRobo& msg);

        void robo_terminou(pid_t pid, int status);

        int buscar_por_id(int id);

        int buscar_por_pid(pid_t pid);

        void enviar_evento(const EventoGestor& evento);

    public:

        GestorRobos(FonteNovosRobos* fonte, const std::string& exe_robo);

        ~GestorRobos();

        bool iniciar(void);

        // Para de aceitar robôs, termina todos os processos e junta as threads.
        void encerrar(void);

        int robos_ativos(void);
};

#endif
