#pragma once
#include <enclavamiento.h>
#include "topology.h"
class aguja : public seccion_via, public estado_aguja
{
    std::optional<PosicionAguja> comprobacion;
    std::optional<std::pair<PosicionAguja, int64_t>> mandada;
    bool talonable;
    std::optional<PosicionAguja> talonable_muelle;
    bool cedida_mantenimiento;
    bool bloqueo;
    std::set<ruta*> enclavada;
    Lado lado;
    public:
    const std::string topic_mando;
    aguja(const std::string &id, const json &j);
    void update()
    {
        if (talonable_muelle) comprobacion = talonable_muelle;
        if (talonable) {
            active_outs[opp_lado(lado)][0] = 0;
            active_outs[opp_lado(lado)][1] = 0;
        } else if (comprobacion) {
            bool invertida = comprobacion == PosicionAguja::Invertida;
            active_outs[opp_lado(lado)][invertida ? 1 : 0] = 0;
            active_outs[opp_lado(lado)][invertida ? 0 : 1] = -1;
        } else {
            active_outs[opp_lado(lado)][0] = -1;
            active_outs[opp_lado(lado)][1] = -1;
        }
        active_outs[lado][0] = comprobacion == PosicionAguja::Invertida ? 1 : (comprobacion ? 0 : -1);
        remota_cambio_elemento(ElementoRemota::AG, id);
    }
    void message_aguja(const std::string &comp)
    {
        auto prev = comprobacion;
        if (comp == "0") comprobacion = PosicionAguja::Normal;
        else if (comp == "1") comprobacion = PosicionAguja::Invertida;
        else comprobacion = std::nullopt;
        if (comprobacion != prev) {
            if (comprobacion == PosicionAguja::Normal) log(id, "comprobando a normal", LOG_INFO);
            else if (comprobacion == PosicionAguja::Invertida) log(id, "comprobando a invertida", LOG_INFO);
            else log(id, "sin comprobación", LOG_INFO);
        }
        update();
    }
    void message_cv(const std::string &id, estado_cv ev) override
    {
        if (id != id_cv) return;
        seccion_via::message_cv(id, ev);
    }
    void liberar(ruta *ruta)
    {
        seccion_via::liberar(ruta);
        enclavada.erase(ruta);
    }
    bool is_ruta_fija(seccion_via *prev, Lado dir)
    {
        if (!talonable_muelle) return false;
        if (dir == lado) return true;
        int in = get_in(prev, dir);
        return in == (talonable_muelle == PosicionAguja::Invertida ? 1 : 0);
    }
    bool posible_mover(PosicionAguja pos)
    {
        if ((mandada && mandada->first == pos) || comprobacion == pos) return true;
        if (bloqueo || !enclavada.empty() || talonable_muelle) return false;
        return true;
    }
    bool mover(PosicionAguja pos)
    {
        if (!posible_mover(pos)) return false;
        if (comprobacion == pos) return true;
        mandada = {pos, get_milliseconds()};
        comprobacion = std::nullopt;
        send_message(topic_mando, pos == PosicionAguja::Invertida ? "1" : "0");
        update();
        log(id, pos == PosicionAguja::Invertida ? "mandada a invertida" : "mandada a normal");
        return true;
    }
    bool enclavar(ruta *r, PosicionAguja pos)
    {
        if (enclavada.find(r) != enclavada.end()) return true;
        if (comprobacion != pos) return false;
        enclavada.insert(r);
        log(id, "enclavada", LOG_INFO);
        return true;
    }
    PosicionAguja get_posicion(Lado dir, int in, int out)
    {
        return (dir == lado ? out : in) == 1 ? PosicionAguja::Invertida : PosicionAguja::Normal;
    }
    RespuestaMando mando(const std::string &cmd, int me) override
    {
        if (cmd == "MAT") {
            if (talonable_muelle) {
                talonable_muelle = talonable_muelle == PosicionAguja::Invertida ? PosicionAguja::Normal : PosicionAguja::Invertida;
                update();
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "ATN") {
            if (talonable_muelle != PosicionAguja::Normal) {
                talonable_muelle = PosicionAguja::Normal;
                update();
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "ATI") {
            if (talonable_muelle != PosicionAguja::Invertida) {
                talonable_muelle = PosicionAguja::Invertida;
                update();
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "MA" || cmd == "AN" || cmd == "AI") {
            PosicionAguja pos;
            if (cmd == "AN" || (cmd == "MA" && !mandada && comprobacion && *comprobacion == PosicionAguja::Invertida) || (cmd == "MA" && mandada && mandada->first == PosicionAguja::Invertida))
                pos = PosicionAguja::Normal;
            else if (cmd == "AI" || (cmd == "MA" && !mandada && comprobacion && *comprobacion == PosicionAguja::Normal) || (cmd == "MA" && mandada && mandada->first == PosicionAguja::Normal))
                pos = PosicionAguja::Invertida;
            else
                return RespuestaMando::OrdenRechazada;
            if (mover(pos))
                return RespuestaMando::Aceptado;
        } else if (cmd == "BA") {
            if (!bloqueo) {
                bloqueo = true;
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "ABA") {
            if (bloqueo) {
                bloqueo = false;
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "BIA") {
            return seccion_via::mando("BIV", me);
        } else if (cmd == "DIA") {
            return seccion_via::mando("DIV", me);
        }
        return RespuestaMando::OrdenRechazada;
    }
    RemotaAG get_estado_remota();
};
