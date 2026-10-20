/*
 * Processo do Coletor de Dados (tarefa 6).
 *
 * Uso: coletor [--dir <pasta>]
 *
 *   --dir  pasta onde os registros são gravados (padrão: dados, relativa ao
 *          diretório de onde o sistema é executado)
 *
 * Deve rodar junto com o Gestor de Robôs (que cria a memória compartilhada da
 * frota); se for iniciado antes, espera o Gestor aparecer.
 * Termina de forma ordenada com SIGINT (Ctrl+C) ou SIGTERM.
 */

#include "coletor/coletor.hpp"
#include "comum/log.hpp"

#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <sys/mman.h>
#include <unistd.h>

#define PASTA_DADOS_PADRAO  "dados"

// A memória da frota existe? (sem registrar erro enquanto o Gestor não subiu)
static bool gestor_pronto(void) {

    int fd = shm_open(SHM_FROTA, O_RDWR, 0);

    if(fd < 0)
        return false;

    close(fd);

    return true;
}

int main(int argc, char** argv) {

    std::string dir = PASTA_DADOS_PADRAO;

    for(int i=1;i<argc;i++) {

        if(strcmp(argv[i], "--dir") == 0 && i + 1 < argc) {
            dir = argv[++i];
        } else {
            fprintf(stderr, "uso: %s [--dir <pasta>]\n", argv[0]);
            return 2;
        }
    }

    // Só a thread principal trata SIGINT/SIGTERM, de forma síncrona
    sigset_t sinais;
    sigemptyset(&sinais);
    sigaddset(&sinais, SIGINT);
    sigaddset(&sinais, SIGTERM);
    pthread_sigmask(SIG_BLOCK, &sinais, NULL);

    bool avisou = false;

    while(!gestor_pronto()) {

        if(!avisou) {
            log_msg("COLETOR", "aguardando o Gestor de Robôs criar %s", SHM_FROTA);
            avisou = true;
        }

        struct timespec espera = {0, 200 * 1000000L};

        if(sigtimedwait(&sinais, NULL, &espera) > 0)
            return 0;
    }

    ColetorDados coletor(dir);

    if(!coletor.iniciar())
        return 1;

    int sinal;
    sigwait(&sinais, &sinal);

    log_msg("COLETOR", "recebido %s", strsignal(sinal));

    coletor.encerrar();

    return 0;
}
