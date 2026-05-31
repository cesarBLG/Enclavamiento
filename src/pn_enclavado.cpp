#include "enclavamiento.h"
#include "pn_enclavado.h"
#include "items.h"
pn_enclavado::pn_enclavado(const id_elemento &id, const json &j) : id(id), topic("pn/"+id_to_mqtt(id.id)+"/cierre")
{
    seccion = secciones[id_elemento(j["Sección"])];
    seccion->pns.insert(this);
    if (j.contains("TiempoApertura")) {
        tiempo_apertura = j["TiempoApertura"];
        tiempo_apertura[Lado::Impar] *= 1000;
        tiempo_apertura[Lado::Par] *= 1000;
    } else {
        tiempo_apertura[Lado::Impar] = tiempo_apertura[Lado::Par] = 0;
    }
    if (j.contains("Tipo"))
        tipo = j["Tipo"];
    else if (seccion->is_trayecto())
        tipo = {TipoPN::Automatico, TipoPN::Automatico};
    else
        tipo = {TipoPN::Enclavado, TipoPN::Enclavado};
}
