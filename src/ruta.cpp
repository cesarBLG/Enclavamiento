#include "ruta.h"
#include "items.h"
destino_ruta::destino_ruta(const id_elemento &id, const json &j) : id(id), tipo(j["Tipo"]), topic("destino/"+id_to_mqtt(id.id)+"/state")
{
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
            return ((ruta*)ruta_activa)->dei() ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
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
ruta::ruta(const std::string &estacion, const json &j) : movimiento(estacion, j["Tipo"], j["Inicio"].get<std::string>()+" "+j["Destino"].get<std::string>(), true, j.value("ERTMS", false)), id_inicio(j["Inicio"]), id_destino(j["Destino"]), bloqueo_salida(j.contains("Bloqueo") ? std::optional<id_elemento>(id_elemento(j["Bloqueo"])) : std::nullopt)
{
    id_elemento id_señal(estacion, id_inicio);
    if (señal_impls.find(id_señal) == señal_impls.end()) {
        log(id, "señal de inicio inválida", LOG_ERROR);
        return;
    }

    tiempo_espera_fai = parametros.tiempo_espera_fai;
    desactivar_diferimetro = j.value("DesactivarDiferímetroDAI", false);
    temporizador_dai1 = j.value("DiferímetroDAI1", tipo == TipoMovimiento::Maniobra ? 0 : parametros.diferimetro_dai1);
    temporizador_dai2 = j.value("DiferímetroDAI2", parametros.diferimetro_dai2);
    temporizador_dei = j.value("DiferímetroDEI", parametros.diferimetro_dei);
    temporizador_deslizamiento = 30000;

    señal_inicio = señal_impls[id_señal];
    lado = señal_inicio->lado;
    señales.push_back(señal_inicio);

    id_elemento full_id_destino = id_elemento::from_default_dep(id_destino, estacion);
    if (destinos_ruta.find(full_id_destino) == destinos_ruta.end()) {
        log(id, "destino inválido", LOG_ERROR);
        return;
    }
    destino = destinos_ruta[full_id_destino];
    if (destino->tipo == TipoDestino::Señal) {
        auto it = señal_impls.find(destino->id);
        if (it != señal_impls.end()) señales_fin.push_back(it->second);
    }

    maniobra_compatible = j.value("Compatible", CompatibilidadManiobra::IncompatibleBloqueo);
    if (j.contains("PosiciónAparatos")) {
        for (auto &[sec_id, jpos] : j["PosiciónAparatos"].items()) {
            auto id_aparato = id_elemento::from_default_dep(sec_id, estacion);
            if (::secciones.find(id_aparato) == ::secciones.end()) {
                log(id, "sección de posición de aparato inválida", LOG_ERROR);
                continue;
            }
            posicion_aparatos[::secciones[id_aparato]] = jpos;
        }
    }
    if (j.contains("SecciónFin")) {
        auto *prv = señal_inicio->seccion_prev;
        auto *sec = señal_inicio->seccion;
        Lado dir = lado;
        Lado sig_dir = lado;
        auto id_fin = id_elemento::from_default_dep(j["SecciónFin"], estacion);
        if (::secciones.find(id_fin) == ::secciones.end()) {
            log(id, "sección de fin inválida", LOG_ERROR);
            return;
        }
        auto *fin = ::secciones[id_fin];
        do
        {
            ocupacion_maxima_secciones[sec] = sec == fin && (tipo == TipoMovimiento::Rebase || tipo == TipoMovimiento::Maniobra) && j.contains("DiferímetroDeslizamiento") ? EstadoCanton::Ocupado : EstadoCanton::Libre;

            if (sec != señal_inicio->seccion) {
                auto *sig = sec->señal_inicio(dir, prv);
                if (sig != nullptr && señal_impls.find(sig->id) != señal_impls.end()) señales.push_back(señal_impls[sig->id]);
            }

            lados<int> outs;
            seccion_via *next;
            auto it = posicion_aparatos.find(sec);
            if (it != posicion_aparatos.end()) {
                outs = it->second;
                auto p = sec->get_seccion_in(opp_lado(dir), it->second[dir]);
                next = p.first;
                sig_dir = opp_lado(p.second);
            } else if (prv == nullptr) {
                next = sec->siguiente_seccion(señal_inicio->pin, sig_dir);
                outs[opp_lado(dir)] = señal_inicio->pin;
                outs[dir] = sec->get_out(next, dir);
            } else {
                next = sec->siguiente_seccion(prv, sig_dir);
                outs[opp_lado(dir)] = sec->get_in(prv, dir);
                outs[dir] = sec->get_out(next, dir);
            }
            secciones.push_back({sec, dir, outs});
            prv = sec;
            sec = next;
            dir = sig_dir;
        }
        while (sec != nullptr && prv != fin);

        if (j.contains("DiferímetroDeslizamiento")) {
            auto &jdesliz = j["DiferímetroDeslizamiento"];
            if (jdesliz.contains("InicioTemporizador")) seccion_inicio_temporizador_deslizamiento = ::secciones[id_elemento::from_default_dep(jdesliz["InicioTemporizador"], estacion)];
            else if (!jdesliz.is_boolean() || jdesliz) seccion_inicio_temporizador_deslizamiento = secciones.back().seccion;
            if (jdesliz.contains("Valor")) temporizador_deslizamiento = jdesliz["Valor"].get<int64_t>()*1000;
        } else if (fin->is_estacionamiento()) {
            seccion_inicio_temporizador_deslizamiento = secciones.back().seccion;
        }
    }
    if (secciones.empty()) lado_bloqueo = lado;
    else lado_bloqueo = *secciones.back().dir;
    if (j.contains("SeñalLiberación")) {
        señales.push_back(señal_impls[id_elemento::from_default_dep(j["SeñalLiberación"], estacion)]);
    }
    valid = true;
    if (j.value("FAI", false)) set_fai(true);
    tipo_fai = j.value("TipoFAI", "Proximidad") == "Proximidad" ? TipoFAI::Proximidad : TipoFAI::Reserva;
}
void movimiento::mover_agujas()
{
    for (auto &[sec, pins] : posicion_aparatos) {
        if (sec->tipo == TipoSeccion::Aguja) {
            aguja *a = (aguja*)sec;
            auto pos = a->get_posicion(pins);
            if (dependencias[sec->id.dependencia]->bloqueo_agujas || !a->mover(pos)) {
                a->requerir_movimiento(this, pos);
            }
        }
    }
}
bool movimiento::posible_establecer(bool msg)
{
    // Alguna de las señales de la ruta está mandada por otra ruta
    for (auto &sig : señales) {
        if (sig->ruta_activa != nullptr && sig->ruta_activa != this) {
            if (msg) log(id, "señal mandada por otra ruta", LOG_DEBUG);
            return false;
        }
        if (sig->bloqueo_señal) {
            if (msg) log(id, "señal bloqueada", LOG_DEBUG);
            return false;
        }
    }
    for (auto &sig : señales_fin) {
        if (sig->ruta_fin != nullptr && sig->ruta_fin != this) {
            if (msg) log(id, "señal destino de otra ruta", LOG_DEBUG);
            return false;
        }
    }
    // Agujas no enclavadas o bloqueadas
    for (auto &[sec, pins] : posicion_aparatos) {
        if (sec->tipo == TipoSeccion::Aguja) {
            aguja *a = (aguja*)sec;
            auto pos = a->get_posicion(pins);
            if (!a->posible_mover(pos, true)) {
                if (msg) log(id, "aguja bloqueada", LOG_DEBUG);
                return false;
            }
        }
    }

    deslizamientos_afectados.clear();
    for (int i=0; i<secciones.size(); i++) {
        auto *sec = secciones[i].seccion;
        // La ruta requiere secciones ya aseguradas por otra ruta
        if (!sec->asegurar_posible(this, secciones[i].outs, secciones[i].dir)) {
            if (msg) log(id, "asegurar imposible", LOG_DEBUG);
            return false;
        }
        // Bloqueo de vía establecido
        if (sec->is_bloqueo_seccion()) {
            if (msg) log(id, "bloqueo seccion", LOG_DEBUG);
            return false;
        }
        for (auto &nodo : sec->get_deslizamiento_potencial()) {
            deslizamientos_afectados[nodo->deslizamiento] = -1;
        }
    }
    for (auto &sig : señales_fin) {
        auto *desliz = sig->get_deslizamiento(this);
        if (desliz != nullptr) deslizamientos_afectados[desliz] = -1;
    }

    for (auto &[desliz, id] : deslizamientos_afectados) {
        int compat = desliz->compatible(this);
        if (compat < 0) {
            if (msg) log(this->id, "deslizamiento incompatible", LOG_DEBUG);
            return false;
        }
        id = compat;
    }
    return true;
}
bool movimiento::establecer(bool msg)
{
    if (!posible_establecer(msg)) return false;
    log(id, "mandada", LOG_DEBUG);
    mandada = true;
    for (auto  &sig : señales) {
        sig->ruta_mandada(this);
    }
    for (auto &sig : señales_fin) {
        sig->ruta_fin = this;
    }
    for (int i=0; i<secciones.size(); i++) {
        // Asegurar todas las secciones en el sentido de la ruta
        auto *sec = secciones[i].seccion;
        sec->asegurar(this, secciones[i].outs, secciones[i].dir);
        secciones_aseguradas.insert(sec);
    }
    mover_agujas();
    for (auto &[desliz, id] : deslizamientos_afectados) {
        desliz->activar(id);
    }
    return true;
}
bool ruta::posible_establecer(bool msg)
{
    if (destino->bloqueo_destino) {
        if (msg) log(id, "bloqueo destino", LOG_DEBUG);
        return false;
    }
    
    // Existe otro itinerario con el mismo destino
    if (destino->ruta_activa != nullptr && destino->ruta_activa != this) {
        if (msg) log(id, "destino activo", LOG_DEBUG);
        return false;
    }
    // Existe otro itinerario en sentido contrario con el mismo origen
    if (señal_inicio->seccion_prev != nullptr) {
        auto ruta_opp = señal_inicio->seccion_prev->get_ruta_asegurada();
        if (ruta_opp && ruta_opp->lado != señal_inicio->lado_prev) {
            auto &sigs = ruta_opp->ruta_asegurada->get_señales();
            if (!sigs.empty() && sigs[0]->seccion == señal_inicio->seccion_prev) {
                if (msg) log(id, "origen activo", LOG_DEBUG);
                return false;
            }
        }
    }
    if (señal_inicio->frontera_salida != nullptr && !señal_inicio->frontera_salida->salida_permitida(this)) {
        if (msg) log(id, "frontera inicio bloqueada", LOG_DEBUG);
        return false;
    }
    if (destino->get_frontera() != nullptr && !destino->get_frontera()->entrada_permitida(this)) {
        if (msg) log(id, "frontera fin bloqueada", LOG_DEBUG);
        return false;
    }
    if (bloqueo_salida) {
        TipoMovimiento colat = bloqueo_act.ruta[opp_lado(lado)];
        // Existe una maniobra establecida en la colateral y no se permiten movimientos simultáneos
        if (colat == TipoMovimiento::Maniobra) {
            CompatibilidadManiobra compat = bloqueo_act.maniobra_compatible[opp_lado(lado)];
            if (compat == CompatibilidadManiobra::Incompatible || compat == CompatibilidadManiobra::IncompatibleMovimiento) {
                if (msg) log(id, "maniobra incompatible", LOG_DEBUG);
                return false;
            } else if (compat < CompatibilidadManiobra::Compatible && tipo == TipoMovimiento::Itinerario) {
                if (msg) log(id, "maniobra incompatible", LOG_DEBUG);
                return false;
            }
        }
        bool bloqueo_emisor = bloqueo_act.estado == (lado_bloqueo == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar);
        bool bloqueo_receptor = bloqueo_act.estado == (lado_bloqueo == Lado::Par ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar);
        if (tipo == TipoMovimiento::Maniobra) {
            // Maniobras simultáneas incompatibles
            if (maniobra_compatible == CompatibilidadManiobra::Incompatible && colat != TipoMovimiento::Ninguno) {
                if (msg) log(id, "maniobra incompatible", LOG_DEBUG);
                return false;
            }
            // Maniobra incompatible con movimientos en la colateral
            if (maniobra_compatible == CompatibilidadManiobra::IncompatibleItinerario && (colat == TipoMovimiento::Itinerario/* || bloqueo_act.estacion_cerrada[opp_lado(lado)]*/)) {
                if (msg) log(id, "maniobra incompatible con itinerario", LOG_DEBUG);
                return false;
            }
            if (maniobra_compatible == CompatibilidadManiobra::IncompatibleMovimiento && (colat != TipoMovimiento::Ninguno/* || bloqueo_act.estacion_cerrada[opp_lado(lado)]*/)) {
                if (msg) log(id, "maniobra incompatible con movimiento", LOG_DEBUG);
                return false;
            }

            // Maniobra incompatible con bloqueo receptor
            if (bloqueo_receptor && maniobra_compatible <= CompatibilidadManiobra::IncompatibleBloqueo) {
                if (msg) log(id, "maniobra incompatible con bloqueo", LOG_DEBUG);
                return false;
            }
            
            if (!bloqueo_emisor) {
                // Si no está establecido el bloqueo emisor y la avanzada protege la maniobra, su proximidad debe estar libre
                // También se requieren libres los circuitos de trayecto hasta la avanzada no incluidos en la maniobra
                seccion_via *sec;
                seccion_via *sig;
                Lado l;
                if (secciones.empty()) {
                    sec = señal_inicio->seccion_prev;
                    sig = señal_inicio->seccion;
                    l = señal_inicio->lado;
                } else {
                    sec = secciones.back().seccion;
                    auto p = secciones.back().seccion->get_seccion_in(opp_lado(*secciones.back().dir), secciones.back().outs[*secciones.back().dir]);
                    sig = p.first;
                    l = opp_lado(p.second);
                }
                if (sec->is_trayecto() || (bloqueos.find(*bloqueo_salida) != bloqueos.end() ? bloqueos[*bloqueo_salida]->deslizamiento_bloqueo : parametros.deslizamiento_bloqueo)) {
                    // Requerir CV de avanzada libre
                    // También debe estar libre la proximidad de la avanzada (completa si hay bloqueo receptor)
                    while (sig != nullptr && sig->is_trayecto()) {
                        auto *señal = sec->señal_inicio(opp_lado(l), 0);
                        if (señal != nullptr && sec->is_trayecto()) {
                            if (señal->tipo == TipoSeñal::Avanzada) {
                                nodo_proximidad *n = ((señal_impl*)señal)->proximidad_señal.root.get();
                                while (n && n->estado > 0) {
                                    if (n->estado == 2 && !bloqueo_receptor) break;
                                    if (n->seccion->get_cv() != nullptr && n->seccion->get_cv()->get_ocupacion(opp_lado(n->dir)) == EstadoCanton::Ocupado) {
                                        if (msg) log(id, "proximidad ocupada", LOG_DEBUG);
                                        return false;
                                    }
                                    n = n->prev_activo;
                                }
                            }
                            break;
                        }
                        if (sig->get_ocupacion(sec, l) == EstadoCanton::Ocupado) {
                            if (msg) log(id, "proximidad ocupada", LOG_DEBUG);
                            return false;
                        }
                        auto *prv = sig;
                        sig = sig->siguiente_seccion(sec, l);
                        sec = prv;
                    }
                }
            }
        // No permitir rutas de salida con bloqueo receptor
        } else if (bloqueo_receptor) {
            if (msg) log(id, "itineario incompatible con bloqueo", LOG_DEBUG);
            return false;
        }
        // No permitir varias rutas hacia el mismo bloqueo
        if (bloqueo_act.ruta[lado_bloqueo] != tipo && bloqueo_act.ruta[lado_bloqueo] != TipoMovimiento::Ninguno) {
            if (msg) log(id, "ruta a bloqueo activa", LOG_DEBUG);
            return false;
        }
    }

    return movimiento::posible_establecer(msg);
}
bool ruta::establecer(bool msg)
{
    if (mandada && !ocupada) {
        if (destino->bloqueo_destino || señal_inicio->bloqueo_señal) {
            if (msg) log(id, "bloqueo destino o señal", LOG_DEBUG);
            return false;
        }
        clear_timer(diferimetro_dai);
        diferimetro_dai = nullptr;
        for (auto  &sig : señales) {
            sig->ruta_mandada(this);
        }
        mover_agujas();
        log(id, "re-mandada", LOG_DEBUG);
        return true;
    }
    destino->ruta_activa = this;
    if (!movimiento::establecer(msg)) {
        destino->ruta_activa = nullptr;
        return false;
    }
    clear_timer(diferimetro_dei);
    diferimetro_dei = nullptr;
    return true;
}
void movimiento::update()
{
    if (!mandada) return;

    bool agujas_dispuestas = true;
    for (auto &[sec, pins] : posicion_aparatos) {
        if (sec->tipo == TipoSeccion::Aguja && (!formada || sec->is_asegurada(this))) {
            aguja *a = (aguja*)sec;
            auto pos = a->get_posicion(pins);
            if (!a->enclavar(this, a->get_posicion(pins))) {
                agujas_dispuestas = false;
                break;
            }
        }
    }
    if (agujas_dispuestas && !formada) {
        formada = true;
        log(id, "formada", LOG_INFO);
    }

    for (auto &sig : señales_fin) {
        auto *desliz = sig->get_deslizamiento(this);
        if (desliz != nullptr) desliz->update();
    }
}
void ruta::update()
{
    bool activar_fai = false;

    if (mandada)
    {
        int zona = 0;
        bool proximidad_libre = true;
        nodo_proximidad *n = señal_inicio->proximidad_señal.root.get();

        id_elemento destino_actual;
        movimiento *ruta_actual = nullptr;
        señal_fundamental *sig = señal_inicio;

        while (n && n->estado > 0 && (proximidad_libre || zona == 1)) {
            if ((tipo == TipoMovimiento::Maniobra || tipo == TipoMovimiento::Rebase) && n->estado == 2) break;
            
            seccion_via *prev = n->prev_activo != nullptr ? n->prev_activo->seccion : nullptr;
            if (sig != nullptr) {
                auto sig_impl = señal_impls.find(sig->id);
                if (sig != señal_inicio && sig->aspecto <= Aspecto::RebaseAutorizadoDestellos) {
                    if (sig_impl != señal_impls.end()) {
                        auto *s = sig_impl->second;
                        if ((ruta_actual == nullptr && s->ruta_necesaria) || ruta_actual != s->ruta_activa)
                            break;
                        if (ruta_actual != nullptr && ruta_actual->es_ruta && !((ruta*)ruta_actual)->is_supervisada())
                            break;
                    } else if (!n->seccion->is_trayecto()) {
                        auto it = estado_destinos_ruta.find(destino_actual);
                        if (it != estado_destinos_ruta.end()) {
                            if (it->second.señal_inicio != sig->id || !it->second.supervisada)
                                break;
                        }
                    }
                }
                if (n->seccion->is_trayecto() && n->seccion->bloqueo_asociado) {
                    ruta_actual = nullptr;
                    auto blq = bloqueos.find(*n->seccion->bloqueo_asociado);
                    if (blq != bloqueos.end() && blq->second->lado != n->dir) destino_actual = id_elemento(blq->second->estacion_colateral, blq->second->estacion+blq->second->via);
                    else destino_actual = *n->seccion->bloqueo_asociado;
                } else if (sig_impl == señal_impls.end()) {
                    ruta_actual = nullptr;
                    destino_actual = sig->id;
                } else if (ruta_actual == nullptr || !n->seccion->is_asegurada(ruta_actual)) {
                    ruta_actual = sig_impl->second->ruta_fin;
                    destino_actual = sig->id;
                }
            }
            if (n->siguiente->is_trayecto() && !n->seccion->is_trayecto()) {
                auto it = estado_destinos_ruta.find(destino_actual);
                if (it != estado_destinos_ruta.end() && !it->second.supervisada)
                    break;
            }

            if (n->seccion->get_cv() != nullptr) {
                if (n->estado > zona) zona = n->estado;
                if (n->seccion->get_cv()->get_state() != EstadoCV::Libre) {
                    proximidad_libre = false;
                }
            }

            if (ruta_actual != nullptr && !n->seccion->is_asegurada(ruta_actual)) break;
            sig = n->seccion->señal_inicio(n->dir, prev);

            n = n->prev_activo;
        }
        if (zona == 0) proximidad_ocupada = EstadoProximidad::Inexistente;
        else if (proximidad_libre) proximidad_ocupada = EstadoProximidad::Libre;
        else proximidad_ocupada = zona == 2 ? EstadoProximidad::Zona2 : EstadoProximidad::Zona1;
    }
    else
    {
        proximidad_ocupada == EstadoProximidad::Inexistente;
    }

    // Con FAI activo, el itinerario se establece si la proximidad está ocupada y se permite la salida al bloqueo (en señales de salida)
    if (fai) {
        bool aut_salida = true;
        int prioridad = 0;
        if (bloqueo_salida) {
            aut_salida &= !bloqueo_act.cierre_señales[lado_bloqueo] && !bloqueo_act.prohibido[lado_bloqueo] && bloqueo_act.actc[lado_bloqueo] != ACTC::Denegada;
            prioridad = bloqueo_act.prioridad_itinerario[lado_bloqueo]-bloqueo_act.prioridad_itinerario[opp_lado(lado_bloqueo)];
        }
        bool solicitud_fai = false;
        if (tipo_fai == TipoFAI::Proximidad) {
            int zona_ocupacion = 0;
            nodo_proximidad *n = señal_inicio->proximidad_señal.root.get();

            movimiento *ruta_actual = nullptr;
            bool trayecto = false;
            señal_fundamental *sig = señal_inicio;

            while (n && n->estado > 0 && zona_ocupacion == 0) {
                if ((tipo == TipoMovimiento::Maniobra || tipo == TipoMovimiento::Rebase) && n->estado == 2) break;
                
                seccion_via *prev = n->prev_activo != nullptr ? n->prev_activo->seccion : nullptr;
                if (sig != nullptr) {
                    auto sig_impl = señal_impls.find(sig->id);
                    if (sig != señal_inicio && sig->aspecto <= Aspecto::RebaseAutorizadoDestellos) {
                        auto *s = sig_impl != señal_impls.end() ? sig_impl->second : nullptr;
                        if (s != nullptr) {
                            if (!s->clear_request)
                                break;
                            if (ruta_actual != s->ruta_activa || (s->ruta_necesaria && ruta_actual == nullptr))
                                break;
                        } else if (!n->siguiente->is_trayecto()) {
                            break;
                        }
                    }
                    if (sig_impl == señal_impls.end() || n->seccion->is_trayecto()) {
                        ruta_actual = nullptr;
                        trayecto = true;
                    } else if (ruta_actual == nullptr || !n->seccion->is_asegurada(ruta_actual)) {
                        ruta_actual = sig_impl->second->ruta_fin;
                        trayecto = false;
                    }
                }
                
                auto *cv = n->seccion->get_cv();
                if (cv != nullptr) {
                    EstadoCV est = cv->get_state();
                    if (est == (n->dir == Lado::Impar ? EstadoCV::OcupadoImpar : EstadoCV::OcupadoPar) || (est == EstadoCV::Ocupado && !cv->is_averia())) {
                        zona_ocupacion = n->estado;
                        break;
                    }
                }

                if (ruta_actual != nullptr && !n->seccion->is_asegurada(ruta_actual)) break;
                sig = n->seccion->señal_inicio(n->dir, prev);

                n = n->prev_activo;
            }
            if (estado_fai == EstadoFAI::EnEspera) {
                // Retrasar itinerario respecto al último lanzamiento para evitar que el mismo tren active la ruta dos veces
                // No retrasar si se libera la proximidad (son realmente dos trenes distintos)
                if (zona_ocupacion != 1) inicio_temporizacion_fai = 0;
                if (aut_salida && zona_ocupacion > 0 && prioridad >= 0 && get_milliseconds() - inicio_temporizacion_fai > tiempo_espera_fai) {
                    estado_fai = EstadoFAI::Solicitud;
                    inicio_temporizacion_fai = get_milliseconds();
                }
            }
            solicitud_fai = zona_ocupacion > 0;
        } else if (tipo_fai == TipoFAI::Reserva) {
            if (señal_inicio->seccion_prev != nullptr) {
                auto r = señal_inicio->seccion_prev->get_ruta_asegurada();
                if (r && r->lado == señal_inicio->lado_prev)
                    solicitud_fai = true;
                auto blq = señal_inicio->seccion_prev->bloqueo_asociado;
                if (blq && bloqueos.find(*blq) != bloqueos.end() && bloqueos[*blq]->get_estado().estado == (señal_inicio->lado_prev == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar))
                    solicitud_fai = true;
            }
            if (estado_fai == EstadoFAI::EnEspera) {
                if (aut_salida && solicitud_fai && prioridad >= 0) {
                    estado_fai = EstadoFAI::Solicitud;
                    inicio_temporizacion_fai = get_milliseconds();
                }
            }
        }
        // Si se ha cancelado manualmente el FAI, se resetea al liberarse la proximidad
        if (estado_fai == EstadoFAI::Cancelado && !solicitud_fai) estado_fai = EstadoFAI::EnEspera;
        // Si no se cumplen las condiciones, disolvemos la ruta automáticamente
        if ((!aut_salida || !solicitud_fai || prioridad < 0) && !fai_disparo_unico && estado_fai != EstadoFAI::EnEspera && estado_fai != EstadoFAI::Cancelado) {
            dai(true);
            estado_fai = EstadoFAI::EnEspera;
            inicio_temporizacion_fai = 0;
            dependencias[señal_inicio->id.dependencia]->set_alarma(TipoAlarma::FI, señal_inicio, 0);
        }
    }
    if (!fai && !fai_disparo_unico) {
        // Desactivación FAI
        if (señal_inicio->ruta_fai == this) señal_inicio->ruta_fai = nullptr;
        if (estado_fai != EstadoFAI::EnEspera) {
            estado_fai = EstadoFAI::EnEspera;
            inicio_temporizacion_fai = 0;
            dependencias[señal_inicio->id.dependencia]->set_alarma(TipoAlarma::FI, señal_inicio, 0);
        }
    } else if (estado_fai == EstadoFAI::Solicitud || estado_fai == EstadoFAI::AperturaNoPosible || estado_fai == EstadoFAI::AperturaNoPosibleReconocida) {
        if (señal_inicio->aspecto > Aspecto::Parada && señal_inicio->ruta_activa == this) {
            // Itinerario establecido y señal abierta
            estado_fai = EstadoFAI::Activo;
            fai_disparo_unico = false;
            dependencias[señal_inicio->id.dependencia]->set_alarma(TipoAlarma::FI, señal_inicio, 1);
        } else if (estado_fai == EstadoFAI::Solicitud) {
            // FAI mandando ruta
            if (señal_inicio->ruta_activa != this || diferimetro_dai != nullptr) establecer();
            // Señal no abre tras temporizador, dejar de re-mandar la ruta
            if (estado_fai == EstadoFAI::Solicitud && get_milliseconds() - inicio_temporizacion_fai > 3*60*1000) {
                estado_fai = EstadoFAI::AperturaNoPosible;
            dependencias[señal_inicio->id.dependencia]->set_alarma(TipoAlarma::FI, señal_inicio, 2, true);
            }
        }
    } else if (estado_fai == EstadoFAI::Activo) {
        // Si la señal se cierra por cualquier causa, es necesario que se vuelva a ocupar la proximidad para volver a abrir
        if (señal_inicio->aspecto <= Aspecto::Parada) {
            estado_fai = EstadoFAI::Cancelado;
        }
    }

    movimiento::update();

    if (!mandada) return;

    // Mandar cierre de PN si la proximidad está ocupada
    if (proximidad_ocupada != EstadoProximidad::Libre && señal_inicio->ruta_activa != nullptr) {
        activar_pns();
    }

    // Desenclavar ruta al paso de la circulación
    if ((ocupada || señal_inicio->get_cv_inicio() == nullptr) && !sucesion_automatica) {
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
            for (auto &sig : señales) {
                if (sig->seccion == secciones[i].seccion && sig->ruta_activa == this) sig->ruta_activa = nullptr;
            }
            if (secciones[i].seccion->get_cv() == nullptr || cv_libre(secciones[i].seccion->get_cv()->get_state())) {
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
            if (secciones[i].seccion->get_cv() != nullptr && !cv_libre(secciones[i].seccion->get_cv()->get_state())) {
                libre = false;
                break;
            }
        }
        // Si la ruta sigue enclavada para otro tren aún estando ocupada,
        // pasar a estado libre cuando todas las secciones están libres
        if (libre) {
            ocupada = false;
            supervisada = señal_inicio->get_state() > Aspecto::Parada;
        }
    }
    // La ruta pasa a estar supervisada con la apertura de la señal
    if (formada && !supervisada && señal_inicio->get_state() > Aspecto::Parada) {
        supervisada = true;
        log(id, "supervisada");
    }
}
void ruta::message_cv(const id_elemento &id, estado_cv ecv)
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
    cv *cv_inicio = señal_inicio->get_cv_inicio();
    if (cv_inicio != nullptr && cv_inicio->id == id && ecv.is_ocupacion(lado)) {
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
    // En maniobra, cerrar señal si se libera el circuito anterior a la señal o el de señal
    if (tipo == TipoMovimiento::Maniobra && !sucesion_automatica && señal_inicio->ruta_activa == this && (ocupada || cv_inicio == nullptr) && cv_libre(ecv.estado) && !cv_libre(ecv.estado_previo)) {
        bool proximidad_liberada = true;
        bool cambio_proximidad = false;
        nodo_proximidad *n = señal_inicio->proximidad_señal.root.get();
        while (n && n->estado == 1) {
            auto *cv = n->seccion->get_cv();
            if (cv != nullptr) {
                if (cv->get_state() > EstadoCV::Prenormalizado) {
                    proximidad_liberada = false;
                    break;
                }
                if (cv->id == id) cambio_proximidad = true;
            }
            n = n->prev_activo;
        }
        if (!cambio_proximidad) proximidad_liberada = false;
        if (proximidad_liberada || (cv_inicio != nullptr && cv_inicio->id == id)) señal_inicio->ruta_activa = nullptr;
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
    // Si la proximidad está libre, la disolución es inmediata
    // Si no hay secciones aseguradas por la ruta, ni un bloqueo tomado, no es necesario temporizar
    if (proximidad_ocupada == EstadoProximidad::Libre || (secciones.size() == 0 && !bloqueo_salida) || (desactivar_diferimetro && !dependencias[estacion]->cerrada)) {
        disolucion_parcial(anular_bloqueo);
    } else if (diferimetro_dai == nullptr) {
        int64_t temporizador = proximidad_ocupada == EstadoProximidad::Zona2 ? temporizador_dai2 : temporizador_dai1;
        if (temporizador == 0) {
            disolucion_parcial(anular_bloqueo);
            return true;
        }
        diferimetro_dai = set_timer([this, anular_bloqueo]() {
            diferimetro_dai = nullptr;
            disolucion_parcial(anular_bloqueo);
        }, temporizador);
        if (señal_inicio->ruta_activa == this && señal_inicio->ruta_necesaria) señal_inicio->clear_request = false;
        for (int i=0; i<secciones.size(); i++) {
            auto *sec = secciones[i].seccion;
            auto *prev = i>0 ? secciones[i-1].seccion : señal_inicio->seccion_prev;
            for (auto &sig : señales) {
                if (sig->seccion == sec && sig->ruta_activa == this && sig->ruta_necesaria) sig->clear_request = false;
            }
            if (!sec->is_asegurada(this)) break;
            if (sec->get_ocupacion(prev, *secciones[i].dir) > EstadoCanton::Libre) break;
            if (i + 1 == secciones.size() && !señales.empty()) {
                auto *sig = señales.back();
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
    for (auto &sig : señales) {
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
void movimiento::disolver()
{
    for (auto &sig : señales) {
        if (sig->ruta_activa == this) sig->ruta_activa = nullptr;
    }
    for (auto &sig : señales_fin) {
        if (sig->ruta_fin == this) sig->ruta_fin = nullptr;
    }
    mandada = false;
    formada = false;
    for (auto *sec : secciones_aseguradas) {
        sec->liberar(this);
    }
    for (auto &[ag, pos] : posicion_aparatos) {
        ag->liberar(this);
    }
    secciones_aseguradas.clear();
    for (auto &[desliz,id] : deslizamientos_afectados) {
        id = desliz->compatible(nullptr);
        desliz->activar(id);
    }
    log(id, "disuelta");
}
void ruta::disolver()
{
    supervisada = false;
    ocupada = false;
    clear_timer(diferimetro_dai);
    clear_timer(diferimetro_dei);
    clear_timer(diferimetro_deslizamiento);
    diferimetro_dai = nullptr;
    diferimetro_dei = nullptr;
    diferimetro_deslizamiento = nullptr;
    diferimetro_cancelado = false;
    for (auto &sig : señales_fin) {
        auto *desliz = sig->get_deslizamiento(this);
        if (desliz != nullptr) desliz->liberar();
    }
    if (destino->ruta_activa == this) destino->ruta_activa = nullptr;
    movimiento::disolver();
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
        for (auto &sig : señales) {
            if (sig->seccion == sec && sig->ruta_activa == this) sig->ruta_activa = nullptr;
        }
        if (!sec->is_asegurada(this)) break;
        if (sec->get_ocupacion(prev, *secciones[i].dir) > EstadoCanton::Libre) break;
        sec->liberar(this);
        secciones_aseguradas.erase(sec);
        if (i + 1 == secciones.size() && !señales.empty()) {
            auto *sig = señales.back();
            if (sig->seccion_prev == sec && sig->ruta_activa == this) sig->ruta_activa = nullptr;
        }
    }
}
void ruta::activar_pns()
{
    for (auto &e : secciones) {
        for (auto *pn : e.seccion->pns) {
            if (e.dir) {
                pn->activar_ruta(*e.dir);
            } else {
                pn->activar_ruta(Lado::Impar);
                pn->activar_ruta(Lado::Par);
            }
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
bool ruta::set_fai(bool activar) {
    if (activar) {
        if (!fai && señal_inicio->ruta_fai == nullptr) {
            fai = true;
            señal_inicio->ruta_fai = this;
            dependencias[señal_inicio->id.dependencia]->set_alarma(TipoAlarma::FI, señal_inicio, 1);
            return true;
        }
    } else {
        if ((fai || fai_disparo_unico) && señal_inicio->ruta_fai == this) {
            fai = false;
            fai_disparo_unico = false;
            señal_inicio->ruta_fai = nullptr;
            dependencias[señal_inicio->id.dependencia]->set_alarma(TipoAlarma::FI, señal_inicio, 0);
            return true;
        }
    }
    return false;
}
RespuestaMando ruta::mando(const std::string &inicio, const std::string &fin, const std::string &cmd)
{
    if (inicio != id_inicio || fin != id_destino) return RespuestaMando::OrdenNoAplicable;

    auto mando0 = dependencias[señal_inicio->id.dependencia]->mando_actual;
    for (auto &sec : secciones) {
        if (sec.seccion->is_trayecto()) continue;
        auto it = dependencias.find(sec.seccion->id.dependencia);
        if (it == dependencias.end() || it->second->mando_actual.central != mando0.central || it->second->mando_actual.puesto != mando0.puesto) {
            return RespuestaMando::NoMando;
        }
    }
    for (auto &sig : señales) {
        auto it = dependencias.find(sig->id.dependencia);
        if (it == dependencias.end() || it->second->mando_actual.central != mando0.central || it->second->mando_actual.puesto != mando0.puesto) {
            return RespuestaMando::NoMando;
        }
    }

    if ((cmd == "I" || cmd == "FAI" || cmd == "AFA" || cmd == "ID") && (tipo != TipoMovimiento::Itinerario || ertms)) return RespuestaMando::OrdenNoAplicable;
    if (cmd == "M" && tipo != TipoMovimiento::Maniobra) return RespuestaMando::OrdenNoAplicable;
    if (cmd == "R" && tipo != TipoMovimiento::Rebase) return RespuestaMando::OrdenNoAplicable;
    if (cmd == "ER" && !ertms) return RespuestaMando::OrdenNoAplicable;
    if (cmd == "I" || cmd == "ER" || cmd == "R" || cmd == "M") return establecer(true) ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
    else if (cmd == "ID") {
        if (establecer(true)) return RespuestaMando::Aceptado;
        if (!fai_disparo_unico && (estado_fai == EstadoFAI::Cancelado || estado_fai == EstadoFAI::EnEspera)) {
            fai_disparo_unico = true;
            señal_inicio->ruta_fai = this;
            estado_fai = EstadoFAI::Solicitud;
            inicio_temporizacion_fai = get_milliseconds();
            dependencias[señal_inicio->id.dependencia]->set_alarma(TipoAlarma::FI, señal_inicio, 1);
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "FAI") {
        if (set_fai(true)) return RespuestaMando::Aceptado;
    } else if (cmd == "AFA") {
        if (set_fai(false)) return RespuestaMando::Aceptado;
    }
    return RespuestaMando::OrdenRechazada;
}
maniobra_local::maniobra_local(const std::string &estacion, const std::string &id, const json &j) : movimiento(estacion, TipoMovimiento::Maniobra, id, false, false), temporizador_anulacion(3*60*1000)
{
    if (j.contains("PosiciónAparatos")) {
        for (auto &[sec_id, jpos] : j["PosiciónAparatos"].items()) {
            auto id_aparato = id_elemento::from_default_dep(sec_id, estacion);
            if (::secciones.find(id_aparato) == ::secciones.end()) {
                log(id, "sección de posición de aparato inválida", LOG_ERROR);
                continue;
            }
            posicion_aparatos[::secciones[id_aparato]] = jpos;
        }
    }
    for (auto &id : j["Señales"]) {
        señales.push_back(::señal_impls[id_elemento::from_default_dep(id, estacion)]);
    }
    if (j.contains("Secciones")) {
        for (auto &[id, sec] : j["Secciones"].items()) {
            secciones.push_back({::secciones[id_elemento::from_default_dep(id, estacion)], std::nullopt, sec});
            ocupacion_maxima_secciones[secciones.back().seccion] = EstadoCanton::Libre;
        }
    } else {
        std::set<señal_impl*> señales_intermedias;
        for (auto *sig : señales) {
            /*if (sig->seccion_prev != nullptr) {
                int out = sig->seccion_prev->get_out(sig->seccion, sig->lado_prev);
                std::set<int> ins;
                for (auto &[in, outs] : sig->seccion_prev->all_outs[sig->lado_prev]) {
                    if (outs.find(out) != outs.end()) ins.insert(in);
                }
                if (ins.size() == 1) {
                    secciones.push_back({sig->seccion_prev, std::nullopt, lados<int>::from_directional(*ins.begin(), out, sig->lado_prev)});
                } else {
                    secciones.push_back({sig->seccion_prev, std::nullopt, lados<int>::from_directional(-1, out, sig->lado_prev)});
                }
                ocupacion_maxima_secciones[secciones.back().seccion] = EstadoCanton::Libre;
            }*/
            recorrer_seccion(sig->seccion, sig->pin, sig->lado, señales_intermedias, sig);
        }
        señales.insert(señales.end(), señales_intermedias.begin(), señales_intermedias.end());
    }
}
void maniobra_local::recorrer_seccion(seccion_via *sec, int in, Lado dir, std::set<señal_impl*> &señales_intermedias, señal_impl *señal_origen)
{
    auto *sig = sec->señal_inicio(dir, in);
    if (sig != nullptr && señal_impls.find(sig->id) != señal_impls.end()) {
        bool found = false;
        for (auto *sig2 : señales) {
            if (sig == sig2) {
                found = true;
                break;
            }
        }
        if (!found) señales_intermedias.insert(señal_impls[sig->id]);
    }

    std::set<int> outs;
    seccion_via *next;
    auto it = posicion_aparatos.find(sec);
    if (it != posicion_aparatos.end()) {
        if (it->second[opp_lado(dir)] != in) return;
        outs.insert(it->second[dir]);
    } else {
        outs = sec->all_outs[dir][in];
    }
    bool found = false;
    for (int j=0; j<secciones.size(); j++) {
        if (secciones[j].seccion == sec) {
            if (secciones[j].dir && secciones[j].dir != dir) secciones[j].dir = std::nullopt;
            if (secciones[j].outs[opp_lado(dir)] != in) secciones[j].outs[opp_lado(dir)] = -1;
            if (outs.size() != 1 || secciones[j].outs[dir] != *outs.begin()) secciones[j].outs[dir] = -1;
            found = true;
            break;
        }
    }
    if (!found) {
        if (outs.size() == 1) {
            secciones.push_back({sec, dir, lados<int>::from_directional(in, *outs.begin(), dir)});
        } else {
            secciones.push_back({sec, dir, lados<int>::from_directional(in, -1, dir)});
        }
        ocupacion_maxima_secciones[secciones.back().seccion] = EstadoCanton::Libre;
    }
    for (auto &out : outs) {
        auto *sig = sec->señal_inicio(opp_lado(dir), out);
        if (sig != nullptr) {
            bool found = false;
            for (auto &sig2 : señales) {
                if (sig == sig2) {
                    found = true;
                    break;
                }
            }
            if (found) continue;
        }
        auto p = sec->get_seccion_in(opp_lado(dir), out);
        if (p.first == nullptr) continue;
        recorrer_seccion(p.first, p.first->get_in(sec, opp_lado(p.second)), opp_lado(p.second), señales_intermedias, señal_origen);
    }
}
bool maniobra_local::anular(bool emergencia)
{
    if (!mandada || diferimetro_anulacion != nullptr) return false;
    for (auto &sig : señales) {
        sig->clear_request = false;
    }
    if (!emergencia) {
        anulacion_normal_imposible = true;
        for (int i=0; i<secciones.size(); i++) {
            auto *cv_seccion = secciones[i].seccion->get_cv();
            if (cv_seccion != nullptr && !cv_libre(cv_seccion->get_state())) {
                log(id, "cv ocupado", LOG_DEBUG);
                return false;
            }
        }
        anulacion_normal_imposible = false;
    }
    diferimetro_anulacion = set_timer([this]() {
        disolver();
    }, temporizador_anulacion);
    return true;
}
bool maniobra_local::posible_establecer(bool msg)
{
    for (int i=0; i<secciones.size(); i++) {
        auto *cv_seccion = secciones[i].seccion->get_cv();
        if (cv_seccion == nullptr) continue;
        auto it = ocupacion_maxima_secciones.find(secciones[i].seccion);
        if (it == ocupacion_maxima_secciones.end()) continue;
        EstadoCV est = cv_seccion->get_state();
        if (it->second == EstadoCanton::Libre && est > EstadoCV::Libre) {
            if (msg) log(id, "cv ocupado", LOG_DEBUG);
            return false;
        }
    }
    return movimiento::posible_establecer(msg);
}
bool maniobra_local::establecer(bool msg)
{
    if (!mandada && !movimiento::establecer(msg)) return false;
    for (auto  &sig : señales) {
        sig->ruta_mandada(this);
    }
    anulacion_normal_imposible = false;
    clear_timer(diferimetro_anulacion);
    diferimetro_anulacion = nullptr;
    return true;
}
RespuestaMando maniobra_local::mando(const std::string &cmd, int me)
{
    if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
    bool pend = me_pendiente;
    me_pendiente = false;
    if (cmd == "ML") return establecer(true) ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
    else if (cmd == "AML") return anular() ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
    else if (cmd == "AMLE") {
        if (mandada && anulacion_normal_imposible) {
            if (me) {
                return anular(true) ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
            } else {
                me_pendiente = true;
                return RespuestaMando::MandoEspecialNecesario;
            }
        }
    }
    return RespuestaMando::OrdenRechazada;
}
