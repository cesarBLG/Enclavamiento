#include "enclavamiento.h"
#include "pn_enclavado.h"
#include "items.h"
pn_enclavado::pn_enclavado(const std::string &id, const json &j) : id(id), topic("pn/"+id_to_mqtt(id)+"/cierre")
{
    seccion = secciones[j["Sección"]];
    seccion->pns.insert(this);
    if (j.contains("TiempoApertura")) {
        tiempo_apertura = j["TiempoApertura"];
        tiempo_apertura[Lado::Impar] *= 1000;
        tiempo_apertura[Lado::Par] *= 1000;
    } else {
        tiempo_apertura[Lado::Impar] = tiempo_apertura[Lado::Par] = 0;
    }
}
