#pragma once
#include <optional>
#include "enums.h"
struct velocidad_agujas
{
    int velocidad=0;
    LadoAguja posicion=LadoAguja::Recto;
    void update(const velocidad_agujas &o)
    {
        if (o.velocidad < velocidad) velocidad = o.velocidad;
        if (posicion == LadoAguja::Recto) posicion = o.posicion;
    }
    bool operator==(const velocidad_agujas &o) const = default;
    auto operator<=>(const velocidad_agujas &o) const
    {
        if (velocidad != o.velocidad) return velocidad <=> o.velocidad;
        return posicion <=> o.posicion;
    }
    static inline velocidad_agujas max()
    {
        return {1000, LadoAguja::Recto};
    }
};
struct estado_aguja
{
    
};
std::string to_string(const velocidad_agujas &ag);
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const velocidad_agujas &vel);
void from_json(const json &j, velocidad_agujas &vel);
#endif
