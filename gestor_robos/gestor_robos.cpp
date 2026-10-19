#include "gestor_robos/gestor_robos.hpp"
#include "comum/log.hpp"

#include <cerrno>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <sys/prctl.h>
#include <sys/wait.h>
#include <unistd.h>

#define TIMEOUT_ENVIO_MS        100     // espera máxima se a fila do Organizador estiver cheia
#define TIMEOUT_TERMINO_MS      2000    // tempo para os robôs saírem após SIGTERM


static void prazo_em(struct timespec* ts, int ms) {

    clock_gettime(CLOCK_REALTIME, ts);

    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;

    if(ts->tv_nsec >= 1000000000L) {
        ts->tv_sec++;
        ts->tv_nsec -= 1000000000L;
    }
}

static long us_entre(const struct timespec& a, const struct timespec& b) {
    return (b.tv_sec - a.tv_sec) * 1000000L + (b.tv_nsec - a.tv_nsec) / 1000L;
}


GestorRobos::GestorRobos(FonteNovosRobos* fonte, const std::string& exe_robo) {

    this->_fonte = fonte;
    this->_exe_robo = exe_robo;
    this->_mq = (mqd_t)-1;
    this->_threads_ativas = false;
    this->_n_ativos = 0;
    this->_encerrando = false;

    memset(_robos, 0, sizeof(_robos));

    pthread_mutex_init(&_mtx, NULL);
    pthread_mutex_init(&_mtx_envio, NULL);
    pthread_cond_init(&_cv_tem_filhos, NULL);
    pthread_cond_init(&_cv_sem_filhos, NULL);
}

GestorRobos::~GestorRobos() {

    encerrar();

    if(_mq != (mqd_t)-1)
        mq_close(_mq);

    pthread_cond_destroy(&_cv_sem_filhos);
    pthread_cond_destroy(&_cv_tem_filhos);
    pthread_mutex_destroy(&_mtx_envio);
    pthread_mutex_destroy(&_mtx);
}

bool GestorRobos::iniciar(void) {

    if(access(_exe_robo.c_str(), X_OK) < 0) {
        log_msg("GESTOR", "executável do robô não encontrado: %s", _exe_robo.c_str());
        return false;
    }

    _mq = mq_gestor_abrir_escrita();

    if(_mq == (mqd_t)-1)
        return false;

    if(!_fonte->iniciar())
        return false;

    if(pthread_create(&_th_monitor, NULL, GestorRobos::thread_monitor, this) != 0) {
        log_msg("GESTOR", "falha ao criar thread monitora");
        return false;
    }

    if(pthread_create(&_th_receptor, NULL, GestorRobos::thread_receptor, this) != 0) {
        log_msg("GESTOR", "falha ao criar thread receptora");
        pthread_mutex_lock(&_mtx);
        _encerrando = true;
        pthread_cond_broadcast(&_cv_tem_filhos);
        pthread_mutex_unlock(&_mtx);
        pthread_join(_th_monitor, NULL);
        return false;
    }

    _threads_ativas = true;

    log_msg("GESTOR", "iniciado (pid %d, máx. %d robôs, robô = %s)",
            getpid(), MAX_ROBOS, _exe_robo.c_str());

    return true;
}

void* GestorRobos::thread_receptor(void* arg) {

    ((GestorRobos*)arg)->loop_receptor();

    return NULL;
}

void* GestorRobos::thread_monitor(void* arg) {

    ((GestorRobos*)arg)->loop_monitor();

    return NULL;
}

void GestorRobos::loop_receptor(void) {

    MsgNovoRobo msg;

    while(_fonte->aguardar(msg) == 1) {
        criar_robo(msg);
    }

    log_msg("GESTOR", "thread receptora encerrada");
}

int GestorRobos::buscar_por_id(int id) {

    for(int i=0;i<MAX_ROBOS;i++) {
        if(_robos[i].ativo && _robos[i].id == id)
            return i;
    }

    return -1;
}

int GestorRobos::buscar_por_pid(pid_t pid) {

    for(int i=0;i<MAX_ROBOS;i++) {
        if(_robos[i].ativo && _robos[i].pid == pid)
            return i;
    }

    return -1;
}

