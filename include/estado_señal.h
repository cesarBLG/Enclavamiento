#pragma once
#include "enums.h"
struct estado_señal
{
    Aspecto aspecto;
    Aspecto aspecto_maximo_anterior_señal;
    unsigned int focos_mandados_mask = 0;
    bool desviada_propagada = false;
    bool sin_datos = false;
    id_elemento id_señal_focos;
    bool operator<=>(const estado_señal &o) const = default;
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_señal &estado);
void from_json(const json &j, estado_señal &estado);
#endif
