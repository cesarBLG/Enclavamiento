#pragma once
#include <enclavamiento.h>
#include "topology.h"
#include "remota.h"
class ruta;
class señal : public estado_señal
{
public:
    const std::string id;
    const Lado lado;
    const TipoSeñal tipo;
    const int pin;
    const std::string bloqueo_asociado;
    seccion_via * const seccion;
    seccion_via * const seccion_prev;
    señal(const std::string &estacion, const json &j);
    virtual ~señal() {}
    Aspecto get_state()
    {
        return aspecto;
    }
    bool is_rebasada() { return rebasada; }
    virtual void message_señal(estado_señal est)
    {
        *((estado_señal*)this) = est;
    }
};
class señal_impl : public señal
{
public:
    const std::string topic;
protected:
    std::map<Aspecto, Aspecto> aspectos_maximos_anterior_señal;
    std::map<EstadoCanton, Aspecto> aspecto_maximo_ocupacion;

    estado_bloqueo bloqueo_act;

    bool cierre_stick;
    bool cleared = false;

    bool ruta_necesaria = true;

    bool me_pendiente = false;

    int64_t ultimo_paso_abierta;
    bool paso_circulacion = false;
public:
    bool clear_request=false;
    ruta *ruta_activa=nullptr;
    ruta *ruta_fin=nullptr;
    ruta *ruta_fai=nullptr;
    bool bloqueo_señal = false;
    bool sucesion_automatica = false;
    señal_impl(const std::string &estacion, const json &j);
    void send_state()
    {
        send_message(topic, json(*(estado_señal*)this).dump(), 1);
    }
    void determinar_aspecto();
    void update();
    void message_bloqueo(const std::string &id, const estado_bloqueo &estado)
    {
        if (id != bloqueo_asociado) return;
        bloqueo_act = estado;
        update();
    }
    void message_cv(const std::string &id, estado_cv ev)
    {
        if (id != seccion->get_cv()->id) return;

        paso_circulacion = false;
        if (ev.evento && ev.evento->ocupacion && ev.evento->lado == lado && (ev.evento->cv_colateral == "" || seccion_prev == nullptr || ev.evento->cv_colateral == seccion_prev->get_cv()->id)) {
            if (aspecto == Aspecto::Parada && get_milliseconds() - ultimo_paso_abierta > 30000) {
                if (tipo != TipoSeñal::PostePuntoProtegido) {
                    rebasada = true;
                    log(this->id, "rebasada", LOG_WARNING);
                }
            } else {
                ultimo_paso_abierta = get_milliseconds();
                paso_circulacion = true;
            }
        }
    }
    void message_señal(estado_señal est) override {}
    RespuestaMando mando(const std::string &cmd, int me);
    std::pair<RemotaSIG, RemotaIMV> get_estado_remota();
    bool condiciona_anteriores()
    {
        return aspectos_maximos_anterior_señal.size() > 1;
    }
};
