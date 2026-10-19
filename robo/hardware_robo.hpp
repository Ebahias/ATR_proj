#ifndef HARDWARE_ROBO_HPP
#define HARDWARE_ROBO_HPP

#include <pthread.h>

/*
 * Interface com o hardware embarcado do robô (Tabela 1 do enunciado).
 * As threads de navegação e status só conhecem esta interface. Na Etapa 1 o
 * hardware é simulado dentro do próprio processo (HardwareSimulado); na
 * Etapa 2 basta uma implementação que troca estas variáveis via MQTT com o
 * simulador.
 */

struct LeituraHardware {
    int pos_x;                  // i_posicao_x
    int pos_y;                  // i_posicao_y
    bool carga_atracada;        // i_carga_atracada
    int bateria;                // i_percentual_bateria
    bool falha;                 // i_falha
};

class HardwareRobo {

    public:

        virtual ~HardwareRobo() = default;

        virtual bool iniciar(void) = 0;

        virtual void parar(void) = 0;

        // Entradas (I). Thread-safe.
        virtual LeituraHardware ler(void) = 0;

        // Saídas (O). Thread-safe.
        virtual void escrever_setpoint(int x, int y) = 0;      // o_sp_posicao_x/y

        virtual void acoplar_e_erguer(bool ativo) = 0;          // o_acoplar_e_erguer

        // Injeção de condições pelo simulador. Na Etapa 2 isso é feito na
        // interface do simulador; aqui existe só para testes da Etapa 1.
        virtual bool simular_falha(bool) { return false; }

        virtual bool simular_bateria(int) { return false; }
};


/*
 * Hardware simulado: uma thread periódica move o robô em linha reta até o
 * setpoint, descarrega a bateria (e carrega, se parado numa estação de
 * recarga) e simula o tempo de acoplamento.
 */
class HardwareSimulado : public HardwareRobo {

    protected:

        pthread_mutex_t _mtx;

        pthread_t _thread;

        bool _rodando;

        double _x;
        double _y;
        int _sp_x;
        int _sp_y;
        double _bateria;
        bool _falha;
        bool _acoplar;
        bool _atracada;
        double _t_acoplando;

        static void* thread_fisica(void* arg);

        void loop_fisica(void);

        void passo(double dt);

    public:

        HardwareSimulado(int x, int y);

        ~HardwareSimulado() override;

        bool iniciar(void) override;

        void parar(void) override;

        LeituraHardware ler(void) override;

        void escrever_setpoint(int x, int y) override;

        void acoplar_e_erguer(bool ativo) override;

        bool simular_falha(bool falha) override;

        bool simular_bateria(int percentual) override;
};

#endif
