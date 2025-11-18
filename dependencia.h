#pragma once
#include <string>
#include "ruta.h"
#include <enclavamiento.h>
#include "nlohmann/json.hpp"
using json = nlohmann::json;
struct dependencia
{
    const std::string id;
    estado_mando mando_actual;
    std::string mando_especial_pendiente;
    std::set<ruta*> rutas;
    std::map<std::string, estado_colateral_bloqueo> movimiento_bloqueos;
    dependencia(const std::string id) : id(id), mando_actual({false, "PLO_"+id, std::nullopt, false}) {}
    void update()
    {
        std::map<std::string, estado_colateral_bloqueo> movimiento_bloqueos_new;
        for (auto *ruta : rutas) {
            movimiento_bloqueos_new[ruta->bloqueo_salida] = estado_colateral_bloqueo(TipoMovimiento::Ninguno, false, mando_actual);
        }
        for (auto *ruta : rutas) {
            ruta->update();
            auto it = movimiento_bloqueos_new.find(ruta->bloqueo_salida);
            if (ruta->is_mandada()) {
                it->second.ruta = ruta->tipo;
                it->second.maniobra_compatible = ruta->maniobra_compatible;
            } else if (ruta->anulacion_bloqueo_pendiente) {
                ruta->anulacion_bloqueo_pendiente = false;
                if (it->second.ruta == TipoMovimiento::Ninguno) it->second.anular_bloqueo = true;
            }
        }
        for (auto &kvp : movimiento_bloqueos_new) {
            if (kvp.first == "" || movimiento_bloqueos[kvp.first] == kvp.second) continue;
            json j = kvp.second;
            send_message("bloqueo/"+kvp.first+"/ruta/"+id, j.dump());
        }
        movimiento_bloqueos = movimiento_bloqueos_new;
    }
    void send_state()
    {
        send_message("mando/"+id+"/state", json(mando_actual).dump());
        remota_cambio_elemento(ElementoRemota::DEP, id+":DEP");
    }
    void set_mando(estado_mando estado)
    {
        mando_actual = estado;
        send_state();
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
