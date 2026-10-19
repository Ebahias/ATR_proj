CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
CXXFLAGS += -pthread -I. -MMD -MP
LDLIBS   += -pthread -lrt

BIN   := bin
BUILD := build

# Gestor de Robôs (tarefa 1)
GESTOR_SRC := gestor_robos/main.cpp gestor_robos/gestor_robos.cpp \
              gestor_robos/fonte_novos_robos.cpp comum/ipc_gestor.cpp comum/ipc_robo.cpp

# Processo de cada robô (tarefa 2; threads das tarefas 3 e 4)
ROBO_SRC := robo/main.cpp robo/contexto_robo.cpp robo/hardware_robo.cpp \
            robo/navegacao.cpp robo/status.cpp comum/ipc_robo.cpp

# Ferramentas de teste
NOVO_ROBO_SRC := ferramentas/novo_robo.cpp gestor_robos/fonte_novos_robos.cpp
ESCUTA_SRC    := ferramentas/escuta_gestor.cpp comum/ipc_gestor.cpp
COMANDO_SRC   := ferramentas/comando_robo.cpp comum/ipc_robo.cpp
MONITOR_SRC   := ferramentas/monitor_frota.cpp comum/ipc_robo.cpp

PROGRAMAS := $(BIN)/gestor_robos $(BIN)/robo $(BIN)/novo_robo $(BIN)/escuta_gestor \
             $(BIN)/comando_robo $(BIN)/monitor_frota

obj = $(patsubst %.cpp,$(BUILD)/%.o,$(1))

.PHONY: all clean teste teste-gestor teste-robo

all: $(PROGRAMAS)

$(BIN)/gestor_robos: $(call obj,$(GESTOR_SRC))
$(BIN)/robo: $(call obj,$(ROBO_SRC))
$(BIN)/novo_robo: $(call obj,$(NOVO_ROBO_SRC))
$(BIN)/escuta_gestor: $(call obj,$(ESCUTA_SRC))
$(BIN)/comando_robo: $(call obj,$(COMANDO_SRC))
$(BIN)/monitor_frota: $(call obj,$(MONITOR_SRC))

$(PROGRAMAS):
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

teste: teste-gestor teste-robo

teste-gestor: all
	./testes/teste_gestor.sh

teste-robo: all
	./testes/teste_robo.sh

clean:
	rm -rf $(BIN) $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
