#include "ponte_mqtt.hpp"
#include "despacho.hpp"
#include "robo_manager.hpp"

#include "comum/cliente_mqtt.hpp"
#include "comum/ipc_coletor.hpp"
#include "comum/log.hpp"
#include "comum/mapa.hpp"
#include "comum/mqtt_topicos.hpp"
#include "comum/periodico.hpp"

#include <atomic>
#include <cerrno>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <string>
#include <vector>

#include <pthread.h>
#include <time.h>

// Intervalo entre tentativas da primeira conexão, se o broker não estava no ar
#define PONTE_ESPERA_CONEXAO_S      5

// A thread de cargas acorda pelo menos neste intervalo para perceber uma
// reconexão: o broker reiniciado perde a mensagem retida e ela é republicada
#define PONTE_VERIFICA_CARGAS_MS    500

// Limite da fila de comandos da Tela: o callback nunca espera, descarta
#define PONTE_MAX_COMANDOS          64

#define PONTE_CLIENT_ID             "organizador"

enum TipoComandoOperacao {
    OP_INVALIDO = 0,
    OP_MODO,                    // a = robô, b = ModoRobo
    OP_IR,                      // a = robô, b = x, c = y
    OP_PARAR,                   // a = robô
    OP_CAIXA                    // a = baia (1..4), b = tamanho
};

struct ComandoOperacao {
    int tipo;
    int a;
    int b;
    int c;
    char texto[96];             // payload original, para os logs
};

static ClienteMqtt* g_cliente = NULL;

static Robo_manager* g_manager = NULL;

static std::atomic<bool> g_rodando(false);

static pthread_t g_th_status, g_th_coletor, g_th_cargas, g_th_comandos, g_th_conexao;
static bool g_criou_status = false, g_criou_coletor = false, g_criou_cargas = false;
static bool g_criou_comandos = false, g_criou_conexao = false;

// Inicializadores estáticos (e nunca destruídos): ponte_mqtt_cargas_mudaram
// pode ser chamada por uma missão antes de a ponte iniciar ou depois de ela
// encerrar
static pthread_mutex_t g_mtx_cargas = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_cargas = PTHREAD_COND_INITIALIZER;
static bool g_cargas_mudaram = true;

static pthread_mutex_t g_mtx_cmd = PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t g_cv_cmd = PTHREAD_COND_INITIALIZER;
static std::deque<ComandoOperacao> g_fila_cmd;
static unsigned long g_cmd_descartados = 0;


/* ------------------------------------------------------------------ */
/* JSON (montado à mão: o contrato é pequeno e fixo)                   */
/* ------------------------------------------------------------------ */

static long long ms_de(const struct timespec& t) {

    return (long long)t.tv_sec * 1000LL + t.tv_nsec / 1000000L;
}

static long long agora_ms(void) {

    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);

    return ms_de(t);
}

static void json_fmt(std::string& s, const char* fmt, ...) __attribute__((format(printf, 2, 3)));

static void json_fmt(std::string& s, const char* fmt, ...) {

    char buf[256];
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if(n > 0)
        s.append(buf, n < (int)sizeof(buf) ? n : (int)sizeof(buf) - 1);
}

// String JSON entre aspas. Os textos vêm dos robôs (eventos) e das áreas do
// mapa; escapa " e \ e troca caracteres de controle por \u00XX. Bytes UTF-8
// passam como estão (ex.: "automático").
static void json_texto(std::string& s, const char* txt, size_t max_len) {

    s += '"';

    for(size_t i=0;i<max_len && txt[i] != '\0';i++) {

        unsigned char ch = (unsigned char)txt[i];

        if(ch == '"' || ch == '\\') {
            s += '\\';
            s += (char)ch;
        } else if(ch < 0x20) {
            json_fmt(s, "\\u%04x", ch);
        } else {
            s += (char)ch;
        }
    }

    s += '"';
}

static void json_texto(std::string& s, const char* txt) {

    json_texto(s, txt, (size_t)-1);
}

