#ifndef FONTE_NOVOS_ROBOS_HPP
#define FONTE_NOVOS_ROBOS_HPP

#include <string>

/*
 * Mensagem "novo robô disponível", enviada pelo hardware do robô (simulador).
 *
 * Formato texto, uma mensagem por linha:
 *
 *     NOVO_ROBO <id> [<x> <y>]
 *
 * Sem posição, o robô nasce na posição fixa POS_INICIAL_X/Y (estação de
 * recarga 1 do mapa, comum/mapa.hpp). O mesmo formato
 * será usado como payload MQTT na Etapa 2 (tópico TOPICO_MQTT_NOVO_ROBO).
 */

#define FIFO_NOVO_ROBO_PADRAO   "run/novo_robo.fifo"    // relativo ao diretório de execução
#define TOPICO_MQTT_NOVO_ROBO   "atr/robos/novo"        // Etapa 2

#define POS_INICIAL_X           100
#define POS_INICIAL_Y           30

struct MsgNovoRobo {
    int id;
    int x;
    int y;
};

// Retorna true se a linha é uma mensagem válida.
bool parse_msg_novo_robo(const std::string& linha, MsgNovoRobo& msg);

std::string formata_msg_novo_robo(const MsgNovoRobo& msg);


/*
 * De onde chegam os anúncios de novos robôs. O Gestor só conhece esta
 * interface; na Etapa 1 a fonte é um FIFO, na Etapa 2 basta implementar
 * uma FonteMqtt com a mesma interface.
 */
class FonteNovosRobos {

    public:

        virtual ~FonteNovosRobos() = default;

        virtual bool iniciar(void) = 0;

        // Bloqueia até chegar uma mensagem válida ou interromper() ser chamado.
        // Retorna 1 com msg preenchida, 0 se interrompida, -1 em erro.
        virtual int aguardar(MsgNovoRobo& msg) = 0;

        // Desbloqueia aguardar() (chamado por outra thread no encerramento).
        virtual void interromper(void) = 0;
};


/*
 * Fonte por FIFO (named pipe). Qualquer processo pode anunciar um robô com
 *     echo "NOVO_ROBO 3 10 20" > run/novo_robo.fifo
 * ou com a ferramenta bin/novo_robo.
 */
class FonteFifo : public FonteNovosRobos {

    protected:

        std::string _caminho;

        int _fd;

        int _fd_parar;      // eventfd usado por interromper()

        std::string _pendente;  // bytes lidos que ainda não formam linha completa

        bool proxima_linha(std::string& linha);

    public:

        explicit FonteFifo(const std::string& caminho);

        ~FonteFifo() override;

        bool iniciar(void) override;

        int aguardar(MsgNovoRobo& msg) override;

        void interromper(void) override;
};

#endif
