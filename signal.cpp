#include "signal.h"
#include "ruta.h"
#include "items.h"
señal::señal(const std::string &id, const json &j) : id(id), lado(j["Lado"]), tipo(j["Tipo"]), pin(j.value("Pin", 0)), bloqueo_asociado(j.value("Bloqueo", "")), seccion(secciones[j["Sección"]]), seccion_prev(seccion->get_seccion_in(lado, pin).first)
{
    seccion->vincular_señal(this, lado, pin);
}
señal_impl::señal_impl(const std::string &id, const json &j) : señal(id, j), topic("signal/"+id_to_mqtt(id)+"/state")
{
    for (auto &[est, asp] : j["AspectoCanton"].items()) {
        aspecto_maximo_ocupacion[json(est)] = asp;
    }
    for (auto &[asp1, asp2] : j["AspectoAnteriorSeñal"].items()) {
        aspectos_maximos_anterior_señal[json(asp1)] = asp2;
    }
    ruta_necesaria = tipo != TipoSeñal::Intermedia && tipo != TipoSeñal::Avanzada;
    cierre_stick = ruta_necesaria;
    clear_request = !cierre_stick;
}
void señal_impl::determinar_aspecto()
{
    Aspecto prev_aspecto = aspecto;
    seccion_via* sec_act = seccion;
    seccion_via* sec_prv = seccion_prev;
    Lado dir = lado;
    EstadoCanton canton = EstadoCanton::Libre;
    señal *sig_señal = nullptr;
    bool btv = false;
    while (sec_act != nullptr && sig_señal == nullptr) {
        canton = std::max(sec_act->get_ocupacion(sec_prv, dir), canton);
        if (sec_act->get_cv()->is_btv()) btv = true;
        seccion_via *next = sec_act->siguiente_seccion(sec_prv, dir);
        sec_prv = sec_act;
        sec_act = next;
        if (sec_act != nullptr) sig_señal = sec_act->señal_inicio(dir, pin);
        if (sec_act == seccion) break;
        if (ruta_activa != nullptr && ruta_activa->get_ultima_seccion_señal() == sec_prv) break;
    }
    bool cerrar_itinerario = false;
    bool cerrar = false;
    bool prohibir_abrir = btv;
    bool prohibir_abrir_itinerario = false;
    if (bloqueo_asociado != "") {
        cerrar |= bloqueo_act.estado == EstadoBloqueo::SinDatos;
        cerrar |= bloqueo_act.ruta[opp_lado(dir)] != TipoMovimiento::Ninguno && tipo == TipoSeñal::Avanzada;
        cerrar |= bloqueo_act.escape[opp_lado(dir)];
        cerrar |= bloqueo_act.escape[lado] && ruta_activa != nullptr && (!ruta_activa->maniobra_compatible || ruta_activa->tipo != TipoMovimiento::Maniobra);
        cerrar_itinerario |= (bloqueo_act.estado != (dir == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar) && tipo != TipoSeñal::Avanzada);
        cerrar_itinerario |= bloqueo_act.cierre_señales[dir];
        prohibir_abrir_itinerario |= bloqueo_act.prohibido[dir] || bloqueo_act.actc[dir] == ACTC::Denegada;
    }
    cerrar |= canton == EstadoCanton::Ocupado;
    if ((prohibir_abrir && prev_aspecto == Aspecto::Parada) || 
        cerrar || !clear_request || 
        (ruta_necesaria && (ruta_activa == nullptr || ruta_activa->dai_activo() || !ruta_activa->is_formada()))) {
        aspecto = Aspecto::Parada;
    } else if (ruta_activa != nullptr && ruta_activa->tipo == TipoMovimiento::Maniobra) {
        aspecto = Aspecto::RebaseAutorizado;
    } else if (cerrar_itinerario || (prohibir_abrir_itinerario && prev_aspecto == Aspecto::Parada)) {
        aspecto = Aspecto::Parada;
    } else {
        aspecto = aspecto_maximo_ocupacion[canton];
        if (sig_señal != nullptr) aspecto = std::min(aspecto, sig_señal->aspecto_maximo_anterior_señal);
    }
}
void señal_impl::update()
{
    Aspecto prev_aspecto = aspecto;

    determinar_aspecto();

    auto it = aspectos_maximos_anterior_señal.upper_bound(aspecto);
    if (it == aspectos_maximos_anterior_señal.begin()) {
        aspecto_maximo_anterior_señal = Aspecto::ViaLibre;
    } else {
        aspecto_maximo_anterior_señal = (--it)->second;
    }

    if (cierre_stick && !sucesion_automatica) {
        if (aspecto == Aspecto::Parada) {
            if (cleared) {
                cleared = false;
                if (!paso_circulacion) clear_request = false;
            }
            if (ruta_activa == nullptr) clear_request = false;
        } else if (!cleared) {
            cleared = true;
        }
    }
    paso_circulacion = false;

    if (aspecto != Aspecto::Parada) rebasada = false;

    if (aspecto != prev_aspecto) send_state();
}
RespuestaMando señal_impl::mando(const std::string &cmd, int me)
{
    if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
    bool pend = me_pendiente;
    me_pendiente = false;
    if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
    if (cmd == "CS" || cmd == "CSEÑ") {
        if (clear_request) {
            log(id, "cierre señal", LOG_DEBUG);
            clear_request = false;
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "NPS" && !cierre_stick) {
        if (!clear_request) {
            log(id, "normalizar señal", LOG_DEBUG);
            clear_request = true;
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "BS") {
        if (!bloqueo_señal) {
            log(id, "bloqueo señal", LOG_DEBUG);
            bloqueo_señal = true;
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "ABS" || cmd == "DS") {
        if (bloqueo_señal) {
            if (me) {
                log(id, "anular bloqueo señal", LOG_DEBUG);
                bloqueo_señal = false;
                return RespuestaMando::Aceptado;
            } else {
                me_pendiente = true;
                return RespuestaMando::MandoEspecialNecesario;
            }
        } 
    } else if (cmd == "SA") {
        if (!sucesion_automatica) {
            log(id, "sucesión automática", LOG_DEBUG);
            sucesion_automatica = true;
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "ASA") {
        if (sucesion_automatica) {
            log(id, "anular sucesión automática", LOG_DEBUG);
            sucesion_automatica = false;
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "DAI" || cmd == "DAB") {
        bool aceptado = false;
        if (ruta_fai != nullptr && ruta_fai->cancelar_fai()) aceptado = true;
        if (ruta_activa != nullptr && ruta_activa->dai(cmd == "DAB")) aceptado = true;
        return aceptado ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
    } else if (cmd == "AFA") {
        if (ruta_fai != nullptr) {
            return ruta_fai->mando(ruta_fai->id_inicio, ruta_fai->id_destino, cmd);
        }
    }
    return RespuestaMando::OrdenRechazada;
}
std::pair<RemotaSIG, RemotaIMV> señal_impl::get_estado_remota()
{
    RemotaSIG r;
    r.SIG_DAT = 1;
    r.SIG_TIPO = tipo == TipoSeñal::Intermedia || tipo == TipoSeñal::Avanzada ? 1 : 2;
    r.SIG_EAR = 0;
    switch (aspecto) {
        default:
            r.SIG_IND = 0;
            break;
        case Aspecto::Parada:
            r.SIG_IND = 1;
            break;
        case Aspecto::RebaseAutorizado:
            r.SIG_IND = 4;
            break;
        case Aspecto::ParadaDiferida:
            r.SIG_IND = 13;
            break;
        case Aspecto::Precaucion:
            r.SIG_IND = 12;
            break;
        case Aspecto::ViaLibre:
            r.SIG_IND = 11;
            break;
    }
    r.SIG_FOCO_R = 1;
    r.SIG_FOCO_BL_C = 1;
    r.SIG_FOCO_BL_V = 0;
    r.SIG_FOCO_BL_H = 0;
    r.SIG_FOCO_AZ = 0;
    r.SIG_FOCO_AM = 1;
    r.SIG_FOCO_V = 1;
    r.SIG_ME = me_pendiente ? 1 : 0;
    r.SIG_B = bloqueo_señal ? 1 : 0;
    r.SIG_UIC = 0;
    r.SIG_SA = sucesion_automatica ? 1 : 0;
    if (ruta_fai != nullptr) {
        r.SIG_FAI = 1;
        r.SIG_FAI_IND = ruta_fai->get_estado_fai();
    } else {
        r.SIG_FAI = 0;
        r.SIG_FAI_IND = 0;
    }
    r.SIG_GRP_ARS = 0;
    RemotaIMV i;
    if (ruta_activa) {
        i = ruta_activa->get_estado_remota_inicio();
    } else {
        i.IMV_DAT = 1;
        i.IMV_DIF_VAL = 0;
        i.IMV_EST = rebasada ? 7 : 0;
    }
    return {r, i};
}
