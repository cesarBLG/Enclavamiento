#pragma once
#include <set>
#include <map>
#include <vector>
#include "lado.h"
#include "enums.h"
#include "id_elemento.h"
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
class nodo_deslizamiento;
struct reserva_seccion
{
    ruta *ruta_asegurada;
    Lado lado;
    lados<int> outs;
};
class seccion_via
{
public:
    struct conexion
    {
        id_elemento id;
        bool invertir_paridad;
    };
    const id_elemento id;
    const std::optional<id_elemento> bloqueo_asociado;
    const TipoSeccion tipo;
    const id_elemento id_cv;

    std::set<pn_enclavado*> pns;
protected:
    lados<std::map<int,señal*>> señales;
    cv *cv_seccion;
    bool trayecto;
    estado_bloqueo bloqueo_act;

    lados<std::vector<conexion>> siguientes_secciones;
    lados<std::map<int,int>> active_outs;
    std::optional<reserva_seccion> ruta_asegurada;

    std::map<ruta*, nodo_deslizamiento*> deslizamiento;

    lados<int> ocupacion_outs;

    bool me_pendiente = false;
    bool bloqueo_seccion = false;
public:
    seccion_via(const id_elemento &id, const json &j, TipoSeccion tipo=TipoSeccion::Lineal);
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
    virtual void asegurar(ruta *ruta, int in, int out, Lado dir);
    void asegurar_deslizamiento(ruta *ruta, nodo_deslizamiento* nodo);
    virtual void liberar(ruta *ruta);
    bool is_asegurada(ruta *ruta=nullptr)
    {
        if (ruta_asegurada) {
            return ruta == nullptr || ruta == ruta_asegurada->ruta_asegurada;
        } else {
            return false;
        }
    }
    bool asegurar_posible(ruta *ruta, int in, int out, Lado dir)
    {
        if (cv_seccion != nullptr) {
            for (auto &sec : cv_seccion->secciones) {
                if (sec->is_asegurada() && !sec->is_asegurada(ruta)) return false;
            }
        }
        if (ruta_asegurada && ruta_asegurada->ruta_asegurada != ruta) return false;
        return true;
    }
    bool deslizamiento_posible(int in, int out, Lado dir)
    {
        if (ruta_asegurada && (ruta_asegurada->lado != dir || ruta_asegurada->outs[dir] != in || ruta_asegurada->outs[opp_lado(dir)] != out)) {
            return false;
        }
        return true;
    }
    std::optional<reserva_seccion> get_ruta_asegurada()
    {
        return ruta_asegurada;
    }
    const std::map<ruta*, nodo_deslizamiento*> &get_deslizamiento()
    {
        return deslizamiento;
    }
    TipoMovimiento get_tipo_movimiento();
    bool is_bloqueo_seccion() { return bloqueo_seccion; }
    bool is_me_pendiente() { return me_pendiente; }
    bool is_trayecto() { return trayecto; }
    virtual void message_cv(const id_elemento &id, estado_cv ev);
    void message_bloqueo(const id_elemento &id, estado_bloqueo eb)
    {
        if (!bloqueo_asociado || id != *bloqueo_asociado) return;
        bloqueo_act = eb;
        remota_cambio_elemento(ElementoRemota::CV, id_cv);
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
            remota_cambio_elemento(ElementoRemota::CV, id_cv);
        return aceptado;
    }
    void vincular_señal(señal *sig, Lado lado, int pin)
    {
        señales[lado][pin] = sig;
    }
    EstadoCanton get_ocupacion(seccion_via* prev, Lado dir);
    int get_in(seccion_via* prev, Lado dir);
    int get_out(seccion_via* next, Lado dir);
    int num_outs(Lado l)
    {
        return siguientes_secciones[l].size();
    }
};
void from_json(const json &j, seccion_via::conexion &conex);
