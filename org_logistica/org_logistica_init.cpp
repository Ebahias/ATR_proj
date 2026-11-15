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

// Prazo para cada filho terminar depois do SIGTERM. O Gestor encerra os
// robôs e espera por eles antes de sair, por isso tem mais tempo.
#define ESPERA_FIM_GESTOR_MS    10000
#define ESPERA_FIM_COLETOR_MS   5000

// Intervalo entre as verificações (sem espera ocupada)
#define INTERVALO_VERIFICA_MS   100

// Depois de pronto, o filho ainda pode falhar logo em seguida (ex.: Gestor
// com --fonte mqtt sem broker, exec do Coletor que falhou): observa mais um
// pouco antes de seguir
#define ESTABILIZACAO_MS        500

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

    // A máscara de sinais é herdada pelo exec: se o Organizador já estiver
    // com SIGINT/SIGTERM bloqueados, o filho também ficaria
    sigset_t vazio;
    sigemptyset(&vazio);
    sigprocmask(SIG_SETMASK, &vazio, NULL);

    prctl(PR_SET_PDEATHSIG, SIGTERM);

    // O pai pode ter morrido antes do prctl
    if(getppid() != pai)
        _exit(1);
}
#endif

static void registrar_saida(const char* nome, pid_t pid, int status) {

    if(WIFEXITED(status))
        log_msg("ORGANIZADOR", "%s (pid %d) terminou com código %d", nome, pid, WEXITSTATUS(status));
    else if(WIFSIGNALED(status))
        log_msg("ORGANIZADOR", "%s (pid %d) terminou por sinal %d (%s)", nome, pid, WTERMSIG(status), strsignal(WTERMSIG(status)));
}

// Manda SIGTERM e espera o filho com waitpid (sem isso ele viraria zumbi e o
// Organizador sairia antes de ele terminar). Esgotado o prazo, SIGKILL.
static void encerrar_filho(pid_t pid, const char* nome, int prazo_ms) {

    kill(pid, SIGTERM);

    struct timespec intervalo = { 0, INTERVALO_VERIFICA_MS * 1000000L };

    for(int ms=0;ms<prazo_ms;ms+=INTERVALO_VERIFICA_MS) {

        int status;
        pid_t r = waitpid(pid, &status, WNOHANG);

        if(r == pid) {
            registrar_saida(nome, pid, status);
            return;
        }

        if(r < 0 && errno == ECHILD)
            return;

        nanosleep(&intervalo, NULL);
    }

    log_msg("ORGANIZADOR", "%s (pid %d) não terminou em %d ms; enviando SIGKILL", nome, pid, prazo_ms);

    kill(pid, SIGKILL);

    int status;
    if(waitpid(pid, &status, 0) == pid)
        registrar_saida(nome, pid, status);
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

#if !defined(GESTOR_NOVO_TERMINAL) || !defined(COLETOR_NOVO_TERMINAL)
// true se o filho já terminou (e registra como)
static bool filho_terminou(pid_t pid, const char* nome) {

    int status;

    if(waitpid(pid, &status, WNOHANG) != pid)
        return false;

    log_msg("ORGANIZADOR", "%s terminou antes de ficar pronto", nome);
    registrar_saida(nome, pid, status);

    return true;
}

// Observa o filho por ms milissegundos; false se ele terminar nesse tempo
static bool continua_vivo(pid_t pid, const char* nome, int ms) {

    struct timespec intervalo = { 0, INTERVALO_VERIFICA_MS * 1000000L };

    for(int t=0;t<ms;t+=INTERVALO_VERIFICA_MS) {
        if(filho_terminou(pid, nome))
            return false;
        nanosleep(&intervalo, NULL);
    }

    return !filho_terminou(pid, nome);
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

        // Só chega aqui se o exec falhou. Sem o _exit, o filho continuaria
        // rodando uma segunda cópia do Organizador.
        perror(exe.c_str());
        _exit(127);
    }

    #endif

    log_msg("ORGANIZADOR", "Gestor de Robôs iniciado (pid %d)", pid_gestor);

    return pid_gestor;
}

bool org_logistica_esperar_gestor(pid_t pid_gestor, int timeout_ms) {

    struct timespec intervalo = { 0, INTERVALO_VERIFICA_MS * 1000000L };

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
        ms += INTERVALO_VERIFICA_MS;
    }

    #ifndef GESTOR_NOVO_TERMINAL
    if(pid_gestor > 0 && !continua_vivo(pid_gestor, "Gestor de Robôs", ESTABILIZACAO_MS))
        return false;
    #endif

    log_msg("ORGANIZADOR", "Gestor de Robôs pronto (%s criada em %d ms)", SHM_FROTA, ms);

    return true;
}

bool org_logistica_esperar_coletor(pid_t pid_coletor) {

    #ifndef COLETOR_NOVO_TERMINAL
    if(pid_coletor > 0 && !continua_vivo(pid_coletor, "Coletor de Dados", ESTABILIZACAO_MS))
        return false;
    #else
    (void)pid_coletor;
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

    encerrar_filho(pid_gestor, "Gestor de Robôs", ESPERA_FIM_GESTOR_MS);

    #endif

}

int org_logistica_carregar_cargas(const std::string& arquivo, std::vector<Baia_carga*>& baias) {

    FILE* f = fopen(arquivo.c_str(), "r");

    if(f == NULL) {
        log_msg("ORGANIZADOR", "%s: %s; sem cargas iniciais (caixas novas só pelo comando CAIXA)",
                arquivo.c_str(), strerror(errno));
        return 0;
    }

    char linha[256];
    int n_linha = 0;
    int carregadas = 0;

    while(fgets(linha, sizeof(linha), f) != NULL) {

        n_linha++;

        // Tira o fim de linha (\n ou \r\n, se o arquivo foi editado no Windows)
        linha[strcspn(linha, "\r\n")] = '\0';

        const char* p = linha + strspn(linha, " \t");

        if(*p == '\0' || *p == '#')
            continue;

        int baia, tamanho;
        char sobra;

        if(sscanf(p, "%d %d %c", &baia, &tamanho, &sobra) != 2
           || baia < 1 || baia > (int)baias.size() || tamanho < 1 || tamanho > 4) {
            log_msg("ORGANIZADOR", "%s, linha %d inválida ignorada: \"%s\" (esperado <baia 1-%d> <tamanho 1-4>)",
                    arquivo.c_str(), n_linha, linha, (int)baias.size());
            continue;
        }

        // Mesmo limite das caixas do comando CAIXA: o arquivo também não
        // pode passar da capacidade da baia
        if(baias[baia - 1]->add_cargo(Carga(tamanho)) < 0) {
            log_msg("ORGANIZADOR", "%s, linha %d: baia %d cheia (%d caixas), caixa de tamanho %d recusada",
                    arquivo.c_str(), n_linha, baia, MAX_CARGAS_BAIA, tamanho);
            continue;
        }

        carregadas++;
    }

    fclose(f);

    log_msg("ORGANIZADOR", "%d caixas carregadas de %s", carregadas, arquivo.c_str());

    return carregadas;
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

        perror(exe.c_str());
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

    encerrar_filho(pid_coletor, "Coletor de Dados", ESPERA_FIM_COLETOR_MS);

    #endif

}
