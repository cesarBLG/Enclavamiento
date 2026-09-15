#pragma once
#include <enclavamiento.h>
#include <set>
class seccion_via;
class destino_ruta;
class movimiento;
class ruta_deslizamiento;
struct nodo_deslizamiento
{
    seccion_via *seccion;
    const Lado dir;
    seccion_via *prev;
    std::vector<std::shared_ptr<nodo_deslizamiento>> next;
    ruta_deslizamiento *deslizamiento;
    EstadoCanton maxima_ocupacion;
    bool asegurado = false;
    bool accesible = false;
    bool acceso_impedido = false;
    nodo_deslizamiento(seccion_via *prev, seccion_via *sec, Lado dir, ruta_deslizamiento *deslizamiento, const std::set<seccion_via*> &stop);
    bool compatible(movimiento *r, int id_deslizamiento);
    bool continuacion_posible(Lado dir2, int in2, int out2);
    void actualizar(bool set);
    void cambio_activacion(bool accesible, bool acceso_impedido);
    bool is_asegurado(int id_deslizamiento);
};
struct ruta_deslizamiento
{
    destino_ruta *fin_movimiento;
    std::set<movimiento*> rutas_afectadas;
    std::shared_ptr<nodo_deslizamiento> root;
    std::vector<std::map<seccion_via*, std::pair<int,int>>> deslizamientos_orientados;
    int deslizamiento_activo = -1;
    bool formado = false;
    ruta_deslizamiento(destino_ruta *fin, const json &j);
    int compatible(movimiento *r)
    {
        rutas_afectadas.clear();
        if (r != nullptr) rutas_afectadas.insert(r);
        for (int i=0; i<deslizamientos_orientados.size(); i++) {
            if (root->compatible(r, i)) {
                return i;
            }
        }
        return -1;
    }
    void activar(int id);
    void liberar();
    bool is_asegurado(bool id_orig)
    {
        if (deslizamiento_activo < 0)
            return false;
        if (!root->is_asegurado(id_orig) && (id_orig == deslizamiento_activo || !root->is_asegurado(deslizamiento_activo)))
            return false;
        return true;
    }
    void update();
};
