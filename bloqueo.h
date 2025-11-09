#pragma once
#include "enums.h"
#include <vector>
#include <map>
#include "lado.h"
#include "mqtt.h"
#include "cv.h"
#include "log.h"
#include "mando.h"
struct estado_bloqueo
{
    EstadoBloqueo estado;
    bool ocupado;
    bool normalizado;
    lados<bool> prohibido;
    lados<bool> escape;
    lados<ACTC> actc;
    lados<bool> cierre_señales;
    lados<EstadoCanton> estado_cantones_inicio;
    lados<TipoMovimiento> ruta;
    lados<bool> maniobra_compatible;
    lados<estado_mando> mando_estacion;
};
struct estado_colateral_bloqueo
{
    bool sin_datos = false;
    TipoMovimiento ruta;
    bool anular_bloqueo;
    bool maniobra_compatible;
    estado_mando mando;
    estado_colateral_bloqueo() {}
    estado_colateral_bloqueo(TipoMovimiento ruta, bool maniobra_compatible, estado_mando mando) : ruta(ruta), maniobra_compatible(maniobra_compatible), mando(mando)
    {
        anular_bloqueo = false;
    }
    bool operator==(const estado_colateral_bloqueo &o) const
    {
        return sin_datos == o.sin_datos && ruta == o.ruta && anular_bloqueo == o.anular_bloqueo && maniobra_compatible == o.maniobra_compatible && mando == o.mando;
    }
};
void to_json(json &j, const estado_bloqueo &estado);
void from_json(const json &j, estado_bloqueo &estado);
void to_json(json &j, const estado_colateral_bloqueo &col);
void from_json(const json &j, estado_colateral_bloqueo &col);
class bloqueo : estado_bloqueo
{
    std::vector<seccion_via*> cvs;

    Lado ultimo_lado;

    bool me_pendiente = false;
    lados<bool> datos_estaciones;
public:
    const lados<std::string> estaciones;
    const std::string via;
    const std::string id;
    const std::string topic;
    bloqueo(const json &j);

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
                if (estado_cantones_inicio[l] > EstadoCanton::Prenormalizado) r.BLQ_EST_SAL = 2;
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

    bool desbloquear(Lado lado, bool normalizar_escape=true)
    {
        bool aceptado = false;
        if (ruta[lado] == TipoMovimiento::Ninguno && !ocupado && !cierre_señales[lado]) {
            if (escape[opp_lado(lado)]) {
                escape[opp_lado(lado)] = false;
                aceptado = true;
                log(id, "escape normalizado", LOG_INFO);
            }
            if (estado == (lado == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar)) {
                estado = EstadoBloqueo::Desbloqueo;
                log(id, "desbloqueo", LOG_DEBUG);
                aceptado = true;
            }
        }
        return aceptado;
    }

    void update()
    {
        if (ruta[Lado::Impar] == TipoMovimiento::Itinerario) establecer(Lado::Impar);
        if (ruta[Lado::Par] == TipoMovimiento::Itinerario) establecer(Lado::Par);
        send_state();
    }

    void message_cv(const std::string &id, estado_cv ecv);

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
        } else if (cmd == "AB") {
            if (desbloquear(lado))  aceptado = RespuestaMando::Aceptado;
        } else if (cmd == "PB") {
            if (!prohibido[opp_lado(lado)]) {
                log(id, "prohibido", LOG_DEBUG);
                prohibido[opp_lado(lado)] = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "APB") {
            if (prohibido[opp_lado(lado)]) {
                if (me == 1) {
                    log(id, "anulación prohibición", LOG_DEBUG);
                    prohibido[opp_lado(lado)] = false;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    me_pendiente = true;
                    aceptado = RespuestaMando::MandoEspecialNecesario;
                }
            }
        } else if (cmd == "AS" && actc[opp_lado(lado)] == ACTC::Denegada) {
            log(id, "a/ctc concedida", LOG_DEBUG);
            actc[opp_lado(lado)] = ACTC::Concedida;
            aceptado = RespuestaMando::Aceptado;
        } else if (cmd == "AAS" && actc[opp_lado(lado)] == ACTC::Concedida) {
            log(id, "a/ctc denegada", LOG_DEBUG);
            actc[opp_lado(lado)] = ACTC::Denegada;
            aceptado = RespuestaMando::Aceptado;
        } else if (cmd == "CSB" && estado == (lado == Lado::Impar ? EstadoBloqueo::BloqueoPar : EstadoBloqueo::BloqueoImpar)) {
            if (!cierre_señales[opp_lado(lado)]) {
                log(id, "cierre señales", LOG_DEBUG);
                cierre_señales[opp_lado(lado)] = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "NSB") {
            if (cierre_señales[opp_lado(lado)]) {
                if (me == 1) {
                    log(id, "cierre señales normalizado", LOG_DEBUG);
                    cierre_señales[opp_lado(lado)] = false;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    me_pendiente = true;
                    return RespuestaMando::MandoEspecialNecesario;
                }
            }
        } else if (cmd == "NB") {
            if (!normalizado && !ocupado) {
                if (me == 1) {
                    log(id, "normalizado", LOG_DEBUG);
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

    void message_ruta(const std::string &estacion, estado_colateral_bloqueo est)
    {
        Lado lado;
        if (estacion == estaciones[Lado::Impar]) lado = Lado::Impar;
        else if (estacion == estaciones[Lado::Par]) lado = Lado::Par;
        else return;
        if (est.sin_datos) {
            datos_estaciones[lado] = false;
            return;
        }
        datos_estaciones[lado] = true;
        ruta[lado] = est.ruta;
        maniobra_compatible[lado] = est.maniobra_compatible;
        if (mando_estacion[lado] != est.mando) {
            estado_mando &mando_colateral = mando_estacion[opp_lado(lado)];
            if (!est.mando.central && mando_estacion[lado].central) {
                if (mando_colateral.central) actc[lado] = ACTC::Concedida;
                else if (mando_colateral == mando_estacion[lado]) actc[opp_lado(lado)] = ACTC::NoNecesaria;
            } else if (est.mando.central && !mando_estacion[lado].central) {
                if (mando_colateral.central) actc[lado] = ACTC::NoNecesaria;
                else actc[opp_lado(lado)] = ACTC::Concedida;
            } else if (est.mando.central && mando_colateral.central) {
                if (mando_colateral == est.mando) actc[lado] = actc[opp_lado(lado)] = ACTC::NoNecesaria;
                else if (mando_colateral == mando_estacion[lado]) actc[lado] = actc[opp_lado(lado)] = ACTC::Concedida;
            }
        }
        if (est.anular_bloqueo) desbloquear(lado);
        update();
    }
};
