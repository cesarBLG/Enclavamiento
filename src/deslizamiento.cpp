#include "deslizamiento.h"
#include "items.h"
ruta_deslizamiento::ruta_deslizamiento(movimiento *r, const json &j) : r(r)
{
    auto rend = r->get_secciones().back();
    auto start = rend.seccion->get_seccion_in(opp_lado(*rend.dir), rend.out);
    std::set<seccion_via*> stop;
    for (auto &id : j["Límite"]) {
        stop.insert(secciones[id_elemento::from_default_dep(id, r->estacion)]);
    }
    root = std::make_shared<nodo_deslizamiento>(rend.seccion, start.first, opp_lado(start.second), this, stop);
    deslizamientos_orientados.push_back({});
    if (j.contains("DeslizamientosOrientados")) {
        for (auto &jo : j["DeslizamientosOrientados"]) {
            std::map<seccion_via*, std::pair<int,int>> pos;
            for (auto &[sec_id, jpos] : jo.items()) {
                pos[::secciones[id_elemento::from_default_dep(sec_id, r->estacion)]] = jpos;
            }
            deslizamientos_orientados.push_back(pos);
        }
    }
}
nodo_deslizamiento::nodo_deslizamiento(seccion_via *prev, seccion_via *sec, Lado dir, ruta_deslizamiento *deslizamiento, const std::set<seccion_via*> &stop) : seccion(sec), dir(dir), prev(prev), deslizamiento(deslizamiento), maxima_ocupacion(EstadoCanton::Ocupado)
{
    bool end = stop.find(sec) != stop.end();
    int num = sec->num_outs(dir);
    for (int out=0; out<num; out++) {
        auto p = sec->get_seccion_in(opp_lado(dir), out);
        if (p.first == nullptr) continue;
        if (end && stop.find(p.first) == stop.end()) continue;
        next.push_back(std::make_shared<nodo_deslizamiento>(sec, p.first, opp_lado(p.second), deslizamiento, stop));
    }
}
bool nodo_deslizamiento::compatible(movimiento *r, int id_deslizamiento)
{
    int in = seccion->get_in(prev, dir);
    auto &posicion_aparatos = deslizamiento->deslizamientos_orientados[id_deslizamiento];
    auto it = posicion_aparatos.find(seccion);
    if (it != posicion_aparatos.end()) {
        if (seccion->tipo == TipoSeccion::Aguja) {
            aguja *a = (aguja*)seccion;
            auto pos = a->get_posicion(Lado::Impar, it->second.first, it->second.second);
            if (!a->posible_mover(pos)) {
                return false;
            }
        }
        if (in != (dir == Lado::Impar ? it->second.second : it->second.first))
            return false;
    }
    int num = seccion->num_outs(dir);
    for (int out=0; out<num; out++) {
        if (it != posicion_aparatos.end()) {
            if (out != (dir == Lado::Impar ? it->second.first : it->second.second))
                continue;
        }
        // Comprobar si el deslizamiento es compatible con rutas ya formadas
        if (!seccion->deslizamiento_posible(in, out, dir)) {
            if (seccion->get_ruta_asegurada())
                deslizamiento->rutas_afectadas.insert(seccion->get_ruta_asegurada()->ruta_asegurada);
            return false;
        }
        // Comprobar si el deslizamiento es compatible con la ruta a formar
        if (r != nullptr) {
            auto secciones_ruta = r->get_secciones();
            for (int i=0; i<secciones_ruta.size(); i++) {
                auto [sec2, dir2, in2, out2] = secciones_ruta[i];
                if (sec2 != seccion) continue;
                if (dir != dir2 || in != in2/* || out != out2*/) return false;
            }
            // TODO: comprobar aparatos fuera de la ruta (e.g. escapes)
        }
        for (auto &n : next) {
            int out2 = seccion->get_out(n->seccion, dir);
            if (out != out2) continue;
            if (!n->compatible(r, id_deslizamiento)) return false;
        }
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
    if (deslizamiento->deslizamiento_activo >= 0) {
        auto &posicion_aparatos = deslizamiento->deslizamientos_orientados[deslizamiento->deslizamiento_activo];
        auto it = posicion_aparatos.find(seccion);
        if (seccion->tipo == TipoSeccion::Aguja && it == posicion_aparatos.end()) {
            aguja *a = (aguja*)seccion;
            a->desenclavar(deslizamiento->r);
        }
    }
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
void ruta_deslizamiento::activar(int id)
{
    log(r->id, "deslizamiento activo " + std::to_string(id));
    deslizamiento_activo = id;
    root->actualizar(true);
    for (auto *r2 : rutas_afectadas) {
        r2->deslizamientos_afectados[r] = id;
    }
}
void ruta_deslizamiento::update()
{
    if (deslizamiento_activo < 0) return;
    auto &posicion_aparatos = deslizamientos_orientados[deslizamiento_activo];
    for (auto &[sec, pins] : posicion_aparatos) {
        if (sec->tipo == TipoSeccion::Aguja) {
            aguja *a = (aguja*)sec;
            auto pos = a->get_posicion(Lado::Impar, pins.first, pins.second);
            if (!a->enclavar(r, a->get_posicion(Lado::Impar, pins.first, pins.second))) {
                break;
            }
        }
    }
}
