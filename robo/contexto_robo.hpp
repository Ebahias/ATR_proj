#ifndef CONTEXTO_ROBO_HPP
#define CONTEXTO_ROBO_HPP

#include "comum/ipc_robo.hpp"
#include "robo/hardware_robo.hpp"

#include <pthread.h>

/*
 * Estado do robô compartilhado entre as threads do processo (Comando de
 * Navegação e Status do Robô). Todo acesso passa pelo mutex _mtx.
 *
 * Canais entre as duas threads (enunciado, itens 3 e 4):
 *  - Status -> Navegação: bloquear_movimento() (ex.: falha, içar carga) e
 *    forcar_destino() (ex.: bateria fraca, ir para a recarga);
 *  - Navegação -> Status: estado, modo e destino atuais, lidos por
 *    instantaneo() para publicar no buffer compartilhado.
 */

struct InstantaneoRobo {
    int estado;
    int modo;
    bool tem_destino;
    int destino_x;
    int destino_y;
    bool movimento_bloqueado;
    bool tem_destino_forcado;
    int forcado_x;
    int forcado_y;
    double distancia_total;
};

class ContextoRobo {

    protected:

        pthread_mutex_t _mtx;

        bool _encerrar;

        int _estado;

        int _modo;

        bool _tem_destino;
        int _destino_x;
        int _destino_y;

        bool _movimento_bloqueado;

        bool _tem_destino_forcado;
        int _forcado_x;
        int _forcado_y;

        double _distancia_total;

    public:

        // Imutáveis depois da criação: podem ser lidos sem mutex
        const int id;
        const int slot;
        ShmFrota* const shm;
        HardwareRobo* const hw;
        const mqd_t mq_comando;

        ContextoRobo(int id, int slot, ShmFrota* shm, HardwareRobo* hw, mqd_t mq_comando);

        ~ContextoRobo();

        bool deve_encerrar(void);

        void solicitar_encerramento(void);

        InstantaneoRobo instantaneo(void);

        int estado(void);

        // Muda o estado e registra o evento para o Coletor (se mudou).
        void mudar_estado(int novo, const char* motivo);

        int modo(void);

        void mudar_modo(int novo);

        void definir_destino(int x, int y);

        void bloquear_movimento(bool bloquear);

        void forcar_destino(int x, int y);

        void liberar_destino_forcado(void);

        void definir_distancia_total(double distancia);
};

#endif
