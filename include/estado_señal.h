#pragma once
#include "enums.h"
struct estado_señal
{
    Aspecto aspecto;
    Aspecto aspecto_maximo_anterior_señal;
    bool sin_datos = false;
    bool operator<=>(const estado_señal &o) const = default;
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_señal &estado);
void from_json(const json &j, estado_señal &estado);
#endif
