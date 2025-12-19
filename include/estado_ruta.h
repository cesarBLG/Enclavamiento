#pragma once
#include "enums.h"
#include <optional>
struct estado_inicio_ruta
{
    TipoMovimiento tipo;
    bool rebasada;
    bool ocupada_diferimetro;
    bool formada;
    std::optional<EstadoFAI> fai;
    bool sucesion_automatica;
    bool bloqueo_señal;
    bool me_pendiente;
    std::optional<int64_t> fin_diferimetro;
    bool operator<=>(const estado_inicio_ruta &o) const = default;
};
struct estado_fin_ruta
{
    TipoMovimiento tipo;
    bool supervisada;
    bool bloqueo_destino;
    bool me_pendiente;
    std::optional<int64_t> fin_diferimetro;
    bool operator<=>(const estado_fin_ruta &o) const = default;
};
