#ifndef COLETOR_COLETOR_HPP
#define COLETOR_COLETOR_HPP

#include "comum/ipc_coletor.hpp"
#include "comum/ipc_robo.hpp"
#include "coletor/rastreio.hpp"

#include <map>
#include <pthread.h>
#include <set>
#include <string>
#include <vector>

/*
 * Coletor de Dados (tarefa 6).
 *
 *   robôs ──(buffer circular em /atr_frota)──▶ thread COLETA ──(fila interna)──▶ thread GRAVAÇÃO
 *                                                                                    │
 *                                          historico_robo_<id>.log  ◀────────────────┤
 *                                          cargas.csv               ◀────────────────┤
 *                                          fila POSIX ao Organizador ◀───────────────┘
 *
 * A thread de coleta só esvazia o buffer compartilhado (que é pequeno e do qual
 * os robôs dependem para não descartar eventos); nunca faz E/S em disco. A de
 * gravação faz o trabalho lento (disco, rastreabilidade, envio ao Organizador).
 */

#define LIMITE_FILA_INTERNA     4096

class ColetorDados {

    private:

        std::string _dir;

        ShmFrota* _shm;
        mqd_t _mq;                      // Coletor -> Organizador ((mqd_t)-1 = desativado)
        int _fd_cargas;

        // Fila interna coleta -> gravação
        pthread_mutex_t _mtx;
        pthread_cond_t _cv_fila;
        std::vector<EventoRobo> _fila;
        bool _fim_coleta;               // a thread de coleta terminou (protegido por _mtx)
        bool _encerrando;               // pedido de encerramento (atômico)

        pthread_t _th_coleta;
        pthread_t _th_gravacao;
        bool _ativo;

        // Só a thread de gravação mexe nestes (e o encerrar(), depois do join)
        Rastreio _rastreio;
        std::map<int, int> _fd_historico;   // robo_id -> descritor
        std::set<int> _sujos;               // descritores gravados no lote atual

        // Estatísticas (análise de escalonabilidade)
        unsigned long _eventos_coletados;       // thread de coleta
        unsigned long _descartados_fila;        // thread de coleta (fila interna cheia)
        size_t _max_fila;                       // protegido por _mtx
        unsigned long _eventos_gravados;
        unsigned long _cargas_gravadas;
        unsigned long _mq_perdidos;
        unsigned long _erros_gravacao;
        long _max_latencia_ns;                  // evento publicado -> gravado

        static void* thread_coleta(void* arg);
        static void* thread_gravacao(void* arg);

        void laco_coleta(void);
        void laco_gravacao(void);

        void enfileirar(const EventoRobo& ev);
        void processar_lote(std::vector<EventoRobo>& lote);
        void processar(const EventoRobo& ev);

        int fd_historico(int robo_id);
        bool gravar(int fd, const char* texto, size_t n);
        void gravar_historico(const EventoRobo& ev);
        void gravar_carga(const RegistroCarga& c);

        void publicar_evento(const EventoRobo& ev);
        void publicar_carga(const RegistroCarga& c);

        void estatisticas(void);

    public:

        explicit ColetorDados(const std::string& dir);
        ~ColetorDados();

        ColetorDados(const ColetorDados&) = delete;
        ColetorDados& operator=(const ColetorDados&) = delete;

        // Abre a memória da frota (o Gestor já precisa estar rodando), cria a
        // pasta de dados e inicia as threads.
        bool iniciar(void);

        // Esvazia o que restou, grava as cargas incompletas e encerra as threads.
        void encerrar(void);
};

#endif
