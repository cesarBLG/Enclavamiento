#pragma once
#include "cv.h"
#include "bloqueo.h"
class ruta;
class señal
{
public:
    const std::string id;
    const Lado lado;
    const std::string topic;
protected:
    cv *cv_señal;
    Aspecto aspecto;
    std::map<Aspecto, Aspecto> aspectos_maximos_anterior_señal;
    std::map<EstadoCanton, Aspecto> aspecto_maximo_ocupacion;
    Aspecto aspecto_maximo_anterior_señal;

    std::string bloqueo_asociado;
    estado_bloqueo bloqueo_act;

    bool cierre_stick;
    bool cleared = false;

    bool ruta_necesaria = true;

    int64_t ultimo_paso_abierta;
    bool rebasada = false;
    bool paso_circulacion = false;
public:
    bool clear_request=false;
    ruta *ruta_activa=nullptr;
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
        if (id != cv_señal->id) return;

        paso_circulacion = false;
        if (ev.evento && ev.evento->second && ev.evento->first == lado) {
            if (aspecto == Aspecto::Parada && get_milliseconds() - ultimo_paso_abierta > 15000) {
                rebasada = true;
                log(this->id, "rebasada", LOG_WARNING);
            } else {
                ultimo_paso_abierta = get_milliseconds();
                paso_circulacion = true;
            }
        }
    }
    bool mando(std::string cmd)
    {
        if (cmd == "CS" || cmd == "CSEÑ") {
            clear_request = false;
            return true;
        } else if (cmd == "NPS" && !cierre_stick) {
            clear_request = true;
            return true;
        }
        return false;
    }
    Aspecto get_state()
    {
        return aspecto;
    }
    cv* get_cv_señal()
    {
        return cv_señal;
    }
};