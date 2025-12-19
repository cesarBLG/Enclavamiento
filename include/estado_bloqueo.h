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
    lados<CompatibilidadManiobra> maniobra_compatible;
    lados<estado_mando> mando_estacion;
    bool operator<=>(const estado_bloqueo &o) const = default;
};
struct estado_bloqueo_lado
{
    std::map<std::string,EstadoCV> estado_cvs;
    bool prohibido=false;
    bool escape=false;
    ACTC actc=ACTC::NoNecesaria;
    bool cierre_señales=false;
    TipoMovimiento ruta=TipoMovimiento::Ninguno;
    CompatibilidadManiobra maniobra_compatible=CompatibilidadManiobra::IncompatibleBloqueo;
    estado_mando mando_estacion;
    EstadoBloqueo estado=EstadoBloqueo::SinDatos;
    EstadoBloqueo estado_objetivo=EstadoBloqueo::SinDatos;
    bool normalizar_escape;
    bool bloqueo_siguiente = false;
    bool operator<=>(const estado_bloqueo_lado &o) const = default;
};
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const estado_bloqueo &estado);
void from_json(const json &j, estado_bloqueo &estado);
void to_json(json &j, const estado_bloqueo_lado &estado);
void from_json(const json &j, estado_bloqueo_lado &estado);
#endif