int GestorRobos::criar_robo(const MsgNovoRobo& msg) {

    struct timespec t_anuncio;
    clock_gettime(CLOCK_MONOTONIC, &t_anuncio);

    // Argumentos montados antes do fork: entre fork e exec o filho de um
    // processo multithread só pode chamar funções async-signal-safe.
    char arg_id[16], arg_x[16], arg_y[16];
    snprintf(arg_id, sizeof(arg_id), "%d", msg.id);
    snprintf(arg_x, sizeof(arg_x), "%d", msg.x);
    snprintf(arg_y, sizeof(arg_y), "%d", msg.y);

    char* const argv[] = { (char*)_exe_robo.c_str(), arg_id, arg_x, arg_y, NULL };

    pid_t pid_gestor = getpid();

    pthread_mutex_lock(&_mtx);

    if(_encerrando) {
        pthread_mutex_unlock(&_mtx);
        return -1;
    }

    if(buscar_por_id(msg.id) >= 0) {
        pthread_mutex_unlock(&_mtx);
        log_msg("GESTOR", "robô %d já está ativo; anúncio ignorado", msg.id);
        return -1;
    }

    int slot = -1;

    for(int i=0;i<MAX_ROBOS;i++) {
        if(!_robos[i].ativo) {
            slot = i;
            break;
        }
    }

    if(slot < 0) {
        pthread_mutex_unlock(&_mtx);
        log_msg("GESTOR", "frota cheia (%d robôs); robô %d recusado", MAX_ROBOS, msg.id);
        return -1;
    }

    // O fork acontece com _mtx travado: assim o filho é registrado na tabela
    // antes que a thread monitora possa recolhê-lo com waitpid().
    pid_t pid = fork();

    if(pid == 0) {
        // Filho: as threads do Gestor bloqueiam sinais e a máscara é herdada
        // pelo exec; o robô precisa recebê-los normalmente.
        sigset_t vazio;
        sigemptyset(&vazio);
        pthread_sigmask(SIG_SETMASK, &vazio, NULL);

        // Se o Gestor morrer, o robô recebe SIGTERM (não fica órfão)
        prctl(PR_SET_PDEATHSIG, SIGTERM);
        if(getppid() != pid_gestor)
            _exit(1);

        execv(argv[0], argv);
        _exit(127);
    }

    if(pid < 0) {
        int erro = errno;
        pthread_mutex_unlock(&_mtx);
        log_msg("GESTOR", "fork falhou para o robô %d: %s", msg.id, strerror(erro));
        return -1;
    }

    RegistroRobo& r = _robos[slot];
    r.ativo = true;
    r.id = msg.id;
    r.pid = pid;
    r.pos_x = msg.x;
    r.pos_y = msg.y;
    clock_gettime(CLOCK_REALTIME, &r.criado_em);

    _n_ativos++;
    int n_ativos = _n_ativos;

    EventoGestor evento;
    memset(&evento, 0, sizeof(evento));
    evento.tipo = EVENTO_ROBO_NOVO;
    evento.robo_id = r.id;
    evento.pid = r.pid;
    evento.pos_x = r.pos_x;
    evento.pos_y = r.pos_y;
    evento.timestamp = r.criado_em;

    pthread_cond_signal(&_cv_tem_filhos);

    // Passagem de bastão: pega o mutex de envio antes de soltar o da tabela,
    // então a ordem dos eventos na fila é a ordem das mudanças na tabela.
    pthread_mutex_lock(&_mtx_envio);
    pthread_mutex_unlock(&_mtx);

    enviar_evento(evento);

    pthread_mutex_unlock(&_mtx_envio);

    struct timespec t_fim;
    clock_gettime(CLOCK_MONOTONIC, &t_fim);

    log_msg("GESTOR", "robô %d criado: pid %d, posição (%d, %d), %d ativo(s), %ld us",
            msg.id, pid, msg.x, msg.y, n_ativos, us_entre(t_anuncio, t_fim));

    return slot;
}

void GestorRobos::loop_monitor(void) {

    while(true) {

        pthread_mutex_lock(&_mtx);

        // Sem filhos, waitpid() retornaria ECHILD na hora: dorme até existir um
        while(_n_ativos == 0 && !_encerrando)
            pthread_cond_wait(&_cv_tem_filhos, &_mtx);

        bool sair = (_n_ativos == 0);

        pthread_mutex_unlock(&_mtx);

        if(sair)
            break;

        int status;
        pid_t pid = waitpid(-1, &status, 0);

        if(pid < 0) {

            if(errno == EINTR)
                continue;

            // Não deveria acontecer: a tabela diz que há filhos mas o kernel não.
            log_msg("GESTOR", "waitpid: %s; limpando tabela", strerror(errno));

            pthread_mutex_lock(&_mtx);
            memset(_robos, 0, sizeof(_robos));
            _n_ativos = 0;
            pthread_cond_broadcast(&_cv_sem_filhos);
            pthread_mutex_unlock(&_mtx);

            continue;
        }

        robo_terminou(pid, status);
    }

    log_msg("GESTOR", "thread monitora encerrada");
}

