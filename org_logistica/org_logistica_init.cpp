#include "org_logistica_init.hpp"
#include <fstream>

// Define para gerar um terminal separado para o gestor de robôs.
// Se for utilizar, retirar o comemntário abaixo
// #define GESTOR_NOVO_TERMINAL

// Define para gerar um terminal separado para o coletor de dados.
// Se for utilizar, retirar o comemntário abaixo
// #define COLETOR_NOVO_TERMINAL



#define DATA_PATH "data/"

/**
 * @brief Função simples, só pra gerar um timestamp para salvar os dados
 * @return Retorna a timestamp em string
*/
inline std::string get_timestamp(void) {

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_local;
    localtime_r(&ts.tv_sec, &tm_local);

    char hora[32];
    strftime(hora, sizeof(hora), "%Y-%m-%d_%H:%M:%S", &tm_local);

    return std::string(hora);
}


pid_t org_logistica_init_gestor(void) {

    pid_t pid_gestor = fork();

    if(pid_gestor < 0) 
        return (pid_t)-1;

    #ifdef GESTOR_NOVO_TERMINAL

    if(pid_gestor == 0) {

          execlp(
                "gnome-terminal",
                "gnome-terminal",
                "--",
                "bash",
                "-c",
                "echo $$ > /tmp/gestor_robos.pid; exec ./bin/gestor_robos",
                (char*)NULL
        );


    }
    
    #endif

    #ifndef GESTOR_NOVO_TERMINAL
 
    if(pid_gestor == 0) {

          execlp(
                "./bin/gestor_robos",
                "gestor",
                (char*)NULL
        );
    }

    #endif

     return pid_gestor;
}

void org_logistica_close_gestor(pid_t pid_gestor) {

    #ifdef GESTOR_NOVO_TERMINAL

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
    
    kill(pid_gestor, SIGTERM);
    
    #endif

}

pid_t org_logistica_init_coletor(void) {

    // Gera a string com o caminho da pasta que será utilizada
    std::string data_path = DATA_PATH;
    data_path += "data_on_";
    data_path += get_timestamp();

    // Cria um arquivo novo para salvar os arquivos

    pid_t pid_mkdir = fork();
    
     if(pid_mkdir == 0) {

          execlp(
                "mkdir",
                "cria_dir",
                data_path.c_str(),
                (char*)NULL
        );

        _exit(1);
    }

    int status_mkdir;

    waitpid(pid_mkdir, &status_mkdir, NULL);

    // ============================================
    
    pid_t pid_coletor = fork();

    if(pid_coletor < 0) 
        return (pid_t)-1;

    #ifdef COLETOR_NOVO_TERMINAL

    // Gera a string para o comando usado na criação de um terminal
    std::string echo_cmd_s = "echo $$ > /tmp/coletor.pid; exec ./bin/coletor --dir ";
    echo_cmd_s += data_path;

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


     }
    
    #endif

    #ifndef COLETOR_NOVO_TERMINAL

    if(pid_coletor == 0) {

          execlp(
                "./bin/coletor",
                "coletor",
                "--dir",
                data_path.c_str(),
                (char*)NULL
        );
    }

    #endif

     return pid_coletor;
}

void org_logistica_close_coletor(pid_t pid_coletor) {

    #ifdef COLETOR_NOVO_TERMINAL

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
    
    kill(pid_coletor, SIGTERM);
    
    #endif

}
