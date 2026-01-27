#pragma once
#include <enclavamiento.h>
#include "topology.h"
#include "remota.h"
class ruta;
class frontera;
class señal : public estado_señal
{
public:
    const std::string id;
    const std::string bloqueo_asociado;
    const Lado lado;
    const TipoSeñal tipo;
    const bool señal_virtual=false;
    const int pin;
    seccion_via * const seccion;
    seccion_via * const seccion_prev;
    frontera *frontera_entrada=nullptr;
    señal(const std::string &estacion, const json &j);
    virtual ~señal() {}
    Aspecto get_state()
    {
        return aspecto;
    }
    virtual void message_señal(estado_señal est)
    {
        *((estado_señal*)this) = est;
    }
};
class señal_impl : public señal
{
public:
    const std::string topic;
    const std::string topic_inicio;
protected:
    std::map<Aspecto, Aspecto> aspectos_maximos_anterior_señal;
    std::map<EstadoCanton, Aspecto> aspecto_maximo_ocupacion;

    estado_bloqueo bloqueo_act;

    bool cleared = false;

    bool me_pendiente = false;
    bool rebasada;

    int64_t ultimo_paso_abierta;
    bool paso_circulacion = false;

    estado_inicio_ruta estado_inicio;
public:
    bool clear_request=false;
    ruta *ruta_activa=nullptr;

    bool ruta_necesaria = true;
    bool cierre_stick;

    ruta *ruta_fin=nullptr;
    ruta *ruta_fai=nullptr;
    bool bloqueo_señal = false;
    bool sucesion_automatica = false;

    frontera *frontera_salida=nullptr;

    señal_impl(const std::string &estacion, const json &j);
    void send_state(bool aspecto=true, bool inicio=true)
    {
        if (aspecto) send_message(topic, json(*(estado_señal*)this).dump());
        if (inicio) send_message(topic_inicio, json(estado_inicio).dump());
    }
    void determinar_aspecto();
    void update();
    void message_cv(const std::string &id, estado_cv ev)
    {
        if (id != seccion->get_cv()->id) return;

        paso_circulacion = false;
        // Detección de paso de tren por la señal
        if (ev.evento && ev.evento->ocupacion && ev.evento->lado == lado && (ev.evento->cv_colateral == "" || seccion_prev == nullptr || ev.evento->cv_colateral == seccion_prev->get_cv()->id)) {
            // Si la señal estaba cerrada, es un rebase de señal
            if (aspecto == Aspecto::Parada) {
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
    void message_señal(estado_señal est) override {}
    RespuestaMando mando(const std::string &cmd, int me);
    std::pair<RemotaSIG, RemotaIMV> get_estado_remota();
    estado_inicio_ruta get_estado_inicio();
    bool is_rebasada() { return rebasada; }
};
