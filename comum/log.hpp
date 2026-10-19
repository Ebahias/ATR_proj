#ifndef COMUM_LOG_HPP
#define COMUM_LOG_HPP

#include <cstdarg>
#include <cstdio>
#include <ctime>

// Log simples com timestamp (data, hora e ms). Uma única chamada a fprintf
// por linha, então linhas de threads/processos diferentes não se misturam.
inline void log_msg(const char* modulo, const char* fmt, ...) {

    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);

    struct tm tm_local;
    localtime_r(&ts.tv_sec, &tm_local);

    char hora[32];
    strftime(hora, sizeof(hora), "%Y-%m-%d %H:%M:%S", &tm_local);

    char texto[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(texto, sizeof(texto), fmt, args);
    va_end(args);

    fprintf(stderr, "[%s.%03ld] [%s] %s\n", hora, ts.tv_nsec / 1000000, modulo, texto);
}

#endif
