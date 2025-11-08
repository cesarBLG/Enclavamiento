#pragma once
#include "topology.h"
#include "bloqueo.h"
#include "remota.h"
class ruta;
class señal
{
public:
    const std::string id;
    const Lado lado;
    const std::string topic;
    const TipoSeñal tipo;
    const int pin;
    seccion_via * const seccion;
    seccion_via * const seccion_prev;
protected:
    Aspecto aspecto;
    std::map<Aspecto, Aspecto> aspectos_maximos_anterior_señal;
    std::map<EstadoCanton, Aspecto> aspecto_maximo_ocupacion;
    Aspecto aspecto_maximo_anterior_señal;

    std::string bloqueo_asociado;
    estado_bloqueo bloqueo_act;

    bool cierre_stick;
    bool cleared = false;

    bool ruta_necesaria = true;

    bool me_pendiente = false;

    int64_t ultimo_paso_abierta;
    bool rebasada = false;
    bool paso_circulacion = false;
public:
    bool clear_request=false;
    ruta *ruta_activa=nullptr;
    bool bloqueo_señal = false;
    bool sucesion_automatica = false;
    señal(const std::string &estacion, const json &j);
    void send_state()
    {
        send_message(topic, to_string(aspecto), 1);
    }
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
            if (aspecto == Aspecto::Parada && get_milliseconds() - ultimo_paso_abierta > 15000) {
                rebasada = true;
                log(this->id, "rebasada", LOG_WARNING);
            } else {
                ultimo_paso_abierta = get_milliseconds();
                paso_circulacion = true;
            }
        }
    }
    RespuestaMando mando(const std::string &cmd, int me);
    Aspecto get_state()
    {
        return aspecto;
    }
    bool is_rebasada() { return rebasada; }
    std::pair<RemotaSIG, RemotaIMV> get_estado_remota();
};
