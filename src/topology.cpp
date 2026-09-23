#include "topology.h"
#include "ruta.h"
#include "items.h"
#include "pn_enclavado.h"
seccion_via::seccion_via(const id_elemento &id, const json &j, TipoSeccion tipo) : id(id), bloqueo_asociado(j.contains("Bloqueo") ? std::optional<id_elemento>(id_elemento(j["Bloqueo"])) : std::nullopt), tipo(tipo), id_cv(j.value("CV", id.id))
{
    if (tipo == TipoSeccion::Lineal || tipo == TipoSeccion::Cruzamiento) {
        for (int i=0; i<(tipo == TipoSeccion::Cruzamiento ? 2 : 1); i++) {
            for (auto &l : {Lado::Impar, Lado::Par}) {
                active_outs[l][i] = i;
                all_outs[l][i].insert(i);
            }
        }
    }
    auto cv_it = cvs.find(id_cv);
    if (cv_it != cvs.end()) {
        cv_seccion = cvs[id_cv];
        cv_seccion->secciones.insert(this);
    } else {
        cv_seccion = nullptr;
    }
    if (j.contains("Conexiones")) {
        siguientes_secciones = j["Conexiones"];
    }
    if (j.contains("Flanco")) {
        for (auto &jf : j["Flanco"]) {
            proteccion_flanco.push_back(new flanco(this, jf));
        }
    }
    if (j.contains("PuntosNegros")) {
        for (auto &jg : j["PuntosNegros"]) {
            punto_negro pt(this, jg);
            puntos_negros.push_back(pt);
        }
    }
    trayecto = j.value("Trayecto", bloqueo_asociado.has_value());
}
void seccion_via::asegurar(movimiento *ruta, lados<int> outs, std::optional<Lado> dir)
{
    if (ruta_asegurada || ruta == nullptr) return;
    auto r = reserva_seccion();
    r.ruta_asegurada = ruta;
    r.outs = outs;
    r.lado = dir;
    log(id, "reservada", LOG_DEBUG);
    ruta_asegurada = r;
    for (auto *f : proteccion_flanco) {
        if (f->in == r.outs[opp_lado(f->dir)])
            continue;
        f->activar(ruta);
    }
    remota_cambio_elemento("sec", id);
}
void seccion_via::asegurar_deslizamiento(movimiento *ruta, nodo_deslizamiento* nodo)
{
    if (deslizamiento.find(nodo) != deslizamiento.end()) return;
    deslizamiento[nodo] = ruta;
    log(id, "deslizamiento asegurado", LOG_DEBUG);
    remota_cambio_elemento("sec", id);
}
void seccion_via::liberar_deslizamiento(movimiento *ruta, nodo_deslizamiento* nodo)
{
    if (deslizamiento.find(nodo) == deslizamiento.end()) return;
    deslizamiento.erase(nodo);
    remota_cambio_elemento("sec", id);
    for (auto &[n, r] : deslizamiento) {
        if (r == ruta) return;
    }
    log(id, "fin deslizamiento", LOG_DEBUG);
    liberar(ruta);
}
void seccion_via::liberar(movimiento *ruta)
{
    if (ruta_asegurada && ruta_asegurada->ruta_asegurada == ruta) {
        log(id, "desenclavada");
        for (auto *pn : pns) {
            if (!ruta_asegurada->lado) {
                pn->desactivar_ruta(Lado::Impar);
                pn->desactivar_ruta(Lado::Par);
            } else {
                pn->desactivar_ruta(*ruta_asegurada->lado);
            }
        }
        for (auto *f : proteccion_flanco) {
            if (f->in == ruta_asegurada->outs[opp_lado(f->dir)])
                continue;
            f->desactivar(ruta_asegurada->ruta_asegurada);
        }
        ruta_asegurada = std::nullopt;
        remota_cambio_elemento("sec", id);
    }
}
bool seccion_via::invade_galibo(lados<int> outs, movimiento *ruta)
{
    // Comprobar si hay otra ruta asegurada incompatible por gálibo
    for (auto &pt : puntos_negros) {
        if (!pt.afectado_propio(outs)) continue;
        auto *sec = secciones[pt.seccion_causante];
        if (sec->ruta_asegurada && sec->ruta_asegurada->ruta_asegurada != ruta) {
            if (pt.afectado_ajeno(sec->ruta_asegurada->outs))
                return true;
        }
    }
    return false;
}
bool seccion_via::asegurar_posible(movimiento *ruta, lados<int> outs, std::optional<Lado> dir)
{
    if (cv_seccion != nullptr) {
        for (auto &sec : cv_seccion->secciones) {
            if (sec->is_asegurada() && !sec->is_asegurada(ruta)) return false;
        }
    }
    if (ruta_asegurada && ruta_asegurada->ruta_asegurada != ruta) return false;

    if (invade_galibo(outs, ruta)) return false;

    return true;
}
bool seccion_via::transitable(int in, Lado dir)
{
    if (in < 0) return false;
    int out = active_outs[dir][in];
    if (out < 0) return false;
    auto outs = lados<int>::from_directional(in, out, dir);

    if (afectada_galibo(outs)) return false;

    // Comprobar que no hay un deslizamiento que invada gálibo
    if (!ruta_asegurada) {
        if (invade_galibo(outs)) return false;
        for (auto &pt : puntos_negros) {
            if (!pt.afectado_propio(outs)) continue;
            auto *sec = secciones[pt.seccion_causante];
            for (auto &[desliz, r] : sec->deslizamiento) {
                if (desliz->invade_galibo(pt.pin_ajeno, desliz->deslizamiento->deslizamiento_activo))
                    return false;
            }
        }
    }

    for (auto*f : proteccion_flanco) {
        if (f->in == (dir == f->dir ? in : out))
            continue;
        if (!f->protegido(ruta_asegurada ? ruta_asegurada->ruta_asegurada : nullptr))
            return false;
    }
    // TODO: Maniobra local
    if (ruta_asegurada && (ruta_asegurada->outs[dir] != out || ruta_asegurada->outs[opp_lado(dir)] != in))
        return false;
    return true;
}
bool seccion_via::afectada_galibo(lados<int> outs)
{
    for (auto &pt : puntos_negros) {
        if (!pt.afectado_propio(outs)) continue;
        auto *sec = secciones[pt.seccion_causante];
        auto *cv = sec->get_cv();
        if (cv != nullptr && cv->get_state() > EstadoCV::Prenormalizado) {
            // Ocupación en la posición de falta de gálibo
            if (pt.afectado_ajeno(sec->ocupacion_outs))
                return true;
            // Ocupación en posición desconocida
            if (sec->ocupacion_outs[pt.pin_ajeno->first] < 0 && (cv->ocupacion_intempestiva || sec->ocupacion_outs[opp_lado(pt.pin_ajeno->first)] >= 0))
                return true;
            // Posición actual desconocida
            int in2 = sec->active_outs[opp_lado(pt.pin_ajeno->first)][pt.pin_ajeno->second];
            for (auto &[in3,out3] : sec->active_outs[pt.pin_ajeno->first]) {
                if ((out3 < 0 && in2 == in3) || out3 == pt.pin_ajeno->second)
                    return true;
            }
        }
    }
    return false;
}
TipoMovimiento seccion_via::get_tipo_movimiento()
{
    if (ruta_asegurada)
        return ruta_asegurada->ruta_asegurada->tipo;
    if (bloqueo_asociado) {
        if (bloqueo_act.estado != EstadoBloqueo::Desbloqueo && bloqueo_act.estado != EstadoBloqueo::SinDatos)
            return TipoMovimiento::Itinerario;
    }
    return TipoMovimiento::Ninguno;
}
void seccion_via::message_cv(const id_elemento &id, estado_cv ev)
{
    if (id != id_cv) return;

    std::optional<reserva_seccion> ruta_asegurada_cv;
    for (auto *sec : cv_seccion->secciones) {
        if (sec->ruta_asegurada) {
            ruta_asegurada_cv = sec->ruta_asegurada;
            break;
        }
    }
    bool intempestiva = false;
    if ((ev.evento && ev.evento->ocupacion || (!ev.evento && ev.estado_previo <= EstadoCV::Prenormalizado)) && ev.estado > EstadoCV::Prenormalizado) {
        if (trayecto) {
            if (ev.evento && bloqueo_asociado && bloqueo_act.estado != (ev.evento->lado == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar) && bloqueo_act.ruta[ev.evento->lado] != TipoMovimiento::Maniobra) {
                //intempestiva = true;
            }
        } else {
            if (!ruta_asegurada_cv) {
                // Si no hay ninguna ruta que discurra por el CV, es intempestiva
                intempestiva = true;
            } else if (ev.evento && ev.evento->seccion == this->id) {
                // Si se conoce el punto de entrada de la ocupación, comprobar que corresponde al de la ruta asegurada
                if (!ruta_asegurada || ruta_asegurada->outs[opp_lado(ev.evento->lado)] != ev.evento->pin)
                    intempestiva = true;
            } else if (ruta_asegurada && ruta_asegurada->lado) {
                // Comprobar que el CV anterior está ocupado
                Lado opp = opp_lado(*ruta_asegurada->lado);
                int in = ruta_asegurada->outs[opp];
                if (in >= 0 && siguientes_secciones[opp][in].id.id != "") {
                    auto *sec = secciones[siguientes_secciones[opp][in].id];
                    if (sec->ruta_asegurada && sec->ruta_asegurada->ruta_asegurada == ruta_asegurada->ruta_asegurada && sec->get_cv() != nullptr && sec->get_cv() != cv_seccion && sec->get_cv()->get_state() <= EstadoCV::Prenormalizado)
                        intempestiva = true;
                }
            }
        }
    }
    if (intempestiva) {
        log(cv_seccion->id, "ocupacion intempestiva", LOG_WARNING);
        cv_seccion->ocupacion_intempestiva = true;
    }

    if (ev.estado_previo <= EstadoCV::Prenormalizado && ev.estado > EstadoCV::Prenormalizado) {
        // Determina los pines por los que se produce la ocupación
        for (Lado l : {Lado::Impar, Lado::Par}) {
            // Punto de entrada al CV en esta sección + punto de salida comprobando
            if (ev.evento && ev.evento->seccion == this->id && (ev.evento->lado != l || active_outs[l][ev.evento->pin] >= 0))
                ocupacion_outs[l] = ev.evento->pin;
            // Punto de entrada normal para la ruta + punto de salida comprobando
            else if (ruta_asegurada && !intempestiva && ((ruta_asegurada->lado && ruta_asegurada->lado == opp_lado(l)) || ruta_asegurada->outs[l] == active_outs[l][ruta_asegurada->outs[opp_lado(l)]]))
                ocupacion_outs[l] = ruta_asegurada->outs[l];
            // Punto de entrada/salida único
            else if ((!ruta_asegurada_cv || ruta_asegurada || intempestiva) && active_outs[opp_lado(l)].size() == 1)
                ocupacion_outs[l] = active_outs[opp_lado(l)].begin()->first;
        }
    } else if (ev.estado <= EstadoCV::Prenormalizado) {
        ocupacion_outs = {-1, -1};
    }

    for (auto *pn : pns) {
        pn->message_cv(ev);
    }

    for (auto &pt : puntos_negros) {
        remota_cambio_elemento("sec", pt.seccion_causante);
    }

    remota_cambio_elemento("cv", id);
}
EstadoCanton seccion_via::get_ocupacion(seccion_via* prev, Lado dir)
{
    if (cv_seccion == nullptr) return EstadoCanton::Libre;
    EstadoCanton estado = cv_seccion->get_ocupacion(dir);
    if (estado != EstadoCanton::OcupadoMismoSentido) return estado;
    if (cv_seccion->ocupacion_intempestiva) return EstadoCanton::Ocupado;
    auto *in_ocupacion = get_seccion_in(dir, ocupacion_outs[opp_lado(dir)]).first;
    if (in_ocupacion != prev) return EstadoCanton::Ocupado;
    return EstadoCanton::OcupadoMismoSentido;
}
señal *seccion_via::señal_inicio(Lado lado, int pin)
{
    auto it = señales[lado].find(pin);
    if (it != señales[lado].end()) return it->second;
    return nullptr;
}
seccion_via* seccion_via::siguiente_seccion(int in, Lado &dir, bool usar_ruta_asegurada)
{
    int out;
    // TODO: usar_ruta_asegurada en maniobra local
    if (usar_ruta_asegurada) out = ruta_asegurada ? ruta_asegurada->outs[dir] : -1;
    else out = active_outs[dir][in];
    if (out < 0 || siguientes_secciones[dir].empty()) return nullptr;
    auto p = siguientes_secciones[dir][out];
    if (p.invertir_paridad) dir = opp_lado(dir);
    if (p.id.id == "") return nullptr;
    return secciones[p.id];
}

