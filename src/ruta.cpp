#include "ruta.h"
#include "items.h"
destino_ruta::destino_ruta(const std::string &id, const json &j) : id(id), tipo(j["Tipo"]), topic("destino/"+id_to_mqtt(id)+"/state")
{
    auto it = señal_impls.find(id);
    if (it != señal_impls.end()) señal_fin = it->second;
}
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
estado_fin_ruta destino_ruta::get_estado()
{
    if (ruta_activa != nullptr) return ruta_activa->get_estado_fin();
    estado_fin_ruta e;
    e.me_pendiente = me_pendiente;
    e.bloqueo_destino = bloqueo_destino;
    return e;
}
frontera *destino_ruta::get_frontera()
{
    if (tipo == TipoDestino::Señal) {
        return señales[id]->frontera_entrada;
    }
    return nullptr;
}
ruta::ruta(const std::string &estacion, const json &j) : estacion(estacion), tipo(j["Tipo"]), id_inicio(j["Inicio"]), id_destino(j["Destino"]), id((tipo == TipoMovimiento::Itinerario ? (ertms ? "ER " : "I ") : (tipo == TipoMovimiento::Rebase ? "R " : "M "))+estacion+" "+id_inicio+" "+id_destino), bloqueo_salida(j.value("Bloqueo", ""))
{
    std::string id_señal = estacion+":"+id_inicio;
    if (señal_impls.find(id_señal) == señal_impls.end()) {
        log(id, "señal de inicio inválida", LOG_ERROR);
        return;
    }

    tiempo_espera_fai = parametros.tiempo_espera_fai;
    temporizador_dai1 = parametros.diferimetro_dai1;
    temporizador_dai2 = parametros.diferimetro_dai2;
    temporizador_dei = parametros.diferimetro_dei;

    señal_inicio = señal_impls[id_señal];
    lado = señal_inicio->lado;
    if (secciones.empty()) lado_bloqueo = lado;
    else lado_bloqueo = secciones.back().dir;

    std::string full_id_destino = id_destino.find(':') != std::string::npos ? id_destino : estacion+":"+id_destino;
    if (destinos_ruta.find(full_id_destino) == destinos_ruta.end()) {
        log(id, "destino inválido", LOG_ERROR);
        return;
    }
    destino = destinos_ruta[full_id_destino];

    maniobra_compatible = j.value("Compatible", CompatibilidadManiobra::IncompatibleBloqueo);
    if (j.contains("LímiteProximidad")) {
        ultimos_cvs_proximidad = j["LímiteProximidad"];
    }
    if (j.contains("PosiciónAparatos")) {
        for (auto &[sec_id, jpos] : j["PosiciónAparatos"].items()) {
            posicion_aparatos[::secciones[sec_id]] = jpos;
        }
    }
    if (j.contains("SecciónFin")) {
        auto *prv = señal_inicio->seccion_prev;
        auto *sec = señal_inicio->seccion;
        Lado dir = lado;
        auto *fin = ::secciones[j["SecciónFin"]];
        do
        {
            ocupacion_maxima_secciones[sec] = EstadoCanton::Prenormalizado;

            if (sec != señal_inicio->seccion) {
                auto *sig = sec->señal_inicio(dir, prv);
                if (sig != nullptr && señal_impls.find(sig->id) != señal_impls.end()) señales.push_back({señal_impls[sig->id], dir});
            }

            std::pair<int,int> pins;
            seccion_via *next;
            if (posicion_aparatos.find(sec) != posicion_aparatos.end()) {
                auto p = sec->get_seccion_in(opp_lado(dir), posicion_aparatos[sec].second);
                next = p.first;
                dir = opp_lado(p.second);
                pins = posicion_aparatos[sec];
            } else {
                next = sec->siguiente_seccion(prv, dir, false);
                pins = {sec->get_in(prv, dir), sec->get_out(next, dir)};
            }
            secciones.push_back({sec, dir, pins.first, pins.second});
            prv = sec;
            sec = next;
        }
        while (sec != nullptr && prv != fin);
    }
    if (j.contains("Deslizamiento")) {
        if (j.contains("InicioTemporizador")) seccion_inicio_temporizador_deslizamiento = ::secciones[j["InicioTemporizador"]];
        else seccion_inicio_temporizador_deslizamiento = secciones.back().seccion;
        temporizador_deslizamiento = j.value("Diferímetro", 15000);
    }
    if (j.contains("SeñalLiberación")) {
        señales.push_back({señal_impls[j["SeñalLiberación"]], secciones.back().dir});
    }
    valid = true;
}
bool ruta::establecer()
{
    if (destino->bloqueo_destino || señal_inicio->bloqueo_señal) return false;
    clear_timer(diferimetro_dai);
    diferimetro_dai = nullptr;
    if (mandada && !ocupada) {
        señal_inicio->clear_request = true;
        for (auto &[sig, dir] : señales) {
            sig->clear_request = true;
        }
        log(id, "re-mandada", LOG_DEBUG);
        return true;
    }
    // Existe otro itinerario con misma señal de inicio
    if (señal_inicio->ruta_activa != nullptr && señal_inicio->ruta_activa != this) {
        return false;
    }
    // Alguna de las señales de la ruta está mandada por otra ruta
    for (auto &[sig, dir] : señales) {
        if (sig->ruta_activa != nullptr && sig->ruta_activa != this) return false;
    }
    // Existe otro itinerario con el mismo destino
    if (destino->ruta_activa != nullptr && destino->ruta_activa != this) {
        return false;
    }
    if (señal_inicio->frontera_salida != nullptr && !señal_inicio->frontera_salida->salida_permitida(this)) {
        return false;
    }
    if (destino->get_frontera() != nullptr && !destino->get_frontera()->entrada_permitida(this)) {
        return false;
    }
    if (bloqueo_salida != "") {
        // Existe una maniobra establecida en la colateral y no se permiten maniobras simultáneas
        if (bloqueo_act.ruta[opp_lado(lado)] == TipoMovimiento::Maniobra) {
            CompatibilidadManiobra compat = bloqueo_act.maniobra_compatible[opp_lado(lado)];
            if (compat == CompatibilidadManiobra::IncompatibleManiobra) return false;
            //else if (compat == CompatibilidadManiobra::IncompatibleItinerario && tipo == TipoMovimiento::Itinerario) return false;
        }
        bool bloqueo_emisor = bloqueo_act.estado == (lado_bloqueo == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar);
        bool bloqueo_receptor = bloqueo_act.estado == (lado_bloqueo == Lado::Par ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar);
        if (tipo == TipoMovimiento::Maniobra) {
            TipoMovimiento colat = bloqueo_act.ruta[opp_lado(lado)];
            // Maniobras simultáneas compatibles
            if (maniobra_compatible == CompatibilidadManiobra::IncompatibleManiobra && colat != TipoMovimiento::Ninguno) return false;
            // Maniobra incompatible con itinerario de salida de la colateral
            if (maniobra_compatible == CompatibilidadManiobra::IncompatibleItinerario && colat == TipoMovimiento::Itinerario) return false;

            // Maniobra incompatible con bloqueo receptor
            if (bloqueo_receptor && maniobra_compatible <= CompatibilidadManiobra::IncompatibleBloqueo) {
                return false;
            }
            
            if (!bloqueo_emisor) {
                // Si no está establecido el bloqueo emisor, requerir libre la proximidad de la avanzada
                // TODO: considerar proximidad completa
                seccion_via *sec;
                seccion_via *sig;
                Lado l;
                if (secciones.empty()) {
                    sec = señal_inicio->seccion;
                    sig = señal_inicio->seccion->siguiente_seccion(señal_inicio->seccion_prev, lado, false);
                } else {
                    sec = secciones.back().seccion;
                    auto p = secciones.back().seccion->get_seccion_in(opp_lado(secciones.back().dir), secciones.back().out);
                    sig = p.first;
                    l = opp_lado(p.second);
                }
                if (parametros.deslizamiento_bloqueo || sec->is_trayecto()) {
                    if (sig != nullptr && sig->is_trayecto() && sig->get_ocupacion(sec, l) == EstadoCanton::Ocupado) return false;
                }
            }
        // No permitir rutas de salida con bloqueo receptor
        } else if (bloqueo_receptor) {
            return false;
        }
        // No permitir varias rutas hacia el mismo bloqueo
        if (bloqueo_act.ruta[lado_bloqueo] != tipo && bloqueo_act.ruta[lado_bloqueo] != TipoMovimiento::Ninguno)
            return false;
    }
    deslizamientos_afectados.clear();
    for (int i=0; i<secciones.size(); i++) {
        auto *sec = secciones[i].seccion;
        // La ruta requiere secciones ya aseguradas por otra ruta
        if (!sec->asegurar_posible(this, secciones[i].in, secciones[i].out, secciones[i].dir)) return false;
        // Bloqueo de vía establecido
        if (sec->is_bloqueo_seccion()) return false;
        for (auto &[r,d] : sec->get_deslizamiento()) {
            deslizamientos_afectados[r] = -1;
        }
    }
    // TODO: comprobar agujas enclavadas o bloqueadas
    if (deslizamiento) deslizamientos_afectados[this] = -1;
    for (auto &[r, id] : deslizamientos_afectados) {
        int compat = r->deslizamiento->compatible(this);
        if (compat < 0) return false;
        id = compat;
    }
    log(id, "mandada", LOG_DEBUG);
    clear_timer(diferimetro_dei);
    diferimetro_dei = nullptr;
    mandada = formada = true;
    señal_inicio->clear_request = true;
    señal_inicio->ruta_activa = this;
    for (auto &[sig, dir] : señales) {
        sig->ruta_activa = this;
        sig->clear_request = true;
    }
    destino->ruta_activa = this;
    // Asegurar todas las secciones en el sentido de la ruta
    for (int i=0; i<secciones.size(); i++) {
        auto *sec = secciones[i].seccion;
        sec->asegurar(this, secciones[i].in, secciones[i].out, secciones[i].dir);
        secciones_aseguradas.insert(sec);
    }
    for (auto &[r, id] : deslizamientos_afectados) {
        r->deslizamiento->activar(id);
    }
    return true;
}

