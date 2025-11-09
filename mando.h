#pragma once
#include <string>
#include "nlohmann/json.hpp"
using json = nlohmann::json;
struct estado_mando
{
    bool central;
    std::string puesto;
    bool operator==(const estado_mando &o) const
    {
        return central == o.central && puesto == o.puesto;
    }
    bool operator!=(const estado_mando &o) const
    {
        return central != o.central || puesto != o.puesto;
    }
};
void to_json(json &j, const estado_mando &estado);
void from_json(const json &j, estado_mando &estado);
