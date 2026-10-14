#include "indicadoras.h"
#include "ruta.h"
#include "items.h"
void indicadora_salida::update()
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
void indicadora_direccion::update()
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
    for (auto &[id, asp] : aspectos) {
        auto it = destinos_ruta.find(id);
        if (it == destinos_ruta.end()) continue;
        auto *r2 = it->second->ruta_activa;
        if (r2 == nullptr) continue;
        if (r == r2) {
            aspecto = asp;
            break;
        }
        if (r == nullptr || r2->get_señal_inicio()->id == r->get_destino()->id) {
            aspecto = asp;
            break;
        }
    }
}
void indicadora_agujas::update()
{
    aspecto = "Apagada";
    auto ra = seccion->get_ruta_asegurada();
    if (!ra || ra->lado == opp_lado(lado) || ra->outs[opp_lado(lado)] != pin || ra->ruta_asegurada->tipo != TipoMovimiento::Itinerario)
        return;
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
            if (aligned) {
                aspecto = asp;
                break;
            }
        }
    } else {
        velocidad_agujas vel = velocidad_agujas::max();
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
        int vact = 0;
        for (auto &[vel2, asp] : aspectos) {
            if (vel2.posicion != vel.posicion) continue;
            if (vel2.velocidad <= vel.velocidad && vel2.velocidad > vact) {
                aspecto = asp;
                vact = vel2.velocidad;
            }
        }
    }
}
