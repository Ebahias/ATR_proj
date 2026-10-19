#ifndef THREADS_ROBO_HPP
#define THREADS_ROBO_HPP

// Ambas recebem um ContextoRobo* e rodam até ContextoRobo::deve_encerrar().

void* thread_navegacao(void* arg);  // Comando de Navegação (tarefa 3)

void* thread_status(void* arg);     // Status do Robô (tarefa 4)

#endif
