#include "dependencia.h"
#include "items.h"
dependencia::dependencia(const std::string id, const json &j) : id(id), mando_actual({false, "PLO_"+id, std::nullopt, false})
{
    if (j.contains("ServicioIntermitente")) {
        auto &si = j["ServicioIntermitente"];
        cerrada = si.value("Activo", false);
        servicio_intermitente = config_servicio_intermitente();
        if (si.contains("FAI")) {
            servicio_intermitente->fais = si["FAI"];
        }
        if (si.contains("SeñalesAbiertas")) {
            servicio_intermitente->señales_abiertas = si["SeñalesAbiertas"];
        }
    }
}
void dependencia::calcular_vinculacion_bloqueos()
{
    // Para cada bloqueo en estación cerrada, indicar si es posible realizar cruces, 
    // o si las señales de la estación se deben comportar como un puesto de bloqueo, enlazando
    // los bloqueos de las estaciones colaterales
    for (auto &[id, bloq] : bloqueos) {
        if (cerrada) {
            std::string vinculo = "";
            bool señal_ruta = false;
            Lado dir = opp_lado(bloq->lado);
            seccion_via *prev = bloq->get_cvs()[0];
            seccion_via *sec = prev->siguiente_seccion(nullptr, dir, false);
            while (sec != nullptr) {
                if (sec->tipo == TipoSeccion::Aguja && !((aguja*)sec)->is_ruta_fija(prev, dir)) break;
                auto *sig = sec->señal_inicio(dir, prev);
                if (sig != nullptr && señal_impls.find(sig->id) != señal_impls.end() && señal_impls[sig->id]->ruta_necesaria)
                    señal_ruta = true;
                if (sec->bloqueo_asociado != "") {
                    for (auto &[id2, bloq2] : bloqueos) {
                        if (bloq2->get_cvs()[0] == sec) {
                            vinculo = id2;
                            break;
                        }
                    }
                    break;
                }
                auto *next = sec->siguiente_seccion(prev, dir, false);
                prev = sec;
                sec = next;
            }
            bloq->vincular(vinculo, !señal_ruta);
        } else {
            bloq->vincular("");
        }
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
                if (sec->tipo == TipoSeccion::Aguja && !((aguja*)sec)->is_ruta_fija(prev, dir)) break;
                auto *sig = sec->señal_inicio(dir, prev);
                if (sec->bloqueo_asociado != "") {
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
                auto *next = sec->siguiente_seccion(prev, dir, false);
                prev = sec;
                sec = next;
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
        size_t idx = ids.find_first_of(':');
        std::string dep = ids.substr(0, idx);
        std::string elem = ids.substr(idx+1);
        if (dep == id) {
            if (servicio_intermitente->señales_abiertas.find(elem) != servicio_intermitente->señales_abiertas.end()) {
                sig->cierre_stick = !cerrar;
                sig->ruta_necesaria = !cerrar;
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
    cerrada = cerrar;

    calcular_vinculacion_bloqueos();
    return true;
}
