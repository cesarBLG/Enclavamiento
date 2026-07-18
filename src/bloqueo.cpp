#include "bloqueo.h"
#include "items.h"
bloqueo::bloqueo(const std::string &estacion, const json &j) : estacion(estacion), estacion_colateral(j["Colateral"]), via(j.value("Vía", "")), lado(j["Lado"]), id(estacion,estacion_colateral+via), topic("bloqueo/"+estacion+"/"+estacion_colateral+via+"/state"), topic_colateral("bloqueo/"+estacion_colateral+"/"+estacion+via+"/colateral"),
tipo(j.value("Tipo", TipoBloqueo::BAU)), bloqueo_emisor(lado == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar), bloqueo_receptor(lado == Lado::Par ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar)
{
    me_pendiente = false;
    if (tipo != TipoBloqueo::BAU && tipo != TipoBloqueo::BLAU) sentido_preferente = j["SentidoPreferente"];
    if (tipo == TipoBloqueo::BAD || tipo == TipoBloqueo::BLAD) estado_inicial = sentido_preferente == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar;
    else estado_inicial = EstadoBloqueo::Desbloqueo;
    for (auto &cv : j["CVs"]) {
        cvs.push_back(secciones[id_elemento(cv)]);
    }
    if (j.contains("CVsEntrada")) {
        for (auto &cv : j["CVsEntrada"]) {
            cvs_entrada.push_back(::cvs[id_elemento(cv)]);
        }
    }
}
bool bloqueo::bloqueo_permitido(bool emisor)
{
    // Condiciones para establecer bloqueo en un sentido:
    // - Sin bloqueo establecido
    // - Trayecto libre de trenes
    // - Sin escape de material
    // - No está establecido el itinerario de salida en la colateral
    // - No está establecida una maniobra de salida en la colateral incompatible con el bloqueo
    // - No está prohibido el bloqueo
    // - Si no existe posibilidad de cruce en la colateral (estación cerrada sin agujas talonables),
    //   el bloqueo debe también poder establecerse de la colateral a la estación siguiente 
    if (estado == EstadoBloqueo::Desbloqueo && colateral.estado == EstadoBloqueo::Desbloqueo && !ocupado[emisor ? opp_lado(lado) : lado] && !escape && !colateral.escape) {
        if (emisor) {
            if (colateral.ruta == TipoMovimiento::Maniobra) {
                if (colateral.maniobra_compatible <= CompatibilidadManiobra::IncompatibleBloqueo)
                    return false;
                /*if (colateral.maniobra_compatible < CompatibilidadManiobra::Compatible && dependencias[estacion]->cerrada)
                    return false;*/
            }
            return !colateral.prohibido && colateral.ruta != TipoMovimiento::Itinerario;
        } else {
            if (bloqueo_vinculado != nullptr && bloqueo_vinculado->estado != bloqueo_vinculado->bloqueo_emisor) {
                if (bloqueo_vinculado->colateral.bloqueo_siguiente || propagacion_completa)
                    return false;
                else if (bloqueo_vinculado->estado != EstadoBloqueo::Desbloqueo)
                    return false;
            }
            if (ruta == TipoMovimiento::Maniobra) {
                if (maniobra_compatible <= CompatibilidadManiobra::IncompatibleBloqueo)
                    return false;
            }
            return !prohibido && ruta != TipoMovimiento::Itinerario;
        }
    }
    return false;
}
bool bloqueo::desbloqueo_permitido()
{
    // Condiciones para el desbloqueo de un sentido:
    // - No está establecido el itinerario de salida
    // - El trayecto está libre de trenes
    // - No está establecido el cierre de señales por la colateral
    // - Si no existe posibilidad de cruce en la estación emisora (estación cerrada sin agujas talonables),
    //   esta estación no puede ser receptora de otro bloqueo
    if (ruta == TipoMovimiento::Itinerario || colateral.ruta == TipoMovimiento::Itinerario || ocupado.par || ocupado.impar) return false;
    if (escape && !cvs_entrada.empty() && cvs_entrada[0]->get_state() > EstadoCV::Prenormalizado) return false;
    if (tipo == TipoBloqueo::BAD || tipo == TipoBloqueo::BLAD) return false;
    if (estado == bloqueo_emisor) {
        if (bloqueo_vinculado != nullptr && (colateral.bloqueo_siguiente || bloqueo_vinculado->propagacion_completa) && (bloqueo_vinculado->estado == bloqueo_vinculado->bloqueo_receptor || bloqueo_vinculado->colateral.estado_objetivo == bloqueo_vinculado->bloqueo_receptor || bloqueo_vinculado->estado == EstadoBloqueo::SinDatos)) {
            return false;
        }
        return !colateral.cierre_señales;
    } else if (estado == bloqueo_receptor) {
        return !cierre_señales;
    }
    return true;
}
void bloqueo::send_state()
{
    bloqueo_siguiente = bloqueo_vinculado != nullptr;
    cerrada = dependencias[estacion]->cerrada;
    estado_completo.estado = estado;
    estado_completo.ocupado = ocupado.impar || ocupado.par;
    for (int i=0; i<2; i++) {
        Lado l = i == 0 ? lado : opp_lado(lado);
        estado_bloqueo_lado &e = i == 0 ? *((estado_bloqueo_lado*)this) : colateral;
        estado_bloqueo_lado &o = i == 1 ? *((estado_bloqueo_lado*)this) : colateral;
        estado_completo.actc[l] = o.actc;
        estado_completo.cierre_señales[l] = o.cierre_señales;
        estado_completo.escape[l] = e.escape;
        estado_completo.mando_estacion[l] = e.mando_estacion;
        estado_completo.maniobra_compatible[l] = e.maniobra_compatible;
        estado_completo.prohibido[l] = o.prohibido;
        estado_completo.ruta[l] = e.ruta;
        estado_completo.prioridad_itinerario[l] = e.prioridad_itinerario;
        estado_completo.estacion_cerrada[l] = e.cerrada;
    }
    json j = *((estado_bloqueo_lado*)this);
    send_message(topic_colateral, j.dump());
    j = estado_completo;
    send_message(topic, j.dump());
    remota_cambio_elemento("blq", id);
}
void bloqueo::message_cv(const id_elemento &id, estado_cv ecv)
{
    int index = -1;
    for (int i=0; i<cvs.size(); i++) {
        if (cvs[i]->id == id) {
            index = i;
            break;
        }
    }
    
    // Detección de escape de material
    if (!escape && estado != bloqueo_emisor && tipo != TipoBloqueo::BAD && tipo != TipoBloqueo::BLAD) {
        bool esc=false;
        // Liberación circuito de agujas estando ocupado el de entrada
        if (cvs_entrada.size() > 1 && id == cvs_entrada[1]->id && ((ecv.evento->lado == lado && !ecv.evento->ocupacion) || (!ecv.evento && ecv.estado_previo > EstadoCV::Prenormalizado && ecv.estado <= EstadoCV::Prenormalizado)) && cvs_entrada[0]->get_state() > EstadoCV::Prenormalizado) {
            esc = true;
        }
        if ((ecv.evento && ecv.evento->lado == lado && ecv.evento->ocupacion) || (!ecv.evento && ecv.estado_previo <= EstadoCV::Prenormalizado && ecv.estado > EstadoCV::Prenormalizado && !ecv.averia)) {
            // Ocupación del circuito de entrada hacia el bloqueo
            if (cvs_entrada.size() == 1 && id == cvs_entrada[0]->id && ecv.evento && ruta == TipoMovimiento::Ninguno) {
                esc = true;
            }
            // Ocupación del primer circuito de trayecto
            if (index == 0 && (ecv.evento || (!cvs_entrada.empty() && cvs_entrada[0]->get_state() > EstadoCV::Prenormalizado))) {
                // No se produce escape si está establecida maniobra que incluye el CV de trayecto
                if (ruta != TipoMovimiento::Maniobra || !cvs[index]->is_asegurada()) {
                    esc = true;
                }
            }
            // Ocupación del siguiente circuito de trayecto por la maniobra
            if (index == 1 && (ecv.evento || estado_cvs[cvs[0]->id_cv.id] > EstadoCV::Prenormalizado)) {
                if (ruta == TipoMovimiento::Maniobra) {
                    esc = true;
                }
            }
        }
        if (esc && estado == EstadoBloqueo::Desbloqueo && dependencias[estacion]->cerrada && !colateral.prohibido && colateral.actc != ACTC::Denegada) esc = false;
        if (esc) {
            escape = true;
            sonerias[TipoSoneria::EscapeMaterial] = get_milliseconds();
            log(this->id, "escape emisor", LOG_WARNING);
            if (index < 0) update();
        }
    }

    if (index < 0)
        return;

    EstadoCV est_cv = ecv.estado;
    EstadoCV prev_est = ecv.estado_previo;

    bool liberar = false;

    // Desbloqueo si se produce liberación del último CV de trayecto
    if (estado == bloqueo_receptor && index == 0 && est_cv <= EstadoCV::Prenormalizado
         && prev_est == (lado == Lado::Impar ? EstadoCV::OcupadoPar : EstadoCV::OcupadoImpar)
         && (!ecv.evento || ecv.evento->lado == lado))
            liberar = true;

    for (auto *sec : cvs) {
        estado_cvs[sec->id.id] = sec->get_cv()->get_state();
    }
    calcular_ocupacion();

    // Gestión de bloqueo en estaciones cerradas
    if (dependencias[estacion]->cerrada) {
        // Si se ocupa el primer CV, establecer bloqueo si no lo estaba
        if (ecv.evento && ecv.evento->lado == lado && estado == EstadoBloqueo::Desbloqueo
            && ecv.estado_previo <= EstadoCV::Prenormalizado && ecv.estado > EstadoCV::Prenormalizado
            && index == 0 && bloqueo_permitido(true)) {

            estado_objetivo = bloqueo_emisor;
        }
        // Si el trayecto se libera, anular bloqueo emisor
        if (!ocupado.par && !ocupado.impar && estado == bloqueo_emisor) liberar = true;
    }

    /*estado_cantones_inicio[Lado::Impar] = EstadoCanton::Libre;
    estado_cantones_inicio[Lado::Par] = EstadoCanton::Libre;
    for (int i=0; i<cvs.size() && (i == 0 || cvs[i]->señal_inicio(Lado::Impar, 0) == nullptr); i++) {
        estado_cantones_inicio[Lado::Impar] = std::max(estado_cantones_inicio[Lado::Impar], cvs[i]->get_cv()->get_ocupacion(Lado::Impar));
    }
    for (int i=cvs.size()-1; i>= 0 && (i == cvs.size()-1 || cvs[i]->señal_inicio(Lado::Par, 0) == nullptr); i--) {
        estado_cantones_inicio[Lado::Par] = std::max(estado_cantones_inicio[Lado::Par], cvs[i]->get_cv()->get_ocupacion(Lado::Par));
    }*/

    // No liberar bloqueos con sentido preferente
    if (liberar && tipo != TipoBloqueo::BAU && tipo != TipoBloqueo::BLAU && (estado == EstadoBloqueo::BloqueoImpar ? Lado::Impar : Lado::Par) == sentido_preferente && !colateral.cerrada) liberar = false;

    // Gestionar desbloqueo
    if (liberar && desbloqueo_permitido()) estado_objetivo = EstadoBloqueo::Desbloqueo;

    // Soneria proximidad
    if (index == 0 && ecv.estado_previo <= EstadoCV::Prenormalizado && ecv.estado > EstadoCV::Prenormalizado && estado == bloqueo_receptor) {
        sonerias[TipoSoneria::OcupacionProximidad] = get_milliseconds();
    }

    update();
}
void bloqueo::desvincular()
{
    if (bloqueo_vinculado != nullptr) {
        auto *blq = bloqueo_vinculado;
        bloqueo_vinculado = nullptr;
        blq->desvincular();
        this->propagacion_completa = false;
        log(this->id, "desvinculado");
        if (estado == bloqueo_emisor && desbloqueo_permitido() && dependencias[estacion]->cerrada) estado_objetivo = EstadoBloqueo::Desbloqueo;
        update();
    }
}
void bloqueo::vincular(const id_elemento &id, bool propagacion_completa)
{
    this->propagacion_completa = propagacion_completa;
    if (bloqueo_vinculado && bloqueo_vinculado->id == id) return;
    bloqueo_vinculado = bloqueos[id];
    bloqueo_vinculado->bloqueo_vinculado = this;
    log(this->id, "vinculado a "+id.id);
    update();
    bloqueo_vinculado->update();
}
void bloqueo::update()
{
    // Bloqueo sin datos
    if (colateral.estado == EstadoBloqueo::SinDatos) {
        bool cambio = estado_objetivo != estado_inicial;
        if (cambio) estado_objetivo = estado_inicial;
        // Fallo de conexión con la colateral, mantener bloqueo sin datos
        if (colateral.estado_objetivo == colateral.estado && estado != EstadoBloqueo::SinDatos) {
            estado = EstadoBloqueo::SinDatos;
            send_state();
        // Conexión con colateral recuperada, restablecer bloqueo a situación inicial
        } else if (colateral.estado_objetivo == estado_inicial && estado != estado_inicial) {
            estado = estado_inicial;
            send_state();
        } else if (cambio) {
            send_state();
        }
        return;
    }
    if (estado == EstadoBloqueo::SinDatos) {
        // Conexión con colateral recuperada, restablecer bloqueo a situación inicial
        if (colateral.estado == estado_inicial) {
            estado = estado_inicial;
            send_state();
        } else {
            return;
        }
    }
    // Cambio de estado de bloqueo en la colateral, hacer coincidir el estado de ambas
    if (estado_objetivo != estado && colateral.estado == estado_objetivo) {
        estado = colateral.estado;
        if (estado == EstadoBloqueo::Desbloqueo) {
            log(id, "desbloqueo", LOG_DEBUG);
        } else if (estado == bloqueo_emisor) {
            log(id, "establecido emisor", LOG_DEBUG);
            if (bloqueo_vinculado != nullptr) bloqueo_vinculado->update();
        } else {
            log(id, "establecido receptor por discrepancia", LOG_DEBUG);
        }
    // Estación colateral reporta un estado erróneo
    } else if (colateral.estado != estado && colateral.estado_objetivo == colateral.estado) {
        estado = EstadoBloqueo::SinDatos;
        estado_objetivo = EstadoBloqueo::Desbloqueo;
        log(id, "discrepancia con colateral, estado inconsistente", LOG_DEBUG);
        send_state();
        return;
    }
    if (estado_objetivo != estado && estado_objetivo == EstadoBloqueo::Desbloqueo && !desbloqueo_permitido()) {
        estado_objetivo = estado;
        log(id, "anulación cancelada, desbloqueo no posible", LOG_DEBUG);
    }
    if (estado_objetivo != estado && estado_objetivo == bloqueo_emisor && !bloqueo_permitido(true)) {
        estado_objetivo = estado;
        log(id, "solicitud cancelada, bloqueo no posible", LOG_DEBUG);
    }
    if (colateral.escape && (bloqueo_vinculado == nullptr || !bloqueo_vinculado->escape) && dependencias[estacion]->cerrada) {
        normalizar_escape = true;
    }
    if (normalizar_escape && (!colateral.escape || !desbloqueo_permitido())) {
        normalizar_escape = false;
    }
    if (colateral.normalizar_escape && escape && desbloqueo_permitido()) {
        escape = false;
        log(id, "escape emisor normalizado", LOG_INFO);
        if (bloqueo_vinculado != nullptr && propagacion_completa) {
            bloqueo_vinculado->normalizar_escape = true;
            bloqueo_vinculado->update();
        }
    }
    // Gestionar petición de bloqueo por la estación colateral
    if (colateral.estado_objetivo != estado) {
        if (colateral.estado_objetivo == EstadoBloqueo::Desbloqueo) {
            // Conceder desbloqueo
            if (desbloqueo_permitido()) {
                auto prev_estado = estado;
                estado = estado_objetivo = EstadoBloqueo::Desbloqueo;
                if (prev_estado == bloqueo_receptor && bloqueo_vinculado != nullptr) {
                    if (propagacion_completa && bloqueo_vinculado->estado == bloqueo_vinculado->bloqueo_emisor && bloqueo_vinculado->desbloqueo_permitido())
                        bloqueo_vinculado->estado_objetivo = EstadoBloqueo::Desbloqueo;
                    bloqueo_vinculado->update();
                }
            }
        } else if (colateral.estado_objetivo == bloqueo_receptor) {
            // Conceder bloqueo a colateral
            if (bloqueo_permitido(false)) {
                estado = estado_objetivo = bloqueo_receptor;
                log(id, "bloqueo receptor", LOG_DEBUG);
                sonerias[TipoSoneria::EstablecimientoBloqueo] = get_milliseconds();
            } else if (bloqueo_vinculado != nullptr && bloqueo_vinculado->bloqueo_permitido(true)) {
                bloqueo_vinculado->update();
            }
        // Colateral solicita un estado inconsistente
        } else {
            estado = EstadoBloqueo::SinDatos;
            estado_objetivo = EstadoBloqueo::Desbloqueo;
            log(id, "discrepancia con colateral, solicita estado no permitido", LOG_DEBUG);
            send_state();
            return;
        }
    }
    // Solicitar bloqueo si:
    // - Está establecido un itinerario de salida
    // - Si el bloqueo vinculado está solicitado en sentido receptor, si hay propagación completa
    //   Si no hay propagación, permite hacer maniobras en la colateral deteniendo el tren en esta estación,
    //   aunque se impide el bloqueo receptor en esta estación
    if ((ruta == TipoMovimiento::Itinerario || (bloqueo_vinculado != nullptr && bloqueo_vinculado->colateral.estado_objetivo == bloqueo_vinculado->bloqueo_receptor && (colateral.bloqueo_siguiente || bloqueo_vinculado->propagacion_completa))) && bloqueo_permitido(true)) {
        estado_objetivo = bloqueo_emisor;
    }
    if (bloqueo_vinculado != nullptr) {
        if (dependencias[estacion]->cerrada) {
            if (estado == bloqueo_receptor && bloqueo_vinculado->estado == bloqueo_vinculado->bloqueo_emisor) cierre_señales = bloqueo_vinculado->colateral.cierre_señales;
            prohibido = bloqueo_vinculado->colateral.prohibido;
        }
    }
    send_state();
}
