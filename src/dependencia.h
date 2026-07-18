#pragma once
#include <string>
#include "ruta.h"
#include "bloqueo.h"
#include <enclavamiento.h>
#include "nlohmann/json.hpp"
using json = nlohmann::json;
struct config_señal_servicio_intermitente
{
    bool ruta_necesaria;
    bool abierta_desbloqueo;
    bool abierta_bloqueo_receptor;
};
struct config_servicio_intermitente
{
    std::set<std::string> fais;
    std::set<std::string> itinerarios_apertura;
    std::map<std::string, config_señal_servicio_intermitente> señales_abiertas;
    std::map<std::string, std::pair<int, int>> posicion_aparatos;
    std::map<std::string, std::pair<int, int>> secciones;
};
class movimiento_servicio_intermitente : public movimiento
{
    public:
    movimiento_servicio_intermitente(const std::string &estacion, config_servicio_intermitente &cfg);
};
struct dependencia
{
    const std::string id;
    estado_mando mando_actual;
    bool me_pendiente = false;
    bool cerrada = false;
    std::string mando_especial_pendiente;
    std::set<ruta*> rutas;
    std::map<id_elemento, bloqueo*> bloqueos;
    bool bloqueo_agujas = false;
    std::optional<config_servicio_intermitente> cfg_servicio_intermitente;
    movimiento_servicio_intermitente *servicio_intermitente = nullptr;
    TipoSoneria soneria;
    bool inicializada = false;
    dependencia(const std::string id, const json &j);
    void update();
    void send_state()
    {
        send_message("mando/"+id+"/state", json(mando_actual).dump());
        remota_cambio_elemento("dep", id+":DEP");
    }
    void set_mando(estado_mando estado)
    {
        if (estado.central && cerrada) set_servicio_intermitente(false);
        mando_actual = estado;
        for (auto &[id, bloqueo] : bloqueos) {
            bloqueo->cambio_mando(estado);
        }
        send_state();
    }
    void calcular_vinculacion_bloqueos();
    bool set_servicio_intermitente(bool cerrar);
    RespuestaMando mando(const std::string &cmd, int me) {
        if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
        bool pend = me_pendiente;
        me_pendiente = false;
        if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        if (cmd == "SI") {
            if (!cerrada && set_servicio_intermitente(true)) {
                log(id, "cerrar estación", LOG_DEBUG);
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "ASI") {
            if (cerrada && set_servicio_intermitente(false)) {
                log(id, "abrir estación", LOG_DEBUG);
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "LD") {
            send_message("luz", "día", 0, true);
            log(id, "luz día", LOG_DEBUG);
            return RespuestaMando::Aceptado;
        } else if (cmd == "LN") {
            send_message("luz", "noche", 0, true);
            log(id, "luz noche", LOG_DEBUG);
            return RespuestaMando::Aceptado;
        } else if (cmd == "LD") {
        } else if (cmd == "BCA") {
            if (!bloqueo_agujas) {
                bloqueo_agujas = true;
                log(id, "bloqueo conjunto de agujas");
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "DCA") {
            if (bloqueo_agujas) {
                if (me) {
                    bloqueo_agujas = false;
                    log(id, "desbloqueo conjunto de agujas");
                    return RespuestaMando::Aceptado;
                } else {
                    me_pendiente = true;
                    send_state();
                    return RespuestaMando::MandoEspecialNecesario;
                }
            }
        } else if (cmd == "RST") {
            if (me) {
                log(id, "reinicio", LOG_DEBUG);
                extern bool running;
                running = false;
                return RespuestaMando::Aceptado;
            } else {
                me_pendiente = true;
                send_state();
                return RespuestaMando::MandoEspecialNecesario;
            }
        }
        return RespuestaMando::OrdenRechazada;
    }
    RespuestaMando mando_ruta(const std::string &inicio, const std::string &fin, const std::string &cmd) {
        for (auto *ruta : rutas) {
            auto resp = ruta->mando(inicio, fin, cmd);
            if (resp != RespuestaMando::OrdenNoAplicable) return resp;
        }
        return RespuestaMando::OrdenNoAplicable;
    }
    RemotaDEP get_estado_remota()
    {
        RemotaDEP r;
        r.DEP_DAT = 1;
        if (mando_actual.cedido) r.DEP_MAN_EST = 1;
        else if (mando_actual.central) r.DEP_MAN_EST = 0;
        else if (mando_actual.emergencia) r.DEP_MAN_EST = 3;
        else r.DEP_MAN_EST = 2;
        r.DEP_ME = mando_especial_pendiente != "" ? 1: 0;
        if (mando_actual.central) r.DEP_MAN_P = mando_actual.puesto.size() > 2 && mando_actual.puesto.substr(0, 3) == "PRO" ? 2 : 3;
        else r.DEP_MAN_P = mando_actual.puesto == "PLO_"+id ? 0 : 1;
        if (mando_actual.cedido) {
            if (!mando_actual.central) r.DEP_MAN_C = 3;
            else if (mando_actual.cedido != "" && mando_actual.cedido != "PLO_"+id) r.DEP_MAN_C = 2;
            else r.DEP_MAN_C = 1;
        } else {
            r.DEP_MAN_C = 0;
        }
        r.DEP_CON = 0;
        r.DEP_BCK = 0;
        return r;
    }
};
