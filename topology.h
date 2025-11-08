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
#include <optional>
class ruta;
class señal;
class seccion_via
{
public:
    struct conexion
    {
        std::string id;
        bool invertir_paridad;
    };
    const std::string id;
protected:
    lados<std::map<int,std::string>> señales;
    cv *cv_seccion;
    bool trayecto;
    std::string bloqueo;
    estado_bloqueo bloqueo_act;

    lados<std::vector<conexion>> siguientes_secciones;
    lados<std::map<int,int>> active_outs;
    lados<int> route_outs;
    ruta *ruta_asegurada;

    lados<int> ocupacion_outs;

    bool me_pendiente = false;
    bool bloqueo_seccion = false;
public:
    seccion_via(const std::string &id, const json &j);
    seccion_via* siguiente_seccion(seccion_via *prev, Lado &dir);
    std::pair<seccion_via*,Lado> get_seccion_in(Lado dir, int pin);
    void prev_secciones(seccion_via *next, Lado dir_fwd, std::vector<std::pair<seccion_via*, Lado>> &secciones);
    señal *señal_inicio(Lado lado, int pin);
    cv *get_cv()
    {
        return cv_seccion;
    }
    void asegurar(ruta *ruta, seccion_via *prev, seccion_via *next, Lado dir);
    void liberar(ruta *ruta)
    {
        if (ruta_asegurada == ruta) {
            ruta_asegurada = nullptr;
            route_outs = {-1, -1};
            remota_cambio_elemento(ElementoRemota::CV, cv_seccion->id);
        }
    }
    bool is_asegurada(ruta *ruta=nullptr)
    {
        if (ruta == nullptr) return ruta_asegurada != nullptr;
        return ruta_asegurada == ruta;
    }
    TipoMovimiento get_tipo_movimiento();
    bool is_bloqueo_seccion() { return bloqueo_seccion; }
    bool is_trayecto() { return trayecto; }
    void message_cv(const std::string &id, estado_cv ev);
    void message_bloqueo(const std::string &id, estado_bloqueo eb)
    {
        if (id != bloqueo) return;
        bloqueo_act = eb;
        remota_cambio_elemento(ElementoRemota::CV, cv_seccion->id);
    }
    RespuestaMando mando(const std::string &cmd, int me)
    {
        if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
        bool pend = me_pendiente;
        me_pendiente = false;
        if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        RespuestaMando aceptado = RespuestaMando::OrdenRechazada;
        if (cmd == "BV" || cmd == "BIV") {
            if (!bloqueo_seccion && !trayecto) {
                bloqueo_seccion = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "ABV" || cmd == "DIV") {
            if (bloqueo_seccion) {
                if (me) {
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
    EstadoCanton get_ocupacion(seccion_via* prev, Lado dir);
protected:
    int get_in(seccion_via* prev, Lado dir);
    int get_out(seccion_via* next, Lado dir);
};
class aguja : public seccion_via
{
    Lado lado;
    bool talonable;
    bool invertida;
    aguja(const std::string &id, const json &j) : seccion_via(id, j), lado(j["Lado"])
    {
    }
    void update()
    {
        active_outs[lado][0] = invertida ? 1 : 0;
        active_outs[opp_lado(lado)][invertida ? 1 : 0] = 0;
        active_outs[opp_lado(lado)][invertida ? 0 : 1] = talonable ? 0 : -1;
    }
};
void from_json(const json &j, seccion_via::conexion &conex);