std::pair<seccion_via*,Lado> seccion_via::get_seccion_in(Lado dir, int pin)
{
    Lado lado = opp_lado(dir);
    auto &sigs = siguientes_secciones[lado];
    if (pin < 0 || pin >= sigs.size() || sigs[pin].id.id == "") return {nullptr, dir};
    return {secciones[sigs[pin].id],sigs[pin].invertir_paridad ? lado : dir};
}

void seccion_via::prev_secciones(seccion_via *next, Lado dir_fwd, std::vector<std::pair<seccion_via*, Lado>> &secciones, bool activas)
{
    Lado lado = opp_lado(dir_fwd);
    int out = get_out(next, dir_fwd);
    if (out < 0) return;
    auto &sig = siguientes_secciones[lado];
    for (int in=0; in<sig.size(); in++) {
        if (activas) {
            if (active_outs[dir_fwd][in] != out && (!ruta_asegurada || ruta_asegurada->outs[dir_fwd] != out))
                continue;
        } else {
            auto &s = all_outs[dir_fwd][in];
            if (s.find(out) == s.end())
                continue;
        }
        auto p = sig[in];
        if (p.id.id == "") continue;
        secciones.push_back({::secciones[p.id], p.invertir_paridad ? lado : dir_fwd});
    }
}

