#include "dependencia.h"
#include "items.h"
void from_json(const json &j, config_señal_servicio_intermitente &cfg)
{
    cfg.abierta_desbloqueo = j.value("Desbloqueo", false);
    cfg.abierta_bloqueo_receptor = j.value("BloqueoReceptor", false);
}
void from_json(const json &j, config_servicio_intermitente &cfg)
{
    if (j.contains("FAI")) {
        cfg.fais = j["FAI"];
    }
    if (j.contains("SeñalesAbiertas")) {
        cfg.señales_abiertas = j["SeñalesAbiertas"];
    }
}
dependencia::dependencia(const std::string id, const json &j) : id(id), mando_actual({false, "PLO_"+id, std::nullopt, false})
{
    if (j.contains("ServicioIntermitente")) {
        auto &si = j["ServicioIntermitente"];
        cerrada = si.value("Activo", false);
        servicio_intermitente = si;
    }
}
void dependencia::calcular_vinculacion_bloqueos()
{
    // Para cada bloqueo en estación cerrada, indicar si es posible realizar cruces, 
    // o si las señales de la estación se deben comportar como un puesto de bloqueo, enlazando
    // los bloqueos de las estaciones colaterales
    for (auto &[id, bloq] : bloqueos) {
        std::optional<id_elemento> vinculo;
        bool señal_ruta = false;
        Lado dir = opp_lado(bloq->lado);
        seccion_via *prev = bloq->get_cvs()[0];
        seccion_via *sec = prev->siguiente_seccion(nullptr, dir, false);
        while (sec != nullptr) {
            auto *sig = sec->señal_inicio(dir, prev);
            if (sig != nullptr && señal_impls.find(sig->id) != señal_impls.end() && señal_impls[sig->id]->ruta_necesaria)
                señal_ruta = true;
            if (sec->bloqueo_asociado) {
                for (auto &[id2, bloq2] : bloqueos) {
                    if (bloq2->get_cvs()[0] == sec) {
                        vinculo = id2;
                        break;
                    }
                }
                break;
            }
            seccion_via *next;
            if (sec->tipo == TipoSeccion::Aguja) {
                aguja *ag = (aguja*)sec;
                next = ag->ruta_fija(prev, dir);
            } else {
                next = sec->siguiente_seccion(prev, dir, false);
            }
            prev = sec;
            sec = next;
        }
        if (vinculo) bloq->vincular(*vinculo, !señal_ruta);
        else bloq->desvincular();
    }
}
bool dependencia::set_servicio_intermitente(bool cerrar)
{
    if (!servicio_intermitente) return false;
    if (cerrar) {
        // Comprobar que no hay bloqueos en sentidos opuestos
        for (auto &[id, bloq] : bloqueos) {
            Lado dir = opp_lado(bloq->lado);
            seccion_via *prev = bloq->get_cvs()[0];
            seccion_via *sec = prev->siguiente_seccion(nullptr, dir, false);
            while (sec != nullptr) {
                if (sec->bloqueo_asociado) {
                    for (auto &[id2, bloq2] : bloqueos) {
                        if (bloq2->get_cvs()[0] == sec) {
                            auto e1 = bloq->get_estado();
                            auto e2 = bloq2->get_estado();
                            if (e1.estado == bloq->bloqueo_emisor && e2.estado == bloq2->bloqueo_emisor) return false;
                            if (e1.estado == bloq->bloqueo_receptor && e2.estado == bloq2->bloqueo_receptor) return false;
                            break;
                        }
                    }
                    break;
                }
                seccion_via *next;
                if (sec->tipo == TipoSeccion::Aguja) {
                    aguja *ag = (aguja*)sec;
                    next = ag->ruta_fija(prev, dir);
                } else {
                    next = sec->siguiente_seccion(prev, dir, false);
                }
                prev = sec;
                sec = next;
            }
        }
        // Comprobar agujas
        for (auto &[id, pins] : servicio_intermitente->posicion_aparatos) {
            seccion_via *sec = ::secciones[id_elemento(id)];
            if (sec->tipo == TipoSeccion::Aguja) {
                aguja *a = (aguja*)sec;
                auto pos = a->get_posicion(Lado::Impar, pins.first, pins.second);
                if (!a->posible_mover(pos)) return false;
            }
        }
    }
    for (auto *r : rutas) {
        if (servicio_intermitente->fais.find(r->id) != servicio_intermitente->fais.end()) {
            if (cerrar) {
                auto *r2 = r->get_señal_inicio()->ruta_fai;
                if (r2 != r && r2 != nullptr) r2->set_fai(false);
                if (r2 != r) r->set_fai(true);
            } else {
                r->set_fai(false);
            }
        }
    }
    for (auto &[ids, sig] : señal_impls) {
        if (ids.dependencia == id) {
            auto it = servicio_intermitente->señales_abiertas.find(ids.id_corto);
            if (it != servicio_intermitente->señales_abiertas.end()) {
                sig->cierre_stick = !cerrar;
                sig->ruta_necesaria = !cerrar;
                sig->abierta_desbloqueo = cerrar && it->second.abierta_desbloqueo;
                if (cerrar) sig->clear_request = true;
                else if (sig->ruta_activa == nullptr) sig->clear_request = false;
            } else {
                sig->cierre_stick = sig->ruta_necesaria && !cerrar;
                bool clear = sig->ruta_activa != nullptr || !sig->ruta_necesaria;
                if (cerrar) sig->clear_request |= clear;
                else sig->clear_request &= clear;
            }
        }
    }
    if (!bloqueo_agujas) {
        // Mover agujas
        for (auto &[id, pins] : servicio_intermitente->posicion_aparatos) {
            seccion_via *sec = ::secciones[id_elemento(id)];
            if (sec->tipo == TipoSeccion::Aguja) {
                aguja *a = (aguja*)sec;
                auto pos = a->get_posicion(Lado::Impar, pins.first, pins.second);
                a->mover(pos);
            }
        }
    }
    cerrada = cerrar;

    calcular_vinculacion_bloqueos();
    return true;
}
