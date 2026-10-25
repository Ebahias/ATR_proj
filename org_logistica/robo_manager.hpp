#ifndef ROBO_MANAGER_HPP
#define ROBO_MANAGER_HPP

#include "robo.hpp"
#include "b_carga.hpp"
#include "b_descarga.hpp"
#include "z_recarga.hpp"


class Robo_manager {

    protected:

        std::vector<Robo*> _robo;

        std::vector<Baia_carga*> _b_carga;

        std::vector<Zona_recarga*> _z_recarga;

        std::vector<Node*> _grafo;

    public:

        std::vector<Baia_descarga*> _b_descarga;

        Robo_manager(std::vector<Baia_carga*> b_carga, std::vector<Baia_descarga*> b_descarga, std::vector<Zona_recarga*> z_recarga);

        Robo* get_avaiable_robo(void);

        
        int add_robo(Robo* robo);
        
        int set_robo_to_z_recarga(Robo* robo, Zona_recarga* z_recarga);

        int wait(void);

        void add_grafo(std::vector<Node*> grafo);

        Node* get_nearest_node(Robo* robo);
};

#endif