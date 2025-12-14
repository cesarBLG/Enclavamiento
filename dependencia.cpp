#include "dependencia.h"
#include "items.h"
void dependencia::calcular_vinculacion_bloqueos()
{
    // Para cada bloqueo, indicar si es posible realizar cruces, o si las señales de la estación se deben comportar
    // como un puesto de bloqueo
    // Condición actual: no existen agujas talonables que conecten los bloqueos en ambas direcciones
    // Condición a incluir: estación cerrada con agujas no talonables
    for (auto &[id, bloq] : bloqueos) {
        std::string vinculo = "";
        Lado dir = opp_lado(bloq->lado);
        seccion_via *prev = bloq->get_cvs()[0];
        seccion_via *sec = prev->siguiente_seccion(nullptr, dir, false);
        bool señal_ruta = false;
        while (sec != nullptr) {
            if (sec->tipo == TipoSeccion::Aguja && !((aguja*)sec)->is_ruta_fija(prev, dir)) break;
            auto *sig = sec->señal_inicio(dir, prev);
            if (sig != nullptr && señal_impls.find(sig->id) != señal_impls.end() && señal_impls[sig->id]->ruta_necesaria)
                señal_ruta = true;
            if (sec->bloqueo_asociado != "") {
                for (auto &[id2, bloq2] : bloqueos) {
                    if (bloq2->get_cvs()[0] == sec) {
                        vinculo = id2;
                        break;
                    }
                }
                break;
            }
            auto *next = sec->siguiente_seccion(prev, dir, false);
            prev = sec;
            sec = next;
        }
        bloq->vincular(vinculo, !señal_ruta);
    }
}
