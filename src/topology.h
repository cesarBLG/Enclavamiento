#pragma once
#include <set>
#include <map>
#include <vector>
#include "lado.h"
#include "enums.h"
#include "mqtt.h"
#include "time.h"
#include "log.h"
#include "cv.h"
#include "bloqueo.h"
#include "estado_aguja.h"
#include <optional>
class ruta;
class señal;
class pn_enclavado;
class seccion_via
{
public:
    struct conexion
    {
        std::string id;
        bool invertir_paridad;
    };
    const std::string id;
    const std::string bloqueo_asociado;
    const TipoSeccion tipo;

    std::set<pn_enclavado*> pns;
protected:
    lados<std::map<int,señal*>> señales;
    cv *cv_seccion;
    bool trayecto;
    estado_bloqueo bloqueo_act;

    lados<std::vector<conexion>> siguientes_secciones;
    lados<std::map<int,int>> active_outs;
    lados<int> route_outs;
    ruta *ruta_asegurada = nullptr;

    lados<int> ocupacion_outs;

    bool me_pendiente = false;
    bool bloqueo_seccion = false;
public:
    seccion_via(const std::string &id, const json &j, TipoSeccion tipo=TipoSeccion::Lineal);
    virtual ~seccion_via() = default;
    seccion_via* siguiente_seccion(seccion_via *prev, Lado &dir, bool usar_ruta_asegurada=false);
    std::pair<seccion_via*,Lado> get_seccion_in(Lado dir, int pin);
    void prev_secciones(seccion_via *next, Lado dir_fwd, std::vector<std::pair<seccion_via*, Lado>> &secciones);
    señal *señal_inicio(Lado lado, int pin);
    señal *señal_inicio(Lado lado, seccion_via *prev)
    {
        int in = get_in(prev, lado);
        if (in < 0) return nullptr;
        return señal_inicio(lado, in);
    }
    cv *get_cv()
    {
        return cv_seccion;
    }
    void asegurar(ruta *ruta, int in, int out, Lado dir);
    void asegurar(ruta *ruta, seccion_via *prev, seccion_via *next, Lado dir);
    void liberar(ruta *ruta);
    bool is_asegurada(ruta *ruta=nullptr)
    {
        if (ruta == nullptr) return ruta_asegurada != nullptr;
        return ruta_asegurada == ruta;
    }
    TipoMovimiento get_tipo_movimiento();
    bool is_bloqueo_seccion() { return bloqueo_seccion; }
    bool is_me_pendiente() { return me_pendiente; }
    bool is_trayecto() { return trayecto; }
    virtual void message_cv(const std::string &id, estado_cv ev);
    void message_bloqueo(const std::string &id, estado_bloqueo eb)
    {
        if (id != bloqueo_asociado) return;
        bloqueo_act = eb;
        remota_cambio_elemento(ElementoRemota::CV, cv_seccion->id);
    }
    virtual RespuestaMando mando(const std::string &cmd, int me)
    {
        if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
        bool pend = me_pendiente;
        me_pendiente = false;
        if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        RespuestaMando aceptado = RespuestaMando::OrdenRechazada;
        if (cmd == "BV" || cmd == "BIV") {
            if (!bloqueo_seccion && !trayecto) {
                log(id, "biv", LOG_DEBUG);
                bloqueo_seccion = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "ABV" || cmd == "DIV") {
            if (bloqueo_seccion) {
                if (me) {
                    log(id, "anular biv", LOG_DEBUG);
                    bloqueo_seccion = false;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    me_pendiente = true;
                    aceptado = RespuestaMando::MandoEspecialNecesario;
                }
            }
        }
        if (aceptado != RespuestaMando::OrdenRechazada)
            remota_cambio_elemento(ElementoRemota::CV, cv_seccion->id);
        return aceptado;
    }
    void vincular_señal(señal *sig, Lado lado, int pin)
    {
        señales[lado][pin] = sig;
    }
    EstadoCanton get_ocupacion(seccion_via* prev, Lado dir);
protected:
    int get_in(seccion_via* prev, Lado dir);
    int get_out(seccion_via* next, Lado dir);
};
class aguja : public seccion_via, public estado_aguja
{
    std::optional<PosicionAguja> comprobacion;
    std::optional<PosicionAguja> mandada;
    std::optional<PosicionAguja> talonable;
    bool cedida_mantenimiento;
    Lado lado;
    public:
    aguja(const std::string &id, const json &j);
    void update()
    {
        if (talonable) comprobacion = talonable;
        if (talonable) {
            active_outs[opp_lado(lado)][0] = 0;
            active_outs[opp_lado(lado)][1] = 0;
        } else if (comprobacion) {
            bool invertida = comprobacion == PosicionAguja::Invertida;
            active_outs[opp_lado(lado)][invertida ? 1 : 0] = 0;
            active_outs[opp_lado(lado)][invertida ? 0 : 1] = -1;
        } else {
            active_outs[opp_lado(lado)][0] = -1;
            active_outs[opp_lado(lado)][1] = -1;
        }
        active_outs[lado][0] = comprobacion == PosicionAguja::Invertida ? 1 : (comprobacion ? 0 : -1);
    }
    void message_aguja(estado_aguja estado)
    {
        *((estado_aguja*)this) = estado;
        update();
    }
    void message_cv(const std::string &id, estado_cv ev) override
    {
        if (id != cv_seccion->id) return;
        seccion_via::message_cv(id, ev);
    }
    bool is_ruta_fija(seccion_via *prev, Lado dir)
    {
        if (!talonable) return false;
        if (dir == lado) return true;
        int in = get_in(prev, dir);
        return in == (talonable == PosicionAguja::Invertida ? 1 : 0);
    }
    RespuestaMando mando(const std::string &cmd, int me) override
    {
        if (cmd == "MAT") {
            if (talonable) {
                talonable = talonable == PosicionAguja::Invertida ? PosicionAguja::Normal : PosicionAguja::Invertida;
                update();
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "ATN") {
            if (talonable != PosicionAguja::Normal) {
                talonable = PosicionAguja::Normal;
                update();
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "ATI") {
            if (talonable != PosicionAguja::Invertida) {
                talonable = PosicionAguja::Invertida;
                update();
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "BA") {
        } else if (cmd == "ABA") {
        } else if (cmd == "BIA") {
            return seccion_via::mando("BIV", me);
        } else if (cmd == "DIA") {
            return seccion_via::mando("DIV", me);
        }
        return RespuestaMando::OrdenRechazada;
    }
};
void from_json(const json &j, seccion_via::conexion &conex);
