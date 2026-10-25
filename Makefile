CXX      ?= g++
CXXFLAGS ?= -std=c++20 -Wall -Wextra -O2
CXXFLAGS += -pthread -I. -MMD -MP
LDLIBS   += -pthread -lrt

BIN   := bin
BUILD := build

# Gestor de Robôs (tarefa 1)
# Módulos comuns de IPC (memória da frota, passagens estreitas, mapa)
IPC_ROBO := comum/ipc_robo.cpp comum/passagens.cpp comum/recarga.cpp comum/mapa.cpp

GESTOR_SRC := gestor_robos/main.cpp gestor_robos/gestor_robos.cpp \
              gestor_robos/fonte_novos_robos.cpp comum/ipc_gestor.cpp $(IPC_ROBO)

# Processo de cada robô (tarefa 2; threads das tarefas 3 e 4)
ROBO_SRC := robo/main.cpp robo/contexto_robo.cpp robo/hardware_robo.cpp \
            robo/navegacao.cpp robo/navegador.cpp robo/status.cpp $(IPC_ROBO)

# Coletor de Dados (tarefa 6)
COLETOR_SRC := coletor/main.cpp coletor/coletor.cpp coletor/rastreio.cpp \
               comum/ipc_coletor.cpp $(IPC_ROBO)

# Ferramentas de teste
NOVO_ROBO_SRC := ferramentas/novo_robo.cpp gestor_robos/fonte_novos_robos.cpp
ESCUTA_SRC    := ferramentas/escuta_gestor.cpp comum/ipc_gestor.cpp
COMANDO_SRC   := ferramentas/comando_robo.cpp $(IPC_ROBO)
MONITOR_SRC   := ferramentas/monitor_frota.cpp $(IPC_ROBO)
MAPA_SRC      := ferramentas/mapa.cpp comum/mapa.cpp
ESCUTA_COL_SRC := ferramentas/escuta_coletor.cpp comum/ipc_coletor.cpp $(IPC_ROBO)


# OrganiZador de logistica
ORGANIZADOR_SRC 	  := org_logistica/org_logistica.cpp org_logistica/robo_manager_threads.cpp org_logistica/b_carga_threads.cpp \
						 org_logistica/robo_threads.cpp org_logistica/robo_manager.cpp org_logistica/robo.cpp \
						 org_logistica/b_carga.cpp org_logistica/b_descarga.cpp org_logistica/z_recarga.cpp \
						 org_logistica/robo.cpp org_logistica/carga.cpp org_logistica/grafo.cpp			 

PROGRAMAS := $(BIN)/gestor_robos $(BIN)/robo $(BIN)/novo_robo $(BIN)/escuta_gestor \
             $(BIN)/comando_robo $(BIN)/monitor_frota $(BIN)/mapa \
             $(BIN)/coletor $(BIN)/escuta_coletor $(BIN)/org_logistica

obj = $(patsubst %.cpp,$(BUILD)/%.o,$(1))

.PHONY: all clean teste teste-gestor teste-robo teste-navegacao teste-status teste-coletor

all: $(PROGRAMAS)

$(BIN)/gestor_robos: $(call obj,$(GESTOR_SRC))
$(BIN)/robo: $(call obj,$(ROBO_SRC))
$(BIN)/novo_robo: $(call obj,$(NOVO_ROBO_SRC))
$(BIN)/escuta_gestor: $(call obj,$(ESCUTA_SRC))
$(BIN)/comando_robo: $(call obj,$(COMANDO_SRC))
$(BIN)/monitor_frota: $(call obj,$(MONITOR_SRC))
$(BIN)/mapa: $(call obj,$(MAPA_SRC))
$(BIN)/coletor: $(call obj,$(COLETOR_SRC))
$(BIN)/escuta_coletor: $(call obj,$(ESCUTA_COL_SRC))
$(BIN)/org_logistica: $(call obj,$(ORGANIZADOR_SRC))


$(PROGRAMAS):
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) $^ -o $@ $(LDLIBS)

$(BUILD)/%.o: %.cpp
	@mkdir -p $(@D)
	$(CXX) $(CXXFLAGS) -c $< -o $@

teste: teste-gestor teste-robo teste-navegacao teste-status teste-coletor

teste-gestor: all
	./testes/teste_gestor.sh

teste-robo: all
	./testes/teste_robo.sh

teste-navegacao: all
	./testes/teste_navegacao.sh

teste-status: all
	./testes/teste_status.sh

teste-coletor: all
	./testes/teste_coletor.sh

clean:
	rm -rf $(BIN) $(BUILD)

-include $(shell find $(BUILD) -name '*.d' 2>/dev/null)
