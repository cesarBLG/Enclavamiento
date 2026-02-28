#pragma once
#include <enclavamiento.h>
#include <vector>
#include <map>
#include "remota.h"
#include "topology.h"
#include "mqtt.h"
#include "log.h"
class bloqueo : estado_bloqueo_lado
{
public:
    const std::string estacion;
    const std::string estacion_colateral;
    const std::string via;
    const std::string id;
    const TipoBloqueo tipo;
    const Lado lado;
    const EstadoBloqueo bloqueo_emisor;
    const EstadoBloqueo bloqueo_receptor;
    const std::string topic;
    const std::string topic_colateral;
protected:
    Lado sentido_preferente;
    EstadoBloqueo estado_inicial;
    std::vector<seccion_via*> cvs;
    lados<bool> ocupado;
    std::optional<int64_t> timer_desbloqueo;
    estado_bloqueo_lado colateral;
    estado_bloqueo estado_completo;
    bool me_pendiente;
    bloqueo *bloqueo_vinculado = nullptr;
    bool propagacion_completa = false;
    std::map<TipoSoneria,int64_t> sonerias;
public:
    bloqueo(const std::string &estacion, const json &j);
    void send_state()
    {
        bloqueo_siguiente = bloqueo_vinculado != nullptr;
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
        }
        json j = *((estado_bloqueo_lado*)this);
        send_message(topic_colateral, j.dump());
        j = estado_completo;
        send_message(topic, j.dump());
        remota_cambio_elemento(ElementoRemota::BLQ, id);
    }
    bool bloqueo_permitido(bool propio)
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
        if (estado == EstadoBloqueo::Desbloqueo && colateral.estado == EstadoBloqueo::Desbloqueo && !ocupado[propio ? opp_lado(lado) : lado] && !escape && !colateral.escape) {
            if (propio) {
                return !colateral.prohibido && colateral.ruta != TipoMovimiento::Itinerario && (colateral.ruta != TipoMovimiento::Maniobra || colateral.maniobra_compatible > CompatibilidadManiobra::IncompatibleBloqueo);
            } else {
                if (bloqueo_vinculado != nullptr && bloqueo_vinculado->estado != bloqueo_vinculado->bloqueo_emisor) {
                    if (bloqueo_vinculado->colateral.bloqueo_siguiente || propagacion_completa)
                        return false;
                    else if (bloqueo_vinculado->estado != EstadoBloqueo::Desbloqueo)
                        return false;
                }
                return !prohibido && ruta != TipoMovimiento::Itinerario && (ruta != TipoMovimiento::Maniobra || maniobra_compatible > CompatibilidadManiobra::IncompatibleBloqueo);
            }
        }
        return false;
    }
    bool desbloqueo_permitido()
    {
        // Condiciones para el desbloqueo de un sentido:
        // - No está establecido el itinerario de salida
        // - El trayecto está libre de trenes
        // - No está establecido el cierre de señales por la colateral
        // - Si no existe posibilidad de cruce en la estación emisora (estación cerrada sin agujas talonables),
        //   esta estación no puede ser receptora de otro bloqueo
        if (ruta == TipoMovimiento::Itinerario || colateral.ruta == TipoMovimiento::Itinerario || ocupado.par || ocupado.impar) return false;
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
    void calcular_ocupacion()
    {
        lados<bool> ocupado = {false,false};
        for (auto lado : {Lado::Impar, Lado::Par}) {
            for (auto &est : estado_cvs) {
                if (est.second == EstadoCV::Ocupado || est.second == (lado == Lado::Impar ? EstadoCV::OcupadoImpar : EstadoCV::OcupadoPar)) {
                    ocupado[lado] = true;
                    break;
                }
            }
            for (auto &est : colateral.estado_cvs) {
                if (estado_cvs.find(est.first) != estado_cvs.end()) continue;
                if (est.second == EstadoCV::Ocupado || est.second == (lado == Lado::Impar ? EstadoCV::OcupadoImpar : EstadoCV::OcupadoPar)) {
                    ocupado[lado] = true;
                    break;
                }
            }
        }
        // Desbloquear si se ha producido una secuencia de paso correcta de circulación,
        // pero el trayecto se libera con retardo debido al fallo de detección en algún CV
        if (this->ocupado != ocupado) {
            if (timer_desbloqueo && get_milliseconds() - *timer_desbloqueo < 30000 && desbloqueo_permitido()) {
                timer_desbloqueo = std::nullopt;
                estado_objetivo = EstadoBloqueo::Desbloqueo;
            }
            this->ocupado = ocupado;
            if (ocupado.par || ocupado.impar) log(this->id, "ocupado");
        }
    }
    void update();
    TipoSoneria update_soneria()
    {
        auto it = sonerias.find(TipoSoneria::EstablecimientoBloqueo);
        if (it != sonerias.end() && get_milliseconds() - it->second > 3000) sonerias.erase(it);
        it = sonerias.find(TipoSoneria::OcupacionProximidad);
        if (it != sonerias.end() && get_milliseconds() - it->second > 10000) sonerias.erase(it);
        auto son = TipoSoneria::Apagada;
        for (auto &[s, t] : sonerias) {
            if ((s == TipoSoneria::EstablecimientoBloqueo || s == TipoSoneria::OcupacionProximidad) && mando_estacion.puesto == colateral.mando_estacion.puesto) continue;
            if (s > son) son = s;
        }
        return son;
    }
    void anular()
    {
        if (estado == bloqueo_emisor && desbloqueo_permitido()) {
            estado_objetivo = EstadoBloqueo::Desbloqueo;
            update();
        }
    }
    void cambio_mando(const estado_mando &mando)
    {
        if (!mando.central) actc = ACTC::NoNecesaria; // Paso a ML
        else if (colateral.mando_estacion.central && mando.puesto == colateral.mando_estacion.puesto) actc = ACTC::NoNecesaria; // Ambas estaciones en mismo CTC
        else if (actc == ACTC::NoNecesaria) actc = ACTC::Concedida; // Estaciones en distinto CTC o colateral en ML
        mando_estacion = mando;
        update();
    }
    void set_ruta(TipoMovimiento tipo, CompatibilidadManiobra maniobra_compatible, bool anular_bloqueo)
    {
        if (tipo != ruta || maniobra_compatible != this->maniobra_compatible) {
            ruta = tipo;
            this->maniobra_compatible = maniobra_compatible;
            if (ruta == TipoMovimiento::Ninguno && anular_bloqueo && estado == bloqueo_emisor && desbloqueo_permitido()) estado_objetivo = EstadoBloqueo::Desbloqueo;
            update();
        }
    }
    void message_colateral(estado_bloqueo_lado colateral)
    {
        if (colateral == this->colateral) return;
        if (mando_estacion.central && colateral.mando_estacion != this->colateral.mando_estacion) {
            if (!colateral.mando_estacion.central) { // Colateral a ML
                if (actc == ACTC::NoNecesaria) actc = ACTC::Concedida;
            } else if (colateral.mando_estacion.puesto == mando_estacion.puesto) { // Mismo CTC
                actc = ACTC::NoNecesaria;
            } else { // Distinto CTC
                if (actc == ACTC::NoNecesaria) actc = ACTC::Concedida;
            }
        }
        if (!this->colateral.escape && colateral.escape) {
            sonerias[TipoSoneria::EscapeMaterial] = get_milliseconds();
            if (bloqueo_vinculado != nullptr && bloqueo_vinculado->propagacion_completa) {
                bloqueo_vinculado->escape = true;
                bloqueo_vinculado->update();
            }
        }
        if (!this->colateral.cierre_señales && colateral.cierre_señales) {
            sonerias[TipoSoneria::Averia] = get_milliseconds();
        }
        this->colateral = colateral;
        calcular_ocupacion();
        update();
    }
    RespuestaMando mando(const std::string &cmd, int me)
    {
        RespuestaMando aceptado = RespuestaMando::OrdenRechazada;
        Lado lado;
        if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
        bool pend = me_pendiente;
        me_pendiente = false;
        if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        if (cmd == "B") {
            if (bloqueo_permitido(true)) {
                estado_objetivo = bloqueo_emisor;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "AB") {
            if ((estado == bloqueo_emisor || (estado == EstadoBloqueo::Desbloqueo && estado_objetivo == bloqueo_emisor)) && desbloqueo_permitido()) {
                estado_objetivo = EstadoBloqueo::Desbloqueo;
                aceptado = RespuestaMando::Aceptado;
            }
            if (colateral.escape && desbloqueo_permitido()) {
                normalizar_escape = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "PB") {
            if (!prohibido && tipo != TipoBloqueo::BAD && tipo != TipoBloqueo::BLAD) {
                log(id, "prohibido", LOG_DEBUG);
                prohibido = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "APB") {
            if (prohibido) {
                if (me == 1) {
                    log(id, "anulación prohibición", LOG_DEBUG);
                    prohibido = false;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    me_pendiente = true;
                    aceptado = RespuestaMando::MandoEspecialNecesario;
                }
            }
        } else if (cmd == "AS" && actc == ACTC::Denegada) {
            log(id, "a/ctc concedida", LOG_DEBUG);
            actc = ACTC::Concedida;
            aceptado = RespuestaMando::Aceptado;
        } else if (cmd == "AAS" && actc == ACTC::Concedida) {
            log(id, "a/ctc denegada", LOG_DEBUG);
            actc = ACTC::Denegada;
            aceptado = RespuestaMando::Aceptado;
        } else if (cmd == "CSB" && estado == bloqueo_receptor && ((tipo != TipoBloqueo::BAD && tipo != TipoBloqueo::BLAD) || lado != sentido_preferente)) {
            if (!cierre_señales) {
                log(id, "cierre señales", LOG_DEBUG);
                cierre_señales = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "NSB") {
            if (cierre_señales) {
                if (me == 1) {
                    log(id, "cierre señales normalizado", LOG_DEBUG);
                    cierre_señales = false;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    me_pendiente = true;
                    return RespuestaMando::MandoEspecialNecesario;
                }
            }
        } else if (cmd == "CSP") {
            if (!sonerias.empty()) {
                sonerias.clear();
                return RespuestaMando::Aceptado;
            }
        } /*else if (cmd == "NB") {
            if (!normalizado && !ocupado) {
                if (me == 1) {
                    log(id, "normalizado", LOG_DEBUG);
                    normalizado = true;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    return RespuestaMando::MandoEspecialNecesario;
                }
            }
        }*/
        if (aceptado != RespuestaMando::OrdenRechazada) update();
        return aceptado;
    }

    RemotaBLQ get_estado_remota()
    {
        RemotaBLQ r;
        r.BLQ_DAT = estado == EstadoBloqueo::SinDatos ? 0 : 1;
        r.BLQ_PROHI_SAL = colateral.prohibido ? 1 : 0;
        if (estado == EstadoBloqueo::Desbloqueo) {
            if (escape) r.BLQ_EST_SAL = 5;
            else if (estado_objetivo == bloqueo_emisor) r.BLQ_EST_SAL = 3;
            else r.BLQ_EST_SAL = 0;
            if (colateral.escape) r.BLQ_EST_ENT = 3;
            else r.BLQ_EST_ENT = 0;
        } else if (estado == bloqueo_emisor) {
            /*if (estado_cantones_inicio[l] > EstadoCanton::Prenormalizado) r.BLQ_EST_SAL = 2;
            else */r.BLQ_EST_SAL = 1;
            if (colateral.escape) r.BLQ_EST_ENT = 2;
            else r.BLQ_EST_ENT = 0;
        } else {
            if (escape) r.BLQ_EST_SAL = 4;
            else r.BLQ_EST_SAL = 0;
            r.BLQ_EST_ENT = 1;
        }
        r.BLQ_PROHI_ENT = prohibido ? 1 : 0;
        r.BLQ_COL_MAN = colateral.mando_estacion.central ? 0 : 1;
        r.BLQ_ACTC = (int)colateral.actc;
        r.BLQ_CSB = colateral.cierre_señales ? 2 : (cierre_señales ? 1 : 0);
        r.BLQ_NB = 0;//normalizado ? 0 : 1;
        r.BLQ_ME = 0;
        r.BLQ_PROX = sonerias.find(TipoSoneria::OcupacionProximidad) != sonerias.end();
        return r;
    }

    estado_bloqueo get_estado()
    {
        return estado_completo;
    }
    
    std::vector<seccion_via*> get_cvs()
    {
        return cvs;
    }

    void message_cv(const std::string &id, estado_cv ecv);
    void vincular(const std::string &id, bool propagacion_completa=false);
};
