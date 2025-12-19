#pragma once
#include <string>
enum struct Lado
{
    Par,
    Impar
};
enum struct EstadoCV
{
    Libre,
    Prenormalizado,
    OcupadoPar,
    OcupadoImpar,
    Ocupado
};
enum struct EstadoCanton
{
    Libre,
    Prenormalizado,
    OcupadoMismoSentido,
    Ocupado,
};
enum struct Aspecto
{
    Parada,
    RebaseAutorizado,
    RebaseAutorizadoDestellos,
    ParadaSelectiva,
    ParadaSelectivaDestellos,
    ParadaDiferida,
    Precaucion,
    AnuncioParada,
    AnuncioPrecaucion,
    ViaLibre,
};
enum struct EstadoBloqueo
{
    Desbloqueo,
    BloqueoPar,
    BloqueoImpar,
    SinDatos,
};
enum struct TipoMovimiento
{
    Ninguno,
    Itinerario,
    Maniobra,
    Rebase,
};
enum struct EstadoFAI
{
    EnEspera,
    Solicitud,
    AperturaNoPosible,
    AperturaNoPosibleReconocida,
    Activo,
    Cancelado
};
enum struct CompatibilidadManiobra
{
    IncompatibleManiobra,
    IncompatibleBloqueo,
    IncompatibleItinerario,
    Compatible,
};
enum struct TipoSeñal
{
    Entrada,
    Salida,
    Avanzada,
    Maniobra,
    Retroceso,
    Intermedia,
    PostePuntoProtegido,
};
enum struct ACTC
{
    NoNecesaria,
    Concedida,
    Denegada,
};
enum struct TipoDestino
{
    Señal,
    Colateral,
    FinalVia,
};
enum struct RespuestaMando
{
    Aceptado,
    MandoEspecialNecesario,
    MandoEspecialEnCurso,
    MandoEspecialNoPermitido,
    OrdenNoAplicable,
    OrdenRechazada,
    OrdenDesconocida,
    NoMando,
};
enum struct TipoBloqueo
{
    BAU,
    BAD,
    BAB,
    BLAU,
    BLAD,
    BLAB,
};
enum struct TipoSeccion
{
    Lineal,
    Cruzamiento,
    Aguja
};
inline Lado opp_lado(Lado lado)
{
    return lado == Lado::Par ? Lado::Impar : Lado::Par;
}
std::string to_string(Lado lado);
std::string to_string(EstadoCV estado);
std::string to_string(Aspecto aspecto);
std::string to_string(EstadoBloqueo estado);
std::string to_string(EstadoCanton estado);
std::string to_string(TipoMovimiento tipo);
std::string to_string(CompatibilidadManiobra comp);
std::string to_string(TipoSeñal tipo);
std::string to_string(ACTC actc);
std::string to_string(TipoBloqueo tipo);
#ifndef WITHOUT_JSON
#include "json.h"
void to_json(json &j, const Lado &lado);
void from_json(const json &j, Lado &lado);
void to_json(json &j, const EstadoCV &estado);
void from_json(const json &j, EstadoCV &estado);
void to_json(json &j, const EstadoBloqueo &estado);
void from_json(const json &j, EstadoBloqueo &estado);
void to_json(json &j, const EstadoCanton &estado);
void from_json(const json &j, EstadoCanton &estado);
void to_json(json &j, const TipoMovimiento &tipo);
void from_json(const json &j, TipoMovimiento &tipo);
void to_json(json &j, const CompatibilidadManiobra &comp);
void from_json(const json &j, CompatibilidadManiobra &comp);
void to_json(json &j, const TipoSeñal &tipo);
void from_json(const json &j, TipoSeñal &tipo);
void to_json(json &j, const Aspecto &asp);
void from_json(const json &j, Aspecto &asp);
void to_json(json &j, const ACTC &actc);
void from_json(const json &j, ACTC &actc);
void from_json(const json &j, TipoDestino &tipo);
void to_json(json &j, const TipoBloqueo &tipo);
void from_json(const json &j, TipoBloqueo &tipo);
#endif
