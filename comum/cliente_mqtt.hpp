#ifndef COMUM_CLIENTE_MQTT_HPP
#define COMUM_CLIENTE_MQTT_HPP

#include <atomic>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include <pthread.h>

struct mosquitto;
struct mosquitto_message;

class ClienteMqtt {

    public:

        typedef std::function<void(const std::string& topico, const std::string& payload)> CallbackMensagem;

    protected:

        struct mosquitto* _mosq;

        pthread_mutex_t _mtx;

        std::atomic<bool> _conectado;

        std::atomic<bool> _desconexao_pedida;

        bool _loop_ativo;

        CallbackMensagem _callback;

        std::vector<std::pair<std::string, int>> _assinaturas;

        bool _tem_will;
        std::string _will_topico;
        std::string _will_payload;
        int _will_qos;
        bool _will_retain;

        std::string _host;

        int _porta;

        static void iniciar_lib(void);

        static void on_connect(struct mosquitto* m, void* obj, int rc);

        static void on_disconnect(struct mosquitto* m, void* obj, int rc);

        static void on_message(struct mosquitto* m, void* obj, const struct mosquitto_message* msg);

        void tratar_conexao(int rc);

        void tratar_desconexao(int rc);

        void tratar_mensagem(const struct mosquitto_message* msg);

    public:

        ClienteMqtt();

        ~ClienteMqtt();

        ClienteMqtt(const ClienteMqtt&) = delete;

        ClienteMqtt& operator=(const ClienteMqtt&) = delete;

        void definir_callback(CallbackMensagem cb);

        bool definir_will(const std::string& topico, const std::string& payload, int qos, bool retain);

        bool conectar(const std::string& client_id);

        bool assinar(const std::string& topico, int qos);

        bool publicar(const std::string& topico, const std::string& payload, int qos, bool retain);

        void desconectar(void);

        bool conectado(void) const;
};

#endif
