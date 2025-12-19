#pragma once
#include <string>
struct estado_mando
{
    bool central;
    std::string puesto;
    std::optional<std::string> cedido;
    bool emergencia;
    bool operator<=>(const estado_mando &o) const = default;
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_mando &estado);
void from_json(const json &j, estado_mando &estado);
#endif