static void montar_frota(std::string& s, const std::vector<DadosStatus>& frota) {

    s.clear();
    json_fmt(s, "{\"t\": %lld, \"robos\": [", agora_ms());

    for(size_t i=0;i<frota.size();i++) {

        const DadosStatus& d = frota[i];

        if(i > 0)
            s += ", ";

        json_fmt(s, "{\"id\": %d, \"estado\": ", d.robo_id);
        json_texto(s, estado_robo_nome(d.estado));
        s += ", \"modo\": ";
        json_texto(s, modo_robo_nome(d.modo));
        json_fmt(s, ", \"x\": %d, \"y\": %d, \"sp_x\": %d, \"sp_y\": %d, \"bateria\": %d, \"atracada\": %d, \"falha\": %d, \"distancia\": %.1f, \"fase\": ",
                 d.pos_x, d.pos_y, d.sp_x, d.sp_y, d.bateria, d.carga_atracada ? 1 : 0, d.falha ? 1 : 0, d.distancia_total);
        json_texto(s, fase_tarefa_nome(d.fase));
        json_fmt(s, ", \"tarefa\": %d, \"progresso\": %d, \"passagem\": %d, \"aguardando_passagem\": %d}",
                 d.tarefa_id, d.progresso, d.passagem, d.aguardando_passagem);
    }

    s += "]}";
}

static void montar_registro(std::string& s, const RegistroColetor& r) {

    s.clear();

    if(r.tipo == REG_EVENTO) {

        json_fmt(s, "{\"tipo\": \"evento\", \"t\": %lld, \"robo\": %d, \"evento\": ", ms_de(r.timestamp), r.robo_id);
        json_texto(s, tipo_evento_robo_nome(r.tipo_evento));
        s += ", \"texto\": ";
        json_texto(s, r.texto, sizeof(r.texto));
        s += "}";
        return;
    }

    json_fmt(s, "{\"tipo\": \"carga\", \"t\": %lld, \"tarefa\": %d, \"baia\": ", ms_de(r.timestamp), r.tarefa_id);
    json_texto(s, r.baia, sizeof(r.baia));
    s += ", \"destino\": ";
    json_texto(s, r.destino, sizeof(r.destino));
    s += ", \"robos\": [";

    int n = r.n_robos;
    if(n < 0)
        n = 0;
    if(n > MAX_ROBOS_CARGA)
        n = MAX_ROBOS_CARGA;

    for(int i=0;i<n;i++)
        json_fmt(s, i > 0 ? ", %d" : "%d", r.robos[i]);

    json_fmt(s, "], \"t_coleta\": %lld, \"completa\": %d}", ms_de(r.t_coleta), r.completa ? 1 : 0);
}

static void montar_cargas(std::string& s, const EstadoCargas& e) {

    s.clear();
    json_fmt(s, "{\"t\": %lld, \"baias\": [", agora_ms());

    for(size_t b=0;b<e.pendentes.size();b++) {

        json_fmt(s, "%s{\"baia\": %d, \"pendentes\": [", b > 0 ? ", " : "", (int)b + 1);

        for(size_t i=0;i<e.pendentes[b].size();i++)
            json_fmt(s, i > 0 ? ", %d" : "%d", e.pendentes[b][i]);

        s += "]}";
    }

    s += "], \"em_transporte\": [";

    for(size_t i=0;i<e.em_transporte.size();i++) {

        const MissaoInfo& m = e.em_transporte[i];

        json_fmt(s, "%s{\"tarefa\": %d, \"baia\": %d, \"destino\": ", i > 0 ? ", " : "", m.tarefa, m.baia + 1);

        // Zona ainda não escolhida (a missão espera pontos livres): null
        const AreaMapa* zona = m.zona >= 0 ? mapa_buscar_area(AREA_ZONA, m.zona + 1) : NULL;

        if(zona != NULL)
            json_texto(s, zona->nome);
        else
            s += "null";

        json_fmt(s, ", \"tamanho\": %d, \"robos\": [", m.tamanho);

        for(size_t k=0;k<m.robos.size();k++)
            json_fmt(s, k > 0 ? ", %d" : "%d", m.robos[k]);

        s += "]}";
    }

    json_fmt(s, "], \"entregues\": %d}", e.entregues);
}


/* ------------------------------------------------------------------ */
/* Threads                                                             */
/* ------------------------------------------------------------------ */

// Periódica: lê a tabela da /atr_frota e publica o status de todos os robôs.
// QoS 0 e retida (contrato): a Tela que conecta já recebe o último status, e
// perder um ciclo não importa, o próximo vem em 200 ms.
static void* thread_status(void*) {

    Periodo periodo(MQTT_PERIODO_FROTA_STATUS_MS);

    std::vector<DadosStatus> frota;
    std::string json;
    json.reserve(16384);

    while(g_rodando) {

        if(g_cliente->conectado()) {
            despacho_ler_frota(frota);
            montar_frota(json, frota);
            g_cliente->publicar(MQTT_TOPICO_FROTA_STATUS, json, MQTT_QOS_FROTA_STATUS, MQTT_RETAIN_FROTA_STATUS);
        }

        periodo.esperar();
    }

    log_msg("PONTE", "status da frota: %lu ciclos, %lu prazos perdidos, pior execução %ld us",
            periodo.ciclos(), periodo.atrasos(), periodo.max_exec_us());

    return NULL;
}

