#pragma once
#include <enclavamiento.h>
#include "topology.h"
#include "remota.h"
class ruta;
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
protected:
    std::map<Aspecto, Aspecto> aspectos_maximos_anterior_señal;
    std::map<EstadoCanton, Aspecto> aspecto_maximo_ocupacion;

    estado_bloqueo bloqueo_act;

    bool cierre_stick;
    bool cleared = false;

    bool me_pendiente = false;
    bool rebasada;

    int64_t ultimo_paso_abierta;
    bool paso_circulacion = false;
public:
    bool clear_request=false;
    ruta *ruta_activa=nullptr;
    bool ruta_necesaria = true;
    ruta *ruta_fin=nullptr;
    ruta *ruta_fai=nullptr;
    bool bloqueo_señal = false;
    bool sucesion_automatica = false;
    señal_impl(const std::string &estacion, const json &j);
    void send_state()
    {
        send_message(topic, json(*(estado_señal*)this).dump());
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
            if (aspecto == Aspecto::Parada && get_milliseconds() - ultimo_paso_abierta > 30000) {
                if (tipo != TipoSeñal::PostePuntoProtegido) {
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
    void message_bloqueo(const std::string &id, estado_bloqueo eb)
    {
        if (id != bloqueo_asociado) return;
        bloqueo_act = eb;
    }
    void message_señal(estado_señal est) override {}
    RespuestaMando mando(const std::string &cmd, int me);
    std::pair<RemotaSIG, RemotaIMV> get_estado_remota();
    bool is_rebasada() { return rebasada; }
    // Indica si el aspecto de la señal condiciona el aspecto de señales anteriores
    bool condiciona_anteriores()
    {
        return aspectos_maximos_anterior_señal.size() > 1;
    }
};
