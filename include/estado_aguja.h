#pragma once
#include <optional>
#include "enums.h"
struct velocidad_agujas
{
    int velocidad=1000;
    LadoAguja posicion=LadoAguja::Recto;
    void update(const velocidad_agujas &o)
    {
        if (o.velocidad < velocidad) velocidad = o.velocidad;
        if (posicion == LadoAguja::Recto) posicion = o.posicion;
    }
};
struct estado_aguja
{
    
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const velocidad_agujas &vel);
void from_json(const json &j, velocidad_agujas &vel);
#endif
