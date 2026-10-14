#pragma once
#include "enums.h"
#include "estado_aguja.h"
struct aspecto_extendido
{
    Aspecto aspecto;
    velocidad_agujas velocidad;
    aspecto_extendido() = default;
    aspecto_extendido(Aspecto asp) : aspecto(asp)
    {
        if (asp == Aspecto::PreanuncioParada) velocidad = {60, LadoAguja::Recto};
        else if (asp == Aspecto::AnuncioPrecaucion) velocidad = {30, LadoAguja::Recto};
        else velocidad = velocidad_agujas();
    }
    aspecto_extendido(Aspecto asp, velocidad_agujas vel) : aspecto(asp), velocidad(vel) {}
    auto operator<=>(const aspecto_extendido &o) const
    {
        if (aspecto != o.aspecto) return aspecto <=> o.aspecto;
        return velocidad.velocidad <=> o.velocidad.velocidad;
    }
    auto operator<=>(const Aspecto &o) const
    {
        return aspecto <=> o;
    }
    bool operator==(const aspecto_extendido &o) const = default;
    bool operator==(const Aspecto &o) const
    {
        return aspecto == o;
    }
    aspecto_extendido &operator=(const aspecto_extendido &o) = default;
    aspecto_extendido &operator=(const Aspecto &o)
    {
        return *this = aspecto_extendido(o);
    }
};
struct estado_señal
{
    aspecto_extendido aspecto;
    aspecto_extendido aspecto_maximo_anterior_señal;
    unsigned int focos_mandados_mask = 0;
    velocidad_agujas desviada_propagada = velocidad_agujas::max();
    bool sin_datos = false;
    id_elemento id_señal_focos;
    auto operator<=>(const estado_señal &o) const = default;
};
std::string to_string(const aspecto_extendido &asp);
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_señal &estado);
void from_json(const json &j, estado_señal &estado);
void to_json(json &j, const aspecto_extendido &estado);
void from_json(const json &j, aspecto_extendido &estado);
#endif