static void fechar_fila(void* arg) {

    mq_close(*(mqd_t*)arg);
}

// Único consumidor da fila do Coletor. Bloqueia em mq_receive, que é ponto
// de cancelamento: é assim que o encerramento a tira daqui. Durante a
// publicação o cancelamento fica desligado, porque a libmosquitto trava
// mutexes internos e uma thread cancelada no meio os deixaria travados.
static void* thread_coletor(void*) {

    mqd_t mq = mq_coletor_abrir_leitura();

    if(mq == (mqd_t)-1) {
        log_msg("PONTE", "fila %s indisponível: eventos não serão publicados", MQ_COLETOR_ORGANIZADOR);
        return NULL;
    }

    pthread_cleanup_push(fechar_fila, &mq);

    RegistroColetor r;
    std::string json;
    struct timespec espera_erro = { 1, 0 };

    while(g_rodando) {

        if(mq_coletor_receber(mq, r) < 0) {
            if(errno != EINTR) {
                log_msg("PONTE", "leitura da fila do Coletor: %s", strerror(errno));
                nanosleep(&espera_erro, NULL);
            }
            continue;
        }

        int antigo;
        pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &antigo);

        montar_registro(json, r);
        g_cliente->publicar(MQTT_TOPICO_EVENTOS, json, MQTT_QOS_EVENTOS, MQTT_RETAIN_EVENTOS);

        pthread_setcancelstate(antigo, NULL);
    }

    pthread_cleanup_pop(1);

    return NULL;
}

// Publica atr/cargas quando o Organizador avisa uma mudança, e de novo depois
// de uma reconexão (um broker reiniciado esquece as mensagens retidas).
static void* thread_cargas(void*) {

    bool estava_conectado = false;
    bool pendente = true;
    std::string json;

    while(true) {

        pthread_mutex_lock(&g_mtx_cargas);

        if(!g_cargas_mudaram && g_rodando) {
            struct timespec limite;
            clock_gettime(CLOCK_REALTIME, &limite);
            limite.tv_nsec += PONTE_VERIFICA_CARGAS_MS * 1000000L;
            if(limite.tv_nsec >= 1000000000L) {
                limite.tv_nsec -= 1000000000L;
                limite.tv_sec++;
            }
            pthread_cond_timedwait(&g_cv_cargas, &g_mtx_cargas, &limite);
        }

        bool mudou = g_cargas_mudaram;
        g_cargas_mudaram = false;

        pthread_mutex_unlock(&g_mtx_cargas);

        if(!g_rodando)
            break;

        bool conectado = g_cliente->conectado();

        if(mudou || (conectado && !estava_conectado))
            pendente = true;

        estava_conectado = conectado;

        if(!pendente || !conectado)
            continue;

        montar_cargas(json, g_manager->estado_cargas());

        if(g_cliente->publicar(MQTT_TOPICO_CARGAS, json, MQTT_QOS_CARGAS, MQTT_RETAIN_CARGAS))
            pendente = false;
    }

    return NULL;
}

static bool robo_existe(const ComandoOperacao& c) {

    if(despacho_slot_do_robo(c.a) >= 0)
        return true;

    log_msg("PONTE", "comando \"%s\" ignorado: robô %d inexistente", c.texto, c.a);

    return false;
}

static void enviar_operador(const ComandoOperacao& c, ComandoRobo& cmd) {

    if(despacho_enviar(c.a, cmd, ORIGEM_OPERADOR))
        log_msg("PONTE", "operação: \"%s\" -> %s para o robô %d", c.texto, tipo_comando_nome(cmd.tipo), c.a);
}

