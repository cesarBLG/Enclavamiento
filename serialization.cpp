#include <enclavamiento.h>
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
        case Aspecto::ParadaDiferida: return "ParadaDiferida";
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
        case TipoSeñal::PostePuntoProtegido: return "PostePuntoProtegido";
    }
    return "";
}
std::string to_string(ACTC actc)
{
    switch (actc) {
        case ACTC::NoNecesaria: return "NoNecesaria";
        case ACTC::Concedida: return "Concedida";
        case ACTC::Denegada: return "NoConcedida";
    }
    return "";
}
std::string to_string(TipoBloqueo tipo)
{
    switch (tipo) {
        case TipoBloqueo::BAU: return "BAU";
        case TipoBloqueo::BAD: return "BAD";
        case TipoBloqueo::BAB: return "BAB";
        case TipoBloqueo::BLAU: return "BLAU";
        case TipoBloqueo::BLAD: return "BLAD";
        case TipoBloqueo::BLAB: return "BLAB";
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
    else if (j == "ParadaDiferida") asp = Aspecto::ParadaDiferida;
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
    else if (j == "PostePuntoProtegido") tipo = TipoSeñal::PostePuntoProtegido;
}
void to_json(json &j, const ACTC &actc)
{
    j = to_string(actc);
}
void from_json(const json &j, ACTC &actc)
{
    if (j == "NoNecesaria") actc = ACTC::NoNecesaria;
    else if (j == "Concedida") actc = ACTC::Concedida;
    else if (j == "NoConcedida") actc = ACTC::Denegada;
}
void from_json(const json &j, TipoDestino &tipo)
{
    if (j == "FinalVía") tipo = TipoDestino::FinalVia;
    else if (j == "Colateral") tipo = TipoDestino::Colateral;
    else if (j == "Señal") tipo = TipoDestino::Señal;
}

void to_json(json &j, const estado_cv &estado)
{
    j["Estado"] = estado.estado;
    j["EstadoPrevio"] = estado.estado_previo;
    j["Avería"] = estado.averia;
    j["PérdidaSecuencia"] = estado.perdida_secuencia;
    j["BTV"] = estado.btv;
    j["ME"] = estado.me_pendiente;
    if (estado.evento) {
        j["Evento"] = *estado.evento;
    }
}
void from_json(const json &j, estado_cv &estado)
{
    if (j == "desconexion") {
        estado.estado = estado.estado_previo = EstadoCV::Ocupado;
        estado.sin_datos = true;
        return;
    }
    estado.estado = j["Estado"];
    estado.estado_previo = j["EstadoPrevio"];
    estado.averia = j["Avería"];
    estado.perdida_secuencia = j["PérdidaSecuencia"];
    estado.btv = j["BTV"];
    estado.me_pendiente = j["ME"];
    if (j.contains("Evento")) {
        estado.evento = j["Evento"];
    }
}
void from_json(const json &j, evento_cv &ev)
{
    ev.lado = j["Lado"];
    ev.ocupacion = j["Ocupación"];
    ev.cv_colateral = j["CVColateral"];
}
void to_json(json &j, const evento_cv &ev)
{
    j["Lado"] = ev.lado;
    j["Ocupación"] = ev.ocupacion;
    j["CVColateral"] = ev.cv_colateral;
}

void to_json(json &j, const estado_bloqueo &estado)
{
    j["Estado"] = estado.estado;
    j["Ocupado"] = estado.ocupado;
    j["Prohibido"] = estado.prohibido;
    j["Ruta"] = estado.ruta;
    j["Escape"] = estado.escape;
    j["CierreSeñales"] = estado.cierre_señales;
    j["A/CTC"] = estado.actc;
    j["Normalizado"] = estado.normalizado;
    j["CantonesEntrada"] = estado.estado_cantones_inicio;
    j["ManiobraCompatible"] = estado.maniobra_compatible;
    j["MandoEstación"] = estado.mando_estacion;
}
void from_json(const json &j, estado_bloqueo &estado)
{
    if (j == "desconexion") {
        estado.estado = EstadoBloqueo::SinDatos;
        return;
    }
    estado.estado = j["Estado"];
    estado.ocupado = j["Ocupado"];
    estado.prohibido = j["Prohibido"];
    estado.ruta = j["Ruta"];
    estado.escape = j["Escape"];
    estado.cierre_señales = j["CierreSeñales"];
    estado.actc = j["A/CTC"];
    estado.normalizado = j["Normalizado"];
    estado.estado_cantones_inicio = j["CantonesEntrada"];
    estado.maniobra_compatible = j["ManiobraCompatible"];
    estado.mando_estacion = j["MandoEstación"];
}
void to_json(json &j, const estado_colateral_bloqueo &col)
{
    j["Ruta"] = col.ruta;
    if (col.anular_bloqueo) j["AnularBloqueo"] = true;
    j["ManiobraCompatible"] = col.maniobra_compatible;
    j["Mando"] = col.mando;
}
void from_json(const json &j, estado_colateral_bloqueo &col)
{
    if (j == "desconexion") {
        col.sin_datos = true;
        return;
    }
    col.ruta = j["Ruta"];
    col.anular_bloqueo = j.value("AnularBloqueo", false);
    col.maniobra_compatible = j["ManiobraCompatible"];
    col.mando = j["Mando"];
}
void to_json(json &j, const estado_mando &estado)
{
    j["Central"] = estado.central;
    j["Puesto"] = estado.puesto;
    if (estado.cedido) j["Cedido"] = *estado.cedido;
    j["Emergencia"] = estado.emergencia;
}
void from_json(const json &j, estado_mando &estado)
{
    estado.central = j["Central"];
    estado.puesto = j["Puesto"];
    if (j.contains("Cedido")) estado.cedido = j["Cedido"];
    estado.emergencia = j.value("Emergencia", false);
}
void to_json(json &j, const estado_señal &estado)
{
    j["Aspecto"] = estado.aspecto;
    j["AspectoAnterior"] = estado.aspecto_maximo_anterior_señal;
}
void from_json(const json &j, estado_señal &estado)
{
    if (j == "desconexion") {
        estado.sin_datos = true;
        estado.aspecto = Aspecto::Parada;
        estado.aspecto_maximo_anterior_señal = Aspecto::Parada;
        return;
    }
    estado.aspecto = j["Aspecto"];
    estado.aspecto_maximo_anterior_señal = j["AspectoAnterior"];
}
void from_json(const json &j, parametros_predeterminados &params)
{
    params.diferimetro_dai1 = j.value("DiferímetroDAI1", 30) * 1000;
    params.diferimetro_dai2 = j.value("DiferímetroDAI2", 2*60+30) * 1000;
    params.diferimetro_dei = j.value("DiferímetroDEI", 3*60) * 1000;
    params.diferimetro_prenormalizacion_cv = j.value("PrenormalizaciónCV", 180) * 1000;
    params.diferimetro_prenormalizacion_cv_tren = j.value("PrenormalizaciónCVTren", 20) * 1000;
    params.tiempo_espera_fai = j.value("EspaciadoFAI", 20) * 1000;
    params.fraccion_ejes_prenormalizacion = j.value("FracciónEjesPrenormalización", 0.5);
}
void to_json(json &j, const TipoBloqueo &tipo)
{
    j = to_string(tipo);
}
void from_json(const json &j, TipoBloqueo &tipo)
{
    if (j == "BAU") tipo = TipoBloqueo::BAU;
    else if (j == "BAD") tipo = TipoBloqueo::BAD;
    else if (j == "BAB") tipo = TipoBloqueo::BAB;
    else if (j == "BLAU") tipo = TipoBloqueo::BLAU;
    else if (j == "BLAD") tipo = TipoBloqueo::BLAD;
    else if (j == "BLAB") tipo = TipoBloqueo::BLAB;
}
