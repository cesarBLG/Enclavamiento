#include "signal.h"
#include "ruta.h"
#include "items.h"
señal::señal(const std::string &id, const json &j) : id(id), lado(j["Lado"]), tipo(j["Tipo"]), pin(0), topic("signal/"+id+"/state"), bloqueo_asociado(j["Bloqueo"]), cierre_stick(j["CierreStick"])
{
    cv_señal = cvs[j["CV"]];
    for (auto &[est, asp] : j["AspectoCanton"].items()) {
        aspecto_maximo_ocupacion[json(est)] = asp;
    }
    for (auto &[asp1, asp2] : j["AspectoAnteriorSeñal"].items()) {
        aspectos_maximos_anterior_señal[json(asp1)] = asp2;
    }

    ruta_necesaria = j["RutaNecesaria"];
    clear_request = !cierre_stick;
}
void señal::update()
{
    Aspecto prev_aspecto = aspecto;

    cv* cv_act = cv_señal;
    cv* cv_prev = cv_act->get_cv_in(lado, pin).first;
    Lado dir = lado;
    EstadoCanton canton = EstadoCanton::Libre;
    señal *sig_señal = nullptr;
    while (cv_act != nullptr && sig_señal == nullptr) {
        canton = std::max(cv_act->get_ocupacion(dir), canton);
        cv_prev = cv_act;
        cv_act = cv_act->siguiente_cv(cv_act, dir);
        if (cv_act == cv_señal) break;
        if (cv_act != nullptr) sig_señal = cv_act->señal_inicio(cv_prev, dir);
    }
    bool cerrar_itinerario = bloqueo_asociado != "" && ((bloqueo_act.estado != (dir == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar) && tipo != TipoSeñal::Avanzada) || bloqueo_act.escape[opp_lado(dir)]);
    bool cerrar = bloqueo_asociado != "" && (bloqueo_act.estado == EstadoBloqueo::SinDatos || (bloqueo_act.ruta[opp_lado(dir)] != TipoMovimiento::Ninguno && tipo == TipoSeñal::Avanzada));
    bool forbid_open = bloqueo_asociado != "" && bloqueo_act.prohibido[dir];
    if ((forbid_open && prev_aspecto == Aspecto::Parada) || 
        cerrar || !clear_request || 
        (ruta_necesaria && (ruta_activa == nullptr || ruta_activa->dai_activo() || !ruta_activa->is_formada()))) {
        aspecto = Aspecto::Parada;
    } else if (ruta_activa != nullptr && ruta_activa->tipo == TipoMovimiento::Maniobra) {
        aspecto = Aspecto::RebaseAutorizado;
    } else if (cerrar_itinerario) {
        aspecto = Aspecto::Parada;
    } else {
        aspecto = aspecto_maximo_ocupacion[canton];
        if (sig_señal != nullptr) aspecto = std::min(aspecto, sig_señal->aspecto_maximo_anterior_señal);
    }

    auto it = aspectos_maximos_anterior_señal.upper_bound(aspecto);
    if (it == aspectos_maximos_anterior_señal.begin()) {
        aspecto_maximo_anterior_señal = Aspecto::ViaLibre;
    } else {
        aspecto_maximo_anterior_señal = (--it)->second;
    }

    if (cierre_stick) {
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

    if (aspecto != prev_aspecto) send_state();
}