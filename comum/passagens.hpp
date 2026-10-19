#ifndef COMUM_PASSAGENS_HPP
#define COMUM_PASSAGENS_HPP

#include "comum/ipc_robo.hpp"

/*
 * Controle de acesso às passagens estreitas (seção crítica com capacidade N).
 *
 * Cada passagem é um monitor na memória compartilhada: mutex robusto +
 * máscara dos robôs dentro + fila FIFO de espera + reservas por equipe.
 *
 *  - Capacidade: no máximo "capacidade" robôs dentro ou com vaga reservada.
 *  - Justiça: a vaga vai para o primeiro da fila (FIFO), sem inanição.
 *  - Equipe: quem chega primeiro reserva vagas para a equipe inteira, que
 *    carrega a mesma caixa e precisa atravessar junta. Os outros membros
 *    entram direto pela reserva.
 *  - Sem bloqueio: passagem_solicitar() nunca espera. O robô fica parado na
 *    entrada e tenta de novo no próximo ciclo, então a thread de navegação
 *    continua atendendo comandos (PARAR, troca de modo) enquanto espera.
 *  - Sem deadlock: o mapa não tem duas passagens seguidas, e o robô sai de
 *    uma antes de pedir a próxima (nunca segura uma e espera outra).
 *  - Falha: o estado é guardado como máscaras de bits por slot. Se um robô
 *    morre, o Gestor chama passagens_liberar_slot() e as vagas voltam.
 */

// Pede para entrar. equipe = máscara de slots da equipe (inclui o próprio
// slot; só o próprio bit para um robô sozinho). equipe_id identifica a
// equipe (tarefa_id). Retorna 1 se pode entrar, 0 se deve esperar.
int passagem_solicitar(ShmFrota* shm, int p, int slot, int equipe_id, uint32_t equipe);

// Sai da passagem (libera a vaga).
void passagem_sair(ShmFrota* shm, int p, int slot);

// Sai da fila de espera da passagem (sem afetar quem está dentro).
void passagem_desistir(ShmFrota* shm, int p, int slot);

// Remove o slot de todas as passagens: dentro, filas e reservas.
void passagens_liberar_slot(ShmFrota* shm, int slot);

// Cópia consistente do estado de uma passagem (para telas e testes).
void passagem_ler(ShmFrota* shm, int p, PassagemShm& copia);

// Vagas em uso (dentro + reservadas).
int passagem_ocupacao(const PassagemShm& p);

#endif
