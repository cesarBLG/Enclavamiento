#include "cv.h"
#include "items.h"
#include "topology.h"
RemotaCV cv::get_estado_remota()
{
    RemotaCV r;
    seccion_via *seccion = secciones.size() == 1 ? *secciones.begin() : nullptr;
    TipoMovimiento tipo = seccion != nullptr ? seccion->get_tipo_movimiento() : TipoMovimiento::Ninguno;
    r.CV_DAT = 1;
    r.CV_ME = me_pendiente || (seccion != nullptr && seccion->is_me_pendiente()) ? 1 : 0;
    r.CV_BV = ((seccion != nullptr && seccion->is_bloqueo_seccion()) || btv) ? 1 : 0;
    r.CV_OCUP_TIPO = ocupacion_intempestiva ? 1 : 0;
    if (estado > EstadoCV::Prenormalizado) r.CV_EST = 3;
    else if (tipo == TipoMovimiento::Maniobra) r.CV_EST = 2;
    else if (tipo == TipoMovimiento::Itinerario || tipo == TipoMovimiento::Rebase) r.CV_EST = 1;
    else if (estado == EstadoCV::Prenormalizado) r.CV_EST = 3;
    else r.CV_EST = 0;
    r.CV_DES = 0;
    r.CV_CEJES_AV = averia ? 1 : 0;
    r.CV_CEJES_PREN = estado == EstadoCV::Prenormalizado ? 1 : 0;
    r.CV_UC = 0;
    r.CV_NSEC = perdida_secuencia ? 1 : 0;
    return r;
}
cv_impl::cv_impl(const std::string &id, const json &j) : cv(id), topic("cv/"+id_to_mqtt(id)+"/state"), cejes(j["ContadoresEjes"])
{
    num_ejes = {0, 0};
    ultimo_eje = {0, 0};
    tiempo_auto_prenormalizacion = parametros.diferimetro_prenormalizacion_cv;
    tiempo_auto_prenormalizacion_tren = parametros.diferimetro_prenormalizacion_cv_tren;
    fraccion_ejes_prenormalizacion = parametros.fraccion_ejes_prenormalizacion;
    normalizado = false;
    perdida_secuencia = false;
    estado_raw = estado = estado_previo = EstadoCV::Prenormalizado;

    lados<bool> lados_cejes;
    for (auto &[id, pos] : cejes) {
        lados_cejes[pos.lado] = true;
    }
    if (!lados_cejes[Lado::Impar] || !lados_cejes[Lado::Par]) topera = true;
}
void from_json(const json &j, cv_impl::cejes_position &position)
{
    position.lado = j["Lado"];
    position.reverse = j.value("Reverse", false);
    position.liberar = j.value("Liberar", true);
    position.ocupar = j.value("Ocupar", true);
}
