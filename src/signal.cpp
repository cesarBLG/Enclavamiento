#include "signal.h"
#include "ruta.h"
#include "items.h"
señal::señal(const std::string &id, const json &j) : id(id), lado(j["Lado"]), tipo(j["Tipo"]), pin(j.value("Pin", 0)), bloqueo_asociado(j.value("Bloqueo", "")), seccion(secciones[j["Sección"]]), seccion_prev(seccion->get_seccion_in(lado, pin).first)
{
    seccion->vincular_señal(this, lado, pin);
}
señal_impl::señal_impl(const std::string &id, const json &j) : señal(id, j), topic("signal/"+id_to_mqtt(id)+"/state"), topic_inicio("signal/"+id_to_mqtt(id)+"/inicio")
{
    for (auto &[est, asp] : j["AspectoCanton"].items()) {
        aspecto_maximo_ocupacion[json(est)] = asp;
    }
    for (auto &[asp1, asp2] : j["AspectoAnteriorSeñal"].items()) {
        aspectos_maximos_anterior_señal[json(asp1)] = asp2;
    }
    ruta_necesaria = j.value("RutaNecesaria", tipo != TipoSeñal::Intermedia && tipo != TipoSeñal::Avanzada);
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
    // Señal siguiente, a continuación del canton
    señal *sig_señal = nullptr;
    // Condiciones que provocan el cierre de señal
    bool cerrar = false;
    bool precaucion = false;
    // Condiciones que impiden abrir la señal, pero no la cierran si estaba abierta
    bool prohibir_abrir = false;
    // Requerir secciones asegurado por ruta (no necesario en trayecto)
    bool comprobar_ruta = ruta_activa != nullptr && !ruta_activa->get_secciones().empty() && ruta_activa->get_secciones().back().first != seccion_prev;
    // Nombre del bloqueo asociado
    std::string bloq_id = bloqueo_asociado;
    if (bloq_id == "" && ruta_activa != nullptr && ruta_activa->bloqueo_salida != "") {
        auto &señales = ruta_activa->get_señales();
        if (señales.empty() || señales.back().first == this || tipo == TipoSeñal::Salida) bloq_id = ruta_activa->bloqueo_salida;
    }
    // Comprobamos todos los CVs hasta la señal siguiente o fin de movimiento
    while (sec_act != nullptr && sig_señal == nullptr) {
        if (!comprobar_ruta && ruta_activa != nullptr && ruta_activa->tipo == TipoMovimiento::Maniobra) {
            break;
        }
        EstadoCanton sec_ocup = sec_act->get_ocupacion(sec_prv, dir);
        canton = std::max(sec_ocup, canton);
        if (sec_act->get_cv() != nullptr && sec_act->get_cv()->is_btv()) prohibir_abrir = true;
        Lado l = dir;
        seccion_via *next = sec_act->siguiente_seccion(sec_prv, dir, false);
        if (comprobar_ruta) {
            if (!sec_act->is_asegurada(ruta_activa) || sec_act->siguiente_seccion(sec_prv, l, true) != next)
                cerrar = true;
            if (ruta_activa->get_ocupacion_maxima_secciones().find(sec_act)->second < sec_ocup && (ruta_activa->tipo != TipoMovimiento::Maniobra || !ruta_activa->is_ocupada()))
                cerrar = true;
            if (sec_ocup == EstadoCanton::Ocupado && sec_act->get_cv()->ocupacion_intempestiva)
                cerrar = true;
            if (ruta_activa->get_secciones().back().first == sec_act) comprobar_ruta = false;
        }
        for (auto *pn : sec_act->pns) {
            if (!pn->is_protegido())
                precaucion = true;
        }
        if (sec_act->is_asegurada() && (ruta_activa == nullptr || !sec_act->is_asegurada(ruta_activa))) cerrar = true;
        if (bloq_id == "" && sec_act->bloqueo_asociado != "") bloq_id = sec_act->bloqueo_asociado;
        sec_prv = sec_act;
        sec_act = next;
        if (sec_act != nullptr) sig_señal = sec_act->señal_inicio(dir, pin);
        if (sec_act == seccion) break;
    }
    // La ruta asegurada se interrumpe antes de llegar al fin de movimiento o señal siguiente
    if (comprobar_ruta && sig_señal == nullptr) cerrar = true;
    // Condiciones que provocan el cierre de la señal en ruta de itinerario
    bool cerrar_itinerario = false;
    // Condiciones que impiden la apertura de señal en itinerario, pero no la cierran si estaba abierta
    bool prohibir_abrir_itinerario = false;
    if (bloq_id != "") {
        bloqueo_act = bloqueos[bloq_id]->get_estado();
        TipoMovimiento tipo_opp = bloqueo_act.ruta[opp_lado(dir)];
        // Cerrar señales intermedias y de salida si falla comunicación con colateral
        cerrar |= bloqueo_act.estado == EstadoBloqueo::SinDatos;
        // Cerrar señal avanzada si está establecido el itinerario o maniobra de salida
        cerrar |= tipo_opp != TipoMovimiento::Ninguno && tipo == TipoSeñal::Avanzada;
        // Cerrar señales intermedias y de salida si hay escape de material en sentido contrario
        cerrar |= bloqueo_act.escape[opp_lado(dir)];
        // Impedir maniobra de salida en caso de escape de material propio, salvo que la maniobra sea compatible con bloqueo receptor
        cerrar |= bloqueo_act.escape[lado] && ruta_activa != nullptr && (ruta_activa->maniobra_compatible <= CompatibilidadManiobra::IncompatibleBloqueo || ruta_activa->tipo != TipoMovimiento::Maniobra);
        // Cerrar maniobra de salida si no se pueden hacer maniobras simultáneas en ambas estaciones
        cerrar |= tipo_opp == TipoMovimiento::Maniobra && bloqueo_act.maniobra_compatible[opp_lado(dir)] == CompatibilidadManiobra::IncompatibleManiobra;
        // Cerrar señales intermedias y de salida si se establece el cierre de señales de bloqueo
        cerrar |= bloqueo_act.cierre_señales[dir];
        // Cerrar señales intermedias y de salida si no está establecido el bloqueo en ese sentido
        // Permitir apertura en desbloqueo de la señal de salida en estaciones cerradas
        // Las señales avanzadas abren según el aspecto de la señal de entrada
        // Las pantallas virtuales no abren sin bloqueo establecido, pero la condición de cierre se establece más adelante
        cerrar_itinerario |= bloqueo_act.estado != (dir == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar) && tipo != TipoSeñal::Avanzada && !señal_virtual && (tipo == TipoSeñal::Intermedia || ruta_necesaria || bloqueo_act.estado != EstadoBloqueo::Desbloqueo || tipo_opp == TipoMovimiento::Itinerario);
        // Cerrar el itinerario de salida si la estación colateral está realizando maniobras,
        // y no hay señales intermedias suficientes que puedan proteger la maniobra
        cerrar_itinerario |= tipo_opp == TipoMovimiento::Maniobra && bloqueo_act.maniobra_compatible[opp_lado(dir)] == CompatibilidadManiobra::IncompatibleItinerario;
        // No permitir la apertura en itinerario de la señal de salida con bloqueo prohibido o A/CTC denegada
        prohibir_abrir_itinerario |= tipo == TipoSeñal::Salida && (bloqueo_act.prohibido[dir] || bloqueo_act.actc[dir] == ACTC::Denegada);
    }
    // Cerrar señal con el cantón ocupado en sentido contrario
    cerrar_itinerario |= canton == EstadoCanton::Ocupado;
    cerrar |= sig_señal != nullptr && sig_señal->aspecto_maximo_anterior_señal == Aspecto::Parada;
    // Señal en parada si
    // - No se permite la apertura y no había abierto previamente
    // - Las condiciones no permiten mantener abierta la señal
    // - Se ha mandado el cierre de señal
    // - La señal es de inicio de ruta y la ruta no está asegurada o está en proceso de disolución
    if ((prohibir_abrir && prev_aspecto == Aspecto::Parada) || 
        cerrar || !clear_request || 
        (ruta_necesaria && (ruta_activa == nullptr || !ruta_activa->is_formada()))) {
        aspecto = Aspecto::Parada;
    } else if (ruta_activa != nullptr && ruta_activa->tipo == TipoMovimiento::Maniobra) {
        aspecto = Aspecto::RebaseAutorizado;
    } else if (ruta_activa != nullptr && ruta_activa->tipo == TipoMovimiento::Rebase) {
        aspecto = Aspecto::RebaseAutorizadoDestellos;
    // Señal en parada si no se cumplen las condiciones para apertura en itinerario
    } else if (cerrar_itinerario || (prohibir_abrir_itinerario && (prev_aspecto == Aspecto::Parada || !ruta_necesaria))) {
        aspecto = Aspecto::Parada;
    } else {
        // Permitir o no la apertura con cantón ocupado en el mismo sentido, o en prenormalización
        aspecto = aspecto_maximo_ocupacion[canton];
        if (aspecto > Aspecto::Precaucion && precaucion) aspecto = Aspecto::Precaucion;
        // Itinerarios ERTMS pueden abrir como máximo en parada selectiva
        if (ruta_activa != nullptr && ruta_activa->ertms && aspecto > Aspecto::ParadaSelectivaDestellos) aspecto = Aspecto::ParadaSelectivaDestellos;
        // Aspecto máximo permitido para cumplir las órdenes de la señal siguiente
        if (sig_señal != nullptr) aspecto = std::min(aspecto, sig_señal->aspecto_maximo_anterior_señal);
    }
    // Indicar a la señal anterior el aspecto máximo que puede mostrar
    auto it = aspectos_maximos_anterior_señal.upper_bound(aspecto);
    if (it == aspectos_maximos_anterior_señal.begin()) {
        // Sin restricciones para la señal anterior
        aspecto_maximo_anterior_señal = Aspecto::ViaLibre;
    } else {
        // Aspecto de la señal anterior restringido por el aspecto de esta señal
        aspecto_maximo_anterior_señal = (--it)->second;
    }
    if (frontera_salida != nullptr) {
        aspecto_maximo_anterior_señal = std::min(aspecto_maximo_anterior_señal, aspecto);
    }
    // En caso de pantallas cerradas, las señal anterior puede ordenar como máximo parada selectiva
    // Además, las pantallas virtuales propagan el aspecto máximo de apertura requerido por la siguiente señal luminosa
    if (señal_virtual && aspecto_maximo_anterior_señal > Aspecto::ParadaSelectiva) {
        if (aspecto == Aspecto::Parada)
            aspecto_maximo_anterior_señal = Aspecto::ParadaSelectiva;
        if (sig_señal != nullptr)
            aspecto_maximo_anterior_señal = std::min(aspecto_maximo_anterior_señal, sig_señal->aspecto_maximo_anterior_señal);
    }
    // Requerir pantallas virtuales en parada sin bloqueo establecido
    // Asignar el aspecto de parada después de calcular el aspecto de la señal anterior
    // Esto permite que la señal avanzada abra en función de la señal de entrada aunque
    // las pantallas estén cerradas por no haber bloqueo
    // Si la pantalla está cerrada por otro motivo, la avanzada mostrará parada selectiva
    if (señal_virtual && bloq_id != "" && bloqueo_act.estado != (dir == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar))
        aspecto = Aspecto::Parada;
}
void señal_impl::update()
{
    Aspecto prev_aspecto = aspecto;
    estado_inicio_ruta prev_estado_inicio = estado_inicio;

    determinar_aspecto();

    // Si la señal cierra en stick, es necesario volver a mandar la ruta para que vuelva a abrir
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

    if (aspecto != Aspecto::Parada) {
        rebasada = false;
        ultimo_paso_abierta = get_milliseconds();
    }
    estado_inicio = get_estado_inicio();

    send_state(aspecto != prev_aspecto, estado_inicio != prev_estado_inicio);
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
        if (ruta_activa != nullptr && ruta_activa->get_señal_inicio() == this && ruta_activa->dai(cmd == "DAB")) aceptado = true;
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
        case Aspecto::RebaseAutorizadoDestellos:
            r.SIG_IND = 5;
            break;
        case Aspecto::ParadaDiferida:
            r.SIG_IND = 13;
            break;
        case Aspecto::ParadaSelectiva:
            r.SIG_IND = 2;
            break;
        case Aspecto::ParadaSelectivaDestellos:
            r.SIG_IND = 3;
            break;
        case Aspecto::Precaucion:
            r.SIG_IND = 12;
            break;
        case Aspecto::AnuncioParada:
            r.SIG_IND = 8;
            break;
        case Aspecto::AnuncioPrecaucion:
            r.SIG_IND = 10;
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
        r.SIG_FAI = ruta_fai->get_estado_fai_remota();
    } else {
        r.SIG_FAI = 0;
    }
    r.SIG_GRP_ARS = 0;
    RemotaIMV i;
    if (ruta_activa != nullptr && ruta_activa->get_señal_inicio() == this) {
        i = ruta_activa->get_estado_remota_inicio();
    } else {
        i.IMV_DAT = 1;
        i.IMV_DIF_VAL = 0;
        i.IMV_EST = rebasada ? 7 : 0;
    }
    return {r, i};
}
estado_inicio_ruta señal_impl::get_estado_inicio()
{
    estado_inicio_ruta e;
    if (ruta_activa != nullptr && ruta_activa->get_señal_inicio() == this) {
        e = ruta_activa->get_estado_inicio();
    }
    e.rebasada = rebasada;
    e.bloqueo_señal = bloqueo_señal;
    e.sucesion_automatica = sucesion_automatica;
    e.me_pendiente = me_pendiente;
    if (ruta_fai != nullptr) {
        e.fai = ruta_fai->get_estado_fai();
    }
    return e;
}
void señal_impl::message_cv(const std::string &id, estado_cv ev)
{
    if (id != get_id_cv_inicio()) return;

    // Con el paso de la circulación se cierra la señal, salvo en maniobras
    if (ruta_activa != nullptr && ruta_activa->tipo != TipoMovimiento::Maniobra && !ruta_activa->is_sucesion_automatica() && ev.evento && ev.evento->ocupacion && ev.evento->lado == lado) {
        ruta_activa = nullptr;
    }

    paso_circulacion = false;
    // Detección de paso de tren por la señal
    if (ev.evento && ev.evento->ocupacion && ev.evento->lado == lado && (ev.evento->cv_colateral == "" || seccion_prev == nullptr || ev.evento->cv_colateral == seccion_prev->id_cv)) {
        // Si la señal estaba cerrada, es un rebase de señal
        if (aspecto == Aspecto::Parada) {
            if (ruta_necesaria && get_milliseconds() - ultimo_paso_abierta > 30000) {
                rebasada = true;
                log(this->id, "rebasada", LOG_WARNING);
            }
        // Si estaba abierta, es un paso normal de circulación
        } else {
            ultimo_paso_abierta = get_milliseconds();
            paso_circulacion = true;
        }
    }
}
const std::string &señal_impl::get_id_cv_inicio()
{
    auto *sec = seccion;
    auto *prv = seccion_prev;
    Lado dir = lado;
    while (sec != nullptr) {
        if (sec->get_cv() != nullptr) return sec->id_cv;
        auto next = sec->siguiente_seccion(prv, dir, false);
        prv = sec;
        sec = next;
    }
    return seccion->id_cv;
}
