/*
 * Envia um comando para a fila de um robô, fazendo o papel do Organizador da
 * Logística (ou do operador, com --operador) enquanto eles não existem.
 *
 * Uso: comando_robo <id> <comando> [args] [--operador]
 *
 *   ir <x> <y>      IR_PARA
 *   parar           PARAR
 *   auto | manual   troca de modo
 *   acoplar         ACOPLAR_E_ERGUER
 *   soltar          SOLTAR_CARGA
 *   falha <0|1>     (simulador) injeta/remove falha
 *   bateria <0-100> (simulador) ajusta a bateria
 */

#include "comum/ipc_robo.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

static void uso(const char* prog) {
    fprintf(stderr, "uso: %s <id> ir <x> <y> | parar | auto | manual | acoplar | soltar"
                    " | falha <0|1> | bateria <0-100>  [--operador]\n", prog);
    exit(2);
}

int main(int argc, char** argv) {

    std::vector<std::string> args;
    ComandoRobo cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.origem = ORIGEM_ORGANIZADOR;

    for(int i=1;i<argc;i++) {
        if(strcmp(argv[i], "--operador") == 0)
            cmd.origem = ORIGEM_OPERADOR;
        else
            args.push_back(argv[i]);
    }

    if(args.size() < 2)
        uso(argv[0]);

    int id = atoi(args[0].c_str());
    const std::string& c = args[1];
    size_t n = args.size() - 2;

    if(id <= 0)
        uso(argv[0]);

    if(c == "ir" && n == 2) {
        cmd.tipo = CMD_IR_PARA;
        cmd.x = atoi(args[2].c_str());
        cmd.y = atoi(args[3].c_str());
    } else if(c == "parar" && n == 0) {
        cmd.tipo = CMD_PARAR;
    } else if(c == "auto" && n == 0) {
        cmd.tipo = CMD_MODO;
        cmd.valor = MODO_AUTOMATICO;
    } else if(c == "manual" && n == 0) {
        cmd.tipo = CMD_MODO;
        cmd.valor = MODO_MANUAL;
    } else if(c == "acoplar" && n == 0) {
        cmd.tipo = CMD_ACOPLAR_E_ERGUER;
    } else if(c == "soltar" && n == 0) {
        cmd.tipo = CMD_SOLTAR_CARGA;
    } else if(c == "falha" && n == 1) {
        cmd.tipo = CMD_SIM_FALHA;
        cmd.valor = atoi(args[2].c_str());
    } else if(c == "bateria" && n == 1) {
        cmd.tipo = CMD_SIM_BATERIA;
        cmd.valor = atoi(args[2].c_str());
    } else {
        uso(argv[0]);
    }

    mqd_t mq = mq_comando_abrir_escrita(id);

    if(mq == (mqd_t)-1) {
        fprintf(stderr, "robô %d não existe (fila de comandos não encontrada)\n", id);
        return 1;
    }

    int ret = mq_comando_enviar(mq, cmd);
    int erro = errno;

    mq_close(mq);

    if(ret < 0) {
        fprintf(stderr, "falha ao enviar para o robô %d: %s\n", id,
                erro == EAGAIN ? "fila cheia (robô não está consumindo)" : strerror(erro));
        return 1;
    }

    return 0;
}
