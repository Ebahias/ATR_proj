#include "gestor_robos/fonte_novos_robos.hpp"
#include "comum/log.hpp"

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <sstream>
#include <sys/eventfd.h>
#include <sys/stat.h>
#include <unistd.h>


bool parse_msg_novo_robo(const std::string& linha, MsgNovoRobo& msg) {

    std::istringstream in(linha);
    std::string comando;

    if(!(in >> comando) || comando != "NOVO_ROBO")
        return false;

    if(!(in >> msg.id) || msg.id <= 0)
        return false;

    msg.x = POS_INICIAL_X;
    msg.y = POS_INICIAL_Y;

    int x, y;

    if(in >> x) {
        if(!(in >> y) || x < 0 || y < 0)
            return false;
        msg.x = x;
        msg.y = y;
    }

    std::string sobra;

    return !(in >> sobra);
}

std::string formata_msg_novo_robo(const MsgNovoRobo& msg) {

    return "NOVO_ROBO " + std::to_string(msg.id) + " "
         + std::to_string(msg.x) + " " + std::to_string(msg.y) + "\n";
}


FonteFifo::FonteFifo(const std::string& caminho) {

    this->_caminho = caminho;
    this->_fd = -1;
    this->_fd_parar = -1;
}

FonteFifo::~FonteFifo() {

    if(_fd >= 0)
        close(_fd);

    if(_fd_parar >= 0)
        close(_fd_parar);
}

bool FonteFifo::iniciar(void) {

    // Cria o diretório do FIFO, se houver (ex.: "run/")
    size_t barra = _caminho.find_last_of('/');

    if(barra != std::string::npos) {
        std::string dir = _caminho.substr(0, barra);
        if(mkdir(dir.c_str(), 0775) < 0 && errno != EEXIST) {
            log_msg("FONTE", "mkdir(%s): %s", dir.c_str(), strerror(errno));
            return false;
        }
    }

    if(mkfifo(_caminho.c_str(), 0660) < 0 && errno != EEXIST) {
        log_msg("FONTE", "mkfifo(%s): %s", _caminho.c_str(), strerror(errno));
        return false;
    }

    struct stat st;

    if(stat(_caminho.c_str(), &st) < 0 || !S_ISFIFO(st.st_mode)) {
        log_msg("FONTE", "%s existe e não é um FIFO", _caminho.c_str());
        return false;
    }

    // O_RDWR: o próprio Gestor mantém uma ponta de escrita aberta, então o
    // read() nunca vê EOF quando um anunciante fecha o FIFO.
    // O_CLOEXEC: os processos dos robôs não herdam este descritor.
    _fd = open(_caminho.c_str(), O_RDWR | O_CLOEXEC);

    if(_fd < 0) {
        log_msg("FONTE", "open(%s): %s", _caminho.c_str(), strerror(errno));
        return false;
    }

    _fd_parar = eventfd(0, EFD_CLOEXEC);

    if(_fd_parar < 0) {
        log_msg("FONTE", "eventfd: %s", strerror(errno));
        return false;
    }

    log_msg("FONTE", "aguardando anúncios de robôs em %s", _caminho.c_str());

    return true;
}

bool FonteFifo::proxima_linha(std::string& linha) {

    size_t fim = _pendente.find('\n');

    if(fim == std::string::npos)
        return false;

    linha = _pendente.substr(0, fim);
    _pendente.erase(0, fim + 1);

    return true;
}

int FonteFifo::aguardar(MsgNovoRobo& msg) {

    while(true) {

        std::string linha;

        while(proxima_linha(linha)) {

            if(linha.empty())
                continue;

            if(parse_msg_novo_robo(linha, msg))
                return 1;

            log_msg("FONTE", "mensagem inválida ignorada: \"%s\"", linha.c_str());
        }

        struct pollfd fds[2];
        fds[0].fd = _fd;
        fds[0].events = POLLIN;
        fds[1].fd = _fd_parar;
        fds[1].events = POLLIN;

        if(poll(fds, 2, -1) < 0) {
            if(errno == EINTR)
                continue;
            log_msg("FONTE", "poll: %s", strerror(errno));
            return -1;
        }

        if(fds[1].revents & POLLIN)
            return 0;

        if(fds[0].revents & POLLIN) {

            char buf[512];
            ssize_t n = read(_fd, buf, sizeof(buf));

            if(n < 0 && errno != EINTR) {
                log_msg("FONTE", "read: %s", strerror(errno));
                return -1;
            }

            if(n > 0)
                _pendente.append(buf, n);

            // Proteção contra lixo sem '\n' crescendo sem limite
            if(_pendente.size() > 4096)
                _pendente.clear();
        }
    }
}

void FonteFifo::interromper(void) {

    uint64_t um = 1;

    if(_fd_parar >= 0 && write(_fd_parar, &um, sizeof(um)) < 0)
        log_msg("FONTE", "interromper: %s", strerror(errno));
}
