#include "cv.h"
#include "items.h"
#include "topology.h"
bool cv::is_asegurada(ruta *ruta)
{
    for (auto *sec : secciones) {
        if (!sec->is_asegurada(ruta)) return false;
    }
    return true;
}
RemotaCV cv::get_estado_remota()
{
    RemotaCV r;
    seccion_via *seccion = secciones.size() == 1 ? *secciones.begin() : nullptr;
    TipoMovimiento tipo = seccion != nullptr ? seccion->get_tipo_movimiento() : TipoMovimiento::Ninguno;
    r.CV_DAT = 1;
    r.CV_ME = me_pendiente ? 1 : 0;
    r.CV_BV = ((seccion != nullptr && seccion->is_bloqueo_seccion()) || btv) ? 1 : 0;
    r.CV_OCUP_TIPO = ocupacion_intempestiva ? 1 : 0;
    if (estado > EstadoCV::Prenormalizado) r.CV_EST = 3;
    else if (tipo == TipoMovimiento::Maniobra) r.CV_EST = 2;
    else if (tipo == TipoMovimiento::Itinerario) r.CV_EST = 1;
    else if (estado == EstadoCV::Prenormalizado) r.CV_EST = 3;
    else r.CV_EST = 0;
    r.CV_DES = 0;
    r.CV_CEJES_AV = averia ? 1 : 0;
    r.CV_CEJES_PREN = estado == EstadoCV::Prenormalizado ? 1 : 0;
    r.CV_UC = 0;
    r.CV_NSEC = perdida_secuencia ? 1 : 0;
    return r;
}
cv_impl::cv_impl(const std::string &id, const json &j) : id(id), topic("cv/"+id_to_mqtt(id)+"/state"), cejes(j["ContadoresEjes"])
{
    normalizado = false;
    perdida_secuencia = false;
    estado = estado_previo = EstadoCV::Prenormalizado;
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
void from_json(const json &j, cv_impl::cejes_position &position)
{
    position.lado = j["Lado"];
    position.reverse = j.value("Reverse", false);
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
