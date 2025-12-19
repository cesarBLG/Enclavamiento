#pragma once
#include "lado.h"
#include <optional>
struct evento_cv
{
    Lado lado;
    bool ocupacion;
    std::string cv_colateral;
};
struct estado_cv
{
    EstadoCV estado = EstadoCV::Prenormalizado;
    EstadoCV estado_previo = EstadoCV::Prenormalizado;
    std::optional<evento_cv> evento;
    bool averia=false;
    bool btv=false;
    bool perdida_secuencia=false;
    bool sin_datos=false;
    bool me_pendiente=false;
    bool operator<=>(const estado_cv &o) const = default;
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_cv &estado);
void from_json(const json &j, estado_cv &estado);
void from_json(const json &j, evento_cv &ev);
void to_json(json &j, const evento_cv &ev);
#endif