void ruta::update()
{
    construir_proximidad();
    bool activar_fai = false;
    bool proximidad_ocupada = false;
    bool proximidad_ocupada_fai = false;
    for (auto &[sec, dir] : proximidad) {
        auto e = sec->get_cv()->get_state();
        if (e > EstadoCV::Prenormalizado && (e != (dir == Lado::Impar ? EstadoCV::OcupadoPar : EstadoCV::OcupadoImpar))) {
            proximidad_ocupada = true;
            if (!sec->get_cv()->is_averia()) proximidad_ocupada_fai = true;
            break;
        }
    }
    // Con FAI activo, el itinerario se establece si la proximidad está ocupada y se permite la salida al bloqueo (en señales de salida)
    if (fai) {
        bool aut_salida = true;
        int prioridad = 0;
        if (bloqueo_salida != "") {
            aut_salida &= !bloqueo_act.cierre_señales[lado] && !bloqueo_act.prohibido[lado] && bloqueo_act.actc[lado] != ACTC::Denegada;
            prioridad = bloqueo_act.prioridad_itinerario[lado]-bloqueo_act.prioridad_itinerario[opp_lado(lado)];
        }
        bool cv_anterior_ocupado = false;
        for (auto &[sec, dir] : proximidad0) {
            if (sec->get_cv()->get_state() > EstadoCV::Prenormalizado) {
                cv_anterior_ocupado = true;
                break;
            }
        }
        if (estado_fai == EstadoFAI::EnEspera) {
            // Retrasar itinerario respecto al último lanzamiento para evitar que el mismo tren active la ruta dos veces
            // No retrasar si se libera la proximidad (son realmente dos trenes distintos)
            if (!cv_anterior_ocupado) inicio_temporizacion_fai = 0;
            if (aut_salida && proximidad_ocupada_fai && prioridad >= 0 && get_milliseconds() - inicio_temporizacion_fai > tiempo_espera_fai) {
                estado_fai = EstadoFAI::Solicitud;
                inicio_temporizacion_fai = get_milliseconds();
            }
        }
        // Si se ha cancelado manualmente el FAI, se resetea al liberarse la proximidad
        if (estado_fai == EstadoFAI::Cancelado && !proximidad_ocupada) estado_fai = EstadoFAI::EnEspera;
        // Si no se cumplen las condiciones, disolvemos la ruta automáticamente
        if ((!aut_salida || !proximidad_ocupada || prioridad < 0) && !fai_disparo_unico && estado_fai != EstadoFAI::EnEspera && estado_fai != EstadoFAI::Cancelado) {
            dai(true);
            estado_fai = EstadoFAI::EnEspera;
            inicio_temporizacion_fai = 0;
        }
    }
    if (!fai && !fai_disparo_unico) {
        // Desactivación FAI
        if (señal_inicio->ruta_fai == this) señal_inicio->ruta_fai = nullptr;
        if (estado_fai != EstadoFAI::EnEspera) {
            estado_fai = EstadoFAI::EnEspera;
            inicio_temporizacion_fai = 0;
        }
    } else if (estado_fai == EstadoFAI::Solicitud || estado_fai == EstadoFAI::AperturaNoPosible || estado_fai == EstadoFAI::AperturaNoPosibleReconocida) {
        if (señal_inicio->aspecto != Aspecto::Parada && señal_inicio->ruta_activa == this) {
            // Itinerario establecido y señal abierta
            estado_fai = EstadoFAI::Activo;
            fai_disparo_unico = false;
        } else if (estado_fai == EstadoFAI::Solicitud) {
            // FAI mandando ruta
            if (señal_inicio->ruta_activa != this || diferimetro_dai != nullptr) establecer();
            // Señal no abre tras temporizador, dejar de re-mandar la ruta
            if (estado_fai == EstadoFAI::Solicitud && get_milliseconds() - inicio_temporizacion_fai > 3*60*1000) {
                estado_fai = EstadoFAI::AperturaNoPosible;
            }
        }
    } else if (estado_fai == EstadoFAI::Activo) {
        // Si la señal se cierra por cualquier causa, es necesario que se vuelva a ocupar la proximidad para volver a abrir
        if (señal_inicio->aspecto == Aspecto::Parada) {
            estado_fai = EstadoFAI::Cancelado;
        }
    }
    if (!mandada) return;

    // Mandar cierre de PN si la proximidad está ocupada
    if ((proximidad_ocupada || proximidad.empty()) && señal_inicio->ruta_activa != nullptr) {
        activar_pns();
    }

    // Desenclavar ruta al paso de la circulación
    if (ocupada && !sucesion_automatica) {
        // En maniobra, cerrar señal cuando se libera el CV anterior o el de señal
        if (tipo == TipoMovimiento::Maniobra && señal_inicio->ruta_activa == this) {
            bool proximidad_liberada = false;
            if (proximidad0.size() > 0) {
                proximidad_liberada = true;
                for (auto &[sec,dir] : proximidad0) {
                    if (sec->get_cv()->get_state() > EstadoCV::Prenormalizado && sec->get_cv()->get_state() != (dir == Lado::Impar ? EstadoCV::OcupadoPar : EstadoCV::OcupadoImpar)) {
                        proximidad_liberada = false;
                        break;
                    }
                }
            }
            if (proximidad_liberada) señal_inicio->ruta_activa = nullptr;
            for (int i=0; i<secciones.size(); i++) {
                if (secciones[i].seccion->get_cv() == nullptr) continue;
                if (secciones[i].seccion->get_cv()->get_state() <= EstadoCV::Prenormalizado) {
                    señal_inicio->ruta_activa = nullptr;
                }
            }
        }
        // Desenclavar secciones conforme se liberan
        for (int i=0; i<secciones.size(); i++) {
            bool anterior_libre = false;
            // Primer CV se desenclava cuando se libera con la señal cerrada
            if (i == 0) {
                if (señal_inicio->ruta_activa == nullptr) anterior_libre = true;
            // Resto de CVs se desenclavan cuando se liberan con la anterior sección desenclavada
            } else if (!secciones[i-1].seccion->is_asegurada(this)) {
                anterior_libre = true;
            }
            if (!anterior_libre) break;
            for (auto &[sig, dir] : señales) {
                if (sig->seccion == secciones[i].seccion && sig->ruta_activa == this) sig->ruta_activa = nullptr;
            }
            if (secciones[i].seccion->get_cv() == nullptr || secciones[i].seccion->get_cv()->get_state() <= EstadoCV::Prenormalizado) {
                if (secciones[i].seccion->is_asegurada(this)) {
                    secciones[i].seccion->liberar(this);
                    secciones_aseguradas.erase(secciones[i].seccion);
                    // Si todas las secciones están libres, se produce disolución normal de la ruta
                    if (i == secciones.size() - 1) {
                        disolver();
                    } else if (secciones[i+1].seccion == seccion_inicio_temporizador_deslizamiento) {
                        if (temporizador_deslizamiento <= 0) {
                            disolver();
                        } else {
                            log(id, "diferimetro deslizamiento", LOG_INFO);
                            diferimetro_deslizamiento = set_timer([this]() {
                                disolver();
                            }, temporizador_deslizamiento);
                        }
                    }
                }
            }
        }
        // Si la ruta no protege ninguna sección (e.g. salida directamente al bloqueo), se disuelve con el cierre de señal
        if (señal_inicio->ruta_activa != this && secciones.empty()) disolver();
    }
    if (ocupada && señal_inicio->ruta_activa == this) {
        bool libre = true;
        for (int i=0; i<secciones.size(); i++) {
            if (secciones[i].seccion->get_cv() != nullptr && secciones[i].seccion->get_cv()->get_state() > EstadoCV::Prenormalizado) {
                libre = false;
                break;
            }
        }
        // Si la ruta sigue enclavada para otro tren aún estando ocupada,
        // pasar a estado libre cuando todas las secciones están libres
        if (libre) {
            ocupada = false;
            supervisada = señal_inicio->get_state() != Aspecto::Parada;
        }
    }
    // La ruta pasa a estar supervisada con la apertura de la señal
    if (formada && !supervisada && señal_inicio->get_state() != Aspecto::Parada) {
        supervisada = true;
        log(id, "supervisada");
    }
}
void ruta::message_cv(const std::string &id, estado_cv ecv)
{
    /*if (estado_fai == EstadoFAI::EnEspera || estado_fai == EstadoFAI::Cancelado || estado_fai == EstadoFAI::AperturaNoPosible || estado_fai == EstadoFAI::AperturaNoPosibleReconocida) {
        for (auto &[s, l] : proximidad) {
            if (id == s->id_cv && ecv.evento && ecv.evento->ocupacion && ecv.evento->lado == l) {
                inicio_temporizacion_fai = 0;
                estado_fai = EstadoFAI::EnEspera;
            }
        }
    }*/
    if (!mandada) return;
    // Ocupación del primer CV de la ruta
    if (id == señal_inicio->get_id_cv_inicio() && ecv.evento && ecv.evento->ocupacion && ecv.evento->lado == lado) {
        if (!supervisada) {
            // La ruta pasa a estar supervisada aunque la señal no haya llegado a abrir
            supervisada = true;
            log(this->id, "supervisada");
            activar_pns();
        }
        if (!ocupada) {
            // Comprobación de si la ruta debe mantenerse enclavada por sucesión automática
            sucesion_automatica = tipo == TipoMovimiento::Itinerario && señal_inicio->sucesion_automatica;
            ocupada = true;
            // Si la ruta se ocupa con diferímetro de disolución, se cancela la disolución
            if (diferimetro_dai != nullptr) {
                clear_timer(diferimetro_dai);
                diferimetro_dai = nullptr;
                diferimetro_cancelado = true;
                log(this->id, "ocupada con dai activo", LOG_WARNING);
            } else {
                log(this->id, "ocupada");
            }
        }
        // Rearme de FAI
        if (estado_fai == EstadoFAI::Activo) estado_fai = EstadoFAI::EnEspera;
        // Impedir nueva activación de FAI hasta que transcurra cierto tiempo
        if (estado_fai == EstadoFAI::EnEspera) inicio_temporizacion_fai = get_milliseconds();
    }
    }