static void executar(const ComandoOperacao& c) {

    ComandoRobo cmd;
    memset(&cmd, 0, sizeof(cmd));

    switch(c.tipo) {

        case OP_MODO:
            if(!robo_existe(c))
                return;
            cmd.tipo = CMD_MODO;
            cmd.valor = c.b;
            enviar_operador(c, cmd);
            return;

        case OP_IR: {
            if(!robo_existe(c))
                return;
            if(!mapa_dentro(c.b, c.c)) {
                log_msg("PONTE", "comando \"%s\" ignorado: (%d, %d) fora do mapa", c.texto, c.b, c.c);
                return;
            }
            // Em automático o robô ignora movimento vindo do operador. Envia
            // assim mesmo (o robô decide), mas deixa o motivo no log daqui.
            DadosStatus d;
            if(despacho_ler_status(c.a, d) && d.modo != MODO_MANUAL)
                log_msg("PONTE", "aviso: robô %d em modo %s ignora IR do operador (envie \"MODO %d manual\" antes)",
                        c.a, modo_robo_nome(d.modo), c.a);
            cmd.tipo = CMD_IR_PARA;
            cmd.x = c.b;
            cmd.y = c.c;
            enviar_operador(c, cmd);
            return;
        }

        case OP_PARAR:
            if(!robo_existe(c))
                return;
            cmd.tipo = CMD_PARAR;
            enviar_operador(c, cmd);
            return;

        case OP_CAIXA:
            if(c.a < 1 || c.a > (int)g_manager->_b_carga.size() || c.b < 1 || c.b > 4) {
                log_msg("PONTE", "comando \"%s\" ignorado: baia 1 a %d, tamanho 1 a 4",
                        c.texto, (int)g_manager->_b_carga.size());
                return;
            }
            g_manager->_b_carga[c.a - 1]->add_cargo(Carga(c.b));
            ponte_mqtt_cargas_mudaram();
            log_msg("PONTE", "operação: carga de tamanho %d acrescentada à Baia %d", c.b, c.a);
            return;

        default:
            log_msg("PONTE", "comando inválido ignorado: \"%s\"", c.texto);
            return;
    }
}

// Consome os comandos que o callback enfileirou. É aqui, e não no callback,
// que se fala com os robôs e com as baias.
static void* thread_comandos(void*) {

    while(true) {

        pthread_mutex_lock(&g_mtx_cmd);

        while(g_fila_cmd.empty() && g_rodando)
            pthread_cond_wait(&g_cv_cmd, &g_mtx_cmd);

        if(!g_rodando) {
            pthread_mutex_unlock(&g_mtx_cmd);
            break;
        }

        ComandoOperacao c = g_fila_cmd.front();
        g_fila_cmd.pop_front();

        pthread_mutex_unlock(&g_mtx_cmd);

        executar(c);
    }

    return NULL;
}

// conectar() faz o connect() TCP, que é ponto de cancelamento; cancelada no
// meio, a libmosquitto ficaria num estado inconsistente
static bool conectar_sem_cancelar(void) {

    int antigo;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &antigo);

    bool ok = g_cliente->conectar(PONTE_CLIENT_ID);

    pthread_setcancelstate(antigo, NULL);

    return ok;
}

// Só existe se a primeira conexão falhou: tenta de novo até conseguir. Depois
// disso quem reconecta é a própria libmosquitto.
static void* thread_conexao(void*) {

    struct timespec espera = { PONTE_ESPERA_CONEXAO_S, 0 };

    while(g_rodando) {

        nanosleep(&espera, NULL);

        if(g_rodando && conectar_sem_cancelar()) {
            log_msg("PONTE", "conectada ao broker MQTT");
            break;
        }
    }

    return NULL;
}


/* ------------------------------------------------------------------ */
/* Callback (thread da libmosquitto: só parse e fila, nunca bloqueia)  */
/* ------------------------------------------------------------------ */

static void interpretar(const char* p, ComandoOperacao& c) {

    char verbo[16];
    int pos = 0;
    char sobra;

    c.tipo = OP_INVALIDO;

    if(sscanf(p, "%15s%n", verbo, &pos) != 1)
        return;

    const char* resto = p + pos;

    // " %c" no fim: só vale se não sobrar nada além de espaços
    if(strcmp(verbo, MQTT_CMD_MODO) == 0) {

        char modo[16];

        if(sscanf(resto, "%d %15s %c", &c.a, modo, &sobra) != 2)
            return;

        if(strcmp(modo, MQTT_MODO_MANUAL) == 0)
            c.b = MODO_MANUAL;
        else if(strcmp(modo, MQTT_MODO_AUTO) == 0)
            c.b = MODO_AUTOMATICO;
        else
            return;

        c.tipo = OP_MODO;

    } else if(strcmp(verbo, MQTT_CMD_IR) == 0) {

        if(sscanf(resto, "%d %d %d %c", &c.a, &c.b, &c.c, &sobra) == 3)
            c.tipo = OP_IR;

    } else if(strcmp(verbo, MQTT_CMD_PARAR) == 0) {

        if(sscanf(resto, "%d %c", &c.a, &sobra) == 1)
            c.tipo = OP_PARAR;

    } else if(strcmp(verbo, MQTT_CMD_CAIXA) == 0) {

        if(sscanf(resto, "%d %d %c", &c.a, &c.b, &sobra) == 2)
            c.tipo = OP_CAIXA;
    }
}