int seccion_via::get_in(seccion_via* prev, Lado dir)
{
    Lado lado = opp_lado(dir);
    if (prev == nullptr && siguientes_secciones[lado].empty()) return 0;
    for (int i=0; i<siguientes_secciones[lado].size(); i++) {
        auto p = siguientes_secciones[lado][i];
        if ((prev != nullptr && p.id == prev->id) || (i == 0 && siguientes_secciones[lado].size() == 1)) {
            return i;
        }
    }
    return -1;
}
int seccion_via::get_out(seccion_via* next, Lado dir)
{
    if (next == nullptr && siguientes_secciones[dir].empty()) return 0;
    for (int i=0; i<siguientes_secciones[dir].size(); i++) {
        auto p = siguientes_secciones[dir][i];
        if ((next != nullptr && p.id == next->id) || (i == 0 && siguientes_secciones[dir].size() == 1)) {
            return i;
        }
    }
    return -1;
}
bool seccion_via::is_desviada(seccion_via *prev, Lado dir)
{
    int in = get_in(prev, dir);
    if (in < 0) return true;
    int out = active_outs[dir][0];
    if (out < 0) return true;
    return in != out;
}
RemotaCV seccion_via::get_estado_remota()
{
    auto cv_it = cvs.find(id_cv);
    if (cv_it != cvs.end()) return cv_it->second->get_estado_remota();

    RemotaCV r;
    TipoMovimiento tipo = get_tipo_movimiento();
    r.CV_DAT = 1;
    r.CV_ME = me_pendiente ? 1 : 0;
    r.CV_BV = bloqueo_seccion? 1 : 0;
    r.CV_OCUP_TIPO = 0;
    if (tipo == TipoMovimiento::Maniobra) r.CV_EST = 2;
    else if (tipo == TipoMovimiento::Itinerario || tipo == TipoMovimiento::Rebase) r.CV_EST = 1;
    else r.CV_EST = 0;
    r.CV_DES = 0;
    r.CV_CEJES_AV = 0;
    r.CV_CEJES_PREN = 0;
    r.CV_UC = 0;
    r.CV_NSEC = 0;
    return r;
}
RemotaCVX cruzamiento::get_estado_remota()
{
    RemotaCVX r;
    TipoMovimiento tipo = get_tipo_movimiento();
    auto cv_it = cvs.find(id_cv);
    cv *cv = cv_it != cvs.end() ? cv_it->second : nullptr;
    r.CVX_DAT = 1;
    r.CVX_ME = me_pendiente || (cv != nullptr && cv->is_me_pendiente()) ? 1 : 0;
    r.CVX_BV = bloqueo_seccion || (cv != nullptr && cv->is_btv()) ? 1 : 0;
    r.CVX_OCUP_TIPO = (cv != nullptr && cv->ocupacion_intempestiva) ? 1 : 0;
    if (cv != nullptr && cv->get_state() > EstadoCV::Prenormalizado) r.CVX_EST = 3;
    else if (tipo == TipoMovimiento::Maniobra) r.CVX_EST = 2;
    else if (tipo == TipoMovimiento::Itinerario || tipo == TipoMovimiento::Rebase) r.CVX_EST = 1;
    else if (cv != nullptr && cv->get_state() == EstadoCV::Prenormalizado) r.CVX_EST = 3;
    else r.CVX_EST = 0;
    r.CVX_DIR = ruta_asegurada ? (ruta_asegurada->outs.impar == 0 ? 1 : 2) : 0;
    r.CVX_DES_N = 0;
    r.CVX_DES_I = 0;
    r.CVX_GAL = 0;
    r.CVX_CEJES_AV = (cv != nullptr && cv->is_averia()) ? 1 : 0;
    r.CVX_CEJES_PREN = cv != nullptr && cv->get_state() == EstadoCV::Prenormalizado ? 1 : 0;
    return r;
}
void from_json(const json &j, seccion_via::conexion &conex)
{
    if (j.contains("Id")) conex.id = id_elemento(j["Id"]);
    conex.invertir_paridad = j.value("InvertirParidad", false);
}

