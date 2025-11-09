#include "dependencia.h"
void to_json(json &j, const estado_mando &estado)
{
    j["Central"] = estado.central;
    j["Puesto"] = estado.puesto;
}
void from_json(const json &j, estado_mando &estado)
{
    estado.central = j["Central"];
    estado.puesto = j["Puesto"];
}
