#include "grafo_armazem.hpp"
#include <vector>

#include <iostream>
#include <fstream>
#include <sstream>
#include <utility>
#include <string>


std::vector<Node*> get_grafo_armazem(std::string nome_arquivo) {



    std::vector<Node*> ret;

    std::ifstream arquivo(nome_arquivo);

    if (!arquivo.is_open()) {
        std::cerr << "Erro ao abrir o arquivo!\n";
        return ret;
    }

    float x, y;
    
    int id = 0;
    
    while (arquivo >> x >> y) {

        std::pair<float, float> coordenada = {x, y};

        ret.push_back(new Node(id, x, y));
        
        id++;
    }   

    arquivo.close();

    return ret;
}

void connect_grafo_armazem(std::string nome_arquivo, std::vector<Node *>& grafo) {


     std::ifstream arquivo(nome_arquivo);

    if (!arquivo.is_open()) {
        std::cerr << "Erro ao abrir o arquivo!\n";
        return;
    }

    std::string linha;
    int numero;
    int id = 0;

    while (std::getline(arquivo, linha)) {

        std::istringstream stream(linha);
        std::vector<int> valores;

        while (stream >> numero) {
            Node* ngb = grafo[numero];
            grafo[id]->add_ngb(ngb);
        }

        id++;
    }
}