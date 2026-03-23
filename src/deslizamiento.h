#pragma once
#include <enclavamiento.h>
class seccion_via;
class ruta;
class ruta_deslizamiento;
struct nodo_deslizamiento
{
    seccion_via *seccion;
    const Lado dir;
    seccion_via *prev;
    std::vector<nodo_deslizamiento*> next;
    ruta_deslizamiento *deslizamiento;
    EstadoCanton maxima_ocupacion;
    bool asegurado = false;
    bool accesible = false;
    bool acceso_impedido = false;
    bool compatible(ruta *r, int id_deslizamiento);
    bool continuacion_posible(Lado dir2, int in2, int out2);
    void actualizar(bool set);
    void cambio_activacion(bool accesible, bool acceso_impedido);
    bool is_asegurado(int id_deslizamiento);
};
struct ruta_deslizamiento
{
    ruta *r;
    seccion_via *fin_ruta_asegurada;
    nodo_deslizamiento* root;
    std::vector<std::map<seccion_via*, std::pair<int,int>>> deslizamientos_orientados;
    int deslizamiento_activo = -1;
    ruta_deslizamiento();
    int compatible(ruta *r)
    {
        for (int i=0; i<deslizamientos_orientados.size(); i++) {
            if (root->compatible(r, i)) {
                return i;
            }
        }
        return -1;
    }
    void activar(int id)
    {
        deslizamiento_activo = id;
        root->actualizar(true);
    }
    void liberar()
    {
        deslizamiento_activo = -1;
        root->actualizar(false);
    }
    bool is_asegurado()
    {
        if (deslizamiento_activo < 0)
            return false;
        for (int i=0; i<deslizamientos_orientados.size(); i++) {
            if (root->is_asegurado(i))
                return true;
        }
        return false;
    }
};
