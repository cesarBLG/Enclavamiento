#include "estado_rbc.h"
void to_json(json &j, const estado_seccion_rbc &estado)
{
    j["Estado"] = estado.estado;
    j["OutPins"] = estado.out_pins;
    j["RBCControl"] = estado.rbc_control;
}
void from_json(const json &j, estado_seccion_rbc &estado)
{
    estado.estado = j["Estado"];
    estado.out_pins = j["OutPins"];
    estado.rbc_control = j["RBCControl"];
}
