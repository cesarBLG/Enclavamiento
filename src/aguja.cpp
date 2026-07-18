#include "aguja.h"
#include "ruta.h"
#include "items.h"
aguja::aguja(const id_elemento &id, const json &j) : seccion_via(id, j, TipoSeccion::Aguja), lado(j["Lado"]), topic_mando("aguja/"+id_to_mqtt(id.id)+"/mando")
{
    if (j.contains("SecciónPunta")) siguientes_secciones[opp_lado(lado)] = std::vector<conexion>({j["SecciónPunta"].get<conexion>()});
    if (j.contains("SeccionesTalón")) siguientes_secciones[lado] = j["SeccionesTalón"];
    talonable = j.value("Talonable", true);
    if (j.contains("PosiciónMuelle")) talonable_muelle = j["PosiciónMuelle"] == 1 ? PosicionAguja::Invertida : PosicionAguja::Normal;
    update();
}
RespuestaMando aguja::mando(const std::string &cmd, int me)
{
    if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
    bool pend = me_pendiente;
    me_pendiente = false;
    if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
    if (cmd == "MAT") {
        if (talonable_muelle) {
            talonable_muelle = talonable_muelle == PosicionAguja::Invertida ? PosicionAguja::Normal : PosicionAguja::Invertida;
            update();
            dependencias[id.dependencia]->calcular_vinculacion_bloqueos();
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "ATN") {
        if (talonable_muelle != PosicionAguja::Normal) {
            talonable_muelle = PosicionAguja::Normal;
            update();
            dependencias[id.dependencia]->calcular_vinculacion_bloqueos();
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "ATI") {
        if (talonable_muelle != PosicionAguja::Invertida) {
            talonable_muelle = PosicionAguja::Invertida;
            update();
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "MA" || cmd == "AN" || cmd == "AI") {
        PosicionAguja pos;
        if (cmd == "AN" || (cmd == "MA" && !mandada && comprobacion && *comprobacion == PosicionAguja::Invertida) || (cmd == "MA" && mandada && mandada->first == PosicionAguja::Invertida))
            pos = PosicionAguja::Normal;
        else if (cmd == "AI" || (cmd == "MA" && !mandada && comprobacion && *comprobacion == PosicionAguja::Normal) || (cmd == "MA" && mandada && mandada->first == PosicionAguja::Normal))
            pos = PosicionAguja::Invertida;
        else
            return RespuestaMando::OrdenRechazada;
        if (mover(pos))
            return RespuestaMando::Aceptado;
    } else if (cmd == "BA" && !bloqueo) {
        bloqueo = true;
        log(id, "bloqueo aguja", LOG_DEBUG);
        return RespuestaMando::Aceptado;
    } else if (cmd == "ABA" && bloqueo) {
        if (me) {
            bloqueo = false;
            log(id, "desbloqueo aguja", LOG_DEBUG);
            remota_cambio_elemento("sec", id);
            return RespuestaMando::Aceptado;
        } else {
            me_pendiente = true;
            remota_cambio_elemento("sec", id);
            return RespuestaMando::MandoEspecialNecesario;
        }
    } else if (cmd == "BIA") {
        return seccion_via::mando("BIV", me);
    } else if (cmd == "DIA") {
        return seccion_via::mando("DIV", me);
    }
    return RespuestaMando::OrdenRechazada;
}
RemotaAG aguja::get_estado_remota()
{
    RemotaAG r;
    r.AG_DAT = 1;
    r.AG_ME = me_pendiente ? 1 : 0;
    r.AG_BIA = bloqueo_seccion ? 1 : 0;
    r.AG_OCUP_TIPO = cv_seccion != nullptr && cv_seccion->ocupacion_intempestiva ? 1 : 0;
    if (cv_seccion != nullptr && cv_seccion->get_state() > EstadoCV::Prenormalizado && (cv_seccion->ocupacion_intempestiva || ruta_asegurada)) r.AG_EST = 3;
    else if (ruta_asegurada && ruta_asegurada->ruta_asegurada->tipo == TipoMovimiento::Maniobra) r.AG_EST = 2;
    else if (ruta_asegurada && (ruta_asegurada->ruta_asegurada->tipo == TipoMovimiento::Itinerario || ruta_asegurada->ruta_asegurada->tipo == TipoMovimiento::Rebase)) r.AG_EST = 1;
    else if (cv_seccion != nullptr && cv_seccion->get_state() == EstadoCV::Prenormalizado) r.AG_EST = 3;
    else r.AG_EST = 0;
    if (!ruta_asegurada) r.AG_DIR = 0;
    else r.AG_DIR = ruta_asegurada->outs[lado] == 1 ? 2 : 1;
    r.AG_DES_N = 0;
    r.AG_DES_I = 0;
    if (comprobacion == PosicionAguja::Normal && (!mandada || mandada->first == comprobacion)) r.AG_COMP = 3;
    else if (comprobacion == PosicionAguja::Invertida && (!mandada || mandada->first == comprobacion)) r.AG_COMP = 4;
    else if (mandada && mandada->first == PosicionAguja::Normal) r.AG_COMP = 1;
    else if (mandada && mandada->first == PosicionAguja::Invertida) r.AG_COMP = 2;
    else r.AG_COMP = 0;
    r.AG_BA = bloqueo ? 1 : 0;
    if (!enclavada.empty()) r.AG_ENC = 1;
    else if (mandada && comprobacion != mandada->first && mandada->second != 0) r.AG_ENC = 2;
    else r.AG_ENC = 0;
    r.AG_GAL = 0;
    return r;
}
seccion_via *aguja::ruta_fija(seccion_via *prev, Lado &dir)
{
    if (!talonable_muelle && !bloqueo) {
        auto it = dependencias.find(id.dependencia);
        if (it != dependencias.end() && !it->second->cerrada) {
            bool enclavada_movimiento = false;
            // Ruta fija si está enclavada por movimiento especial (p. ej. autorizacion entrada/salida, servicio intermitente)
            for (auto &enc : enclavada) {
                if (!enc->es_ruta) enclavada_movimiento = true;
            }
            if (!enclavada_movimiento) return nullptr;
        }
    }
    int pin_fijo = -1;
    if (talonable_muelle) pin_fijo = talonable_muelle == PosicionAguja::Invertida ? 1 : 0;
    else if (mandada) pin_fijo = mandada->first == PosicionAguja::Invertida ? 1 : 0;
    int out = -1;
    if (dir == lado) {
        out = pin_fijo;
    } else {
        int in = get_in(prev, dir);
        if (in != pin_fijo) return nullptr;
        out = 0;
    }
    if (out < 0 || out >= siguientes_secciones[dir].size()) return nullptr;
    auto p = siguientes_secciones[dir][out];
    if (p.invertir_paridad) dir = opp_lado(dir);
    return secciones[p.id];
}
