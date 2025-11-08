#include "ruta.h"
#include "items.h"

RespuestaMando destino_ruta::mando(const std::string &cmd, int me)
{
    if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
    bool pend = me_pendiente;
    me_pendiente = false;
    if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
    if (cmd == "BD" || cmd == "BDS" || cmd == "BDE") {
        if (!bloqueo_destino) {
            bloqueo_destino = true;
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "ABD" || cmd == "ABDS" || cmd == "ABDE") {
        if (bloqueo_destino) {
            if (me) {
                bloqueo_destino = false;
                return RespuestaMando::Aceptado;
            } else {
                me_pendiente = true;
                return RespuestaMando::MandoEspecialNecesario;
            }
        }
    } else if (cmd == "SA") {
        if (!sucesion_automatica) {
            sucesion_automatica = true;
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "ASA") {
        if (sucesion_automatica) {
            sucesion_automatica = false;
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "DEI" && ruta_activa != nullptr) {
        if (me) {
            return ruta_activa->dei() ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        } else {
            me_pendiente = true;
            return RespuestaMando::MandoEspecialNecesario;
        }
    }
    return RespuestaMando::OrdenRechazada;
}
RemotaFMV destino_ruta::get_estado_remota()
{
    if (ruta_activa != nullptr) return ruta_activa->get_estado_remota_fin();
    RemotaFMV r;
    r.FMV_DAT = 1;
    r.FMV_EST = 0;
    r.FMV_DIF_VAL = 0;
    if (tipo == TipoDestino::Colateral) r.FMV_DIF_ELEM = 2;
    else if (tipo == TipoDestino::Señal) r.FMV_DIF_ELEM = 0;
    else r.FMV_DIF_ELEM = 1;
    r.FMV_ME = me_pendiente ? 1 : 0;
    r.FMV_BD = bloqueo_destino ? 1 : 0;
    return r;
}
ruta::ruta(const std::string &estacion, const json &j) : estacion(estacion), tipo(j["Tipo"]), id_inicio(j["Inicio"]), id_destino(j["Destino"]), id((tipo == TipoMovimiento::Itinerario ? "I " : "M ")+estacion+" "+id_inicio+" "+id_destino), bloqueo_salida(j.value("Bloqueo", ""))
{
    std::string id_señal = estacion+":"+id_inicio;
    if (señales.find(id_señal) == señales.end()) {
        log(id, "señal de inicio inválida", LOG_ERROR);
        return;
    }
    señal_inicio = señales[id_señal];
    lado = señal_inicio->lado;
    if (secciones.empty()) lado_bloqueo = lado;
    else lado_bloqueo = secciones.back().second;

    std::string full_id_destino = estacion+":"+id_destino;
    if (destinos_ruta.find(full_id_destino) == destinos_ruta.end()) {
        log(id, "destino inválido", LOG_ERROR);
        return;
    }
    destino = destinos_ruta[full_id_destino];

    maniobra_compatible = j.value("Compatible", false);
    maniobra_simultanea = j.value("ManiobraSimultánea", true);
    if (j.contains("LímiteProximidad")) {
        ultimos_cvs_proximidad = j["LímiteProximidad"];
        construir_proximidad();
    }
    if (j.contains("SecciónFin")) {
        seccion_via *fin = ::secciones[j["SecciónFin"]];
        secciones.push_back({señal_inicio->seccion, lado});
    }
    if (j.contains("SecciónFinSeñal")) ultima_seccion_señal = ::secciones[j["SecciónFinSeñal"]];
    else if (tipo == TipoMovimiento::Maniobra && !secciones.empty()) ultima_seccion_señal = secciones.back().first;
    valid = true;
}
