#include "signal.h"
#include "ruta.h"
#include "items.h"
señal::señal(const std::string &id, const json &j) : id(id), lado(j["Lado"]), topic("signal/"+id+"/state"), bloqueo_asociado(j["Bloqueo"]), cierre_stick(j["CierreStick"])
{
    cv_señal = cvs[j["CV"]];
    for (auto &[est, asp] : j["AspectoCanton"].items()) {
        EstadoCanton ec;
        if (est == "Libre") ec = EstadoCanton::Libre;
        else if (est == "Prenormalizado") ec = EstadoCanton::Prenormalizado;
        else if (est == "OcupadoMismoSentido") ec = EstadoCanton::OcupadoMismoSentido;
        else ec = EstadoCanton::Ocupado;
        aspecto_maximo_ocupacion[ec] = asp;
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

    auto *cv_act = cv_señal;
    Lado act_lado = lado;
    EstadoCanton canton = EstadoCanton::Libre;
    while (cv_act != nullptr/* && (act_cv->señales[act_lado] == this || act_cv->señales[act_lado] == nullptr)*/) {
        canton = std::max(cv_act->get_ocupacion(act_lado), canton);

        cv_act = nullptr;
    }
    bool cerrar_itinerario = bloqueo_asociado != "" && (bloqueo_act.estado != (lado == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar) || bloqueo_act.escape[opp_lado(lado)]);
    bool cerrar = bloqueo_asociado != "" && bloqueo_act.estado == EstadoBloqueo::SinDatos;
    bool forbid_open = bloqueo_asociado != "" && bloqueo_act.prohibido[lado];
    if ((forbid_open && prev_aspecto == Aspecto::Parada) || 
        cerrar || !clear_request || 
        (ruta_necesaria && (ruta_activa == nullptr || ruta_activa->dai_activo()))) {
        aspecto = Aspecto::Parada;
    } else if (ruta_activa != nullptr && ruta_activa->tipo == TipoMovimiento::Maniobra) {
        aspecto = Aspecto::RebaseAutorizado;
    } else if (cerrar_itinerario) {
        aspecto = Aspecto::Parada;
    } else {
        aspecto = aspecto_maximo_ocupacion[canton];
        //if (act_cv != nullptr && act_cv.señales[act_lado] != nullptr) aspecto = std::min(aspecto, act_cv.señales[act_lado]->aspecto_maximo_anterior_señal);
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