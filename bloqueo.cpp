#include "bloqueo.h"
void to_json(json &j, const estado_bloqueo &estado)
{
    j["Estado"] = estado.estado;
    j["Ocupado"] = estado.ocupado;
    j["Prohibido"] = estado.prohibido;
    j["Ruta"] = estado.ruta;
    j["Escape"] = estado.escape;
    j["CierreSeñales"] = estado.cierre_señales;
    j["A/CTC"] = estado.actc;
    j["Normalizado"] = estado.normalizado;
}
void from_json(const json &j, estado_bloqueo &estado)
{
    if (j == "desconexion") {
        estado.estado = EstadoBloqueo::SinDatos;
        return;
    }
    estado.estado = j["Estado"];
    estado.ocupado = j["Ocupado"];
    estado.prohibido = j["Prohibido"];
    estado.ruta = j["Ruta"];
    estado.escape = j["Escape"];
    estado.cierre_señales = j["CierreSeñales"];
    estado.actc = j["A/CTC"];
    estado.normalizado = j["Normalizado"];
}
