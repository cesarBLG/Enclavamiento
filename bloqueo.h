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
    std::vector<seccion_via*> cvs;
    bool ocupado;
    estado_bloqueo_lado colateral;
    estado_bloqueo estado_completo;
    bool me_pendiente;
public:
    bloqueo(const std::string &estacion, const json &j);
    void send_state()
    {
        estado_completo.estado = estado;
        estado_completo.ocupado = ocupado;
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
        send_message(topic_colateral, j.dump(), 1);
        j = estado_completo;
        send_message(topic, j.dump(), 1);
        remota_cambio_elemento(ElementoRemota::BLQ, id);
    }
    bool bloqueo_permitido(bool propio)
    {
        if (estado == EstadoBloqueo::Desbloqueo && !ocupado && !escape && !colateral.escape) {
            if (propio) return !colateral.prohibido && colateral.ruta != TipoMovimiento::Itinerario && (colateral.ruta != TipoMovimiento::Maniobra || colateral.maniobra_compatible);
            else return !prohibido && ruta != TipoMovimiento::Itinerario && (ruta != TipoMovimiento::Maniobra || maniobra_compatible);
        }
        return false;
    }
    bool desbloqueo_permitido()
    {
        if (ruta == TipoMovimiento::Itinerario || colateral.ruta == TipoMovimiento::Itinerario || ocupado) return false;
        if (estado == bloqueo_emisor) return !colateral.cierre_señales;
        else if (estado == bloqueo_receptor) return !cierre_señales;
        return true;
    }
    void calcular_ocupacion()
    {
        bool ocupado = false;
        for (auto &est : estado_cvs) {
            if (est.second > EstadoCV::Prenormalizado) {
                ocupado = true;
                break;
            }
        }
        for (auto &est : colateral.estado_cvs) {
            if (est.second > EstadoCV::Prenormalizado) {
                ocupado = true;
                break;
            }
        }
        if (this->ocupado != ocupado) {
            this->ocupado = ocupado;
            if (ocupado) log(this->id, "ocupado");
        }
    }
    void update()
    {
        if (colateral.estado == EstadoBloqueo::SinDatos) {
            estado_objetivo = EstadoBloqueo::Desbloqueo;
            if (colateral.estado_objetivo == colateral.estado && estado != EstadoBloqueo::SinDatos) {
                estado = EstadoBloqueo::SinDatos;
                send_state();
            } else if (colateral.estado_objetivo == EstadoBloqueo::Desbloqueo && estado != EstadoBloqueo::Desbloqueo) {
                estado = EstadoBloqueo::Desbloqueo;
                send_state();
            }
            return;
        }
        if (estado_objetivo != estado && colateral.estado == estado_objetivo) {
            estado = colateral.estado;
            if (estado == EstadoBloqueo::Desbloqueo) log(id, "desbloqueo", LOG_DEBUG);
            else if (estado == bloqueo_emisor) log(id, "establecido emisor", LOG_DEBUG);
            else log(id, "establecido receptor por discrepancia", LOG_DEBUG);
        } else if (colateral.estado != estado) {
            estado = colateral.estado;
            estado_objetivo = colateral.estado;
            log(id, "discrepancia con colateral", LOG_DEBUG);
        }
        if (estado_objetivo != estado && estado_objetivo == EstadoBloqueo::Desbloqueo && !desbloqueo_permitido()) {
            estado_objetivo = estado;
            log(id, "anulación cancelada, desbloqueo no posible", LOG_DEBUG);
        }
        if (estado_objetivo != estado && estado_objetivo == bloqueo_emisor && !bloqueo_permitido(true)) {
            estado_objetivo = estado;
            log(id, "solicitud cancelada, bloqueo no posible", LOG_DEBUG);
        }
        if (normalizar_escape && (!colateral.escape || !desbloqueo_permitido())) {
            normalizar_escape = false;
        }
        if (colateral.normalizar_escape && escape && desbloqueo_permitido()) {
            escape = false;
            log(id, "escape emisor normalizado", LOG_INFO);
        }
        if (colateral.estado_objetivo != estado) {
            if (colateral.estado_objetivo == EstadoBloqueo::Desbloqueo) {
                if (desbloqueo_permitido()) {
                    estado = estado_objetivo = EstadoBloqueo::Desbloqueo;
                }
            } else if (colateral.estado_objetivo == bloqueo_receptor) {
                if (bloqueo_permitido(false)) {
                    estado = estado_objetivo = bloqueo_receptor;
                    log(id, "bloqueo receptor", LOG_DEBUG);
                }
            } else {
                log(id, "discrepancia con colateral", LOG_DEBUG);
            }
        }
        if (ruta == TipoMovimiento::Itinerario && bloqueo_permitido(true)) {
            estado_objetivo = bloqueo_emisor;
        }
        send_state();
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
        if (!mando.central) { // Mando local
            actc = ACTC::NoNecesaria;
        } else if (mando_estacion.central) { // Cambio de CTC
            if (mando == colateral.mando_estacion) actc = ACTC::NoNecesaria; // Ambas estaciones en mismo CTC
            else if (actc == ACTC::NoNecesaria) actc = ACTC::Concedida; // Estaciones en distinto CTC
        } else { // Paso de mando local a central
            actc = ACTC::Concedida;
        }
        mando_estacion = mando;
    }
    void set_ruta(TipoMovimiento tipo, bool maniobra_compatible, bool anular_bloqueo)
    {
        if (tipo != ruta || maniobra_compatible != maniobra_compatible) {
            ruta = tipo;
            this->maniobra_compatible = maniobra_compatible;
            if (ruta == TipoMovimiento::Ninguno && anular_bloqueo && desbloqueo_permitido()) estado_objetivo = EstadoBloqueo::Desbloqueo;
            update();
        }
    }
    void message_colateral(estado_bloqueo_lado colateral)
    {
        if (colateral == this->colateral) return;
        if (mando_estacion.central && colateral.mando_estacion != this->colateral.mando_estacion) {
            if (!colateral.mando_estacion.central) { // Colateral a ML
                if (actc == ACTC::NoNecesaria) actc = ACTC::Concedida;
            } else if (colateral.mando_estacion == mando_estacion) { // Mismo CTC
                actc = ACTC::NoNecesaria;
            } else { // Distinto CTC
                if (actc == ACTC::NoNecesaria) actc = ACTC::Concedida;
            }
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
            if (estado == bloqueo_emisor && desbloqueo_permitido()) {
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
        r.BLQ_PROHI_SAL = prohibido ? 1 : 0;
        if (estado == EstadoBloqueo::Desbloqueo) {
            if (escape) r.BLQ_EST_SAL = 5;
            else if (ruta == TipoMovimiento::Itinerario) r.BLQ_EST_SAL = 3;
            else r.BLQ_EST_SAL = 0;
            if (colateral.escape) r.BLQ_EST_ENT = 3;
            else r.BLQ_EST_ENT = 0;
        } else if (estado == bloqueo_emisor) {
            /*if (estado_cantones_inicio[l] > EstadoCanton::Prenormalizado) r.BLQ_EST_SAL = 2;
            else */r.BLQ_EST_SAL = 1;
            if (colateral.escape) r.BLQ_EST_ENT = 2;
            else r.BLQ_EST_ENT = 0;
        } else {
            if (colateral.escape) r.BLQ_EST_SAL = 4;
            else r.BLQ_EST_SAL = 0;
            r.BLQ_EST_ENT = 1;
        }
        r.BLQ_PROHI_ENT = colateral.prohibido ? 1 : 0;
        r.BLQ_COL_MAN = 0;
        r.BLQ_ACTC = (int)actc;
        r.BLQ_CSB = cierre_señales ? 2 : (colateral.cierre_señales ? 1 : 0);
        r.BLQ_NB = 0;//normalizado ? 0 : 1;
        r.BLQ_ME = 0;
        r.BLQ_PROX = 0;
        return r;
    }

    estado_bloqueo get_estado(Lado lado)
    {
        return estado_completo;
    }

    void message_cv(const std::string &id, estado_cv ecv);
};
