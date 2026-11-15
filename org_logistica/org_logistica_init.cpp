#include "org_logistica_init.hpp"
#include "comum/ipc_robo.hpp"
#include "comum/log.hpp"

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

// Define para gerar um terminal separado para o gestor de robôs.
// Se for utilizar, retirar o comemntário abaixo
// #define GESTOR_NOVO_TERMINAL

// Define para gerar um terminal separado para o coletor de dados.
// Se for utilizar, retirar o comemntário abaixo
// #define COLETOR_NOVO_TERMINAL



#define DATA_PATH "data/"

// Tempo que o Gestor e o Coletor têm para terminar depois do SIGTERM. O
// Gestor encerra os robôs e espera por eles antes de sair.
#define ESPERA_FILHO_MS         5000

// Depois que a /atr_frota aparece, o Gestor ainda pode falhar logo em
// seguida (ex.: --fonte mqtt sem broker): observa o filho mais um pouco
#define ESTABILIZACAO_GESTOR_MS 500

/**
 * @brief Função simples, só pra gerar um timestamp para salvar os dados
 *
 * Sem ':' no nome (ex.: 2026-11-14_22-29-46): ':' não é aceito em pastas
 * de disco do Windows, como as acessadas pelo WSL em /mnt/c.
 *
 * @return Retorna a timestamp em string
*/
inline std::string get_timestamp(void) {

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_local;
    localtime_r(&ts.tv_sec, &tm_local);

    char hora[32];
    strftime(hora, sizeof(hora), "%Y-%m-%d_%H-%M-%S", &tm_local);

    return std::string(hora);
}

// Os programas ficam na mesma pasta do org_logistica (bin/): não depende de
// onde o Organizador foi chamado
static std::string pasta_do_executavel(void) {

    char caminho[4096];
    ssize_t n = readlink("/proc/self/exe", caminho, sizeof(caminho) - 1);

    if(n <= 0)
        return "./bin";

    caminho[n] = '\0';

    std::string s(caminho);

    return s.substr(0, s.find_last_of('/'));
}

#if !defined(GESTOR_NOVO_TERMINAL) || !defined(COLETOR_NOVO_TERMINAL)
// No filho, antes do exec:
//  - grupo de processos próprio: o Ctrl+C do terminal chega só ao
//    Organizador, que encerra o Gestor e o Coletor na ordem certa. Antes,
//    os três recebiam o SIGINT juntos e o Gestor matava os robôs no meio
//    das missões;
//  - se o Organizador morrer sem encerrar os filhos (kill -9), eles
//    recebem SIGTERM e não ficam órfãos.
static void preparar_filho(pid_t pai) {

    setpgid(0, 0);

    prctl(PR_SET_PDEATHSIG, SIGTERM);

    // O pai pode ter morrido antes do prctl
    if(getppid() != pai)
        _exit(1);
}
#endif

// Espera o filho terminar; se passar do tempo, mata com SIGKILL
static void esperar_filho(pid_t pid, const char* nome) {

    struct timespec intervalo = { 0, 50 * 1000000L };

    for(int ms=0;ms<ESPERA_FILHO_MS;ms+=50) {

        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);

        if(r == pid || (r < 0 && errno == ECHILD))
            return;

        nanosleep(&intervalo, NULL);
    }

    log_msg("ORGANIZADOR", "%s (pid %d) não terminou em %d ms; enviando SIGKILL", nome, pid, ESPERA_FILHO_MS);

    kill(pid, SIGKILL);
    waitpid(pid, NULL, 0);
}

// Pronta quando existe e já tem o tamanho final. Só existir não basta: entre
// o shm_open e o ftruncate do Gestor o objeto tem tamanho 0, e mapeá-lo daria
// SIGBUS ao ler. O número mágico, escrito por último, o shm_frota_abrir confere.
static bool frota_pronta(void) {

    int fd = shm_open(SHM_FROTA, O_RDONLY, 0);

    if(fd < 0)
        return false;

    struct stat st;
    bool pronta = (fstat(fd, &st) == 0 && st.st_size >= (off_t)sizeof(ShmFrota));

    close(fd);

    return pronta;
}

#ifndef GESTOR_NOVO_TERMINAL
// true se o filho já terminou (e registra como)
static bool filho_terminou(pid_t pid, const char* nome) {

    int status;

    if(waitpid(pid, &status, WNOHANG) != pid)
        return false;

    if(WIFEXITED(status))
        log_msg("ORGANIZADOR", "%s terminou com código %d antes de ficar pronto", nome, WEXITSTATUS(status));
    else
        log_msg("ORGANIZADOR", "%s terminou por sinal %d antes de ficar pronto", nome, WTERMSIG(status));

    return true;
}
#endif


