#ifndef COMUM_PERIODICO_HPP
#define COMUM_PERIODICO_HPP

#include <cerrno>
#include <time.h>

/*
 * Ativação periódica de uma thread com instantes absolutos
 * (clock_nanosleep + TIMER_ABSTIME): o período não acumula deriva com o
 * tempo de execução do ciclo. Também mede o pior tempo de execução e conta
 * os ciclos que perderam o prazo (dados para a análise de escalonabilidade).
 */
class Periodo {

    protected:

        long _periodo_ns;

        struct timespec _proxima;   // próxima ativação

        struct timespec _inicio_ciclo;

        unsigned long _ciclos;

        unsigned long _atrasos;

        long _max_exec_ns;

        static long diff_ns(const struct timespec& a, const struct timespec& b) {
            return (b.tv_sec - a.tv_sec) * 1000000000L + (b.tv_nsec - a.tv_nsec);
        }

        static void somar_ns(struct timespec& t, long ns) {
            t.tv_nsec += ns;
            while(t.tv_nsec >= 1000000000L) {
                t.tv_nsec -= 1000000000L;
                t.tv_sec++;
            }
        }

    public:

        explicit Periodo(int periodo_ms) {
            _periodo_ns = (long)periodo_ms * 1000000L;
            _ciclos = 0;
            _atrasos = 0;
            _max_exec_ns = 0;
            clock_gettime(CLOCK_MONOTONIC, &_proxima);
            _inicio_ciclo = _proxima;
        }

        // Chamado no fim do ciclo: dorme até a próxima ativação.
        void esperar(void) {

            struct timespec agora;
            clock_gettime(CLOCK_MONOTONIC, &agora);

            long exec = diff_ns(_inicio_ciclo, agora);
            if(exec > _max_exec_ns)
                _max_exec_ns = exec;

            _ciclos++;
            somar_ns(_proxima, _periodo_ns);

            if(diff_ns(agora, _proxima) < 0) {
                // Perdeu o prazo: conta e realinha em vez de disparar ciclos atrasados em rajada
                _atrasos++;
                _proxima = agora;
            } else {
                while(clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &_proxima, NULL) == EINTR) {}
            }

            clock_gettime(CLOCK_MONOTONIC, &_inicio_ciclo);
        }

        unsigned long ciclos(void) const { return _ciclos; }

        unsigned long atrasos(void) const { return _atrasos; }

        long max_exec_us(void) const { return _max_exec_ns / 1000; }
};

#endif
