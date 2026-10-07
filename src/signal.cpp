#include "signal.h"
#include "ruta.h"
#include "items.h"
señal::señal(const id_elemento &id, const json &j) : id(id), lado(j["Lado"]), tipo(j["Tipo"]), pin(j.value("Pin", 0)), bloqueo_asociado(j.contains("Bloqueo") ? std::optional<id_elemento>(id_elemento(j["Bloqueo"])) : std::nullopt), seccion(secciones[id_elemento::from_default_dep(j["Sección"], id.dependencia)]), seccion_prev(seccion->get_seccion_in(lado, pin).first), lado_prev(seccion->get_seccion_in(lado, pin).second), señal_virtual(j.value("ERTMS", false))
{
    seccion->vincular_señal(this, lado, pin);
}
señal_impl::señal_impl(const id_elemento &id, const json &j) : señal(id, j), topic("signal/"+id_to_mqtt(id.id)+"/state"), topic_inicio("signal/"+id_to_mqtt(id.id)+"/inicio"), proximidad_señal(this)
{
    if (j.contains("AspectoCanton")) {
        for (auto &[est, asp] : j["AspectoCanton"].items()) {
            aspecto_maximo_ocupacion[json(est)] = asp;
        }
    }
    if (aspecto_maximo_ocupacion.empty())
        aspecto_maximo_ocupacion[parametros.prenormalizacion_libre ? EstadoCanton::Prenormalizado : EstadoCanton::Libre] = tipo == TipoSeñal::Maniobra ? Aspecto::MovimientoAutorizado : (tipo == TipoSeñal::Retroceso ? Aspecto::IndicadoraDirecta : Aspecto::ViaLibre);
    
    if (j.contains("AspectoAnteriorSeñal")) {
        for (auto &[asp1, asp2] : j["AspectoAnteriorSeñal"].items()) {
            aspectos_maximos_anterior_señal[json(asp1)] = asp2;
        }
    }
    if (aspectos_maximos_anterior_señal.empty())
        aspectos_maximos_anterior_señal[Aspecto::ParadaDiferida] = Aspecto::ViaLibre;
    if (aspectos_maximos_anterior_señal.upper_bound(Aspecto::Parada) == aspectos_maximos_anterior_señal.begin()) {
        aspectos_maximos_anterior_señal[Aspecto::Parada] = Aspecto::AnuncioParada;
    }

    if (j.contains("LímiteProximidad")) {
        for (auto &jprox : j["LímiteProximidad"]) {
            proximidad_señal.ultimos_cvs_proximidad.insert(id_elemento::from_default_dep(jprox.get<std::string>(), id.dependencia));
        }
    }
    ruta_necesaria = j.value("RutaNecesaria", tipo != TipoSeñal::Intermedia && tipo != TipoSeñal::Avanzada);
    itinerarios_desviada = j.value("ItinerariosDesviada", false);
    cierre_stick = ruta_necesaria;
    clear_request = !cierre_stick;
    aprec_anterior = parametros.aprec_anterior;
    aspecto_desviada = tipo == TipoSeñal::Retroceso ? Aspecto::IndicadoraDesviada : parametros.aspecto_desviada;

    if (j.contains("Aspectos")) aspectos_disponibles = j["Aspectos"].get<std::set<Aspecto>>();
    if (aspectos_disponibles.empty()) {
        if (tipo == TipoSeñal::Maniobra || tipo == TipoSeñal::Retroceso) {
            aspectos_disponibles.insert(Aspecto::IndicadoraDesviada);
            aspectos_disponibles.insert(Aspecto::IndicadoraDirecta);
            aspectos_disponibles.insert(Aspecto::MovimientoAutorizado);
            aspectos_disponibles.insert(Aspecto::Parada);
            aspectos_disponibles.insert(Aspecto::RebaseAutorizado);
        } else {
            if (tipo == TipoSeñal::Entrada || tipo == TipoSeñal::Salida) {
                aspectos_disponibles.insert(Aspecto::RebaseAutorizadoDestellos);
                aspectos_disponibles.insert(Aspecto::RebaseAutorizado);
            }
            if (tipo == TipoSeñal::Avanzada) {
                aspectos_disponibles.insert(Aspecto::ParadaDiferida);
            }
            aspectos_disponibles.insert(Aspecto::ViaLibre);
            aspectos_disponibles.insert(Aspecto::Precaucion);
            aspectos_disponibles.insert(Aspecto::AnuncioPrecaucion);
            aspectos_disponibles.insert(Aspecto::AnuncioParada);
            aspectos_disponibles.insert(Aspecto::ParadaSelectivaDestellos);
            aspectos_disponibles.insert(Aspecto::ParadaSelectiva);
            aspectos_disponibles.insert(Aspecto::Parada);
        }
    }
    if (j.contains("Focos")) {
        std::string focos = j["Focos"];
        for (int i=0; i<focos.size(); i++) {
            char next = (i+1<focos.size() ? focos[i+1] : ' ');
            switch(focos[i]) {
                case 'V':
                    estado_foco_señal[FocoSeñal::V] = EstadoFocoSeñal::Apagado;
                    break;
                case 'R':
                    estado_foco_señal[FocoSeñal::R] = EstadoFocoSeñal::Apagado;
                    break;
                case 'A':
                    if (next == 'z') {
                        estado_foco_señal[FocoSeñal::Az] = EstadoFocoSeñal::Apagado;
                        i++;
                    } else {
                        estado_foco_señal[FocoSeñal::A] = EstadoFocoSeñal::Apagado;
                    }
                    break;
                case 'B':
                    if (next == 'h') {
                        estado_foco_señal[FocoSeñal::Bh] = EstadoFocoSeñal::Apagado;
                        i++;
                    } else if (next == 'v') {
                        estado_foco_señal[FocoSeñal::Bv] = EstadoFocoSeñal::Apagado;
                        i++;
                    } else if (next == 'c') {
                        estado_foco_señal[FocoSeñal::Bc] = EstadoFocoSeñal::Apagado;
                        i++;
                    } else {
                        estado_foco_señal[FocoSeñal::Bh] = EstadoFocoSeñal::Apagado;
                    }
                    break;
            }
        }
    }
    combinaciones_focos = parametros.combinaciones_focos;
    if (!estado_foco_señal.empty()) {
        for (auto it = aspectos_disponibles.begin(); it != aspectos_disponibles.end();) {
            Aspecto asp = *it;
            auto it2 = combinaciones_focos.find(*it);
            if (it2 == combinaciones_focos.end()) {
                it = aspectos_disponibles.erase(it);
                continue;
            }
            bool aspecto_posible = false;
            for (auto &comb : it2->second) {
                bool foco_disponible = true;
                for (auto &[foco, _] : comb) {
                    if (!estado_foco_señal.contains(foco)) {
                        foco_disponible = false;
                        break;
                    }
                }
                if (foco_disponible) {
                    aspecto_posible = true;
                    break;
                }
            }
            if (!aspecto_posible) it = aspectos_disponibles.erase(it);
            else it++;
        }
    }
    subscribe("signal/"+id_to_mqtt(id.id)+"/rec_aprec");
    subscribe("signal/"+id_to_mqtt(id.id)+"/field_state");
}
void señal_impl::determinar_aspecto()
{
    Aspecto prev_aspecto = aspecto;
    seccion_via* sec_act = seccion;
    seccion_via* sec_prv = seccion_prev;
    Lado dir = lado;
    EstadoCanton canton = EstadoCanton::Libre;
    // Señal siguiente, a continuación del canton
    sig_señal = nullptr;
    // Condiciones que provocan el cierre de señal
    bool cerrar = false;
    // Condiciones que impiden abrir la señal, pero no la cierran si estaba abierta
    bool prohibir_abrir = false;
    // Ruta a desviada
    bool desviada = false;
    // Nombre del bloqueo asociado
    std::optional<id_elemento> bloq_id = bloqueo_asociado;
    bool salida_trayecto = (tipo == TipoSeñal::Salida || tipo == TipoSeñal::Entrada) && bloq_id;
    if (ruta_activa != nullptr && ruta_activa->es_ruta && ((ruta*)ruta_activa)->bloqueo_salida) {
        auto &señales = ruta_activa->get_señales();
        if (señales.empty() || señales.back() == this) {
            if (!bloq_id) bloq_id = ((ruta*)ruta_activa)->bloqueo_salida;
            salida_trayecto = true;
        }
    }
    if (ruta_activa != nullptr && ruta_activa->es_ruta && !((ruta*)ruta_activa)->deslizamiento_asegurado())
        cerrar = true;
    // Comprobamos todos los CVs hasta la señal siguiente o fin de movimiento
    while (sec_act != nullptr && sig_señal == nullptr) {
        bool seccion_asegurada = ruta_activa != nullptr && sec_act->is_asegurada(ruta_activa);
        if (!seccion_asegurada && ruta_activa != nullptr && ruta_activa->tipo == TipoMovimiento::Maniobra) {
            break;
        }
        EstadoCanton sec_ocup = sec_act->get_ocupacion(sec_prv, dir);
        canton = std::max(sec_ocup, canton);
        if (sec_act->get_cv() != nullptr && sec_act->get_cv()->is_btv()) prohibir_abrir = true;
        Lado l = dir;
        seccion_via *next = sec_act->siguiente_seccion(sec_prv, dir);
        for (auto *pn : sec_act->pns) {
            if (!pn->is_protegido()) {
                if (pn->get_tipo(l) != TipoPN::Automatico)
                    cerrar = true;
            }
        }
        if ((sec_prv != nullptr && !sec_act->transitable(sec_prv, l)) || (sec_prv == nullptr && !sec_act->transitable(pin, l)))
            cerrar = true;
        if (seccion_asegurada) {
            if (ruta_activa->get_ocupacion_maxima_secciones().find(sec_act)->second < sec_ocup && (ruta_activa->tipo != TipoMovimiento::Maniobra || sec_act != seccion || (sec_ocup == EstadoCanton::Ocupado && sec_act->get_cv()->ocupacion_intempestiva)))
                cerrar = true;
            if (sec_act->is_bloqueo_seccion())
                prohibir_abrir = true;
        }
        for (auto &[d, r] : sec_act->get_deslizamiento()) {
            if (!d->asegurado && !d->acceso_impedido)
                cerrar = true;
        }
        if (sec_act->is_asegurada() && !seccion_asegurada) cerrar = true;
        if (sec_act->bloqueo_asociado) {
            if (!bloq_id) bloq_id = sec_act->bloqueo_asociado;
            if (tipo == TipoSeñal::Salida || tipo == TipoSeñal::Entrada) salida_trayecto = true;
        }
        desviada |= sec_act->is_desviada(sec_prv, dir);
        sec_prv = sec_act;
        sec_act = next;
        if (sec_act != nullptr) sig_señal = sec_act->señal_inicio(dir, sec_prv);
        if (sec_act == seccion) break;
    }
    // Condiciones que provocan el cierre de la señal en ruta de itinerario
    bool cerrar_itinerario = false;
    // Condiciones que impiden la apertura de señal en itinerario, pero no la cierran si estaba abierta
    bool prohibir_abrir_itinerario = false;
    if (bloq_id) {
        bloqueo_asociado_obj = bloqueos[*bloq_id];
        auto bloqueo_act = bloqueo_asociado_obj->get_estado();
        TipoMovimiento tipo_opp = bloqueo_act.ruta[opp_lado(dir)];
        // Cerrar señales intermedias y de salida si falla comunicación con colateral
        cerrar |= bloqueo_act.estado == EstadoBloqueo::SinDatos;
        // Cerrar señal avanzada si está establecido el itinerario o maniobra de salida
        cerrar |= (tipo_opp == TipoMovimiento::Itinerario || (tipo_opp == TipoMovimiento::Maniobra && bloqueos[*bloq_id]->deslizamiento_bloqueo)) && tipo == TipoSeñal::Avanzada;
        // Cerrar señales intermedias y de salida si hay escape de material en sentido contrario
        cerrar |= bloqueo_act.escape[opp_lado(dir)];
        // Impedir maniobra de salida en caso de escape de material propio, salvo que la maniobra sea compatible con bloqueo receptor
        cerrar |= bloqueo_act.escape[lado] && ruta_activa != nullptr && (ruta_activa->maniobra_compatible <= CompatibilidadManiobra::IncompatibleBloqueo || ruta_activa->tipo != TipoMovimiento::Maniobra);
        // Cerrar maniobra de salida si no se pueden hacer maniobras simultáneas en ambas estaciones
        cerrar |= tipo_opp == TipoMovimiento::Maniobra && (bloqueo_act.maniobra_compatible[opp_lado(dir)] == CompatibilidadManiobra::Incompatible || bloqueo_act.maniobra_compatible[opp_lado(dir)] == CompatibilidadManiobra::IncompatibleMovimiento);
        // Cerrar señales intermedias y de salida si se establece el cierre de señales de bloqueo
        cerrar |= bloqueo_act.cierre_señales[dir];
        // Cerrar señales intermedias y de salida si no está establecido el bloqueo en ese sentido
        // Permitir apertura en desbloqueo de la señal de salida en estaciones cerradas
        // Las señales avanzadas abren según el aspecto de la señal de entrada
        // Las pantallas virtuales no abren sin bloqueo establecido, pero la condición de cierre se establece más adelante
        cerrar_itinerario |= bloqueo_act.estado != (dir == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar) && tipo != TipoSeñal::Avanzada && !señal_virtual && (tipo == TipoSeñal::Intermedia || !abierta_desbloqueo || (bloqueo_act.estado != EstadoBloqueo::Desbloqueo && !abierta_bloqueo_receptor) || tipo_opp == TipoMovimiento::Itinerario);
        // Cerrar el itinerario de salida si la estación colateral está realizando maniobras,
        // y no hay señales intermedias suficientes que puedan proteger la maniobra
        cerrar_itinerario |= tipo_opp == TipoMovimiento::Maniobra && bloqueo_act.maniobra_compatible[opp_lado(dir)] < CompatibilidadManiobra::Compatible;
        // No permitir la apertura en itinerario de la señal de salida con bloqueo prohibido o A/CTC denegada
        prohibir_abrir_itinerario |= salida_trayecto && (bloqueo_act.prohibido[dir] || bloqueo_act.actc[dir] == ACTC::Denegada || (!ruta_necesaria && bloqueo_act.prioridad_itinerario[dir] < bloqueo_act.prioridad_itinerario[opp_lado(dir)]));
    } else {
        bloqueo_asociado_obj = nullptr;
    }
    // Cerrar señal con el cantón ocupado en sentido contrario
    cerrar_itinerario |= canton == EstadoCanton::Ocupado;
    cerrar |= sig_señal != nullptr && sig_señal->aspecto_maximo_anterior_señal <= Aspecto::Parada;
    cerrar_itinerario |= sig_señal != nullptr && sig_señal->aspecto_maximo_anterior_señal <= Aspecto::RebaseAutorizadoDestellos;
    desviada |= sig_señal != nullptr && sig_señal->desviada;
    // Señal en parada si
    // - No se permite la apertura y no había abierto previamente
    // - Las condiciones no permiten mantener abierta la señal
    // - Se ha mandado el cierre de señal
    // - La señal es de inicio de ruta y la ruta no está asegurada o está en proceso de disolución
    if ((prohibir_abrir && prev_aspecto <= Aspecto::Parada) || 
        cerrar || !clear_request || aspecto_bloqueado <= Aspecto::Parada ||
        (ruta_necesaria && (ruta_activa == nullptr || !ruta_activa->is_formada()))) {
        aspecto = Aspecto::Parada;
    } else if (ruta_activa != nullptr && ruta_activa->tipo == TipoMovimiento::Maniobra) {
        aspecto = Aspecto::RebaseAutorizado;
    // Señal en parada si no se cumplen las condiciones para apertura en itinerario
    } else if (cerrar_itinerario || (prohibir_abrir_itinerario && (prev_aspecto <= Aspecto::Parada || !ruta_necesaria))) {
        aspecto = Aspecto::Parada;
    } else {
        // Permitir o no la apertura con cantón ocupado en el mismo sentido, o en prenormalización
        // En rebase, abrir señal incluso con cantón ocupado
        bool rebase = ruta_activa != nullptr && ruta_activa->tipo == TipoMovimiento::Rebase;
        auto it = aspecto_maximo_ocupacion.lower_bound(rebase ? EstadoCanton::Libre : canton);
        aspecto = it == aspecto_maximo_ocupacion.end() ? Aspecto::Parada : it->second;
        if (rebase && aspecto > Aspecto::RebaseAutorizadoDestellos) aspecto = Aspecto::RebaseAutorizadoDestellos;
        // Itinerarios ERTMS pueden abrir como máximo en parada selectiva
        if (ruta_activa != nullptr && ruta_activa->ertms && aspecto > Aspecto::ParadaSelectivaDestellos) aspecto = Aspecto::ParadaSelectivaDestellos;
        // Aspecto máximo permitido para cumplir las órdenes de la señal siguiente
        if (sig_señal != nullptr) aspecto = std::min(aspecto, sig_señal->aspecto_maximo_anterior_señal);
        // Itinerarios por vía desviada
        if (desviada) {
            bool fin_itinerario;
            if (ruta_fin != nullptr && ruta_fin->tipo == TipoMovimiento::Maniobra)
                fin_itinerario = false;
            else if (ruta_fin == nullptr && (seccion_prev == nullptr || !seccion_prev->is_trayecto()))
                fin_itinerario = false;
            else
                fin_itinerario = true;
            if (aspecto > aspecto_desviada) {
                if (!aprec_anterior) {
                    if (!itinerarios_desviada)
                        aspecto = aspecto_desviada;
                } else if (itinerarios_desviada) {
                    // Señal en vía de apartado desde la que todos los itinerarios existentes son a vía desviada
                    if (!is_aspecto_disponible(aspecto_desviada)) {
                        // Itinerarios sin anuncio de parada: abrir sin esperar al reconocimiento
                        if (fin_itinerario && (aprec_anterior_reconocido == ReconocimientoAnuncioPrecaucion::Inactivo || aprec_anterior_reconocido == ReconocimientoAnuncioPrecaucion::PerdidaReconocimiento))
                            aspecto = aspecto_desviada;
                    } else if ((fin_itinerario && aprec_anterior_reconocido != ReconocimientoAnuncioPrecaucion::Reconocido) || aprec_anterior_reconocido == ReconocimientoAnuncioPrecaucion::PerdidaReconocimiento) {
                        aspecto = aspecto_desviada;
                    }
                } else {
                    // Resto de casos
                    if (aspecto == Aspecto::AnuncioPrecaucion) {
                        if ((fin_itinerario && aprec_anterior_reconocido != ReconocimientoAnuncioPrecaucion::Reconocido) || aprec_anterior_reconocido == ReconocimientoAnuncioPrecaucion::PerdidaReconocimiento)
                            aspecto = aspecto_desviada;
                    /*
                    } else if (aspecto == Aspecto::PreanuncioParada) {
                        if (!fin_itinerario || aprec_anterior_reconocido != ReconocimientoAnuncioPrecaucion::Reconocido)
                            aspecto = aspecto_desviada;
                    */
                    } else {
                        aspecto = aspecto_desviada;
                    }
                }
            }
        }
    }
    if (aspecto > aspecto_bloqueado) aspecto = aspecto_bloqueado;

    while (!is_aspecto_disponible(aspecto)) {
        Aspecto asp = get_aspecto_degradado(aspecto);
        if (asp == aspecto) {
            if (tipo == TipoSeñal::Avanzada) {
                aspecto = Aspecto::AnuncioParada;
            }
            break;
        }
        aspecto = asp;
    }

    determinar_focos();

    // Requerir pantallas virtuales en parada sin bloqueo establecido
    // Se define variable aspecto_virtual para determinar el aspecto de la señal anterior
    // Esto permite que la señal avanzada abra en función de la señal de entrada aunque
    // las pantallas estén cerradas por no haber bloqueo
    // Si la pantalla está cerrada por otro motivo, la avanzada mostrará parada selectiva
    Aspecto aspecto_virtual = aspecto;
    if (señal_virtual && bloqueo_asociado_obj != nullptr && bloqueo_asociado_obj->get_estado().estado != (dir == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar))
        aspecto = Aspecto::Parada;

    // Indicar a la señal anterior el aspecto máximo que puede mostrar
    aspecto_maximo_anterior_señal = (--aspectos_maximos_anterior_señal.upper_bound(aspecto_virtual))->second;
    if ((tipo == TipoSeñal::Intermedia || tipo == TipoSeñal::Avanzada) && apagada && aspecto == Aspecto::Parada) aspecto_maximo_anterior_señal = Aspecto::Parada;
    
    if (frontera_salida != nullptr) {
        aspecto_maximo_anterior_señal = std::min(aspecto_maximo_anterior_señal, aspecto);
    }
    // En caso de ruta a desviada, mostrar anuncio de precaución en señal anterior
    if (desviada && aprec_anterior && tipo != TipoSeñal::Maniobra && tipo != TipoSeñal::Retroceso && !señal_virtual)
        aspecto_maximo_anterior_señal = std::min(aspecto_maximo_anterior_señal, Aspecto::AnuncioPrecaucion);

    // En caso de pantallas cerradas, las señal anterior puede ordenar como máximo parada selectiva
    // Además, las pantallas virtuales propagan el aspecto máximo de apertura requerido por la siguiente señal luminosa
    if (señal_virtual && aspecto_maximo_anterior_señal > Aspecto::ParadaSelectiva) {
        if (aspecto <= Aspecto::Parada)
            aspecto_maximo_anterior_señal = Aspecto::ParadaSelectiva;
        if (sig_señal != nullptr)
            aspecto_maximo_anterior_señal = std::min(aspecto_maximo_anterior_señal, sig_señal->aspecto_maximo_anterior_señal);
    }
    if ((tipo == TipoSeñal::Maniobra || tipo == TipoSeñal::Retroceso) && sig_señal != nullptr) aspecto_maximo_anterior_señal = std::min(aspecto_maximo_anterior_señal, sig_señal->aspecto_maximo_anterior_señal);
    if (tipo == TipoSeñal::Maniobra || tipo == TipoSeñal::Retroceso || señal_virtual) this->desviada = desviada;
}
void señal_impl::determinar_focos()
{
    if (estado_foco_señal.empty()) {
        focos_mandados_mask = 0;
        return;
    }
    bool bloquear = false;
    std::map<FocoSeñal, EstadoFocoSeñal> *combinacion = nullptr;
    while (!combinacion) {
        auto it = combinaciones_focos.find(aspecto);
        if (it != combinaciones_focos.end() && !it->second.empty()) {
            // Probar una combinación que no tenga focos fundidos
            for (auto &comb : it->second) {
                bool disponible = true;
                for (auto &[foco, estado] : comb) {
                    if (!estado_foco_señal.contains(foco) || estado_foco_señal[foco] == EstadoFocoSeñal::Fundido) {
                        disponible = false;
                        break;
                    }
                }
                if (disponible) {
                    combinacion = &comb;
                    break;
                }
            }
        }
        if (combinacion) break;

        Aspecto prev_aspecto = aspecto;
        do {
            Aspecto asp = get_aspecto_degradado(aspecto);
            // Si no hay ningún aspecto válido, mantener el aspecto fundido y apagar la señal
            if (asp == aspecto) {
                aspecto = prev_aspecto;
                break;
            }
            aspecto = asp;
        } while (!is_aspecto_disponible(aspecto));
        if (aspecto == prev_aspecto) break;

        // bloquear = true;
    }
    if (bloquear) aspecto_bloqueado = aspecto;
    apagada = false;
    if (!combinacion) {
        apagada = true;

        if (is_aspecto_disponible(aspecto)) {
            // Mandar la primera combinación disponible para que se encienda la señal en cuanto se reponga la lámpara
            for (auto &comb : combinaciones_focos[aspecto]) {
                bool disponible = true;
                for (auto &[foco, estado] : comb) {
                    if (!estado_foco_señal.contains(foco)) {
                        disponible = false;
                        break;
                    }
                }
                if (disponible) {
                    combinacion = &comb;
                    break;
                }
            }
        }
    }
    unsigned int mask = 0;
    if (combinacion != nullptr) {
        for (auto &[foco, estado] : *combinacion) {
            mask |= ((unsigned int)estado)<<(2*(int)foco);
        }
    }
    if (mask != focos_mandados_mask) {
        focos_mandados_mask = mask;
        ultimo_cambio_focos = get_milliseconds();
    }
    if (combinacion != nullptr && get_milliseconds() - ultimo_cambio_focos > 15000) {
        for (auto &[foco, estado] : *combinacion) {
            if (estado_foco_señal[foco] == EstadoFocoSeñal::Apagado) {
                estado_foco_señal[foco] = EstadoFocoSeñal::Fundido;
            }
        }
    }
}
void señal_impl::update()
{
    unsigned int prev_mask = focos_mandados_mask;
    Aspecto prev_aspecto = aspecto;
    estado_inicio_ruta prev_estado_inicio = estado_inicio;

    proximidad_señal.construir();

    determinar_aspecto();

    // Gestionar reconocimiento de anuncio de precaución
    if (aspecto <= Aspecto::Parada)
        aprec_anterior_reconocido = ReconocimientoAnuncioPrecaucion::Inactivo;
    else if (aspecto < prev_aspecto && aprec_anterior_reconocido != ReconocimientoAnuncioPrecaucion::Inactivo)
        aprec_anterior_reconocido = ReconocimientoAnuncioPrecaucion::PerdidaReconocimiento;

    ReconocimientoAnuncioPrecaucion prev_rec = aprec_reconocido;
    if (aspecto == Aspecto::AnuncioPrecaucion) {
        if (aprec_reconocido != ReconocimientoAnuncioPrecaucion::Reconocido) {
            aprec_reconocido = ReconocimientoAnuncioPrecaucion::NoReconocido;
            if (prev_aspecto != aspecto) {
                inicio_aprec = get_milliseconds();
                if (prev_aspecto <= Aspecto::Parada) {
                    aprec_reconocido = ReconocimientoAnuncioPrecaucion::Reconocido;
                }
            }
            if (get_milliseconds() - inicio_aprec > 10000) {
                aprec_reconocido = ReconocimientoAnuncioPrecaucion::Reconocido;
                inicio_aprec = get_milliseconds();
            }
        }
    } else if (aprec_reconocido == ReconocimientoAnuncioPrecaucion::Reconocido) {
        if (aspecto > Aspecto::Parada || !paso_circulacion)
            aprec_reconocido = ReconocimientoAnuncioPrecaucion::PerdidaReconocimiento;
        else
            aprec_reconocido = ReconocimientoAnuncioPrecaucion::Inactivo;
    } else if (aprec_reconocido == ReconocimientoAnuncioPrecaucion::NoReconocido) {
        aprec_reconocido = ReconocimientoAnuncioPrecaucion::PerdidaReconocimiento;
    }
    if (señal_siguiente_aprec == nullptr && sig_señal != nullptr) señal_siguiente_aprec = sig_señal;
    if (señal_siguiente_aprec != nullptr && sig_señal != señal_siguiente_aprec) {
        if (aprec_reconocido == ReconocimientoAnuncioPrecaucion::Reconocido || aprec_reconocido == ReconocimientoAnuncioPrecaucion::NoReconocido)
            aprec_reconocido = ReconocimientoAnuncioPrecaucion::PerdidaReconocimiento;
    }
    if (prev_rec != aprec_reconocido && señal_siguiente_aprec != nullptr && aprec_reconocido != ReconocimientoAnuncioPrecaucion::Inactivo) {
        log(id, "anuncio de precaución " + to_string(aprec_reconocido), LOG_INFO);
        send_message("signal/"+id_to_mqtt(señal_siguiente_aprec->id.id)+"/rec_aprec", json(aprec_reconocido).dump());
    }
    señal_siguiente_aprec = sig_señal;

    if (clear_request && ruta_necesaria && ruta_activa == nullptr) clear_request = false;
    if (aspecto < prev_aspecto && !paso_circulacion) {
        // Si la señal cierra en stick, es necesario volver a mandar la ruta para que vuelva a abrir
        if (cierre_stick) aspecto_bloqueado = aspecto;
    }
    if (aspecto_bloqueado < Aspecto::ViaLibre && aspecto < aspecto_bloqueado) aspecto_bloqueado = Aspecto::ViaLibre;

    if (aspecto > Aspecto::Parada) {
        rebasada = false;
        ultimo_paso_abierta = get_milliseconds();
    }
    estado_inicio = get_estado_inicio();

    paso_circulacion = false;

    send_state(aspecto != prev_aspecto || prev_mask != focos_mandados_mask, estado_inicio != prev_estado_inicio);
}
bool señal_impl::is_aspecto_disponible(Aspecto asp)
{
    if (!aspectos_disponibles.empty() && !aspectos_disponibles.contains(asp)) return false;
    bool bla = bloqueo_asociado_obj != nullptr && (bloqueo_asociado_obj->tipo == TipoBloqueo::BLAU || bloqueo_asociado_obj->tipo == TipoBloqueo::BLAD || bloqueo_asociado_obj->tipo == TipoBloqueo::BLAB);
    if (asp == Aspecto::AnuncioParada && tipo != TipoSeñal::Avanzada && bla)
        return false;
    if (asp == Aspecto::Parada && tipo == TipoSeñal::Avanzada && bla)
        return false;
    return true;
}
bool señal_impl::normalizar_fusion()
{
    bool normalizada = false;
    for (auto &[foco, estado] : estado_foco_señal) {
        if (estado == EstadoFocoSeñal::Fundido) {
            estado = EstadoFocoSeñal::Apagado;
            normalizada = true;
        }
    }
    return normalizada;
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
    } else if (cmd == "NPS" && !ruta_necesaria) {
        if (!clear_request || aspecto_bloqueado < Aspecto::ViaLibre || normalizar_fusion()) {
            log(id, "normalizar señal", LOG_DEBUG);
            clear_request = true;
            aspecto_bloqueado = Aspecto::ViaLibre;
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
        if (ruta_activa != nullptr && ruta_activa->es_ruta && ((ruta*)ruta_activa)->get_señal_inicio() == this && ((ruta*)ruta_activa)->dai(cmd == "DAB")) aceptado = true;
        return aceptado ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
    } else if (cmd == "AFA") {
        if (ruta_fai != nullptr) {
            return ruta_fai->mando(ruta_fai->id_inicio, ruta_fai->id_destino, cmd);
        }
    }
    return RespuestaMando::OrdenRechazada;
}
#define SIG_FOCO(x) estado_foco_señal.contains(x) ? (estado_foco_señal[x] == EstadoFocoSeñal::Fundido ? 3 : 1) : 0
RemotaSIG señal_impl::get_estado_remota_sig()
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
        case Aspecto::IndicadoraDesviada:
        case Aspecto::IndicadoraDirecta:
        case Aspecto::MovimientoAutorizado:
            r.SIG_IND = 6;
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
    if (apagada) r.SIG_IND = 0;
    r.SIG_FOCO_R = SIG_FOCO(FocoSeñal::R);
    r.SIG_FOCO_BL_C = SIG_FOCO(FocoSeñal::Bc);
    r.SIG_FOCO_BL_V = SIG_FOCO(FocoSeñal::Bv);
    r.SIG_FOCO_BL_H = SIG_FOCO(FocoSeñal::Bh);
    r.SIG_FOCO_AZ = SIG_FOCO(FocoSeñal::Az);
    r.SIG_FOCO_AM = SIG_FOCO(FocoSeñal::A);
    r.SIG_FOCO_V = SIG_FOCO(FocoSeñal::V);
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
    return r;
}
RemotaIMV señal_impl::get_estado_remota_imv()
{
    RemotaIMV i;
    if (ruta_activa != nullptr && ruta_activa->es_ruta && ((ruta*)ruta_activa)->get_señal_inicio() == this) {
        i = ((ruta*)ruta_activa)->get_estado_remota_inicio();
    } else {
        i.IMV_DAT = 1;
        i.IMV_DIF_VAL = 0;
        i.IMV_EST = rebasada ? 7 : 0;
    }
    return i;
}
RemotaPV señal_impl::get_estado_remota_pv()
{
    RemotaPV r;
    r.PV_DAT = 1;
    r.PV_DAT = aspecto == Aspecto::Parada ? 0 : 1;
    r.PV_CS_IND = clear_request ? 0 : 1;
    return r;
}
estado_inicio_ruta señal_impl::get_estado_inicio()
{
    estado_inicio_ruta e;
    if (ruta_activa != nullptr && ruta_activa->es_ruta && ((ruta*)ruta_activa)->get_señal_inicio() == this) {
        e = ((ruta*)ruta_activa)->get_estado_inicio();
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
void señal_impl::message_cv(const id_elemento &id, estado_cv ev)
{
    cv *cv_inicio = get_cv_inicio();
    if (cv_inicio == nullptr || cv_inicio->id != id) return;

    // Detección de paso de tren por la señal
    paso_circulacion = false;
    if (ev.is_ocupacion(lado)) {
        // Con el paso de la circulación se cierra la señal, salvo en maniobras
        if (ruta_activa != nullptr && ruta_activa->tipo != TipoMovimiento::Maniobra && (!sucesion_automatica || ruta_activa->tipo != TipoMovimiento::Itinerario) && (aspecto > Aspecto::Parada || get_milliseconds() - ultimo_paso_abierta > 30000)) {
            for (auto &sig : ruta_activa->get_señales()) {
                if (sig == this) {
                    ruta_activa = nullptr;
                    break;
                }
                if (sig->ruta_activa == ruta_activa) break;
            }
        }
        if (ev.evento && (ev.evento->seccion.id == "" || (ev.evento->seccion == seccion->id && ev.evento->pin == pin))) {
            // Si la señal estaba cerrada, es un rebase de señal
            if (aspecto <= Aspecto::Parada) {
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
}
cv* señal_impl::get_cv_inicio()
{
    auto *sec = seccion;
    auto *prv = seccion_prev;
    Lado dir = lado;
    while (sec != nullptr) {
        if (sec->get_cv() != nullptr) return sec->get_cv();
        auto next = sec->siguiente_seccion(prv, dir, false);
        prv = sec;
        sec = next;
    }
    return nullptr;
}
void proximidad::construir0(seccion_via *next, seccion_via *sec, Lado dir)
{
    if (sec == nullptr) return;
    if (sec->get_cv() != nullptr) {
        proximidad0[sec] = {dir, next};
    } else {
        std::vector<std::pair<seccion_via *, Lado>> prev;
        sec->prev_secciones(next, dir, prev, true);
        for (auto &[sec2, dir2] : prev) {
            if (sec->señal_inicio(dir, sec2) != nullptr) continue;
            construir0(sec, sec2, dir2);
        }
    }
}
void proximidad::construir()
{
    proximidad0.clear();
    proximidad1.clear();
    construir0(señal_inicio->seccion, señal_inicio->seccion_prev, señal_inicio->lado_prev);
    for (auto &[sec, props] : proximidad0) {
        auto &[dir, next] = props;
        seccion_via *act = sec;
        ruta *ruta_actual = nullptr;
        bool trayecto = false;
        señal *sig = señal_inicio;
        while (act != nullptr) {

            Lado dir_opp = opp_lado(dir);
            auto prev = act->siguiente_seccion(next, dir_opp, true);
            if (prev == nullptr) prev = act->siguiente_seccion(next, dir_opp);

            if (sig != nullptr) {
                if (sig != señal_inicio && sig->aspecto <= Aspecto::Parada) break;
                auto sig_impl = señal_impls.find(sig->id);
                if (sig_impl == señal_impls.end() || (prev != nullptr && prev->is_trayecto())) {
                    ruta_actual = nullptr;
                    trayecto = true;
                } else if (ruta_actual == nullptr || ruta_actual->get_señal_inicio() == sig_impl->second) {
                    ruta_actual = sig_impl->second->ruta_fin;
                    trayecto = false;
                    if (ruta_actual != nullptr && ruta_actual->tipo != TipoMovimiento::Itinerario) ruta_actual = nullptr;
                }
            }

            if (act->get_cv() != nullptr) proximidad1[act] = {dir, next};

            bool afecta_anteriores = trayecto || (ruta_actual != nullptr && act->is_asegurada(ruta_actual));
            if (!afecta_anteriores || ultimos_cvs_proximidad.empty() || ultimos_cvs_proximidad.find(act->id_cv) != ultimos_cvs_proximidad.end()) {
                break;
            }

            dir = opp_lado(dir_opp);
            sig = act->señal_inicio(dir, prev);
            next = act;
            act = prev;
        }
    }
}
