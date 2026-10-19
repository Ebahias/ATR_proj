#ifndef NAVEGADOR_HPP
#define NAVEGADOR_HPP

#include "robo/contexto_robo.hpp"
#include "comum/mapa.hpp"

#include <vector>

/*
 * Comando de Navegação (tarefa 3). Usado só pela thread de navegação.
 *
 * Rotas: o destino vira uma sequência de waypoints do mapa (Dijkstra). O
 * robô vai de waypoint em waypoint; antes de um trecho estreito pede vaga na
 * passagem (comum/passagens.hpp) e, sem vaga, espera parado na entrada.
 *
 * Tarefa de transporte (CMD_TAREFA), em fases:
 *
 *   IR_COLETA ──chegou──▶ AGUARDANDO_EQUIPE ──CMD_ACOPLAR (Organizador libera
 *   quando a equipe toda chegou)──▶ ACOPLANDO ──equipe toda atracada──▶
 *   TRANSPORTANDO ──chegou──▶ ENTREGANDO ──equipe toda chegou, solta──▶ CONCLUIDA
 *
 * No transporte, a equipe anda sincronizada: cada robô só segue para o
 * waypoint k depois que todos os colegas alcançaram o waypoint k-1 (barreira
 * lida do status dos colegas na memória compartilhada, sem passar pelo
 * Organizador). Nas passagens, a equipe reserva vagas para todos e só usa
 * passagens com capacidade suficiente.
 *
 * Interação com o Status: movimento bloqueado = robô parado; destino forçado
 * (ex.: recarga) tem prioridade sobre o destino da tarefa. A tarefa continua
 * de onde parou quando o destino forçado é liberado.
 */
class Navegador {

    protected:

        ContextoRobo* _ctx;

        char _modulo[32];

        // Rota em execução
        bool _tem_rota;
        std::vector<Waypoint> _rota;
        size_t _idx;                // próximo waypoint
        int _alvo_x;                // destino da rota atual
        int _alvo_y;
        bool _sincronizar;          // rota da equipe (barreira por waypoint)

        int _passagem;              // passagem em que está dentro (-1)
        int _aguardando;            // passagem em cuja fila está (-1)

        // Destino simples (IR_PARA)
        bool _tem_destino;
        int _destino_x;
        int _destino_y;

        // Tarefa de transporte
        int _fase;
        int _tarefa_id;
        int _coleta_x;
        int _coleta_y;
        int _entrega_x;
        int _entrega_y;
        uint32_t _equipe;           // máscara de slots (inclui o próprio)
        int _progresso;
        bool _soltou;

        bool _acoplado_manual;      // ACOPLAR sem tarefa (operação manual)

        bool rota_para(int x, int y, bool sincronizar);

        bool planejar(int x, int y, bool sincronizar);

        // Avança na rota. Retorna true quando chegou ao último waypoint.
        bool seguir_rota(const LeituraHardware& l);

        void segurar(const LeituraHardware& l);

        void sair_da_fila(void);

        bool equipe_alcancou(int progresso);

        bool equipe_atracada(void);

        void mudar_fase(int fase, const char* detalhe);

        void cancelar_tarefa(const char* motivo);

        bool tarefa_ativa(void) const;

        void publicar(void);

        const char* nome_local(int x, int y, char* buf, size_t tam);

    public:

        explicit Navegador(ContextoRobo* ctx);

        // Comandos (chamados pela thread de navegação ao ler a fila)
        bool ir_para(int x, int y);

        void parar(void);

        bool iniciar_tarefa(const ComandoRobo& cmd);

        void acoplar(void);

        void soltar(void);

        // Um ciclo da tarefa periódica
        void ciclo(void);

        // Libera passagens e filas (encerramento)
        void liberar(void);
};

#endif
