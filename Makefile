CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O2
CXXFLAGS += -pthread -I. -MMD -MP
LDLIBS   += -pthread -lrt

BIN   := bin
BUILD := build

# Gestor de Robôs (tarefa 1)
GESTOR_SRC := gestor_robos/main.cpp gestor_robos/gestor_robos.cpp \
              gestor_robos/fonte_novos_robos.cpp comum/ipc_gestor.cpp

# Processo do robô (provisório até as tarefas 2-4)
ROBO_SRC := robo/robo_stub.cpp

# Ferramentas de teste
NOVO_ROBO_SRC := ferramentas/novo_robo.cpp gestor_robos/fonte_novos_robos.cpp
ESCUTA_SRC    := ferramentas/escuta_gestor.cpp comum/ipc_gestor.cpp

PROGRAMAS := $(BIN)/gestor_robos $(BIN)/robo $(BIN)/novo_robo $(BIN)/escuta_gestor

obj = $(patsubst %.cpp,$(BUILD)/%.o,$(1))

.PHONY: all clean teste-gestor

all: $(PROGRAMAS)

$(BIN)/gestor_robos: $(call obj,$(GESTOR_SRC))
$(BIN)/robo: $(call obj,$(ROBO_SRC))
$(BIN)/novo_robo: $(call obj,$(NOVO_ROBO_SRC))
$(BIN)/escuta_gestor: $(call obj,$(ESCUTA_SRC))

$(PROGRAMAS):
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

teste-gestor: all
	./testes/teste_gestor.sh

clean:
	rm -rf $(BIN) $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
