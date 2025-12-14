#pragma once
#include "enums.h"
#include "lado.h"
struct estado_seccion_rbc
{
    EstadoCV estado;
    lados<int> out_pins;
    lados<bool> rbc_control;
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_seccion_rbc &estado);
void from_json(const json &j, estado_seccion_rbc &estado);
#endif