bool ruta::dai(bool anular_bloqueo)
{
    if (!mandada) return false;
    if (señal_inicio->frontera_salida != nullptr && !señal_inicio->frontera_salida->dai_salida_permitido()) return false;
    log(id, "dai", LOG_DEBUG);
    // Si la señal no llegó a abrir, la disolución es inmediata
    if (!supervisada) {
        disolucion_parcial(anular_bloqueo);
        return true;
    }
    bool proximidad_libre = true;
    for (auto [sec, dir] : proximidad) {
        auto e = sec->get_cv()->get_state();
        if (e != EstadoCV::Libre) {
            proximidad_libre = false;
            break;
        }
    }
    // Si la proximidad está libre, la disolución es inmediata
    // Si no hay secciones aseguradas por la ruta, ni un bloqueo tomado, no es necesario temporizar
    // Señales de salida de estaciones abiertas no necesitan temporizador por necesitar señal de marche el tren
    if ((proximidad_libre && proximidad.size() > 0) || (secciones.size() == 0 && bloqueo_salida == "") || (señal_inicio->tipo == TipoSeñal::Salida && !dependencias[estacion]->cerrada)) {
        disolucion_parcial(anular_bloqueo);
    } else if (diferimetro_dai == nullptr) {
        int64_t temporizador = proximidad.size() > proximidad0.size() ? temporizador_dai2 : temporizador_dai1;
        diferimetro_dai = set_timer([this, anular_bloqueo]() {
            diferimetro_dai = nullptr;
            disolucion_parcial(anular_bloqueo);
        }, temporizador);
        if (señal_inicio->ruta_activa == this && señal_inicio->ruta_necesaria) señal_inicio->clear_request = false;
        for (int i=0; i<secciones.size(); i++) {
            auto *sec = secciones[i].seccion;
            auto *prev = i>0 ? secciones[i-1].seccion : señal_inicio->seccion_prev;
            for (auto &[sig, dir] : señales) {
                if (sig->seccion == sec && sig->ruta_activa == this && sig->ruta_necesaria) sig->clear_request = false;
            }
            if (!sec->is_asegurada(this)) break;
            if (sec->get_ocupacion(prev, secciones[i].dir) > EstadoCanton::Prenormalizado) break;
            if (i + 1 == secciones.size() && !señales.empty()) {
                auto *sig = señales.back().first;
                if (sig->seccion_prev == sec && sig->ruta_activa == this && sig->ruta_necesaria) sig->clear_request = false;
            }
        }
    }
    return true;
}
// Disolución de emergencia
bool ruta::dei()
{
    frontera *f = destino->get_frontera();
    if (f != nullptr) {
        if (!f->dei_entrada_permitido()) return false;
    }
    if (!ocupada || !supervisada) {
        if (señal_inicio != nullptr && señal_inicio->frontera_salida != nullptr) {
            if (!señal_inicio->frontera_salida->dei_salida_permitido()) return false;
        } else {
            return false;
        }
    }
    if (señal_inicio->ruta_activa == this) señal_inicio->clear_request = false;
    for (auto &[sig, dir] : señales) {
        if (sig->ruta_activa == this) sig->clear_request = false;
    }
    log(id, "dei", LOG_DEBUG);
    if (diferimetro_dei == nullptr) {
        diferimetro_dei = set_timer([this]() {
            diferimetro_dei = nullptr;
            disolver();
            if (señal_inicio != nullptr && señal_inicio->frontera_salida) {
                señal_inicio->frontera_salida->disolver_entrada();
            }
            desactivar_pns();
        }, temporizador_dei);
    }
    return true;
} 
// Disolución completa de la ruta
void ruta::disolver()
{
    if (señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
    for (auto &[sig, dir] : señales) {
        if (sig->ruta_activa == this) sig->ruta_activa = nullptr;
    }
    if (destino->ruta_activa == this) destino->ruta_activa = nullptr;
    mandada = false;
    formada = false;
    supervisada = false;
    ocupada = false;
    clear_timer(diferimetro_dai);
    clear_timer(diferimetro_dei);
    clear_timer(diferimetro_deslizamiento);
    diferimetro_dai = nullptr;
    diferimetro_dei = nullptr;
    diferimetro_deslizamiento = nullptr;
    diferimetro_cancelado = false;
    for (auto *sec : secciones_aseguradas) {
        sec->liberar(this);
    }
    secciones_aseguradas.clear();
    if (deslizamiento) {
        deslizamiento->liberar();
        deslizamientos_afectados.erase(this);
    }
    for (auto &[r,id] : deslizamientos_afectados) {
        id = r->deslizamiento->compatible(nullptr);
        r->deslizamiento->activar(id);
    }
    log(id, "disuelta");
}
// Disolución parcial de la ruta hasta el primer CV ocupado
void ruta::disolucion_parcial(bool anular_bloqueo)
{
    if (!ocupada) {
        anulacion_bloqueo_pendiente = anular_bloqueo;
        frontera *f = destino->get_frontera();
        disolver();
        if (f != nullptr) f->disolver_salida();
        desactivar_pns();
        return;
    }
    if (señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
    if (estado_fai == EstadoFAI::Activo) {
        estado_fai = EstadoFAI::EnEspera;
        inicio_temporizacion_fai = 0;
    }
    clear_timer(diferimetro_dai);
    diferimetro_dai = nullptr;
    diferimetro_cancelado = false;
    log(id, "disolución parcial");
    for (int i=0; i<secciones.size(); i++) {
        auto *sec = secciones[i].seccion;
        auto *prev = i>0 ? secciones[i-1].seccion : señal_inicio->seccion_prev;
        for (auto &[sig, dir] : señales) {
            if (sig->seccion == sec && sig->ruta_activa == this) sig->ruta_activa = nullptr;
        }
        if (!sec->is_asegurada(this)) break;
        if (sec->get_ocupacion(prev, secciones[i].dir) > EstadoCanton::Prenormalizado) break;
        sec->liberar(this);
        secciones_aseguradas.erase(sec);
        if (i + 1 == secciones.size() && !señales.empty()) {
            auto *sig = señales.back().first;
            if (sig->seccion_prev == sec && sig->ruta_activa == this) sig->ruta_activa = nullptr;
        }
    }
}
void ruta::construir_proximidad0(seccion_via *next, seccion_via *sec, Lado dir)
{
    if (sec == nullptr) return;
    if (sec->get_cv() != nullptr) {
        proximidad0.push_back({sec, dir});
        proximidad0_next.push_back(next);
    } else {
        std::vector<std::pair<seccion_via *, Lado>> prev;
        sec->prev_secciones(next, dir, prev);
        for (auto &[sec2, dir2] : prev) {
            if (sec->señal_inicio(dir, sec2) != nullptr) continue;
            construir_proximidad0(sec, sec2, dir2);
        }
    }
}
void ruta::construir_proximidad()
{
    proximidad0.clear();
    proximidad0_next.clear();
    proximidad.clear();
    auto p = señal_inicio->seccion->get_seccion_in(lado, señal_inicio->pin);
    construir_proximidad0(señal_inicio->seccion, p.first, p.second);
    for (int i=0; i<proximidad0.size(); i++) {
        auto &[sec,dir] = proximidad0[i];
        auto next = proximidad0_next[i];
        p = {sec, dir};
        ruta *ruta_actual = nullptr;
        bool trayecto = false;
        señal *sig = señal_inicio;
        while (p.first != nullptr) {
            if (p.first->get_cv() != nullptr) proximidad.push_back({p.first, p.second});

            if (sig != nullptr) {
                if (sig != señal_inicio && sig->aspecto == Aspecto::Parada) break;
                auto sig_impl = señal_impls.find(sig->id);
                if (sig->tipo == TipoSeñal::Entrada || sig->tipo == TipoSeñal::PostePuntoProtegido || sig_impl == señal_impls.end()) {
                    ruta_actual = nullptr;
                    trayecto = true;
                } else if (ruta_actual == nullptr || ruta_actual->señal_inicio == sig_impl->second) {
                    ruta_actual = sig_impl->second->ruta_fin;
                    trayecto = false;
                    if (ruta_actual != nullptr && ruta_actual->tipo != TipoMovimiento::Itinerario) ruta_actual = nullptr;
                }
            }
            bool afecta_anteriores = trayecto || (ruta_actual != nullptr && p.first->is_asegurada(ruta_actual));
            if (p.first != sec && (!afecta_anteriores || ultimos_cvs_proximidad.empty() || ultimos_cvs_proximidad.find(p.first->id_cv) != ultimos_cvs_proximidad.end() || tipo != TipoMovimiento::Itinerario)) {
                break;
            }
            std::pair<seccion_via*, Lado> prev = {nullptr, Lado::Impar};
            Lado dir = opp_lado(p.second);
            prev.first = p.first->siguiente_seccion(next, dir, true);
            if (prev.first == nullptr) prev.first = p.first->siguiente_seccion(next, dir);
            p.second = opp_lado(dir);
            sig = p.first->señal_inicio(p.second, prev.first);
            next = p.first;
            p = prev;
        }
    }
    proximidad0_next.clear();
}
void ruta::activar_pns()
{
    for (auto &e : secciones) {
        for (auto *pn : e.seccion->pns) {
            pn->activar_ruta(e.dir);
        }
    }
    for (auto &[pn, l] : pn_afectados) {
        pn->activar_ruta(l);
    }
}
void ruta::desactivar_pns()
{
    for (auto &[pn, l] : pn_afectados) {
        pn->desactivar_ruta(l);
    } 
}
