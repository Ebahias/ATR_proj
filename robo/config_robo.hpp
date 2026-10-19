#ifndef CONFIG_ROBO_HPP
#define CONFIG_ROBO_HPP

// Períodos das tarefas do processo do robô
#define PERIODO_NAVEGACAO_MS    50      // T1: Comando de Navegação
#define PERIODO_STATUS_MS       100     // T2: Status do Robô
#define PERIODO_HARDWARE_MS     20      // física do hardware simulado (só Etapa 1)

// Hardware simulado (unidades de mapa)
#define VELOCIDADE_ROBO         20.0    // unidades por segundo
#define DRENO_BATERIA_MOVENDO   0.5     // % por segundo
#define DRENO_BATERIA_PARADO    0.05    // % por segundo
#define TEMPO_ACOPLAR_S         0.5     // tempo entre o comando e i_carga_atracada

#endif
