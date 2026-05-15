#include "aguja.h"
#include "ruta.h"
aguja::aguja(const std::string &id, const json &j) : seccion_via(id, j, TipoSeccion::Aguja), lado(j["Lado"]), topic_mando("aguja/"+id_to_mqtt(id)+"/mando")
{
    if (j.contains("SecciónPunta")) siguientes_secciones[opp_lado(lado)] = std::vector<conexion>({j["SecciónPunta"].get<conexion>()});
    if (j.contains("SeccionesTalón")) siguientes_secciones[lado] = j["SeccionesTalón"];
    talonable = true;
    update();
}
RemotaAG aguja::get_estado_remota()
{
    RemotaAG r;
    r.AG_DAT = 1;
    r.AG_ME = me_pendiente ? 1 : 0;
    r.AG_BIA = bloqueo_seccion ? 1 : 0;
    r.AG_OCUP_TIPO = cv_seccion->ocupacion_intempestiva;
    if (cv_seccion->get_state() > EstadoCV::Prenormalizado && (cv_seccion->ocupacion_intempestiva || ruta_asegurada)) r.AG_EST = 3;
    else if (ruta_asegurada && ruta_asegurada->ruta_asegurada->tipo == TipoMovimiento::Maniobra) r.AG_EST = 2;
    else if (ruta_asegurada && (ruta_asegurada->ruta_asegurada->tipo == TipoMovimiento::Itinerario || ruta_asegurada->ruta_asegurada->tipo == TipoMovimiento::Rebase)) r.AG_EST = 1;
    else if (cv_seccion->get_state() == EstadoCV::Prenormalizado) r.AG_EST = 3;
    else r.AG_EST = 0;
    if (!ruta_asegurada) r.AG_DIR = 0;
    else r.AG_DIR = ruta_asegurada->outs[lado] == 1 ? 2 : 1;
    r.AG_DES_N = 0;
    r.AG_DES_I = 0;
    if (comprobacion == PosicionAguja::Normal) r.AG_COMP = 3;
    else if (comprobacion == PosicionAguja::Invertida) r.AG_COMP = 4;
    else if (mandada && mandada->first == PosicionAguja::Normal) r.AG_COMP = 1;
    else if (mandada && mandada->first == PosicionAguja::Invertida) r.AG_COMP = 2;
    else r.AG_COMP = 0;
    r.AG_BA = bloqueo ? 1 : 0;
    if (!enclavada.empty()) r.AG_ENC = 1;
    else if (mandada && comprobacion != mandada->first) r.AG_ENC = 2;
    else r.AG_ENC = 0;
    r.AG_GAL = 0;
    return r;
}
