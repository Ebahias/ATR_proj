/*
 * PROVISÓRIO: processo mínimo de um robô, só para o Gestor ter o que criar.
 * Será substituído pelo processo do robô de verdade (tarefas 2, 3 e 4:
 * threads de Comando de Navegação e Status do Robô).
 *
 * Contrato com o Gestor de Robôs:
 *   - é executado como:  robo <id> <x> <y>
 *   - deve terminar ao receber SIGTERM (o Gestor espera 2 s antes do SIGKILL)
 */

#include "comum/log.hpp"

#include <csignal>
#include <cstdlib>
#include <cstring>
#include <unistd.h>

int main(int argc, char** argv) {

    if(argc != 4) {
        fprintf(stderr, "uso: %s <id> <x> <y>\n", argv[0]);
        return 2;
    }

    int id = atoi(argv[1]);
    int x = atoi(argv[2]);
    int y = atoi(argv[3]);

    char modulo[32];
    snprintf(modulo, sizeof(modulo), "ROBO %d", id);

    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, NULL);

    log_msg(modulo, "processo iniciado (pid %d) em (%d, %d)", getpid(), x, y);

    int sinal;
    sigwait(&sinais, &sinal);

    log_msg(modulo, "encerrando (%s)", strsignal(sinal));

    return 0;
}
