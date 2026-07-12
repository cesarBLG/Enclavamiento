#include "topology.h"
#include "ruta.h"
#include "items.h"
#include "pn_enclavado.h"
seccion_via::seccion_via(const id_elemento &id, const json &j, TipoSeccion tipo) : id(id), bloqueo_asociado(j.contains("Bloqueo") ? std::optional<id_elemento>(id_elemento(j["Bloqueo"])) : std::nullopt), tipo(tipo), id_cv(j.value("CV", id.id))
{
    if (tipo == TipoSeccion::Lineal) {
        active_outs[Lado::Impar][0] = 0;
        active_outs[Lado::Par][0] = 0;
    } else if (tipo == TipoSeccion::Cruzamiento) {
        for (int i=0; i<2; i++) {
            active_outs[Lado::Impar][i] = i;
            active_outs[Lado::Par][i] = i;
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
    trayecto = j.value("Trayecto", bloqueo_asociado.has_value());
}
void seccion_via::asegurar(movimiento *ruta, int in, int out, std::optional<Lado> dir)
{
    auto r = reserva_seccion();
    r.ruta_asegurada = ruta;
    r.outs[dir ? *dir : Lado::Impar] = out;
    r.outs[opp_lado(dir ? *dir : Lado::Impar)] = in;
    r.lado = dir;
    if (ruta_asegurada || ruta == nullptr) return;
    log(id, "reservada", LOG_DEBUG);
    ruta_asegurada = r;
    remota_cambio_elemento(ElementoRemota::CV, id_cv);
}
void seccion_via::asegurar_deslizamiento(movimiento *ruta, nodo_deslizamiento* nodo)
{
    this->deslizamiento[ruta] = nodo;
    remota_cambio_elemento(ElementoRemota::CV, id_cv);
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
        ruta_asegurada = std::nullopt;
        remota_cambio_elemento(ElementoRemota::CV, id_cv);
    } else if (deslizamiento.find(ruta) != deslizamiento.end()) {
        deslizamiento.erase(ruta);
        remota_cambio_elemento(ElementoRemota::CV, id_cv);
    }
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
    if (ev.evento && ev.evento->ocupacion && ev.estado > EstadoCV::Prenormalizado && ocupacion_outs[Lado::Impar] < 0 && ocupacion_outs[Lado::Par] < 0) {
        if (trayecto) {
            ocupacion_outs = {0, 0};
        } else if (ruta_asegurada) {
            ocupacion_outs = ruta_asegurada->outs;
        } else {
            ocupacion_outs = {-1, -1};
        }
    }
    if (ev.estado <= EstadoCV::Prenormalizado)
        ocupacion_outs = {-1, -1};

    if (ev.evento && ev.evento->ocupacion && ev.estado > EstadoCV::Prenormalizado) {
        bool intempestiva = false;
        if (trayecto) {
            if (bloqueo_asociado && bloqueo_act.estado != (ev.evento->lado == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar) && bloqueo_act.ruta[ev.evento->lado] != TipoMovimiento::Maniobra) {
                //intempestiva = true;
            }
        } else {
            if (!ruta_asegurada) {
                intempestiva = true;
            } else if (ev.evento->cv_colateral != "") {
                Lado l = opp_lado(ev.evento->lado);
                auto &sigs = siguientes_secciones[l];
                for (int i=0; i<sigs.size(); i++) {
                    if (secciones[sigs[i].id]->id_cv == ev.evento->cv_colateral && ruta_asegurada->outs[l] != i) {
                        intempestiva = true;
                        break;
                    }
                }
            }
        }
        if (intempestiva) {
            cv_seccion->ocupacion_intempestiva = true;
        }
    }

    for (auto *pn : pns) {
        pn->message_cv(ev);
    }

    remota_cambio_elemento(ElementoRemota::CV, id_cv);
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

void seccion_via::prev_secciones(seccion_via *next, Lado dir_fwd, std::vector<std::pair<seccion_via*, Lado>> &secciones)
{
    Lado lado = opp_lado(dir_fwd);
    int out = get_out(next, dir_fwd);
    if (out < 0) return;
    std::set<int> ins;
    for (auto &[in, out2] : active_outs[dir_fwd]) {
        if (out2 == out) {
            ins.insert(in);
        }
    }
    if (ruta_asegurada && ruta_asegurada->outs[dir_fwd] == out) {
        ins.insert(ruta_asegurada->outs[lado]);
    }
    for (int in : ins) {
        auto &sig = siguientes_secciones[lado];
        if (sig.size() <= in) continue;
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
void from_json(const json &j, seccion_via::conexion &conex)
{
    if (j.contains("Id")) conex.id = id_elemento(j["Id"]);
    conex.invertir_paridad = j.value("InvertirParidad", false);
}
