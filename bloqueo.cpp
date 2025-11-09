#include "bloqueo.h"
#include "items.h"
bloqueo::bloqueo(const json &j) : estaciones(j["Estaciones"]), via(j.value("Vía", "")), id(estaciones[Lado::Impar]+"-"+estaciones[Lado::Par]+(via != "" ? "_" : "")+via), topic("bloqueo/"+id+"/state")
{
    ocupado = false;
    normalizado = true;
    for (auto lado : {Lado::Impar, Lado::Par}) {
        prohibido[lado] = false;
        escape[lado] = false;
        ruta[lado] = TipoMovimiento::Ninguno;
        maniobra_compatible[lado] = false;
        estado = EstadoBloqueo::Desbloqueo;
        actc[lado] = ACTC::NoNecesaria;
        cierre_señales[lado] = false;
    }
    for (auto &cv : j["CVs"]) {
        cvs.push_back(secciones[cv]);
    }
}
void bloqueo::message_cv(const std::string &id, estado_cv ecv)
{
    int index = -1;
    for (int i=0; i<cvs.size(); i++) {
        if (cvs[i]->id == id) {
            index = i;
            break;
        }
    }
    if (index < 0) return;
    EstadoCV est_cv = ecv.estado;
    EstadoCV prev_est = ecv.estado_previo;

    bool liberar = false;
    
    bool cvEntradaPar = index == cvs.size()-1;
    bool cvEntradaImpar = index == 0;

    if (est_cv == EstadoCV::Prenormalizado) normalizado = false;

    if (estado == EstadoBloqueo::BloqueoImpar && cvEntradaPar && est_cv <= EstadoCV::Prenormalizado && prev_est == EstadoCV::OcupadoImpar && (!ecv.evento || ecv.evento->lado == Lado::Par)) liberar = true;
    else if (estado == EstadoBloqueo::BloqueoPar && cvEntradaImpar && est_cv <= EstadoCV::Prenormalizado && prev_est == EstadoCV::OcupadoPar && (!ecv.evento || ecv.evento->lado == Lado::Impar)) liberar = true;
    
    if (ecv.evento) {
        bool ocupacion = ecv.evento->ocupacion;
        Lado lado = ecv.evento->lado;
        if (ocupacion && (ecv.estado_previo == EstadoCV::Libre || ecv.estado_previo == EstadoCV::Prenormalizado) && (ecv.estado != EstadoCV::Libre && ecv.estado != EstadoCV::Prenormalizado)) {
            bool esc=false;
            if (index == (lado == Lado::Impar ? 0 : cvs.size()-1)) {
                if (ruta[lado] != TipoMovimiento::Maniobra) {
                    esc = lado == Lado::Impar ? (estado != EstadoBloqueo::BloqueoImpar) : (estado != EstadoBloqueo::BloqueoPar);
                }
            }
            if (index == (lado == Lado::Impar ? 1 : cvs.size()-2)) {
                if (ruta[lado] == TipoMovimiento::Maniobra) {
                    esc = true;
                }
            }
            if (esc) {
                escape[lado] = true;
                log(this->id, lado == Lado::Impar ? "escape impar" : "escape par", LOG_WARNING);
            }
        }
    }

    bool ocupado = false;
    for (auto *sec : cvs) {
        if (sec->get_cv()->estado > EstadoCV::Prenormalizado) {
            ocupado = true;
            break;
        }
    }
    estado_cantones_inicio[Lado::Impar] = EstadoCanton::Libre;
    estado_cantones_inicio[Lado::Par] = EstadoCanton::Libre;
    for (int i=0; i<cvs.size() && (i == 0 || cvs[i]->señal_inicio(Lado::Impar, 0) == nullptr); i++) {
        estado_cantones_inicio[Lado::Impar] = std::max(estado_cantones_inicio[Lado::Impar], cvs[i]->get_cv()->get_ocupacion(Lado::Impar));
    }
    for (int i=cvs.size()-1; i>= 0 && (i == cvs.size()-1 || cvs[i]->señal_inicio(Lado::Par, 0) == nullptr); i--) {
        estado_cantones_inicio[Lado::Par] = std::max(estado_cantones_inicio[Lado::Par], cvs[i]->get_cv()->get_ocupacion(Lado::Par));
    }

    if (ocupado && !this->ocupado) log(this->id, "ocupado");
    this->ocupado = ocupado;

    if (liberar) desbloquear(estado == EstadoBloqueo::BloqueoImpar ? Lado::Impar : Lado::Par, false);
    update();
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
