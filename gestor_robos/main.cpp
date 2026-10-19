/*
 * Processo do Gestor de Robôs.
 *
 * Uso: gestor_robos [--fifo <caminho>] [--robo <executável>]
 *
 *   --fifo  FIFO onde chegam os anúncios (padrão: run/novo_robo.fifo,
 *           relativo ao diretório de onde o sistema é executado)
 *   --robo  executável do processo de cada robô (padrão: "robo" na mesma
 *           pasta deste executável)
 *
 * Termina de forma ordenada com SIGINT (Ctrl+C) ou SIGTERM.
 */

#include "gestor_robos/gestor_robos.hpp"
#include "comum/log.hpp"

#include <csignal>
#include <cstring>
#include <string>
#include <unistd.h>

static std::string pasta_do_executavel(void) {

    char caminho[4096];
    ssize_t n = readlink("/proc/self/exe", caminho, sizeof(caminho) - 1);

    if(n <= 0)
        return ".";

    caminho[n] = '\0';

    std::string s(caminho);

    return s.substr(0, s.find_last_of('/'));
}

int main(int argc, char** argv) {

    std::string fifo = FIFO_NOVO_ROBO_PADRAO;
    std::string exe_robo = pasta_do_executavel() + "/robo";

    for(int i=1;i<argc;i++) {

        if(strcmp(argv[i], "--fifo") == 0 && i + 1 < argc) {
            fifo = argv[++i];
        } else if(strcmp(argv[i], "--robo") == 0 && i + 1 < argc) {
            exe_robo = argv[++i];
        } else {
            fprintf(stderr, "uso: %s [--fifo <caminho>] [--robo <executável>]\n", argv[0]);
            return 2;
        }
    }

    // Bloqueia SIGINT/SIGTERM antes de criar threads (a máscara é herdada):
    // só a thread principal os trata, de forma síncrona, com sigwait().
    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, NULL);

    signal(SIGPIPE, SIG_IGN);

    FonteFifo fonte(fifo);
    GestorRobos gestor(&fonte, exe_robo);

    if(!gestor.iniciar())
        return 1;

    int sinal;
    sigwait(&sinais, &sinal);

    log_msg("GESTOR", "recebido %s", strsignal(sinal));

    gestor.encerrar();

    return 0;
}
