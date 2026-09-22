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
#include "estado_aguja.h"
#include <optional>
class movimiento;
class señal;
class pn_enclavado;
class nodo_deslizamiento;
struct reserva_seccion
{
    movimiento *ruta_asegurada;
    std::optional<Lado> lado;
    lados<int> outs;
};
struct elemento_ruta
{
    seccion_via *seccion;
    std::optional<Lado> dir;
    lados<int> outs;
};
struct punto_negro
{
    seccion_via *seccion_afectada;
    id_elemento seccion_causante;
    std::optional<std::pair<Lado, int>> pin_propio;
    std::optional<std::pair<Lado, int>> pin_ajeno;
    punto_negro() = default;
    punto_negro(seccion_via *sec, const json &j);
    bool afectado_propio(lados<int> outs)
    {
        return !pin_propio || outs[pin_propio->first] == pin_propio->second;
    }
};
extern std::map<id_elemento, std::vector<punto_negro*>> puntos_negros_por_causa;
class seccion_via;
struct nodo_flanco
{
    seccion_via *next;
    seccion_via *seccion;
    Lado dir;
    std::optional<lados<int>> posicion;
    std::vector<nodo_flanco*> prev;
    nodo_flanco(seccion_via *next, seccion_via *sec, Lado dir, const std::set<seccion_via*> &stop, const std::map<seccion_via*, lados<int>> &posicion_aparatos);
    void activar(movimiento *m);
    void desactivar(movimiento *m);
    bool protegido(movimiento *m);
};
struct flanco
{
    seccion_via *seccion;
    Lado dir;
    int in;
    nodo_flanco *root;
    flanco(seccion_via *sec, const json &j);
    void activar(movimiento *m)
    {
        root->activar(m);
    }
    void desactivar(movimiento *m)
    {
        root->desactivar(m);
    }
    bool protegido(movimiento *m)
    {
        return root->protegido(m);
    }
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
    lados<std::map<int,std::set<int>>> all_outs;
protected:
    lados<std::map<int,señal*>> señales;
    cv *cv_seccion;
    bool trayecto;
    estado_bloqueo bloqueo_act;

    lados<std::vector<conexion>> siguientes_secciones;
    lados<std::map<int,int>> active_outs;
    std::optional<reserva_seccion> ruta_asegurada;

    std::map<nodo_deslizamiento*, movimiento*> deslizamiento;

    std::vector<punto_negro*> puntos_negros;
    std::vector<flanco*> proteccion_flanco;

    lados<int> ocupacion_outs;

    bool me_pendiente = false;
    bool bloqueo_seccion = false;
public:
    seccion_via(const id_elemento &id, const json &j, TipoSeccion tipo=TipoSeccion::Lineal);
    virtual ~seccion_via() = default;
    seccion_via* siguiente_seccion(seccion_via *prev, Lado &dir, bool usar_ruta_asegurada=false)
    {
        int in = get_in(prev, dir);
        if (in < 0) return nullptr;
        return siguiente_seccion(in, dir);
    }
    seccion_via* siguiente_seccion(int pin, Lado &dir, bool usar_ruta_asegurada=false);
    std::pair<seccion_via*,Lado> get_seccion_in(Lado dir, int pin);
    void prev_secciones(seccion_via *next, Lado dir_fwd, std::vector<std::pair<seccion_via*, Lado>> &secciones, bool activas=true);
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
    virtual void asegurar(movimiento *ruta, lados<int> outs, std::optional<Lado> dir);
    void asegurar_deslizamiento(movimiento *ruta, nodo_deslizamiento* nodo);
    void liberar_deslizamiento(movimiento *ruta, nodo_deslizamiento* nodo);
    virtual void liberar(movimiento *ruta);
    bool is_asegurada(movimiento *ruta=nullptr)
    {
        if (ruta_asegurada) {
            return ruta == nullptr || ruta == ruta_asegurada->ruta_asegurada;
        } else {
            return false;
        }
    }
    bool asegurar_posible(movimiento *ruta, lados<int> outs, std::optional<Lado> dir);
    bool deslizamiento_posible(int in, int out, Lado dir);
    bool transitable(seccion_via *prev, Lado dir)
    {
        return transitable(get_in(prev, dir), dir);
    }
    virtual bool transitable(int in, Lado dir);
    bool afectada_galibo(lados<int> outs);
    bool invade_galibo(lados<int> outs, movimiento *ruta=nullptr);
    std::optional<reserva_seccion> get_ruta_asegurada()
    {
        return ruta_asegurada;
    }
    const std::map<nodo_deslizamiento*, movimiento*> &get_deslizamiento()
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
        remota_cambio_elemento("sec", id);
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
            remota_cambio_elemento("sec", id);
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
    int get_active_out(int in, Lado dir)
    {
        return active_outs[dir][in];
    }
    bool acceso_posible(int out, Lado dir, bool activas=false, bool comprobando=false)
    {
        if (activas) {
            for (auto &[in, out2] : active_outs[dir]) {
                if (out == out2)
                    return true;
                if (out2 == -1 && !comprobando) return acceso_posible(in, out, dir);
            }
        } else {
            for (auto &[in, s] : all_outs[dir]) {
                if (s.find(out) != s.end())
                    return true;
            }
        }
    }
    bool acceso_posible(int in, int out, Lado dir, bool activas=false, bool comprobando=false)
    {
        if (activas) {
            int out2 = active_outs[dir][in];
            if (out == out2)
                return true;
            if (out2 == -1 && !comprobando) return acceso_posible(in, out, dir);
        } else {
            auto &s = all_outs[dir][in];
            return s.find(out) != s.end();
        }
        return false;
    }
    virtual bool is_desviada(seccion_via *prev, Lado dir);
    RemotaCV get_estado_remota();
};
class cruzamiento : public seccion_via
{
    public:
    cruzamiento(const id_elemento &id, const json &j) : seccion_via(id, j, TipoSeccion::Cruzamiento) {}
    RemotaCVX get_estado_remota();
};
void from_json(const json &j, seccion_via::conexion &conex);
