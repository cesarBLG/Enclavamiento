#include "deslizamiento.h"
#include "items.h"
ruta_deslizamiento::ruta_deslizamiento(destino_ruta *fin, const json &j) : fin_movimiento(fin)
{;
    std::set<seccion_via*> stop;
    for (auto &id : j["Límite"]) {
        stop.insert(secciones[id_elemento::from_default_dep(id, fin->id.dependencia)]);
    }
    root = new nodo_deslizamiento(fin->señal_fin->seccion_prev, fin->señal_fin->seccion, fin->señal_fin->lado, this, stop);
    if (j.contains("DeslizamientosOrientados")) {
        for (auto &jo : j["DeslizamientosOrientados"]) {
            std::map<seccion_via*, lados<int>> pos;
            for (auto &[sec_id, jpos] : jo.items()) {
                pos[::secciones[id_elemento::from_default_dep(sec_id, fin->id.dependencia)]] = jpos;
            }
            deslizamientos_orientados.push_back(pos);
        }
    }
    if (deslizamientos_orientados.empty())
        deslizamientos_orientados.push_back({});
}
nodo_deslizamiento::nodo_deslizamiento(seccion_via *prev, seccion_via *sec, Lado dir, ruta_deslizamiento *deslizamiento, const std::set<seccion_via*> &stop) : seccion(sec), dir(dir), prev(prev), deslizamiento(deslizamiento), maxima_ocupacion(EstadoCanton::Ocupado)
{
    bool end = stop.find(sec) != stop.end();
    int num = sec->num_outs(dir);
    std::vector<std::pair<seccion_via*,Lado>> secciones;
    sec->prev_secciones(prev, opp_lado(dir), secciones, false);
    for (auto &p : secciones) {
        if (p.first == nullptr) continue;
        if (end && stop.find(p.first) == stop.end()) continue;
        next.push_back(new nodo_deslizamiento(sec, p.first, opp_lado(p.second), deslizamiento, stop));
    }
}
int ruta_deslizamiento::compatible(movimiento *r)
{
    rutas_afectadas.clear();
    if (r != nullptr) {
        rutas_afectadas.insert(r);
        // No comprobar compatibilidad de una ruta con su propio deslizamiento
        if (r->es_ruta && ((ruta*)r)->get_destino() == fin_movimiento)
            r = nullptr;
    }
    for (int i=0; i<deslizamientos_orientados.size(); i++) {
        if (root->compatible(r, i)) {
            return i;
        }
    }
    return -1;
}
bool nodo_deslizamiento::compatible(movimiento *r, int id_deslizamiento)
{
    int in = seccion->get_in(prev, dir);
    auto &posicion_aparatos = deslizamiento->deslizamientos_orientados[id_deslizamiento];
    auto it = posicion_aparatos.find(seccion);
    if (it != posicion_aparatos.end()) {
        if (seccion->tipo == TipoSeccion::Aguja) {
            aguja *a = (aguja*)seccion;
            auto pos = a->get_posicion(it->second);
            if (!a->posible_mover(pos)) {
                return false;
            }
        }
        if (in != it->second[opp_lado(dir)])
            return false;
    }
    // Comprobar si el deslizamiento es compatible con la ruta a formar
    if (r != nullptr) {
        auto secciones_ruta = r->get_secciones();
        for (int i=0; i<secciones_ruta.size(); i++) {
            auto [sec2, dir2, outs] = secciones_ruta[i];
            for (auto *pt : puntos_negros_por_causa[seccion->id]) {
                if (pt->seccion_afectada != sec2 || !pt->afectado_propio(outs))
                    continue;
                if (invade_galibo(pt->pin_ajeno, id_deslizamiento))
                    return false;
            }
            if (sec2 != seccion) continue;
            if (dir != dir2 || in != outs[opp_lado(dir)]) return false;
        }
        // TODO: comprobar aparatos
    }
    int num = seccion->num_outs(dir);
    for (int out=0; out<num; out++) {
        if (it != posicion_aparatos.end()) {
            if (out != it->second[dir])
                continue;
        }
        if (!seccion->acceso_posible(in, out, dir))
            continue;
        // Comprobar si el deslizamiento es compatible con rutas ya formadas
        if (!seccion->deslizamiento_posible(in, out, dir)) {
            if (seccion->get_ruta_asegurada())
                deslizamiento->rutas_afectadas.insert(seccion->get_ruta_asegurada()->ruta_asegurada);
            return false;
        }
    }
    for (auto &n : next) {
        if (it != posicion_aparatos.end()) {
            if (seccion->get_out(n->seccion, dir) != it->second[dir])
                continue;
        }
        if (!n->compatible(r, id_deslizamiento)) return false;
    }
    return true;
}
bool nodo_deslizamiento::invade_galibo(std::optional<std::pair<Lado,int>> pin_causa, int id_deslizamiento)
{
    int in = seccion->get_in(prev, dir);
    if (dir == pin_causa->first) {
        int num = seccion->num_outs(dir);
        auto &posicion_aparatos = deslizamiento->deslizamientos_orientados[id_deslizamiento];
        auto it = posicion_aparatos.find(seccion);
        for (int out=0; out<num; out++) {
            if (it != posicion_aparatos.end()) {
                if (out != it->second[dir])
                    continue;
            }
            if (!seccion->acceso_posible(in, out, dir))
                continue;
            if (out == pin_causa->second)
                return true;
        }
    } else if (in == pin_causa->second) {
        return true;
    }
    return false;
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
        if (seccion->tipo == TipoSeccion::Aguja) {
            aguja *a = (aguja*)seccion;
            if (it == posicion_aparatos.end()) {
                a->desenclavar(deslizamiento->fin_movimiento->ruta_activa);
            } else if (dependencias[seccion->id.dependencia]->bloqueo_agujas || !a->mover(a->get_posicion(it->second))) {
                a->requerir_movimiento(deslizamiento->fin_movimiento->ruta_activa, a->get_posicion(it->second));
            }
        }
    }
    int in = seccion->get_in(prev, dir);
    for (auto &n : next) {
        int out = seccion->get_out(n->seccion, dir);
        if (set) {
            auto &posicion_aparatos = deslizamiento->deslizamientos_orientados[deslizamiento->deslizamiento_activo];
            auto it = posicion_aparatos.find(seccion);
            n->actualizar(it == posicion_aparatos.end() || (it->second[opp_lado(dir)] == in && it->second[dir] == out));
        } else {
            n->actualizar(false);
        }
    }
    if (set) {
        seccion->asegurar_deslizamiento(deslizamiento->fin_movimiento->ruta_activa, this);
        asegurado = true;
    } else {
        seccion->liberar_deslizamiento(deslizamiento->fin_movimiento->ruta_activa, this);
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
        if (it != posicion_aparatos.end() && it->second[opp_lado(dir)] == in && it->second[dir] == out) {
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
    if (it != posicion_aparatos.end() && (it->second[opp_lado(dir)] != in || it->second[dir] != out))
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
    if (deslizamiento_activo == id) return;
    formado = false;
    if (fin_movimiento->ruta_activa == nullptr) {
        liberar();
        return;
    }
    log(fin_movimiento->id, "deslizamiento activo " + std::to_string(id));
    deslizamiento_activo = id;
    root->actualizar(true);
    for (auto *r2 : rutas_afectadas) {
        if (r2->deslizamientos_afectados.find(this) == r2->deslizamientos_afectados.end())
            r2->deslizamientos_afectados[this] = id;
    }
}
void ruta_deslizamiento::liberar()
{
    deslizamiento_activo = -1;
    formado = false;
    root->actualizar(false);
    for (auto *r2 : rutas_afectadas) {
        r2->deslizamientos_afectados.erase(this);
    }
}
void ruta_deslizamiento::update()
{
    if (deslizamiento_activo < 0) return;
    if (!formado) {
        bool agujas_dispuestas = true;
        auto &posicion_aparatos = deslizamientos_orientados[deslizamiento_activo];
        for (auto &[sec, pins] : posicion_aparatos) {
            if (sec->tipo == TipoSeccion::Aguja) {
                aguja *a = (aguja*)sec;
                auto pos = a->get_posicion(pins);
                if (!a->enclavar(fin_movimiento->ruta_activa, a->get_posicion(pins))) {
                    agujas_dispuestas = false;
                    break;
                }
            }
        }
        if (agujas_dispuestas) {
            formado = true;
            log(fin_movimiento->id, "deslizamiento formado " + std::to_string(deslizamiento_activo));
            for (auto &r : rutas_afectadas) {
                r->deslizamientos_afectados[this] = deslizamiento_activo;
            }
        }
    }
}
