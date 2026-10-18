#include "indicadoras.h"
#include "ruta.h"
#include "items.h"
señal_indicadora *señal_indicadora::from_json(const id_elemento &id, const json &j)
{
    TipoIndicadora tipo = j["Tipo"];
    switch (tipo) {
        case TipoIndicadora::Agujas:
            return new indicadora_agujas(id, j);
        case TipoIndicadora::Direccion:
            return new indicadora_direccion(id, j);
        case TipoIndicadora::Salida:
            return new indicadora_salida(id, j);
        default:
            return nullptr;
    }
}
void indicadora_salida::determinar_aspecto()
{
    seccion_via *sec_act = seccion;
    Lado dir = lado;
    int in = pin;
    while (señal_salida == nullptr && sec_act != nullptr) {
        Lado l = dir;
        seccion_via *next = sec_act->siguiente_seccion(in, dir);
        if (next == nullptr) break;
        in = next->get_in(sec_act, l);
        sec_act = next;
        dir = l;
        if (sec_act == seccion) break;

        señal_salida = sec_act->señal_inicio(dir, in);
    }
    if (señal_salida == nullptr || señal_salida->aspecto <= Aspecto::Parada) {
        if (seccion->get_cv() == nullptr || cv_libre(seccion->get_cv()->get_state())) aspecto = "Apagada";
        else aspecto = "SeñalSalidaCerrada";
    } else {
        aspecto = "SeñalSalidaAbierta";
    }
}
indicadora_direccion::indicadora_direccion(const id_elemento &id, const json &j) : señal_indicadora(id, j, TipoIndicadora::Direccion)
{
    for (auto &dir : j["Direcciones"]) {
        ruta_compuesta rc;
        rc.aspecto = dir["Aspecto"];
        rc.destino_propio = id_elemento(dir.value("DestinoPropio", ""));
        if (dir.contains("Rutas")) {
            for (auto &r : dir["Rutas"]) {
                rc.rutas.push_back({id_elemento(r["Inicio"]),id_elemento(r["Destino"])});
            }
        }
        aspectos.push_back(rc);
    }
    for (auto &rc : aspectos) {
        if (rc.destino_propio.id != "") destinos[rc.destino_propio] = estado_fin_ruta();
        for (auto &[_,dest] : rc.rutas) {
            destinos[dest] = estado_fin_ruta();
        }
    }
    for (auto &[dst, _] : destinos) {
        subscribe("destino/"+id_to_mqtt(id.id)+"/state");
    }
}
void indicadora_direccion::determinar_aspecto()
{
    aspecto = "Apagada";
    ruta *r = nullptr;
    if (!seccion->is_trayecto()) {
        auto ra = seccion->get_ruta_asegurada();
        if (!ra 
            || ra->lado == opp_lado(lado) || (ra->outs[opp_lado(lado)] >= 0 && ra->outs[opp_lado(lado)] != pin)
            || !ra->ruta_asegurada->es_ruta || ra->ruta_asegurada->tipo != TipoMovimiento::Itinerario) {
            return;
        }
        r = (ruta*)ra->ruta_asegurada;
    }
    for (auto &asp : aspectos) {
        if (r != nullptr && asp.destino_propio.id != "" && asp.destino_propio != r->get_destino()->id)
            continue;
        bool activo = true;
        for (auto &r2 : asp.rutas) {
            auto it = destinos.find(r2.second);
            if (it == destinos.end() || it->second.señal_inicio != r2.first || it->second.tipo != TipoMovimiento::Itinerario) {
                activo = false;
                break;
            }
        }
        if (activo) {
            aspecto = asp.aspecto;
            break;
        }
    }
}
indicadora_agujas::indicadora_agujas(const id_elemento &id, const json &j) : señal_indicadora(id, j, TipoIndicadora::Agujas)
{
    if (j.contains("AspectosAparatos")) {
        for (auto &jap : j["AspectosAparatos"]) {
            std::map<seccion_via*, lados<int>> posicion_aparatos;
            for (auto &[id, pos] : jap["PosiciónAparatos"].items()) {
                posicion_aparatos[secciones[id]] = pos;
            }
            velocidad_agujas vel = jap["Velocidad"];
            bool found = false;
            for (auto &vel2 : velocidades) {
                if (vel == vel2) {
                    found = true;
                    break;
                }
            }
            if (!found) velocidades.push_back(vel);
            aspectos_aparatos.push_back({std::move(posicion_aparatos), vel});
        }
    } else if (j.contains("Velocidades")) {
        velocidades = j["Velocidades"];
    } else {
        velocidades.push_back(velocidad_agujas::max());
        velocidades.push_back({30, LadoAguja::Derecha});
        velocidades.push_back({30, LadoAguja::Izquierda});
    }
    for (auto &vel : velocidades) {
        aspectos.push_back(to_string(vel));
    }
}
void indicadora_agujas::determinar_aspecto()
{
    aspecto = "Apagada";
    auto ra = seccion->get_ruta_asegurada();
    if (!ra || ra->lado == opp_lado(lado) || ra->outs[opp_lado(lado)] != pin || ra->ruta_asegurada->tipo != TipoMovimiento::Itinerario)
        return;
    velocidad_agujas vel = velocidad_agujas::max();
    if (!aspectos_aparatos.empty()) {
        for (auto &[pos, asp] : aspectos_aparatos) {
            bool aligned = true;
            for (auto &[sec, outs] : pos) {
                auto ra2 = sec->get_ruta_asegurada();
                if (!ra2 || ra2->ruta_asegurada != ra->ruta_asegurada || ra2->outs != outs) {
                    aligned = false;
                    break;
                }
            }
            if (aligned)
                vel.update(asp);
        }
    } else {
        auto *sec_act = seccion;
        Lado dir = lado;
        while (sec_act != nullptr) {
            auto ra2 = sec_act->get_ruta_asegurada();
            if (!ra2 || ra2->ruta_asegurada != ra->ruta_asegurada) break;
            vel.update(sec_act->get_velocidad_agujas(ra2->outs));
            auto p = sec_act->get_seccion_in(opp_lado(dir), ra2->outs[dir]);
            sec_act = p.first;
            dir = opp_lado(p.second);
        }
    }
    velocidad_agujas vact = {0, LadoAguja::Recto};
    int index = -1;
    for (int i=0; i<velocidades.size(); i++) {
        velocidad_agujas vel2 = velocidades[i];
        if (vel2.posicion != vel.posicion) continue;
        if (vel2.velocidad <= vel.velocidad && vel2.velocidad > vact.velocidad) {
            index = i;
            vact = vel2;
        }
    }
    if (index >= 0) aspecto = aspectos[index];
}