void GestorRobos::robo_terminou(pid_t pid, int status) {

    pthread_mutex_lock(&_mtx);

    int slot = buscar_por_pid(pid);

    if(slot < 0) {
        pthread_mutex_unlock(&_mtx);
        log_msg("GESTOR", "processo %d desconhecido terminou", pid);
        return;
    }

    RegistroRobo& r = _robos[slot];

    EventoGestor evento;
    memset(&evento, 0, sizeof(evento));
    evento.tipo = EVENTO_ROBO_REMOVIDO;
    evento.robo_id = r.id;
    evento.pid = pid;
    evento.status_saida = status;
    clock_gettime(CLOCK_REALTIME, &evento.timestamp);

    r.ativo = false;
    _n_ativos--;
    int n_ativos = _n_ativos;

    if(_n_ativos == 0)
        pthread_cond_broadcast(&_cv_sem_filhos);

    pthread_mutex_lock(&_mtx_envio);
    pthread_mutex_unlock(&_mtx);

    enviar_evento(evento);

    pthread_mutex_unlock(&_mtx_envio);

    if(WIFEXITED(status))
        log_msg("GESTOR", "robô %d (pid %d) saiu com código %d; %d ativo(s)",
                evento.robo_id, pid, WEXITSTATUS(status), n_ativos);
    else if(WIFSIGNALED(status))
        log_msg("GESTOR", "robô %d (pid %d) terminado pelo sinal %d (%s); %d ativo(s)",
                evento.robo_id, pid, WTERMSIG(status), strsignal(WTERMSIG(status)), n_ativos);
}

void GestorRobos::enviar_evento(const EventoGestor& evento) {

    // Não espera o Organizador processar; só aguarda um pouco se a fila
    // estiver cheia, para não travar o Gestor caso o Organizador pare.
    if(mq_gestor_enviar(_mq, evento, TIMEOUT_ENVIO_MS) < 0)
        log_msg("GESTOR", "evento %s do robô %d não entregue ao Organizador: %s",
                evento_gestor_nome(evento.tipo), evento.robo_id, strerror(errno));
}

void GestorRobos::encerrar(void) {

    if(!_threads_ativas)
        return;

    _threads_ativas = false;

    log_msg("GESTOR", "encerrando");

    // 1. Para de aceitar robôs novos
    pthread_mutex_lock(&_mtx);
    _encerrando = true;
    pthread_cond_broadcast(&_cv_tem_filhos);
    pthread_mutex_unlock(&_mtx);

    _fonte->interromper();
    pthread_join(_th_receptor, NULL);

    // 2. Pede para todos os robôs terminarem
    pthread_mutex_lock(&_mtx);

    for(int i=0;i<MAX_ROBOS;i++) {
        if(_robos[i].ativo)
            kill(_robos[i].pid, SIGTERM);
    }

    // 3. Espera a thread monitora recolher todos, com prazo
    struct timespec prazo;
    prazo_em(&prazo, TIMEOUT_TERMINO_MS);

    int ret = 0;

    while(_n_ativos > 0 && ret != ETIMEDOUT)
        ret = pthread_cond_timedwait(&_cv_sem_filhos, &_mtx, &prazo);

    // 4. Quem não saiu a tempo é morto à força
    for(int i=0;i<MAX_ROBOS;i++) {
        if(_robos[i].ativo) {
            log_msg("GESTOR", "robô %d (pid %d) não respondeu ao SIGTERM; enviando SIGKILL",
                    _robos[i].id, _robos[i].pid);
            kill(_robos[i].pid, SIGKILL);
        }
    }

    pthread_mutex_unlock(&_mtx);

    pthread_join(_th_monitor, NULL);

    log_msg("GESTOR", "encerrado");
}

int GestorRobos::robos_ativos(void) {

    pthread_mutex_lock(&_mtx);
    int n = _n_ativos;
    pthread_mutex_unlock(&_mtx);

    return n;
}
