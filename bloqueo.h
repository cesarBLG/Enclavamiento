#pragma once
#include "enums.h"
#include <vector>
#include <map>
#include "lado.h"
#include "mqtt.h"
#include "cv.h"
#include "log.h"
struct estado_bloqueo
{
    EstadoBloqueo estado;
    bool ocupado;
    bool normalizado;
    lados<bool> prohibido;
    lados<bool> escape;
    lados<ACTC> actc;
    lados<bool> cierre_señales;
    lados<TipoMovimiento> ruta;
};
void to_json(json &j, const estado_bloqueo &estado);
void from_json(const json &j, estado_bloqueo &estado);
class bloqueo : estado_bloqueo
{
    std::vector<std::string> cvs;
    std::map<std::string, EstadoCV> estado_cvs;
    lados<bool> maniobra_compatible;

    Lado ultimo_lado;

    bool me_pendiente = false;
public:
    const lados<std::string> estaciones;
    const std::string via;
    const std::string id;
    const std::string topic;
    bloqueo(const json &j) : estaciones(j["Estaciones"]), via(j.value("Vía", "")), cvs(j["CVs"]), id(estaciones[Lado::Impar]+"-"+estaciones[Lado::Par]+(via != "" ? "_" : "")+via), topic("bloqueo/"+id+"/state")
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
    }

    estado_bloqueo get_state()
    {
        return *this;
    }

    void send_state()
    {
        json j = get_state();
        send_message(topic, j.dump(), 1);
        remota_cambio_elemento(ElementoRemota::BLQ, id);
    }

    lados<RemotaBLQ> get_estado_remota()
    {
        lados<RemotaBLQ> estados;
        for (Lado l : {Lado::Impar, Lado::Par}) {
            auto &r = estados[l];
            Lado ol = opp_lado(l);
            r.BLQ_DAT = estado == EstadoBloqueo::SinDatos ? 0 : 1;
            r.BLQ_PROHI_SAL = prohibido[l] ? 1 : 0;
            if (estado == EstadoBloqueo::Desbloqueo) {
                if (escape[l]) r.BLQ_EST_SAL = 5;
                else if (ruta[l] == TipoMovimiento::Itinerario) r.BLQ_EST_SAL = 3;
                else r.BLQ_EST_SAL = 0;
                if (escape[ol]) r.BLQ_EST_ENT = 3;
                else r.BLQ_EST_ENT = 0;
            } else if (estado == (l == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar)) {
                if (ocupado) r.BLQ_EST_SAL = 2;
                else r.BLQ_EST_SAL = 1;
                if (escape[ol]) r.BLQ_EST_ENT = 2;
                else r.BLQ_EST_ENT = 0;
            } else {
                if (escape[l]) r.BLQ_EST_SAL = 4;
                else r.BLQ_EST_SAL = 0;
                r.BLQ_EST_ENT = 1;
            }
            r.BLQ_PROHI_ENT = prohibido[ol] ? 1 : 0;
            r.BLQ_COL_MAN = 0;
            r.BLQ_ACTC = (int)actc[l];
            r.BLQ_CSB = cierre_señales[l] ? 2 : (cierre_señales[ol] ? 1 : 0);
            r.BLQ_NB = normalizado ? 0 : 1;
            r.BLQ_ME = 0;
            r.BLQ_PROX = 0;
        }
        return estados;
    }

    bool establecer(Lado lado)
    {
        if (estado == EstadoBloqueo::Desbloqueo && !ocupado && !escape[Lado::Impar] && !escape[Lado::Par] && !prohibido[lado] && ruta[opp_lado(lado)] != TipoMovimiento::Itinerario && (ruta[opp_lado(lado)] != TipoMovimiento::Maniobra || maniobra_compatible[opp_lado(lado)])) {
            estado = lado == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar;
            ultimo_lado = lado;
            log(id, "establecido", LOG_DEBUG);
            return true;
        }
        return false;
    }

    void update()
    {
        if (ruta[Lado::Impar] == TipoMovimiento::Itinerario) establecer(Lado::Impar);
        if (ruta[Lado::Par] == TipoMovimiento::Itinerario) establecer(Lado::Par);
        send_state();
    }

    void message_cv(const std::string &id, estado_cv ecv)
    {
        int index = -1;
        for (int i=0; i<cvs.size(); i++) {
            if (cvs[i] == id) {
                index = i;
                break;
            }
        }
        if (index < 0) return;
        EstadoCV est_cv = ecv.estado;
        if (estado_cvs[id] == est_cv) return;

        bool ocupado = false;
        bool liberar = false;
        
        bool cvEntradaPar = index == cvs.size()-1;
        bool cvEntradaImpar = index == 0;

        if (est_cv == EstadoCV::Prenormalizado) normalizado = false;

        if (estado == EstadoBloqueo::BloqueoImpar && cvEntradaPar && estado_cvs[id] == EstadoCV::OcupadoImpar && (!ecv.evento || ecv.evento->lado == Lado::Par)) liberar = true;
        else if (estado == EstadoBloqueo::BloqueoPar && cvEntradaImpar && estado_cvs[id] == EstadoCV::OcupadoPar && (!ecv.evento || ecv.evento->lado == Lado::Impar)) liberar = true;
        
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

        estado_cvs[id] = est_cv;
        for (auto &kvp : estado_cvs) {
            if (kvp.second > EstadoCV::Prenormalizado) {
                ocupado = true;
                break;
            }
        }

        liberar = liberar && !ocupado && !cierre_señales[estado == EstadoBloqueo::BloqueoImpar ? Lado::Impar : Lado::Par];
        if (estado == EstadoBloqueo::BloqueoImpar && !ocupado && ruta[Lado::Impar] == TipoMovimiento::Itinerario && !prohibido[Lado::Impar]) liberar = false;
        if (estado == EstadoBloqueo::BloqueoPar && !ocupado && ruta[Lado::Par] == TipoMovimiento::Itinerario && !prohibido[Lado::Par]) liberar = false;
        if (liberar) {
            estado = EstadoBloqueo::Desbloqueo;
            log(this->id, "desbloqueo");
        }
    
        if (ocupado && !this->ocupado) log(this->id, "ocupado");
        this->ocupado = ocupado;
        update();
    }

    RespuestaMando mando(const std::string &est1, const std::string &est2, const std::string &cmd, int me)
    {
        RespuestaMando aceptado = RespuestaMando::OrdenRechazada;
        Lado lado;
        if (est1 == estaciones[Lado::Impar] && est2 == estaciones[Lado::Par]+via) lado = Lado::Impar;
        else if (est2 == estaciones[Lado::Impar]+via && est1 == estaciones[Lado::Par]) lado = Lado::Par;
        else return RespuestaMando::OrdenNoAplicable;
        if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
        bool pend = me_pendiente;
        me_pendiente = false;
        if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        if (cmd == "B") {
            if (establecer(lado)) aceptado = RespuestaMando::Aceptado;
        } else if (cmd == "AB" && !cierre_señales[lado]) {
            if (lado == Lado::Impar && estado == EstadoBloqueo::BloqueoImpar && ruta[Lado::Impar] != TipoMovimiento::Itinerario) {
                estado = EstadoBloqueo::Desbloqueo;
                aceptado = RespuestaMando::Aceptado;
                log(id, "desbloqueo", LOG_DEBUG);
            } else if (lado == Lado::Par && estado == EstadoBloqueo::BloqueoPar && ruta[Lado::Par] != TipoMovimiento::Itinerario) {
                estado = EstadoBloqueo::Desbloqueo;
                aceptado = RespuestaMando::Aceptado;
                log(id, "desbloqueo", LOG_DEBUG);
            }
            if (!ocupado && escape[opp_lado(lado)]) {
                escape[opp_lado(lado)] = false;
                aceptado = RespuestaMando::Aceptado;
                log(id, "escape normalizado", LOG_INFO);
            }
        } else if (cmd == "PB") {
            if (!prohibido[opp_lado(lado)]) {
                prohibido[opp_lado(lado)] = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "APB") {
            if (prohibido[opp_lado(lado)]) {
                if (me == 1) {
                    prohibido[opp_lado(lado)] = false;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    me_pendiente = true;
                    aceptado = RespuestaMando::MandoEspecialNecesario;
                }
            }
        } else if (cmd == "AS" && actc[opp_lado(lado)] == ACTC::Denegada) {
            actc[opp_lado(lado)] = ACTC::Concedida;
            aceptado = RespuestaMando::Aceptado;
        } else if (cmd == "AAS" && actc[opp_lado(lado)] == ACTC::Concedida) {
            actc[opp_lado(lado)] = ACTC::Denegada;
            aceptado = RespuestaMando::Aceptado;
        } else if (cmd == "CSB" && estado == (lado == Lado::Impar ? EstadoBloqueo::BloqueoPar : EstadoBloqueo::BloqueoImpar)) {
            if (!cierre_señales[opp_lado(lado)]) {
                cierre_señales[opp_lado(lado)] = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "NSB") {
            if (cierre_señales[opp_lado(lado)]) {
                if (me == 1) {
                    cierre_señales[opp_lado(lado)] = false;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    return RespuestaMando::MandoEspecialNecesario;
                }
            }
        } else if (cmd == "NB") {
            if (!normalizado && !ocupado) {
                if (me == 1) {
                    normalizado = true;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    return RespuestaMando::MandoEspecialNecesario;
                }
            }
        }
        update();
        return aceptado;
    }

    void message_ruta(const std::string &estacion, TipoMovimiento tipo)
    {
        Lado lado;
        if (estacion == estaciones[Lado::Impar]) lado = Lado::Impar;
        else if (estacion == estaciones[Lado::Par]) lado = Lado::Par;
        else return;
        ruta[lado] = tipo;
        update();
    }
};
