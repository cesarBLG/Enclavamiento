#pragma once
#include "enums.h"
struct estado_señal
{
    Aspecto aspecto;
    Aspecto aspecto_maximo_anterior_señal;
    bool rebasada;
    bool sin_datos = false;
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_señal &estado);
void from_json(const json &j, estado_señal &estado);
#endif
