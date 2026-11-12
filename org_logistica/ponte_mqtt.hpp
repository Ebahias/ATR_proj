#ifndef PONTE_MQTT_HPP
#define PONTE_MQTT_HPP

class Robo_manager;

/*
 * Ponte MQTT do Organizador da Logística com a Tela de Operação.
 *
 * Publica (contrato em comum/mqtt_topicos.hpp):
 *   - atr/frota/status   status de todos os robôs ativos, a cada 200 ms
 *                        (thread periódica, comum/periodico.hpp);
 *   - atr/eventos        cada registro que o Coletor de Dados manda pela
 *                        fila "/atr_coletor_organizador" (eventos dos robôs e
 *                        cargas entregues);
 *   - atr/cargas         filas das baias, missões em andamento e total
 *                        entregue, sempre que algo muda (retido no broker).
 * Recebe:
 *   - atr/operacao/cmd   MODO <id> manual|auto, IR <id> <x> <y>, PARAR <id>,
 *                        CAIXA <baia> <tamanho>.
 *
 * Tempo real: o callback do ClienteMqtt roda na thread da libmosquitto e só
 * faz o parse e coloca o comando numa fila; quem fala com os robôs e com as
 * baias é a thread de comandos da ponte. Nenhuma thread do Organizador
 * espera pela rede: publicar() só enfileira na libmosquitto.
 *
 * Sem broker o Organizador funciona igual, só sem a Tela: a ponte tenta a
 * primeira conexão de novo a cada poucos segundos e, depois de conectada, a
 * libmosquitto reconecta sozinha quando o broker cai e volta.
 */

// Chamado pelo main depois de despacho_iniciar() e de criar o manager.
// Retorna false só se não conseguiu criar as threads; sem broker retorna
// true e continua tentando conectar em segundo plano.
bool ponte_mqtt_iniciar(Robo_manager* manager);

// Para as threads e desconecta. Chamado antes de despacho_encerrar().
void ponte_mqtt_encerrar(void);

// O resto do Organizador avisa que uma fila de baia, uma missão ou o total
// entregue mudou. Não bloqueia; pode ser chamada de qualquer thread, a
// qualquer momento (inclusive antes de iniciar ou depois de encerrar).
void ponte_mqtt_cargas_mudaram(void);

#endif
