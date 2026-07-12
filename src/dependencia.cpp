#include "dependencia.h"
#include "items.h"
void from_json(const json &j, config_señal_servicio_intermitente &cfg)
{
    cfg.abierta_desbloqueo = j.value("Desbloqueo", false);
    cfg.abierta_bloqueo_receptor = j.value("BloqueoReceptor", false);
    cfg.ruta_necesaria = j.value("RutaNecesaria", false);
}
void from_json(const json &j, config_servicio_intermitente &cfg)
{
    if (j.contains("FAI")) {
        cfg.fais = j["FAI"];
    }
    if (j.contains("SeñalesAbiertas")) {
        cfg.señales_abiertas = j["SeñalesAbiertas"];
    }
    if (j.contains("PosiciónAparatos")) {
        cfg.posicion_aparatos = j["PosiciónAparatos"];
    }
    if (j.contains("Secciones")) {
        cfg.secciones = j["Secciones"];
    }
    if (j.contains("ItinerariosApertura")) {
        cfg.itinerarios_apertura = j["ItinerariosApertura"];
    }
}
movimiento_servicio_intermitente::movimiento_servicio_intermitente(const std::string &estacion, config_servicio_intermitente &cfg) : movimiento(estacion, TipoMovimiento::Itinerario, "SI "+estacion, false)
{
    for (auto &[id, pos] : cfg.posicion_aparatos) {
        posicion_aparatos[::secciones[id_elemento::from_default_dep(id, estacion)]] = pos;
    }
    for (auto &[id, sig] : cfg.señales_abiertas) {
        if (sig.ruta_necesaria)
            señales.push_back(::señal_impls[id_elemento::from_default_dep(id, estacion)]);
    }
    for (auto &[id, sec] : cfg.secciones) {
        secciones.push_back({::secciones[id_elemento::from_default_dep(id, estacion)], std::nullopt, sec.first, sec.second});
    }
}
dependencia::dependencia(const std::string id, const json &j) : id(id), mando_actual({false, "PLO_"+id, std::nullopt, false})
{
    if (j.contains("ServicioIntermitente")) {
        auto &si = j["ServicioIntermitente"];
        cerrada = si.value("Activo", false);
        cfg_servicio_intermitente = si;
    }
}
void dependencia::update()
{
    if (!inicializada) {
        if (cfg_servicio_intermitente) {
            servicio_intermitente = new movimiento_servicio_intermitente(id, *cfg_servicio_intermitente);
        }
        if (cerrada) set_servicio_intermitente(true);
        inicializada = true;
    }

    std::map<id_elemento, ruta*> movimiento_bloqueos;
    std::set<id_elemento> anular_bloqueos;
    std::set<id_elemento> solicitud_bloqueos;
    // Para cada bloqueo de salida, indicar el itinerario o maniobra establecido
    for (auto *ruta : ::rutas) {
        if (ruta->estacion == id) ruta->update();
        if (!ruta->bloqueo_salida || ruta->bloqueo_salida->dependencia != id) continue;
        if (ruta->is_mandada()) {
            movimiento_bloqueos[*ruta->bloqueo_salida] = ruta;
            if (ruta->tipo == TipoMovimiento::Itinerario) solicitud_bloqueos.insert(*ruta->bloqueo_salida);
        } else {
            if (ruta->anulacion_bloqueo_pendiente) {
                ruta->anulacion_bloqueo_pendiente = false;
                anular_bloqueos.insert(*ruta->bloqueo_salida);
            }
            if (ruta->get_estado_fai() == EstadoFAI::Solicitud) solicitud_bloqueos.insert(*ruta->bloqueo_salida);
        }
    }
    if (servicio_intermitente) servicio_intermitente->update();
    TipoSoneria son = TipoSoneria::Apagada;
    for (auto &[id, bloq] : bloqueos) {
        auto it = movimiento_bloqueos.find(id);
        int pri = solicitud_bloqueos.find(id) != solicitud_bloqueos.end() ? (cerrada ? 1 : 2) : 0;
        if (it == movimiento_bloqueos.end()) {
            bloq->set_ruta(TipoMovimiento::Ninguno, CompatibilidadManiobra::IncompatibleBloqueo, anular_bloqueos.find(id) != anular_bloqueos.end(), pri);
        } else {
            bloq->set_ruta(it->second->tipo, it->second->maniobra_compatible, false, pri);
        }
        son = std::max(bloq->update_soneria(), son);
    }
    if (soneria != son) {
        soneria = son;
        send_message("soneria/"+id, json(soneria).dump());
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
        seccion_via *sec = prev->siguiente_seccion(nullptr, dir);
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
                next = sec->siguiente_seccion(prev, dir);
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
    if (!cfg_servicio_intermitente) return false;
    if (cerrar) {
        // Comprobar que no hay bloqueos en sentidos opuestos
        for (auto &[id, bloq] : bloqueos) {
            Lado dir = opp_lado(bloq->lado);
            seccion_via *prev = bloq->get_cvs()[0];
            seccion_via *sec = prev->siguiente_seccion(nullptr, dir);
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
                    next = sec->siguiente_seccion(prev, dir);
                }
                prev = sec;
                sec = next;
            }
        }
        bool rutas_enclavadas = true;
        for (auto *r : rutas) {
            if (cfg_servicio_intermitente->itinerarios_apertura.find(r->id) != cfg_servicio_intermitente->itinerarios_apertura.end()) {
                if (!r->is_mandada()) {
                    rutas_enclavadas = false;
                    break;
                }
            }
        }
        if (rutas_enclavadas) {
            for (auto *r : rutas) {
                if (cfg_servicio_intermitente->itinerarios_apertura.find(r->id) != cfg_servicio_intermitente->itinerarios_apertura.end()) {
                    ((movimiento*)r)->disolver();
                }
            }
        }
        if (servicio_intermitente) {
            if (!servicio_intermitente->establecer()) return false;
            servicio_intermitente->update();
        }
    } else {
        if (servicio_intermitente) servicio_intermitente->disolver();
        for (auto *r : rutas) {
            if (cfg_servicio_intermitente->itinerarios_apertura.find(r->id) != cfg_servicio_intermitente->itinerarios_apertura.end()) {
                r->establecer();
                r->update();
            }
        }
    }
    for (auto *r : rutas) {
        if (cfg_servicio_intermitente->fais.find(r->id) != cfg_servicio_intermitente->fais.end()) {
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
            auto it = cfg_servicio_intermitente->señales_abiertas.find(ids.id_corto);
            if (it != cfg_servicio_intermitente->señales_abiertas.end()) {
                sig->ruta_necesaria = !cerrar || it->second.ruta_necesaria;
                sig->abierta_desbloqueo = cerrar && it->second.abierta_desbloqueo;
                sig->abierta_bloqueo_receptor = cerrar && it->second.abierta_bloqueo_receptor;
                if (cerrar) sig->clear_request = true;
                else if (sig->ruta_activa == nullptr) sig->clear_request = false;
            } else {
                bool clear = sig->ruta_activa != nullptr || !sig->ruta_necesaria;
                if (cerrar) sig->clear_request |= clear;
                else sig->clear_request &= clear;
            }
            sig->cierre_stick = sig->ruta_necesaria && !cerrar;
        }
    }
    cerrada = cerrar;

    calcular_vinculacion_bloqueos();
    return true;
}
