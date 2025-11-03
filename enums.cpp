#include "enums.h"
std::string to_string(Lado lado)
{
    switch(lado) {
        case Lado::Impar: return "Impar";
        case Lado::Par: return "Par";
    }
    return "";
}
std::string to_string(EstadoCV estado)
{
    switch(estado) {
        case EstadoCV::Libre: return "Libre";
        case EstadoCV::Prenormalizado: return "Prenormalizado";
        case EstadoCV::OcupadoImpar: return "OcupadoImpar";
        case EstadoCV::OcupadoPar: return "OcupadoPar";
        case EstadoCV::Ocupado: return "Ocupado";
    }
    return "";
}
std::string to_string(EstadoCanton estado)
{
    switch(estado) {
        case EstadoCanton::Libre: return "Libre";
        case EstadoCanton::Prenormalizado: return "Prenormalizado";
        case EstadoCanton::OcupadoMismoSentido: return "OcupadoMismoSentido";
        case EstadoCanton::Ocupado: return "Ocupado";
    }
    return "";
}
std::string to_string(Aspecto aspecto)
{
    switch(aspecto) {
        case Aspecto::Parada: return "Parada";
        case Aspecto::RebaseAutorizado: return "RebaseAutorizado";
        case Aspecto::Precaucion: return "Precaución";
        case Aspecto::ViaLibre: return "VíaLibre";
    }
    return "";
}
std::string to_string(EstadoBloqueo estado)
{
    switch(estado) {
        case EstadoBloqueo::Desbloqueo: return "Desbloqueo";
        case EstadoBloqueo::BloqueoImpar: return "BloqueoImpar";
        case EstadoBloqueo::BloqueoPar: return "BloqueoPar";
        case EstadoBloqueo::SinDatos: return "SinDatos";
    }
    return "";
}
std::string to_string(TipoMovimiento tipo)
{
    switch(tipo) {
        case TipoMovimiento::Ninguno: return "Ninguno";
        case TipoMovimiento::Itinerario: return "Itinerario";
        case TipoMovimiento::Maniobra: return "Maniobra";
    }
    return "";
}
std::string to_string(TipoSeñal tipo)
{
    switch (tipo) {
        case TipoSeñal::Entrada:     return "Entrada";
        case TipoSeñal::Salida:      return "Salida";
        case TipoSeñal::Avanzada:    return "Avanzada";
        case TipoSeñal::Maniobra:    return "Maniobra";
        case TipoSeñal::Retroceso:   return "Retroceso";
        case TipoSeñal::Intermedia:  return "Intermedia";
    }
    return "";
}
void to_json(json &j, const Lado &lado)
{
    j = to_string(lado);
}
void from_json(const json &j, Lado &lado)
{
    if (j == "Impar") lado = Lado::Impar;
    else if (j == "Par") lado = Lado::Par;
}
void to_json(json &j, const EstadoCV &estado)
{
    j = to_string(estado);
}
void from_json(const json &j, EstadoCV &estado)
{
    if (j == "Libre") estado = EstadoCV::Libre;
    else if (j == "Prenormalizado") estado = EstadoCV::Prenormalizado;
    else if (j == "OcupadoImpar") estado = EstadoCV::OcupadoImpar;
    else if (j == "OcupadoPar") estado = EstadoCV::OcupadoPar;
    else estado = EstadoCV::Ocupado;
}
void to_json(json &j, const EstadoCanton &estado)
{
    j = to_string(estado);
}
void from_json(const json &j, EstadoCanton &estado)
{
    if (j == "Libre") estado = EstadoCanton::Libre;
    else if (j == "Prenormalizado") estado = EstadoCanton::Prenormalizado;
    else if (j == "OcupadoMismoSentido") estado = EstadoCanton::OcupadoMismoSentido;
    else estado = EstadoCanton::Ocupado;
}
void to_json(json &j, const EstadoBloqueo &estado)
{
    j = to_string(estado);
}
void from_json(const json &j, EstadoBloqueo &estado)
{
    if (j == "Desbloqueo") estado = EstadoBloqueo::Desbloqueo;
    else if (j == "BloqueoImpar") estado = EstadoBloqueo::BloqueoImpar;
    else if (j == "BloqueoPar") estado = EstadoBloqueo::BloqueoPar;
    else estado = EstadoBloqueo::SinDatos;
}
void to_json(json &j, const Aspecto &asp)
{
    j = to_string(asp);
}
void from_json(const json &j, Aspecto &asp)
{
    if (j == "VíaLibre") asp = Aspecto::ViaLibre;
    else if (j == "RebaseAutorizado") asp = Aspecto::RebaseAutorizado;
    else if (j == "Precaución") asp = Aspecto::Precaucion;
    else asp = Aspecto::Parada;
}
void to_json(json &j, const TipoMovimiento &tipo)
{
    j = to_string(tipo);
}
void from_json(const json &j, TipoMovimiento &tipo)
{
    if (j == "Ninguno") tipo = TipoMovimiento::Ninguno;
    else if (j == "Itinerario") tipo = TipoMovimiento::Itinerario;
    else if (j == "Maniobra") tipo = TipoMovimiento::Maniobra;
}
void to_json(json &j, const TipoSeñal &tipo)
{
    j = to_string(tipo);
}
void from_json(const json &j, TipoSeñal &tipo)
{
    if (j == "Entrada") tipo = TipoSeñal::Entrada;
    else if (j == "Salida") tipo = TipoSeñal::Salida;
    else if (j == "Avanzada") tipo = TipoSeñal::Avanzada;
    else if (j == "Maniobra") tipo = TipoSeñal::Maniobra;
    else if (j == "Retroceso") tipo = TipoSeñal::Retroceso;
    else if (j == "Intermedia") tipo = TipoSeñal::Intermedia;
}