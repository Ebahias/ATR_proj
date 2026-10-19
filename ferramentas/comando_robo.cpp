/*
 * Envia um comando para a fila de um robô, fazendo o papel do Organizador da
 * Logística (ou do operador, com --operador) enquanto eles não existem.
 *
 * Uso: comando_robo <id> <comando> [args] [--operador]
 *
 *   tarefa <tarefa_id> <cx> <cy> <ex> <ey> [--equipe <id>,<id>,...]
 *                   TAREFA: levar carga da coleta (cx, cy) até a entrega (ex, ey);
 *                   --equipe lista os robôs da equipe (ids, incluindo este)
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
                    " | falha <0|1> | bateria <0-100>"
                    " | tarefa <tarefa_id> <cx> <cy> <ex> <ey> [--equipe <id>,...]  [--operador]\n", prog);
    exit(2);
}

// "1,2,3" (ids de robôs) -> máscara de slots, consultando a tabela de status
static int resolver_equipe(const std::string& lista, uint32_t& mascara) {

    ShmFrota* shm = shm_frota_abrir();

    if(!shm)
        return -1;

    mascara = 0;
    size_t ini = 0;

    while(ini <= lista.size()) {

        size_t fim = lista.find(',', ini);
        if(fim == std::string::npos)
            fim = lista.size();

        int id = atoi(lista.substr(ini, fim - ini).c_str());
        int slot = -1;

        for(int s=0;s<MAX_ROBOS;s++) {
            DadosStatus d;
            shm_slot_ler(shm, s, d);
            if(d.ativo && d.robo_id == id)
                slot = s;
        }

        if(slot < 0) {
            fprintf(stderr, "robô %d da equipe não está ativo\n", id);
            shm_frota_fechar(shm);
            return -1;
        }

        mascara |= 1u << slot;
        ini = fim + 1;
    }

    shm_frota_fechar(shm);

    return 0;
}

int main(int argc, char** argv) {

    std::vector<std::string> args;
    std::string equipe;
    ComandoRobo cmd;
    memset(&cmd, 0, sizeof(cmd));
    cmd.origem = ORIGEM_ORGANIZADOR;

    for(int i=1;i<argc;i++) {
        if(strcmp(argv[i], "--operador") == 0)
            cmd.origem = ORIGEM_OPERADOR;
        else if(strcmp(argv[i], "--equipe") == 0 && i + 1 < argc)
            equipe = argv[++i];
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
    } else if(c == "tarefa" && n == 5) {
        cmd.tipo = CMD_TAREFA;
        cmd.tarefa_id = atoi(args[2].c_str());
        cmd.x = atoi(args[3].c_str());
        cmd.y = atoi(args[4].c_str());
        cmd.x2 = atoi(args[5].c_str());
        cmd.y2 = atoi(args[6].c_str());
        if(!equipe.empty() && resolver_equipe(equipe, cmd.equipe) < 0)
            return 1;
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
