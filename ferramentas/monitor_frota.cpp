/*
 * Lê a memória compartilhada da frota, fazendo o papel do Organizador
 * (tabela de status) e do Coletor de Dados (buffer de eventos) enquanto eles
 * não existem.
 *
 * Uso: monitor_frota status    imprime a tabela de status uma vez
 *      monitor_frota eventos   consome e imprime eventos até SIGINT/SIGTERM
 */

#include "comum/ipc_robo.hpp"

#include <csignal>
#include <cstdio>
#include <cstring>
#include <ctime>

static volatile sig_atomic_t g_sair = 0;

static void ao_sinal(int) {
    g_sair = 1;
}

static void hora(const struct timespec& ts, char* buf, size_t tam) {

    struct tm t;
    localtime_r(&ts.tv_sec, &t);

    size_t n = strftime(buf, tam, "%Y-%m-%d %H:%M:%S", &t);
    snprintf(buf + n, tam - n, ".%03ld", ts.tv_nsec / 1000000);
}

static int imprimir_status(ShmFrota* shm) {

    printf("%-4s %-5s %-7s %-15s %-10s %-11s %-11s %4s %5s %5s %8s %6s\n",
           "slot", "robo", "pid", "estado", "modo", "posicao", "setpoint",
           "bat", "carga", "falha", "dist", "seq");

    for(int i=0;i<MAX_ROBOS;i++) {

        DadosStatus d;
        shm_slot_ler(shm, i, d);

        if(!d.ativo)
            continue;

        char pos[24], sp[24];
        snprintf(pos, sizeof(pos), "(%d,%d)", d.pos_x, d.pos_y);
        snprintf(sp, sizeof(sp), "(%d,%d)", d.sp_x, d.sp_y);

        printf("%-4d %-5d %-7d %-15s %-10s %-11s %-11s %3d%% %5d %5d %8.1f %6u\n",
               i, d.robo_id, d.pid, estado_robo_nome(d.estado), modo_robo_nome(d.modo),
               pos, sp, d.bateria, d.carga_atracada, d.falha, d.distancia_total, d.seq);
    }

    printf("eventos descartados: %u\n", shm->eventos.descartados);

    return 0;
}

static int consumir_eventos(ShmFrota* shm) {

    signal(SIGINT, ao_sinal);
    signal(SIGTERM, ao_sinal);

    setvbuf(stdout, NULL, _IOLBF, 0);

    while(!g_sair) {

        EventoRobo ev;

        if(shm_evento_consumir(shm, ev, 200) < 0)
            continue;

        char h[40];
        hora(ev.timestamp, h, sizeof(h));

        printf("[%s] robo=%d %s: %s\n", h, ev.robo_id, tipo_evento_robo_nome(ev.tipo), ev.texto);
    }

    return 0;
}

int main(int argc, char** argv) {

    if(argc != 2 || (strcmp(argv[1], "status") != 0 && strcmp(argv[1], "eventos") != 0)) {
        fprintf(stderr, "uso: %s status | eventos\n", argv[0]);
        return 2;
    }

    ShmFrota* shm = shm_frota_abrir();

    if(!shm)
        return 1;

    int ret = strcmp(argv[1], "status") == 0 ? imprimir_status(shm) : consumir_eventos(shm);

    shm_frota_fechar(shm);

    return ret;
}
