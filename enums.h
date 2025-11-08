#pragma once
#include <string>
#include "nlohmann/json.hpp"
using json = nlohmann::json;
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
    Precaucion,
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
};
enum struct TipoSeñal
{
    Entrada,
    Salida,
    Avanzada,
    Maniobra,
    Retroceso,
    Intermedia,
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
std::string to_string(TipoSeñal tipo);
std::string to_string(ACTC actc);
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
void to_json(json &j, const TipoSeñal &tipo);
void from_json(const json &j, TipoSeñal &tipo);
void to_json(json &j, const Aspecto &asp);
void from_json(const json &j, Aspecto &asp);
void to_json(json &j, const ACTC &actc);
void from_json(const json &j, ACTC &actc);
void from_json(const json &j, TipoDestino &tipo);
