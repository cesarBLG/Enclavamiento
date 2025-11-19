#include "bloqueo.h"
#include "items.h"
bloqueo::bloqueo(const std::string &estacion, const json &j) : estacion(estacion), estacion_colateral(j["Colateral"]), via(j.value("Vía", "")), lado(j["Lado"]), id(estacion+":"+estacion_colateral+via), topic("bloqueo/"+estacion+"/"+estacion_colateral+via+"/state"), topic_colateral("bloqueo/"+estacion_colateral+"/"+estacion+via+"/colateral"),
tipo(j.value("Tipo", TipoBloqueo::BAU)), bloqueo_emisor(lado == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar), bloqueo_receptor(lado == Lado::Par ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar)
{
    estado_objetivo = EstadoBloqueo::Desbloqueo;
    me_pendiente = false;
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

    bool cvEntrada = index == 0;
    
    bool cvEntradaPar = index == cvs.size()-1;
    bool cvEntradaImpar = index == 0;

    if (estado == bloqueo_receptor && index == 0 && est_cv <= EstadoCV::Prenormalizado
         && prev_est == (lado == Lado::Impar ? EstadoCV::OcupadoPar : EstadoCV::OcupadoImpar)
         && (!ecv.evento || ecv.evento->lado == lado))
            liberar = true;
    
    if (ecv.evento && ecv.evento->lado == lado) {
        bool ocupacion = ecv.evento->ocupacion;
        if (ocupacion && (ecv.estado_previo == EstadoCV::Libre || ecv.estado_previo == EstadoCV::Prenormalizado) && (ecv.estado != EstadoCV::Libre && ecv.estado != EstadoCV::Prenormalizado)) {
            bool esc=false;
            if (index == 0) {
                if (ruta != TipoMovimiento::Maniobra) {
                    esc = estado != bloqueo_emisor;
                }
            }
            if (index == 1) {
                if (ruta == TipoMovimiento::Maniobra) {
                    esc = true;
                }
            }
            if (esc) {
                escape = true;
                log(this->id, "escape emisor", LOG_WARNING);
            }
        }
    }

    for (auto *sec : cvs) {
        estado_cvs[sec->id] = sec->get_cv()->get_state();
    }
    calcular_ocupacion();
    /*estado_cantones_inicio[Lado::Impar] = EstadoCanton::Libre;
    estado_cantones_inicio[Lado::Par] = EstadoCanton::Libre;
    for (int i=0; i<cvs.size() && (i == 0 || cvs[i]->señal_inicio(Lado::Impar, 0) == nullptr); i++) {
        estado_cantones_inicio[Lado::Impar] = std::max(estado_cantones_inicio[Lado::Impar], cvs[i]->get_cv()->get_ocupacion(Lado::Impar));
    }
    for (int i=cvs.size()-1; i>= 0 && (i == cvs.size()-1 || cvs[i]->señal_inicio(Lado::Par, 0) == nullptr); i--) {
        estado_cantones_inicio[Lado::Par] = std::max(estado_cantones_inicio[Lado::Par], cvs[i]->get_cv()->get_ocupacion(Lado::Par));
    }*/

    if (liberar && tipo != TipoBloqueo::BAU && tipo != TipoBloqueo::BLAU && (estado == EstadoBloqueo::BloqueoImpar ? Lado::Impar : Lado::Par) == sentido_preferente) liberar = false;

    if (liberar && desbloqueo_permitido()) estado_objetivo = EstadoBloqueo::Desbloqueo;
    update();
}
