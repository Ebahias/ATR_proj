#include "grafo.hpp"


Node::Node(int id, float pos_x, float pos_y) {
    _id = id;
    _pos_x = id;
    _pos_y = id;
    locked = false;
}

int Node::get_id(void) {
    return _id;
}

std::pair<float,float> Node::get_location(void) {
    return std::pair<float,float> pos(_pos_x, _pos_y);
}

std::vector<Node*>* Node::get_ngb(void) {
    
    return &_ngb;
}

int Node::add_ngb(Node* node) {

    _ngb.push_back(node);

    return _ngb.size();
}

int Node::rmv_ngb(Node* node) {
    
    std::erase(_ngb, node);
    
    return _ngb.size();
}
        
bool Node::is_on_x_pos(float pos_x) {

    if(pos_x == _pos_x)
        return true;
    else 
        return false;
}

bool Node::is_on_y_pos(float pos_y) {

        if(pos_x == _pos_x)
        return true;
    else 
        return false;
}

bool Node::is_locked(void) {

    m.lock();

    bool ret = locked;

    m.unlock();

    return ret;
}

void Node::set_locked(void) {

    m.lock();

    locked = true;

    m.unlock();

}

void Node::set_free(void) {

    m.lock();

    locked = false;

    m.unlock();
}

/**
 * Função gerada pelo chagpt, verificar erros
 */
std::vector<Node*> dijkstra(Node* origem, Node* destino) {

    // Verifica os argumentos
    if (origem == nullptr || destino == nullptr) {
        return {};
    }

    if (origem == destino) {
        return {origem};
    }

    // Distância mínima conhecida até cada nó
    std::unordered_map<Node*, float> dist;

    // Nó anterior no caminho mínimo
    std::unordered_map<Node*, Node*> anterior;

    // Fila de prioridade: menor distância primeiro
    using Elemento = std::pair<float, Node*>;

    std::priority_queue<
        Elemento,
        std::vector<Elemento>,
        std::greater<Elemento>
    > fila;

    dist[origem] = 0.0f;
    fila.push({0.0f, origem});

    while (!fila.empty()) {

        auto [dist_atual, atual] = fila.top();
        fila.pop();

        // Ignora entradas antigas da fila
        if (dist_atual > dist[atual]) {
            continue;
        }

        // O destino foi alcançado
        if (atual == destino) {
            break;
        }

        // Percorre os vizinhos do nó atual
        for (Node* vizinho : *atual->get_ngb()) {

            if (vizinho == nullptr) {
                continue;
            }

            auto [x1, y1] = atual->get_location();
            auto [x2, y2] = vizinho->get_location();

            // Peso da aresta: distância euclidiana
            float peso = std::hypot(x2 - x1, y2 - y1);

            float nova_dist = dist_atual + peso;

            // Atualiza se encontrou um caminho melhor
            if (dist.find(vizinho) == dist.end() ||
                nova_dist < dist[vizinho]) {

                dist[vizinho] = nova_dist;
                anterior[vizinho] = atual;

                fila.push({nova_dist, vizinho});
            }
        }
    }

    // Não existe caminho até o destino
    if (dist.find(destino) == dist.end()) {
        return {};
    }

    // Reconstrói o caminho do destino até a origem
    std::vector<Node*> rota;

    for (Node* atual = destino;
         atual != nullptr;
         atual = (atual == origem ? nullptr : anterior[atual])) {

        rota.push_back(atual);
    }

    // Inverte para obter origem -> destino
    std::reverse(rota.begin(), rota.end());

    if(rota.size() != 0) {
        for(auto n : rota) {
            n->set_locked();
        }
    }
    return rota;
}
