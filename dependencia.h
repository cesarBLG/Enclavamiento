#pragma once
#include <string>
#include "ruta.h"
#include "bloqueo.h"
#include "mando.h"
#include "nlohmann/json.hpp"
using json = nlohmann::json;
struct dependencia
{
    const std::string id;
    bool mando_cedido;
    estado_mando mando_actual;
    std::string mando_especial_pendiente;
    std::set<ruta*> rutas;
    std::map<std::string, estado_colateral_bloqueo> movimiento_bloqueos;
    dependencia(const std::string id) : id(id), mando_cedido(false), mando_actual({true, "fec"}) {}
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
    RespuestaMando mando_ruta(const std::string &inicio, const std::string &fin, const std::string &cmd) {
        for (auto *ruta : rutas) {
            auto resp = ruta->mando(inicio, fin, cmd);
            if (resp != RespuestaMando::OrdenNoAplicable) return resp;
        }
        return RespuestaMando::OrdenNoAplicable;
    }
};
