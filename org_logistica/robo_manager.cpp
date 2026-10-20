#include "robo_manager.hpp"

Robo_manager::Robo_manager(std::vector<Baia_carga*> b_carga, std::vector<Baia_descarga*> b_descarga, std::vector<Zona_recarga*> z_recarga) {

    _b_carga = b_carga;
    _b_descarga = b_descarga;
    _z_recarga = z_recarga;

}

Robo* Robo_manager::get_avaiable_robo(void) {

    int ret = nullptr;

    for(auto r : _robo) {

        if(r->get_status() == AVAIABLE)
            ret = r->get_id;
    }

    return ret;
}

int Robo_manager::add_robo(Robo* robo) {

    _robo.push_back(robo);

}
        
int Robo_manager::set_robo_to_z_recarga(Robo* robo, Zona_recarga* z_recarga) {

    Node* f_node = z_recarga->get_free_spot();

    Node* i_node = this->get_nearest_node(robo);

    robo->go_to_pos(i_node, f_node);

    return 0;
}

Node* Robo_manager::get_nearest_node(Robo* robo) {

    Node* node = nullptr;

    float min_dist = 3000;

    for(auto n : this->_grafo) {

        float dist = sqrt((n->get_pos_x() - robo->get_pos_x())^2 + (n->get_pos_y() - robo->get_pos_y())^2);

        if(dist < min_dist) {

            min_dist = dist;
            node = n;
        }
    }

    return node;
}

int Robo_manager::wait(void) {

    
}