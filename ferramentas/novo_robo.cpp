/*
 * Anuncia um robô novo ao Gestor de Robôs, fazendo o papel do simulador
 * enquanto o MQTT não existe (Etapa 1).
 *
 * Uso: novo_robo <id> [<x> <y>] [--fifo <caminho>]
 */

#include "gestor_robos/fonte_novos_robos.hpp"

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <unistd.h>

int main(int argc, char** argv) {

    std::string fifo = FIFO_NOVO_ROBO_PADRAO;
    std::string linha = "NOVO_ROBO";
    int n_args = 0;

    for(int i=1;i<argc;i++) {
        if(strcmp(argv[i], "--fifo") == 0 && i + 1 < argc) {
            fifo = argv[++i];
        } else {
            linha += " ";
            linha += argv[i];
            n_args++;
        }
    }

    MsgNovoRobo msg;

    if((n_args != 1 && n_args != 3) || !parse_msg_novo_robo(linha, msg)) {
        fprintf(stderr, "uso: %s <id> [<x> <y>] [--fifo <caminho>]   (id > 0, x/y >= 0)\n", argv[0]);
        return 2;
    }

    // O_NONBLOCK: falha com ENXIO em vez de travar se o Gestor não estiver rodando
    int fd = open(fifo.c_str(), O_WRONLY | O_NONBLOCK);

    if(fd < 0) {
        fprintf(stderr, "não foi possível abrir %s (o Gestor está rodando?): %s\n",
                fifo.c_str(), strerror(errno));
        return 1;
    }

    // Mensagens menores que PIPE_BUF são escritas de forma atômica, então
    // vários anunciantes simultâneos não embaralham as linhas.
    std::string texto = formata_msg_novo_robo(msg);
    ssize_t n = write(fd, texto.c_str(), texto.size());

    close(fd);

    if(n != (ssize_t)texto.size()) {
        fprintf(stderr, "falha ao escrever em %s: %s\n", fifo.c_str(), strerror(errno));
        return 1;
    }

    return 0;
}
