#include "ruta.h"
#include "items.h"
destino_ruta::destino_ruta(const id_elemento &id, const json &j) : id(id), tipo(j["Tipo"]), topic("destino/"+id_to_mqtt(id.id)+"/state")
{
    auto it = señal_impls.find(id);
    if (tipo == TipoDestino::Señal && it != señal_impls.end()) {
        señal_fin = it->second;

        if (j.contains("Deslizamiento")) {
            auto *d = new ruta_deslizamiento(this, j["Deslizamiento"]);
            deslizamientos[TipoMovimiento::Itinerario] = d;
            deslizamientos[TipoMovimiento::Maniobra] = d;
        }
    }
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
ruta::ruta(const std::string &estacion, const json &j) : movimiento(estacion, j["Tipo"], (j["Tipo"] == TipoMovimiento::Itinerario ? (ertms ? "ER " : "I ") : (j["Tipo"] == TipoMovimiento::Rebase ? "R " : "M "))+estacion+" "+j["Inicio"].get<std::string>()+" "+j["Destino"].get<std::string>()), id_inicio(j["Inicio"]), id_destino(j["Destino"]), bloqueo_salida(j.contains("Bloqueo") ? std::optional<id_elemento>(id_elemento(j["Bloqueo"])) : std::nullopt)
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

    señal_inicio = señal_impls[id_señal];
    lado = señal_inicio->lado;
    señales.push_back(señal_inicio);

    id_elemento full_id_destino = id_elemento::from_default_dep(id_destino, estacion);
    if (destinos_ruta.find(full_id_destino) == destinos_ruta.end()) {
        log(id, "destino inválido", LOG_ERROR);
        return;
    }
    destino = destinos_ruta[full_id_destino];

    maniobra_compatible = j.value("Compatible", CompatibilidadManiobra::IncompatibleBloqueo);
    if (j.contains("PosiciónAparatos")) {
        for (auto &[sec_id, jpos] : j["PosiciónAparatos"].items()) {
            posicion_aparatos[::secciones[id_elemento::from_default_dep(sec_id, estacion)]] = jpos;
        }
    }
    if (j.contains("SecciónFin")) {
        auto *prv = señal_inicio->seccion_prev;
        auto *sec = señal_inicio->seccion;
        Lado dir = lado;
        Lado sig_dir = lado;
        auto *fin = ::secciones[id_elemento(j["SecciónFin"])];
        do
        {
            ocupacion_maxima_secciones[sec] = sec == fin && (tipo == TipoMovimiento::Rebase || tipo == TipoMovimiento::Maniobra) && j.contains("Deslizamiento") ? EstadoCanton::Ocupado : EstadoCanton::Prenormalizado;

            if (sec != señal_inicio->seccion) {
                auto *sig = sec->señal_inicio(dir, prv);
                if (sig != nullptr && señal_impls.find(sig->id) != señal_impls.end()) señales.push_back(señal_impls[sig->id]);
            }

            std::pair<int,int> pins;
            seccion_via *next;
            if (posicion_aparatos.find(sec) != posicion_aparatos.end()) {
                if (dir == Lado::Par) pins = {posicion_aparatos[sec].second, posicion_aparatos[sec].first};
                else pins = posicion_aparatos[sec];
                auto p = sec->get_seccion_in(opp_lado(dir), pins.second);
                next = p.first;
                sig_dir = opp_lado(p.second);
            } else if (prv == nullptr) {
                next = sec->siguiente_seccion(señal_inicio->pin, sig_dir);
                pins = {señal_inicio->pin, sec->get_out(next, dir)};
            } else {
                next = sec->siguiente_seccion(prv, sig_dir);
                pins = {sec->get_in(prv, dir), sec->get_out(next, dir)};
            }
            secciones.push_back({sec, dir, pins.first, pins.second});
            prv = sec;
            sec = next;
            dir = sig_dir;
        }
        while (sec != nullptr && prv != fin);
    }
    if (secciones.empty()) lado_bloqueo = lado;
    else lado_bloqueo = *secciones.back().dir;
    if (!ertms && destino->deslizamientos.find(tipo) != destino->deslizamientos.end()) {
        deslizamiento = destino->deslizamientos[tipo];
    }
    if (j.contains("DiferímetroDeslizamiento")) {
        auto &jdesliz = j["DiferímetroDeslizamiento"];
        if (jdesliz.contains("Inicio")) seccion_inicio_temporizador_deslizamiento = ::secciones[id_elemento::from_default_dep(jdesliz["InicioTemporizador"], estacion)];
        else seccion_inicio_temporizador_deslizamiento = secciones.back().seccion;
        temporizador_deslizamiento = jdesliz.value("Valor", 30000);
    }
    if (j.contains("SeñalLiberación")) {
        señales.push_back(señal_impls[id_elemento::from_default_dep(j["SeñalLiberación"], estacion)]);
    }
    valid = true;
    if (j.value("FAI", false)) set_fai(true);
    tipo_fai = j.value("TipoFAI", "Proximidad") == "Proximidad" ? TipoFAI::Proximidad : TipoFAI::Reserva;
}
void movimiento::mover_agujas()
{
    if (!dependencias[estacion]->bloqueo_agujas) {
        for (auto &[sec, pins] : posicion_aparatos) {
            if (sec->tipo == TipoSeccion::Aguja) {
                aguja *a = (aguja*)sec;
                auto pos = a->get_posicion(Lado::Impar, pins.first, pins.second);
                a->mover(pos);
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
    }
    // Agujas no enclavadas o bloqueadas
    for (auto &[sec, pins] : posicion_aparatos) {
        if (sec->tipo == TipoSeccion::Aguja) {
            aguja *a = (aguja*)sec;
            auto pos = a->get_posicion(Lado::Impar, pins.first, pins.second);
            if (!a->posible_mover(pos)) {
                if (msg) log(id, "aguja bloqueada", LOG_DEBUG);
                return false;
            }
        }
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
    deslizamientos_afectados.clear();
    if (!posible_establecer(msg)) return false;
    log(id, "mandada", LOG_DEBUG);
    mandada = true;
    for (auto  &sig : señales) {
        sig->ruta_activa = this;
        sig->clear_request = true;
    }
    for (int i=0; i<secciones.size(); i++) {
        // Asegurar todas las secciones en el sentido de la ruta
        auto *sec = secciones[i].seccion;
        sec->asegurar(this, secciones[i].in, secciones[i].out, secciones[i].dir);
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
    if (destino->bloqueo_destino || señal_inicio->bloqueo_señal) {
        if (msg) log(id, "bloqueo destino o señal", LOG_DEBUG);
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
                    auto p = secciones.back().seccion->get_seccion_in(opp_lado(*secciones.back().dir), secciones.back().out);
                    sig = p.first;
                    l = opp_lado(p.second);
                }
                if (sec->is_trayecto() || parametros.deslizamiento_bloqueo) {
                    // Requerir CV de avanzada libre
                    // También debe estar libre la proximidad de la avanzada (completa si hay bloqueo receptor)
                    while (sig != nullptr && sig->is_trayecto()) {
                        auto *señal = sec->señal_inicio(opp_lado(l), 0);
                        if (señal != nullptr && sec->is_trayecto()) {
                            if (señal->tipo == TipoSeñal::Avanzada) {
                                for (auto &[sec, val] : ((señal_impl*)señal)->proximidad_señal.get(bloqueo_receptor ? TipoMovimiento::Itinerario : TipoMovimiento::Maniobra)) {
                                    auto [dir, next] = val;
                                    if (sec->get_cv()->get_ocupacion(opp_lado(dir)) == EstadoCanton::Ocupado) {
                                        if (msg) log(id, "proximidad ocupada", LOG_DEBUG);
                                        return false;
                                    }
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
            if (msg) log(id, "maniobra incompatible con bloqueo", LOG_DEBUG);
            return false;
        }
        // No permitir varias rutas hacia el mismo bloqueo
        if (bloqueo_act.ruta[lado_bloqueo] != tipo && bloqueo_act.ruta[lado_bloqueo] != TipoMovimiento::Ninguno) {
            if (msg) log(id, "ruta a bloqueo activa", LOG_DEBUG);
            return false;
        }
    }

    deslizamientos_afectados.clear();
    for (int i=0; i<secciones.size(); i++) {
        auto *sec = secciones[i].seccion;
        // La ruta requiere secciones ya aseguradas por otra ruta
        if (!sec->asegurar_posible(this, secciones[i].in, secciones[i].out, secciones[i].dir)) {
            if (msg) log(id, "asegurar imposible", LOG_DEBUG);
            return false;
        }
        // Bloqueo de vía establecido
        if (sec->is_bloqueo_seccion()) {
            if (msg) log(id, "bloqueo seccion", LOG_DEBUG);
            return false;
        }
        for (auto &[nodo,_] : sec->get_deslizamiento()) {
            deslizamientos_afectados[nodo->deslizamiento] = -1;
        }
    }
    if (deslizamiento) deslizamientos_afectados[deslizamiento] = -1;

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
            sig->clear_request = true;
            sig->ruta_activa = this;
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
            auto pos = a->get_posicion(Lado::Impar, pins.first, pins.second);
            if (!a->enclavar(this, a->get_posicion(Lado::Impar, pins.first, pins.second))) {
                agujas_dispuestas = false;
                break;
            }
        }
    }
    if (agujas_dispuestas && !formada) {
        formada = true;
        log(id, "formada", LOG_INFO);
    }
}
void ruta::update()
{
    bool activar_fai = false;
    bool proximidad_ocupada = false;
    bool proximidad_ocupada_fai = false;
    for (auto &[sec, val] : señal_inicio->proximidad_señal.get(tipo)) {
        auto dir = val.first;
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
        if (bloqueo_salida) {
            aut_salida &= !bloqueo_act.cierre_señales[lado_bloqueo] && !bloqueo_act.prohibido[lado_bloqueo] && bloqueo_act.actc[lado_bloqueo] != ACTC::Denegada;
            prioridad = bloqueo_act.prioridad_itinerario[lado_bloqueo]-bloqueo_act.prioridad_itinerario[opp_lado(lado_bloqueo)];
        }
        bool solicitud_fai = false;
        if (tipo_fai == TipoFAI::Proximidad) {
            bool cv_anterior_ocupado = false;
            for (auto &[sec, _] : señal_inicio->proximidad_señal.get(tipo)) {
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
            solicitud_fai = proximidad_ocupada;
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

    movimiento::update();

    if (!mandada) return;

    if (deslizamiento != nullptr) {
        deslizamiento->update();
    }

    // Mandar cierre de PN si la proximidad está ocupada
    if ((proximidad_ocupada || señal_inicio->proximidad_señal.get(tipo).empty()) && señal_inicio->ruta_activa != nullptr) {
        activar_pns();
    }

    // Desenclavar ruta al paso de la circulación
    if ((ocupada || señal_inicio->get_cv_inicio() == nullptr) && !sucesion_automatica) {
        // En maniobra, cerrar señal cuando se libera el CV de señal
        if (tipo == TipoMovimiento::Maniobra && señal_inicio->ruta_activa == this) {
            for (int i=0; i<secciones.size(); i++) {
                if (secciones[i].seccion->get_cv() != nullptr) {
                    if (secciones[i].seccion->get_cv()->get_state() <= EstadoCV::Prenormalizado) {
                        señal_inicio->ruta_activa = nullptr;
                    }
                    break;
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
            for (auto &sig : señales) {
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
    // En maniobra, cerrar señal si se libera el circuito anterior a la señal
    if (tipo == TipoMovimiento::Maniobra && !sucesion_automatica && (ocupada || cv_inicio == nullptr) && ecv.estado <= EstadoCV::Prenormalizado && ecv.estado_previo > EstadoCV::Prenormalizado) {
        bool proximidad_liberada = false;
        if (señal_inicio->proximidad_señal.proximidad0.size() > 0) {
            proximidad_liberada = true;
            bool cambio_proximidad = false;
            for (auto &[sec,_] : señal_inicio->proximidad_señal.proximidad0) {
                if (sec->get_cv()->get_state() > EstadoCV::Prenormalizado) {
                    proximidad_liberada = false;
                    break;
                }
                if (sec->get_cv()->id == id) cambio_proximidad = true;
            }
            if (!cambio_proximidad) proximidad_liberada = false;
        }
        if (proximidad_liberada) señal_inicio->ruta_activa = nullptr;
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
    auto &proximidad = señal_inicio->proximidad_señal.get(tipo);
    auto &proximidad0 = señal_inicio->proximidad_señal.proximidad0;
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
    if ((proximidad_libre && proximidad.size() > 0) || (secciones.size() == 0 && !bloqueo_salida) || (desactivar_diferimetro && !dependencias[estacion]->cerrada)) {
        disolucion_parcial(anular_bloqueo);
    } else if (diferimetro_dai == nullptr) {
        int64_t temporizador = proximidad.size() > proximidad0.size() ? temporizador_dai2 : temporizador_dai1;
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
            if (sec->get_ocupacion(prev, *secciones[i].dir) > EstadoCanton::Prenormalizado) break;
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
    if (deslizamiento) deslizamiento->liberar();
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
        if (sec->get_ocupacion(prev, *secciones[i].dir) > EstadoCanton::Prenormalizado) break;
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
            return RespuestaMando::Aceptado;
        }
    } else if (cmd == "FAI") {
        if (set_fai(true)) return RespuestaMando::Aceptado;
    } else if (cmd == "AFA") {
        if (set_fai(false)) return RespuestaMando::Aceptado;
    }
    return RespuestaMando::OrdenRechazada;
}