static void receber(const std::string& topico, const std::string& payload) {

    if(topico != MQTT_TOPICO_OPERACAO_CMD)
        return;

    ComandoOperacao c;
    memset(&c, 0, sizeof(c));
    snprintf(c.texto, sizeof(c.texto), "%s", payload.c_str());

    interpretar(c.texto, c);

    // Até um comando inválido entra na fila: quem registra no log é a
    // thread de comandos, não esta (log_msg escreve no stderr, que pode
    // bloquear)
    pthread_mutex_lock(&g_mtx_cmd);

    if(g_fila_cmd.size() >= PONTE_MAX_COMANDOS) {
        g_cmd_descartados++;
    } else {
        g_fila_cmd.push_back(c);
        pthread_cond_signal(&g_cv_cmd);
    }

    pthread_mutex_unlock(&g_mtx_cmd);
}


/* ------------------------------------------------------------------ */
/* Interface                                                           */
/* ------------------------------------------------------------------ */

void ponte_mqtt_cargas_mudaram(void) {

    pthread_mutex_lock(&g_mtx_cargas);
    g_cargas_mudaram = true;
    pthread_cond_signal(&g_cv_cargas);
    pthread_mutex_unlock(&g_mtx_cargas);
}

static bool criar(pthread_t& th, void* (*funcao)(void*), const char* nome, bool& criou) {

    criou = (pthread_create(&th, NULL, funcao, NULL) == 0);

    if(!criou)
        log_msg("PONTE", "não foi possível criar a thread de %s", nome);

    return criou;
}

bool ponte_mqtt_iniciar(Robo_manager* manager) {

    if(g_cliente != NULL)
        return true;

    g_manager = manager;
    g_cliente = new ClienteMqtt();

    // Guardados antes de conectar: o ClienteMqtt refaz a assinatura a cada
    // reconexão
    g_cliente->definir_callback(receber);
    g_cliente->assinar(MQTT_TOPICO_OPERACAO_CMD, MQTT_QOS_OPERACAO_CMD);

    g_rodando = true;

    bool conectou = conectar_sem_cancelar();

    if(!conectou)
        log_msg("PONTE", "broker MQTT indisponível: o Organizador segue sem a Tela de Operação e tenta conectar a cada %d s",
                PONTE_ESPERA_CONEXAO_S);

    bool ok = criar(g_th_status, thread_status, "status", g_criou_status)
           && criar(g_th_coletor, thread_coletor, "eventos do Coletor", g_criou_coletor)
           && criar(g_th_cargas, thread_cargas, "cargas", g_criou_cargas)
           && criar(g_th_comandos, thread_comandos, "comandos", g_criou_comandos)
           && (conectou || criar(g_th_conexao, thread_conexao, "conexão", g_criou_conexao));

    if(!ok) {
        ponte_mqtt_encerrar();
        return false;
    }

    log_msg("PONTE", "ponte MQTT iniciada");

    return true;
}

void ponte_mqtt_encerrar(void) {

    if(g_cliente == NULL)
        return;

    g_rodando = false;

    pthread_mutex_lock(&g_mtx_cargas);
    pthread_cond_broadcast(&g_cv_cargas);
    pthread_mutex_unlock(&g_mtx_cargas);

    pthread_mutex_lock(&g_mtx_cmd);
    pthread_cond_broadcast(&g_cv_cmd);
    pthread_mutex_unlock(&g_mtx_cmd);

    // Estas duas podem estar bloqueadas (mq_receive, nanosleep) sem olhar
    // g_rodando
    if(g_criou_coletor)
        pthread_cancel(g_th_coletor);

    if(g_criou_conexao)
        pthread_cancel(g_th_conexao);

    if(g_criou_status)
        pthread_join(g_th_status, NULL);
    if(g_criou_coletor)
        pthread_join(g_th_coletor, NULL);
    if(g_criou_cargas)
        pthread_join(g_th_cargas, NULL);
    if(g_criou_comandos)
        pthread_join(g_th_comandos, NULL);
    if(g_criou_conexao)
        pthread_join(g_th_conexao, NULL);

    g_criou_status = g_criou_coletor = g_criou_cargas = g_criou_comandos = g_criou_conexao = false;

    g_cliente->desconectar();
    delete g_cliente;
    g_cliente = NULL;

    if(g_cmd_descartados > 0)
        log_msg("PONTE", "%lu comandos da Tela descartados por fila cheia", g_cmd_descartados);

    log_msg("PONTE", "ponte MQTT encerrada");
}
