#include "bloqueo.h"
#include "items.h"
bloqueo::bloqueo(const json &j) : estaciones(j["Estaciones"]), via(j.value("Vía", "")), id(estaciones[Lado::Impar]+"-"+estaciones[Lado::Par]+(via != "" ? "_" : "")+via), topic("bloqueo/"+id+"/state")
{
    ocupado = false;
    normalizado = true;
    for (auto lado : {Lado::Impar, Lado::Par}) {
        prohibido[lado] = false;
        escape[lado] = false;
        ruta[lado] = TipoMovimiento::Ninguno;
        maniobra_compatible[lado] = false;
        estado = EstadoBloqueo::Desbloqueo;
        actc[lado] = ACTC::NoNecesaria;
        cierre_señales[lado] = false;
    }
    tipo = j.value("Tipo", TipoBloqueo::BAU);
    if (tipo != TipoBloqueo::BAU && tipo != TipoBloqueo::BLAU) sentido_preferente = j["SentidoPreferente"];
    if (tipo == TipoBloqueo::BAD || tipo == TipoBloqueo::BLAD) estado = sentido_preferente == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar;
    for (auto &cv : j["CVs"]) {
        cvs.push_back(secciones[cv]);
    }
}
void bloqueo::message_cv(const std::string &id, estado_cv ecv)
{
    int index = -1;
    for (int i=0; i<cvs.size(); i++) {
        if (cvs[i]->id == id) {
            index = i;
            break;
        }
    }
    if (index < 0) return;
    EstadoCV est_cv = ecv.estado;
    EstadoCV prev_est = ecv.estado_previo;

    bool liberar = false;
    
    bool cvEntradaPar = index == cvs.size()-1;
    bool cvEntradaImpar = index == 0;

    if (est_cv == EstadoCV::Prenormalizado) normalizado = false;

    if (estado == EstadoBloqueo::BloqueoImpar && cvEntradaPar && est_cv <= EstadoCV::Prenormalizado && prev_est == EstadoCV::OcupadoImpar && (!ecv.evento || ecv.evento->lado == Lado::Par)) liberar = true;
    else if (estado == EstadoBloqueo::BloqueoPar && cvEntradaImpar && est_cv <= EstadoCV::Prenormalizado && prev_est == EstadoCV::OcupadoPar && (!ecv.evento || ecv.evento->lado == Lado::Impar)) liberar = true;
    
    if (ecv.evento) {
        bool ocupacion = ecv.evento->ocupacion;
        Lado lado = ecv.evento->lado;
        if (ocupacion && (ecv.estado_previo == EstadoCV::Libre || ecv.estado_previo == EstadoCV::Prenormalizado) && (ecv.estado != EstadoCV::Libre && ecv.estado != EstadoCV::Prenormalizado)) {
            bool esc=false;
            if (index == (lado == Lado::Impar ? 0 : cvs.size()-1)) {
                if (ruta[lado] != TipoMovimiento::Maniobra) {
                    esc = lado == Lado::Impar ? (estado != EstadoBloqueo::BloqueoImpar) : (estado != EstadoBloqueo::BloqueoPar);
                }
            }
            if (index == (lado == Lado::Impar ? 1 : cvs.size()-2)) {
                if (ruta[lado] == TipoMovimiento::Maniobra) {
                    esc = true;
                }
            }
            if (esc) {
                escape[lado] = true;
                log(this->id, lado == Lado::Impar ? "escape impar" : "escape par", LOG_WARNING);
            }
        }
    }

    bool ocupado = false;
    for (auto *sec : cvs) {
        if (sec->get_cv()->get_state() > EstadoCV::Prenormalizado) {
            ocupado = true;
            break;
        }
    }
    estado_cantones_inicio[Lado::Impar] = EstadoCanton::Libre;
    estado_cantones_inicio[Lado::Par] = EstadoCanton::Libre;
    for (int i=0; i<cvs.size() && (i == 0 || cvs[i]->señal_inicio(Lado::Impar, 0) == nullptr); i++) {
        estado_cantones_inicio[Lado::Impar] = std::max(estado_cantones_inicio[Lado::Impar], cvs[i]->get_cv()->get_ocupacion(Lado::Impar));
    }
    for (int i=cvs.size()-1; i>= 0 && (i == cvs.size()-1 || cvs[i]->señal_inicio(Lado::Par, 0) == nullptr); i--) {
        estado_cantones_inicio[Lado::Par] = std::max(estado_cantones_inicio[Lado::Par], cvs[i]->get_cv()->get_ocupacion(Lado::Par));
    }

    if (ocupado && !this->ocupado) log(this->id, "ocupado");
    this->ocupado = ocupado;

    if (liberar && tipo != TipoBloqueo::BAU && tipo != TipoBloqueo::BLAU && (estado == EstadoBloqueo::BloqueoImpar ? Lado::Impar : Lado::Par) == sentido_preferente) liberar = false;

    if (liberar) desbloquear(estado == EstadoBloqueo::BloqueoImpar ? Lado::Impar : Lado::Par, false);
    update();
}
