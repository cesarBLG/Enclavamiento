#include "topology.h"
#include "ruta.h"
#include "items.h"
#include "pn_enclavado.h"
seccion_via::seccion_via(const std::string &id, const json &j, TipoSeccion tipo) : id(id), bloqueo_asociado(j.value("Bloqueo", "")), tipo(tipo)
{
    active_outs[Lado::Impar][0] = 0;
    active_outs[Lado::Par][0] = 0;
    route_outs = {-1, -1};
    cv_seccion = cvs[j.value("CV", id)];
    cv_seccion->secciones.insert(this);
    if (j.contains("Conexiones")) {
        siguientes_secciones = j["Conexiones"];
    }
    trayecto = j.value("Trayecto", bloqueo_asociado != "");
}
void seccion_via::asegurar(ruta *ruta, int in, int out, Lado dir)
{
    if (ruta_asegurada != nullptr || ruta == nullptr) return;
    ruta_asegurada = ruta;
    route_outs[dir] = out;
    route_outs[opp_lado(dir)] = in;
    remota_cambio_elemento(ElementoRemota::CV, cv_seccion->id);
}
void seccion_via::asegurar(ruta *ruta, seccion_via *prev, seccion_via *next, Lado dir)
{
    if (ruta_asegurada != nullptr || ruta == nullptr) return;
    int in = get_in(prev, dir);
    int out = get_out(next, dir);
    if (in < 0 || out < 0) return;
    ruta_asegurada = ruta;
    route_outs[dir] = out;
    route_outs[opp_lado(dir)] = in;
    remota_cambio_elemento(ElementoRemota::CV, cv_seccion->id);
}
void seccion_via::liberar(ruta *ruta)
{
    if (ruta_asegurada == ruta) {
        ruta_asegurada = nullptr;
        route_outs = {-1, -1};
        for (auto *pn : pns) {
            pn->update();
        }
        remota_cambio_elemento(ElementoRemota::CV, cv_seccion->id);
    }
}
TipoMovimiento seccion_via::get_tipo_movimiento()
{
    if (ruta_asegurada != nullptr)
        return ruta_asegurada->tipo;
    if (bloqueo_asociado != "") {
        if (bloqueo_act.estado != EstadoBloqueo::Desbloqueo && bloqueo_act.estado != EstadoBloqueo::SinDatos)
            return TipoMovimiento::Itinerario;
    }
    return TipoMovimiento::Ninguno;
}
void seccion_via::message_cv(const std::string &id, estado_cv ev)
{
    if (id != cv_seccion->id) return;
    if (ev.evento && ev.evento->ocupacion && ev.estado > EstadoCV::Prenormalizado && ocupacion_outs[Lado::Impar] < 0 && ocupacion_outs[Lado::Par] < 0) {
        if (trayecto) {
            ocupacion_outs = {0, 0};
        } else {
            ocupacion_outs = route_outs;
        }
    }
    if (ev.estado <= EstadoCV::Prenormalizado)
        ocupacion_outs = {-1, -1};

    if (ev.evento && ev.evento->ocupacion && ev.estado > EstadoCV::Prenormalizado) {
        bool intempestiva = false;
        if (trayecto) {
            if (bloqueo_asociado != "" && bloqueo_act.estado != (ev.evento->lado == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar) && bloqueo_act.ruta[ev.evento->lado] != TipoMovimiento::Maniobra) {
                //intempestiva = true;
            }
        } else {
            if (ruta_asegurada == nullptr) {
                intempestiva = true;
            } else if (ev.evento->cv_colateral != "") {
                Lado l = opp_lado(ev.evento->lado);
                auto &sigs = siguientes_secciones[l];
                for (int i=0; i<sigs.size(); i++) {
                    if (secciones[sigs[i].id]->cv_seccion->id == ev.evento->cv_colateral && route_outs[l] != i) {
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

    remota_cambio_elemento(ElementoRemota::CV, cv_seccion->id);
}
EstadoCanton seccion_via::get_ocupacion(seccion_via* prev, Lado dir)
{
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
seccion_via* seccion_via::siguiente_seccion(seccion_via *prev, Lado &dir, bool usar_ruta_asegurada)
{
    int in = get_in(prev, dir);
    if (in < 0) return nullptr;
    int out;
    if (usar_ruta_asegurada) out = route_outs[dir];
    else out = active_outs[dir][in];
    if (out < 0 || siguientes_secciones[dir].empty()) return nullptr;
    auto p = siguientes_secciones[dir][out];
    if (p.invertir_paridad) dir = opp_lado(dir);
    return secciones[p.id];
}

std::pair<seccion_via*,Lado> seccion_via::get_seccion_in(Lado dir, int pin)
{
    Lado lado = opp_lado(dir);
    auto &sigs = siguientes_secciones[lado];
    if (pin < 0 || pin >= sigs.size()) return {nullptr, Lado::Impar};
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
    if (route_outs[dir_fwd] == out) {
        ins.insert(route_outs[lado]);
    }
    for (int in : ins) {
        auto p = siguientes_secciones[lado][in];
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

aguja::aguja(const std::string &id, const json &j) : seccion_via(id, j, TipoSeccion::Aguja), lado(j["Lado"])
{
    if (j.contains("SecciónPunta")) siguientes_secciones[opp_lado(lado)] = std::vector<conexion>({j["SecciónPunta"].get<conexion>()});
    if (j.contains("SeccionesTalón")) siguientes_secciones[lado] = j["SeccionesTalón"];
    talonable = PosicionAguja::Normal;
    update();
}
void from_json(const json &j, seccion_via::conexion &conex)
{
    conex.id = j["Id"];
    conex.invertir_paridad = j.value("Reverse", false);
}
