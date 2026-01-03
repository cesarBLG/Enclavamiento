#pragma once
#include "enums.h"
#include <optional>
struct estado_inicio_ruta
{
    TipoMovimiento tipo = TipoMovimiento::Ninguno;
    bool rebasada = false;
    bool ocupada_diferimetro = false;
    bool formada = false;
    std::optional<EstadoFAI> fai;
    bool sucesion_automatica = false;
    bool bloqueo_señal = false;
    bool me_pendiente = false;
    std::optional<int64_t> fin_diferimetro;
    bool operator<=>(const estado_inicio_ruta &o) const = default;
};
struct estado_fin_ruta
{
    TipoMovimiento tipo = TipoMovimiento::Ninguno;
    bool supervisada = false;
    bool bloqueo_destino = false;
    bool me_pendiente = false;
    std::optional<int64_t> fin_diferimetro;
    bool operator<=>(const estado_fin_ruta &o) const = default;
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_inicio_ruta &estado);
void from_json(const json &j, estado_inicio_ruta &estado);
void to_json(json &j, const estado_fin_ruta &estado);
void from_json(const json &j, estado_fin_ruta &estado);
#endif