punto_negro::punto_negro(seccion_via *sec, const json &j) : seccion_afectada(sec)
{
    seccion_causante = id_elemento::from_default_dep(sec->id.dependencia, j["Id"]);
    if (j.contains("Afectado"))
        pin_propio = {j["Afectado"]["Lado"], j["Afectado"]["Pin"]};
    if (j.contains("Causante"))
        pin_propio = {j["Causante"]["Lado"], j["Causante"]["Pin"]};
}
flanco::flanco(seccion_via *sec, const json &j)
{
    dir = j["Lado"];
    in = j["Pin"];
    std::set<seccion_via*> stop;
    for (auto &id : j["Límite"]) {
        stop.insert(secciones[id_elemento::from_default_dep(id, sec->id.dependencia)]);
    }
    std::map<seccion_via*, lados<int>> pos;
    if (j.contains("PosiciónAparatos")) {
        for (auto &[sec_id, jpos] : j["PosiciónAparatos"].items()) {
            pos[::secciones[id_elemento::from_default_dep(sec_id, sec->id.dependencia)]] = jpos;
        }
    }
    auto p = sec->get_seccion_in(dir, in);
    root = new nodo_flanco(sec, p.first, p.second, stop, pos);
}
nodo_flanco::nodo_flanco(seccion_via *next, seccion_via *sec, Lado dir, const std::set<seccion_via*> &stop, const std::map<seccion_via*, lados<int>> &posicion_aparatos) : next(next), seccion(sec), dir(dir)
{
    auto it = posicion_aparatos.find(sec);
    if (it != posicion_aparatos.end()) posicion = it->second;
    std::vector<std::pair<seccion_via*, Lado>> secciones;
    sec->prev_secciones(next, dir, secciones, false);
    bool end = stop.find(sec) != stop.end();
    for (auto &p : secciones) {
        if (p.first == nullptr) continue;
        if (end && stop.find(p.first) == stop.end()) continue;
        prev.push_back(new nodo_flanco(sec, p.first, p.second, stop, posicion_aparatos));
    }
}
void nodo_flanco::activar(movimiento *m)
{
    if (posicion && seccion->tipo == TipoSeccion::Aguja) {
        auto *a = (aguja*)seccion;
        auto pos = a->get_posicion(*posicion);
        if (dependencias[a->id.dependencia]->bloqueo_agujas || !a->mover(pos)) {
            a->requerir_movimiento(m, pos);
        }
    }
    for (auto &n : prev) {
        n->activar(m);
    }
}
void nodo_flanco::desactivar(movimiento *m)
{
    if (posicion && !seccion->is_asegurada(m)) seccion->liberar(m);
    for (auto &n : prev) {
        n->desactivar(m);
    }
}
bool nodo_flanco::protegido(movimiento *m)
{
    if (posicion) {
        if (seccion->get_active_out((*posicion)[opp_lado(dir)], dir) != (*posicion)[dir])
            return false;
        if (seccion->tipo == TipoSeccion::Aguja && m != nullptr) {
            auto *a = (aguja*)seccion;
            auto pos = a->get_posicion(*posicion);
            a->enclavar(m, pos);
        }
    }
    auto *cv = seccion->get_cv();
    if (cv != nullptr && cv->ocupacion_intempestiva)
        return false;
    int out = seccion->get_out(next, dir);
    for (auto &n : prev) {
        int in = seccion->get_in(n->seccion, dir);
        int out2 = seccion->get_active_out(in, dir);
        if (out != out2 && out2 >= 0)
            continue;
        if (!n->protegido(m))
            return false;
    }
    return true;
}
