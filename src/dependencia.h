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
    std::map<std::string, lados<int>> posicion_aparatos;
    std::map<std::string, std::pair<int, int>> secciones;
};
class movimiento_servicio_intermitente : public movimiento
{
    public:
    movimiento_servicio_intermitente(const std::string &estacion, config_servicio_intermitente &cfg);
};
class dependencia
{
    std::map<TipoAlarma, std::map<void*, std::pair<int,bool>>> alarmas;
public:
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
    TipoSoneria soneria = TipoSoneria::Apagada;
    bool inicializada = false;
    dependencia(const std::string id, const json &j);
    void update();
    void send_state()
    {
        send_message("mando/"+id+"/state", json(mando_actual).dump());
        remota_cambio_elemento("dep", id_elemento(id, "DEP"));
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
    void set_alarma(TipoAlarma al, void *objeto, int num, bool rec=false)
    {
        bool changed = false;
        if (num == 0) {
            changed = alarmas[al].erase(objeto);
        } else {
            auto &est = alarmas[al][objeto];
            if (est.first != num) {
                est = {num, !rec};
                changed = true;
            }
        }
        if (changed) {
            remota_cambio_elemento("iga", id_elemento(id, "IGA"));
            std::vector<int> nums;
            for (auto &[al,objs] : alarmas) {
                if (!objs.empty()) nums.push_back((int)al);
            }
            send_message("alarmas/"+id, json(nums).dump());
        }
    }
    RespuestaMando mando(const std::string &cmd, int me) {
        if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
        bool pend = me_pendiente;
        me_pendiente = false;
        if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        if (cmd == "RAL") {
            bool rec = false;
            for (auto it = alarmas.begin(); it != alarmas.end(); ) {
                auto &[al, objs] = *it;
                for (auto &[obj, state] : objs) {
                    if (!state.second) {
                        state.second = true;
                        rec = true;
                    }
                }
                if (al == TipoAlarma::OV) it = alarmas.erase(it);
                else ++it;
            }
            for (auto &[id,bloq] : bloqueos) {
                if (bloq->ral()) rec = true;
            }
            return rec ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        } else if (cmd == "SI") {
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
    RemotaIGA get_estado_remota_iga()
    {
        RemotaIGA r;
        r.IGA_RAL = soneria == TipoSoneria::Averia ? 1 : 0;
        r.IGA_RM = 0;
        r.IGA_OV = alarmas[TipoAlarma::OV].empty() ? 0 : 1;
        r.IGA_BA = bloqueo_agujas ? 1 : 0;
        r.IGA_MA = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::MA]) {
            if (al.first == 2 && !al.second) r.IGA_MA = 2;
            else if (al.first == 2 && r.IGA_MA != 2) r.IGA_MA = 3;
            else if (al.first == 1 && r.IGA_MA == 0) r.IGA_MA = 1;
        }
        r.IGA_AG = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::AG]) {
            if (!al.second) r.IGA_MA = 1;
            else if (r.IGA_MA == 0) r.IGA_MA = 2;
        }
        r.IGA_SE_A = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::SE]) {
            if (al.first == 2) continue;
            if (!al.second) r.IGA_SE_A = 1;
            else if (r.IGA_SE_A == 0) r.IGA_SE_A = 2;
        }
        r.IGA_SE_R = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::SE]) {
            if (al.first == 1) continue;
            if (!al.second) r.IGA_SE_R = 1;
            else if (r.IGA_SE_R == 0) r.IGA_SE_R = 2;
        }
        r.IGA_LUM = 0;
        r.IGA_RE = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::RE]) {
            if (!al.second) r.IGA_RE = 1;
            else if (r.IGA_RE == 0) r.IGA_RE = 2;
        }
        r.IGA_EN = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::EN]) {
            if (al.first > r.IGA_EN) r.IGA_EN = al.first;
        }
        r.IGA_PN = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::PN]) {
            int num = (2-al.first)*2+al.second+1;
            if (r.IGA_PN == 0 || num < r.IGA_PN) r.IGA_PN = num;
        }
        
        r.IGA_DE_AL = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::DE]) {
            if (al.first == 1) continue;
            if (!al.second) r.IGA_DE_AL = 1;
            else if (r.IGA_DE_AL == 0) r.IGA_DE_AL = 2;
        }
        r.IGA_DE_FS = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::DE]) {
            if (al.first == 2) continue;
            if (!al.second) r.IGA_DE_FS = 1;
            else if (r.IGA_DE_FS == 0) r.IGA_DE_FS = 2;
        }
        r.IGA_EM = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::EM]) {
            if (!al.second) r.IGA_EM = 1;
            else if (r.IGA_EM == 0) r.IGA_EM = 2;
        }
        r.IGA_CB = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::CB]) {
            if (!al.second) r.IGA_CB = 1;
            else if (r.IGA_CB == 0) r.IGA_CB = 2;
        }
        r.IGA_NB = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::NB]) {
            if (!al.second) r.IGA_NB = 1;
            else if (r.IGA_NB == 0) r.IGA_NB = 2;
        }
        r.IGA_FI = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::FI]) {
            if (al.first == 2 && !al.second) r.IGA_FI = 1;
            else if (al.first == 2 && r.IGA_FI != 1) r.IGA_FI = 2;
            else if (r.IGA_FI == 0) r.IGA_FI = 3;
        }
        r.IGA_EI = alarmas[TipoAlarma::EI].empty() ? 0 : 1;
        r.IGA_OR = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::OR]) {
            if (al.first == 2 && !al.second) r.IGA_OR = 3;
            else if (al.first == 2 && r.IGA_OR != 3) r.IGA_OR = 4;
            else if (!al.second && r.IGA_OR < 3) r.IGA_OR = 1;
            else if (r.IGA_OR == 0) r.IGA_OR = 2;
        }
        r.IGA_EL = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::EL]) {
            if (al.first == 2 && !al.second) r.IGA_EL = 3;
            else if (al.first == 2 && r.IGA_EL != 3) r.IGA_EL = 4;
            else if (!al.second && r.IGA_EL < 3) r.IGA_EL = 1;
            else if (r.IGA_EL == 0) r.IGA_EL = 2;
        }
        r.IGA_EAG = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::EA]) {
            if (!al.second) r.IGA_EAG = 1;
            else if (r.IGA_EAG == 0) r.IGA_EAG = 2;
        }
        r.IGA_CF = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::CF]) {
            if (!al.second && al.first == 3) r.IGA_CF = 3;
            else if (al.first == 3 && r.IGA_CF != 3) r.IGA_CF = 4;
            else if (r.IGA_CF < al.first) r.IGA_CF = al.first;
        }
        r.IGA_CE = 0;
        for (auto &[obj, al] : alarmas[TipoAlarma::CE]) {
            if (!al.second) r.IGA_CE = 1;
            else if (r.IGA_CE == 0) r.IGA_CE = 2;
        }
        return r;
    }
};
