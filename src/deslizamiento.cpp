#include "deslizamiento.h"
#include "items.h"
ruta_deslizamiento::ruta_deslizamiento()
{
    deslizamientos_orientados.push_back({});
}
bool nodo_deslizamiento::compatible(movimiento *r, int id_deslizamiento)
{
    auto &posicion_aparatos = deslizamiento->deslizamientos_orientados[id_deslizamiento];
    auto it = posicion_aparatos.find(seccion);
    if (it != posicion_aparatos.end()) {
        // TODO: comprobar si la aguja está enclavada en otra posición o bloqueada
    }
    int in = seccion->get_in(prev, dir);
    for (auto &n : next) {
        int out = seccion->get_out(n->seccion, dir);
        if (it != posicion_aparatos.end()) {
            Lado l = dir;
            seccion_via *sig = seccion->siguiente_seccion(prev, l);
            int out2 = seccion->get_out(sig, dir);
            if (out != it->second.second && it->second.first == in && it->second.second == out2)
                continue;
        }
        // Comprobar si el deslizamiento es compatible con rutas ya formadas
        if (!seccion->deslizamiento_posible(in, out, dir)) return false;
        // Comprobar si el deslizamiento es compatible con la ruta a formar
        if (r != nullptr) {
            auto secciones_ruta = r->get_secciones();
            for (int i=0; i<secciones_ruta.size(); i++) {
                auto [sec2, dir2, in2, out2] = secciones_ruta[i];
                if (sec2 != seccion) continue;
                if (dir != dir2 || in != in2 || out != out2) return false;
            }
            // TODO: comprobar aparatos fuera de la ruta (e.g. escapes)
        }
        if (!n->compatible(it == posicion_aparatos.end() || (it->second.first == in && it->second.second == out) ? r : nullptr, id_deslizamiento)) return false;
    }
    return true;
}
bool nodo_deslizamiento::continuacion_posible(Lado dir2, int in2, int out2)
{
    if (dir2 != dir) return false;
    int in = seccion->get_in(prev, dir);
    if (in != in2) return false;
    for (auto &n : next) {
        int out = seccion->get_out(n->seccion, dir);
        if (out == out2) return true;
    }
    return false;
}
void nodo_deslizamiento::actualizar(bool set)
{
    int in = seccion->get_in(prev, dir);
    for (auto &n : next) {
        int out = seccion->get_out(n->seccion, dir);
        if (set) {
            auto &posicion_aparatos = deslizamiento->deslizamientos_orientados[deslizamiento->deslizamiento_activo];
            auto it = posicion_aparatos.find(seccion);
            n->actualizar(it == posicion_aparatos.end() || (it->second.first == in && it->second.second == out));
        } else {
            n->actualizar(false);
        }
    }
    if (set) {
        seccion->asegurar_deslizamiento(deslizamiento->r, this);
        asegurado = true;
    } else {
        seccion->liberar(deslizamiento->r);
        asegurado = false;
    }
}
void nodo_deslizamiento::cambio_activacion(bool accesible, bool acceso_impedido)
{
    int in = seccion->get_in(prev, dir);
    Lado l = dir;
    seccion_via *sig = seccion->siguiente_seccion(prev, l);
    int out = seccion->get_out(sig, dir);
    bool enclavada_correcta = false;
    if (deslizamiento->deslizamiento_activo >= 0) {
        auto &posicion_aparatos = deslizamiento->deslizamientos_orientados[deslizamiento->deslizamiento_activo];
        auto it = posicion_aparatos.find(seccion);
        if (it != posicion_aparatos.end() && (it->second.first == in && it->second.second == out)) {
            enclavada_correcta = true;
        }
    }
    for (auto &n : next) {
        int out1 = seccion->get_out(n->seccion, dir);
        n->cambio_activacion(out == out1, out != out1 && enclavada_correcta);
    }
    this->accesible = accesible;
    this->acceso_impedido = acceso_impedido;
}
bool nodo_deslizamiento::is_asegurado(int id_deslizamiento)
{
    if (maxima_ocupacion < seccion->get_ocupacion(prev, dir))
        return false;
    int in = seccion->get_in(prev, dir);
    auto r = seccion->get_ruta_asegurada();
    Lado l = dir;
    seccion_via *sig = seccion->siguiente_seccion(prev, l);
    int out = seccion->get_out(sig, dir);
    if (r && (r->lado != dir || r->outs[opp_lado(dir)] != in || r->outs[dir] != out) && r->ruta_asegurada->is_formada())
        return false;
    auto &posicion_aparatos = deslizamiento->deslizamientos_orientados[id_deslizamiento];
    auto it = posicion_aparatos.find(seccion);
    if (it != posicion_aparatos.end() && (it->second.first != in || it->second.second != out))
        return false;
    for (auto &n : next) {
        int out1 = seccion->get_out(n->seccion, dir);
        if ((out < 0 || out == out1) && !n->is_asegurado(id_deslizamiento))
            return false;
    }
    return true;
}