pid_t org_logistica_init_gestor(const std::vector<std::string>& args) {

    std::string exe = pasta_do_executavel() + "/gestor_robos";

    #ifdef GESTOR_NOVO_TERMINAL

    std::string cmd = "echo $$ > /tmp/gestor_robos.pid; exec " + exe;
    for(auto& a : args)
        cmd += " " + a;

    #else

    // argv montado antes do fork (o filho só chama funções simples até o exec)
    std::vector<char*> argv;
    argv.push_back((char*)"gestor_robos");
    for(auto& a : args)
        argv.push_back((char*)a.c_str());
    argv.push_back(NULL);

    pid_t pai = getpid();

    #endif

    pid_t pid_gestor = fork();

    if(pid_gestor < 0) {
        log_msg("ORGANIZADOR", "fork do Gestor de Robôs: %s", strerror(errno));
        return (pid_t)-1;
    }

    #ifdef GESTOR_NOVO_TERMINAL

    if(pid_gestor == 0) {

          execlp(
                "gnome-terminal",
                "gnome-terminal",
                "--",
                "bash",
                "-c",
                cmd.c_str(),
                (char*)NULL
        );

        // Sem o _exit, um exec que falha deixaria o filho rodando uma
        // segunda cópia do Organizador
        _exit(127);
    }

    #endif

    #ifndef GESTOR_NOVO_TERMINAL

    if(pid_gestor == 0) {

        preparar_filho(pai);

        execv(exe.c_str(), argv.data());

        log_msg("ORGANIZADOR", "exec %s: %s", exe.c_str(), strerror(errno));
        _exit(127);
    }

    #endif

    log_msg("ORGANIZADOR", "Gestor de Robôs iniciado (pid %d)", pid_gestor);

    return pid_gestor;
}

bool org_logistica_esperar_gestor(pid_t pid_gestor, int timeout_ms) {

    struct timespec intervalo = { 0, 50 * 1000000L };

    int ms = 0;

    while(!frota_pronta()) {

        #ifndef GESTOR_NOVO_TERMINAL
        if(pid_gestor > 0 && filho_terminou(pid_gestor, "Gestor de Robôs"))
            return false;
        #endif

        if(ms >= timeout_ms) {
            log_msg("ORGANIZADOR", "o Gestor de Robôs não criou %s em %d ms", SHM_FROTA, timeout_ms);
            return false;
        }

        nanosleep(&intervalo, NULL);
        ms += 50;
    }

    #ifndef GESTOR_NOVO_TERMINAL
    for(int t=0;t<ESTABILIZACAO_GESTOR_MS && pid_gestor > 0;t+=50) {
        if(filho_terminou(pid_gestor, "Gestor de Robôs"))
            return false;
        nanosleep(&intervalo, NULL);
    }
    #endif

    return true;
}

void org_logistica_close_gestor(pid_t pid_gestor) {

    #ifdef GESTOR_NOVO_TERMINAL

    (void)pid_gestor;

    std::ifstream file("/tmp/gestor_robos.pid");

    pid_t pid;

    if (!(file >> pid)) {
        return;
    }

    file.close();

    // Solicita encerramento do gestor
    kill(pid, SIGTERM);

    #endif

    #ifndef GESTOR_NOVO_TERMINAL

    if(pid_gestor <= 0)
        return;

    kill(pid_gestor, SIGTERM);

    esperar_filho(pid_gestor, "Gestor de Robôs");

    #endif

}

pid_t org_logistica_init_coletor(void) {

    // Gera a string com o caminho da pasta que será utilizada
    std::string data_path = DATA_PATH;
    data_path += "data_on_";
    data_path += get_timestamp();

    // Cria data/ (se ainda não existir) e a pasta desta execução. mkdir()
    // direto, em vez de rodar o programa mkdir num processo filho.
    if(mkdir(DATA_PATH, 0775) < 0 && errno != EEXIST) {
        log_msg("ORGANIZADOR", "mkdir(%s): %s", DATA_PATH, strerror(errno));
        return (pid_t)-1;
    }

    if(mkdir(data_path.c_str(), 0775) < 0 && errno != EEXIST) {
        log_msg("ORGANIZADOR", "mkdir(%s): %s", data_path.c_str(), strerror(errno));
        return (pid_t)-1;
    }

    // ============================================

    std::string exe = pasta_do_executavel() + "/coletor";

    #ifdef COLETOR_NOVO_TERMINAL

    // Gera a string para o comando usado na criação de um terminal
    std::string echo_cmd_s = "echo $$ > /tmp/coletor.pid; exec " + exe + " --dir ";
    echo_cmd_s += data_path;

    #else

    pid_t pai = getpid();

    #endif

    pid_t pid_coletor = fork();

    if(pid_coletor < 0) {
        log_msg("ORGANIZADOR", "fork do Coletor de Dados: %s", strerror(errno));
        return (pid_t)-1;
    }

    #ifdef COLETOR_NOVO_TERMINAL

    if(pid_coletor == 0) {

          execlp(
                "gnome-terminal",
                "gnome-terminal",
                "--",
                "bash",
                "-c",
                echo_cmd_s.c_str(),
                (char*)NULL
        );

        _exit(127);
     }

    #endif

    #ifndef COLETOR_NOVO_TERMINAL

    if(pid_coletor == 0) {

        preparar_filho(pai);

          execl(
                exe.c_str(),
                "coletor",
                "--dir",
                data_path.c_str(),
                (char*)NULL
        );

        log_msg("ORGANIZADOR", "exec %s: %s", exe.c_str(), strerror(errno));
        _exit(127);
    }

    #endif

    log_msg("ORGANIZADOR", "Coletor de Dados iniciado (pid %d), dados em %s", pid_coletor, data_path.c_str());

     return pid_coletor;
}

void org_logistica_close_coletor(pid_t pid_coletor) {

    #ifdef COLETOR_NOVO_TERMINAL

    (void)pid_coletor;

    std::ifstream file("/tmp/coletor.pid");

    pid_t pid;

    if (!(file >> pid)) {
        return;
    }

    file.close();

    // Solicita encerramento do gestor
    kill(pid, SIGTERM);

    #endif

    #ifndef COLETOR_NOVO_TERMINAL

    if(pid_coletor <= 0)
        return;

    kill(pid_coletor, SIGTERM);

    esperar_filho(pid_coletor, "Coletor de Dados");

    #endif

}
