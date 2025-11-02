#include "ruta.h"
#include "items.h"

ruta::ruta(const std::string &estacion, const json &j) : estacion(estacion), tipo(j["Tipo"]), inicio(j["Inicio"]), destino(j["Destino"]), id((tipo == TipoMovimiento::Itinerario ? "I " : "M ")+estacion+" "+inicio+" "+destino), bloqueo_salida(j.value("Bloqueo", ""))
{
    valid = true;
    std::string id_señal = estacion+"/"+inicio;
    if (señales.find(id_señal) == señales.end()) {
        valid = false;
        log(id, "señal de inicio inválida", LOG_ERROR);
        return;
    }
    señal_inicio = señales[id_señal];
    lado = lado_bloqueo = señal_inicio->lado;
    maniobra_compatible = j.value("Compatible", false);
    if (j.contains("Proximidad")) {
        for (auto &cv : j["Proximidad"]) {
            auto it = ::cvs.find(cv);
            if (it == ::cvs.end()) {
                log(id, "proximidad inválida", LOG_ERROR);
                proximidad.clear();
                break;
            } else {
                proximidad.push_back(it->second);
            }
        }
    }
}