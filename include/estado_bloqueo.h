#pragma once
#include "enums.h"
#include "lado.h"
#include "estado_mando.h"
struct estado_bloqueo
{
    EstadoBloqueo estado;
    bool ocupado;
    bool normalizado;
    lados<bool> prohibido;
    lados<bool> escape;
    lados<ACTC> actc;
    lados<bool> cierre_señales;
    lados<EstadoCanton> estado_cantones_inicio;
    lados<TipoMovimiento> ruta;
    lados<bool> maniobra_compatible;
    lados<estado_mando> mando_estacion;
};
struct estado_colateral_bloqueo
{
    bool sin_datos = false;
    TipoMovimiento ruta;
    bool anular_bloqueo;
    bool maniobra_compatible;
    estado_mando mando;
    estado_colateral_bloqueo() {}
    estado_colateral_bloqueo(TipoMovimiento ruta, bool maniobra_compatible, estado_mando mando) : ruta(ruta), maniobra_compatible(maniobra_compatible), mando(mando)
    {
        anular_bloqueo = false;
    }
    bool operator==(const estado_colateral_bloqueo &o) const
    {
        return sin_datos == o.sin_datos && ruta == o.ruta && anular_bloqueo == o.anular_bloqueo && maniobra_compatible == o.maniobra_compatible && mando == o.mando;
    }
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_bloqueo &estado);
void from_json(const json &j, estado_bloqueo &estado);
void to_json(json &j, const estado_colateral_bloqueo &col);
void from_json(const json &j, estado_colateral_bloqueo &col);
#endif
