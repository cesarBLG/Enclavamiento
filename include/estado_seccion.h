#pragma once
#include "enums.h"
#include <optional>
struct estado_seccion
{
    TipoMovimiento movimiento;
    bool bv;
    bool me_pendiente;
    bool operator<=>(const estado_seccion &o) const = default;
};
