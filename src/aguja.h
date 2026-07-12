#pragma once
#include <enclavamiento.h>
#include "topology.h"
class aguja : public seccion_via, public estado_aguja
{
    std::optional<PosicionAguja> comprobacion;
    std::optional<std::pair<PosicionAguja, int64_t>> mandada;
    bool talonable;
    std::optional<PosicionAguja> talonable_muelle;
    bool cedida_mantenimiento = false;
    bool bloqueo = false;;
    std::set<movimiento*> enclavada;
    std::optional<PosicionAguja> posicion_enclavada;
    Lado lado;
    public:
    aguja *escape = nullptr;
    const std::string topic_mando;
    aguja(const id_elemento &id, const json &j);
    void update()
    {
        if (talonable_muelle && (!mandada || mandada->first != talonable_muelle)) mandada = {*talonable_muelle, 0};
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

        if (mandada && mandada->second != 0) {
            if (mandada->first == comprobacion) {
                mandada->second = 0;
            } else if (get_milliseconds() - mandada->second > 15000) {
                mandada->second = 0;
            }
        }

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
        if (!mandada && comprobacion) mandada = {*comprobacion, 0};
        update();
    }
    void message_cv(const id_elemento &id, estado_cv ev) override
    {
        if (id != id_cv) return;
        seccion_via::message_cv(id, ev);
    }
    void asegurar(movimiento *ruta, int in, int out, std::optional<Lado> dir) override
    {
        seccion_via::asegurar(ruta, in, out, dir);
        remota_cambio_elemento(ElementoRemota::AG, id);
    }
    void liberar(movimiento *ruta)
    {
        seccion_via::liberar(ruta);
        if (escape != nullptr) {
            if (!escape->ruta_asegurada || escape->ruta_asegurada->ruta_asegurada != ruta) {
                enclavada.erase(ruta);
                if (enclavada.empty()) posicion_enclavada = std::nullopt;
                escape->escape = nullptr;
                escape->liberar(ruta);
                escape->escape = this;
            }
        } else {
            enclavada.erase(ruta);
            if (enclavada.empty()) posicion_enclavada = std::nullopt;
        }
        remota_cambio_elemento(ElementoRemota::AG, id);
    }
    bool transitable(int pin, Lado dir) override
    {
        if (!seccion_via::transitable(pin, dir))
            return false;
        if (posicion_enclavada && posicion_enclavada != comprobacion && (!talonable || dir == lado))
            return false;
        if (escape != nullptr && escape->posicion_enclavada && escape->posicion_enclavada != escape->comprobacion && escape->posicion_enclavada == PosicionAguja::Normal)
            return false;
        return true;
    }
    seccion_via *ruta_fija(seccion_via *prev, Lado &dir);
    bool posible_mover(PosicionAguja pos, bool anular_pedal=false)
    {
        if (posicion_enclavada && posicion_enclavada != pos) return false;
        if ((mandada && mandada->first == pos) || comprobacion == pos) return true;
        if (bloqueo || !enclavada.empty() || talonable_muelle) return false;
        if (!anular_pedal && cv_seccion != nullptr && cv_seccion->get_state() > EstadoCV::Prenormalizado) return false;
        if (escape != nullptr && (pos == PosicionAguja::Normal || !escape->talonable)) {
            escape->escape = nullptr;
            if (!escape->posible_mover(pos, anular_pedal)) {
                escape->escape = this;
                return false;
            }
            escape->escape = this;
        }
        return true;
    }
    bool mover(PosicionAguja pos, bool anular_pedal=false)
    {
        if (!posible_mover(pos, anular_pedal)) return false;
        if (escape != nullptr && (pos == PosicionAguja::Normal || !escape->talonable)) {
            escape->escape = nullptr;
            escape->mover(pos, anular_pedal);
            escape->escape = this;
        }
        if (comprobacion == pos) {
            if (mandada && mandada->first != pos) {
                mandada = {pos, 0};
                update();
            }
            return true;
        }
        mandada = {pos, get_milliseconds()};
        if (mandada->second == 0) mandada->second = 1;
        comprobacion = std::nullopt;
        send_message(topic_mando, pos == PosicionAguja::Invertida ? "1" : "0");
        update();
        log(id, pos == PosicionAguja::Invertida ? "mandada a invertida" : "mandada a normal");
        return true;
    }
    bool enclavar(movimiento *r, PosicionAguja pos)
    {
        if (enclavada.find(r) != enclavada.end()) return true;
        if (comprobacion != pos || (posicion_enclavada && posicion_enclavada != pos)) return false;
        if (mandada && mandada->first != pos) return false;
        if (escape != nullptr && (pos == PosicionAguja::Normal || !escape->talonable)) {
            escape->escape = nullptr;
            if (!escape->enclavar(r, pos)) {
                escape->escape = this;
                return false;
            }
            escape->escape = this;
        }
        enclavada.insert(r);
        posicion_enclavada = pos;
        log(id, "enclavada", LOG_INFO);
        update();
        return true;
    }
    PosicionAguja get_posicion(Lado dir, int in, int out)
    {
        return (dir == lado ? out : in) == 1 ? PosicionAguja::Invertida : PosicionAguja::Normal;
    }
    RespuestaMando mando(const std::string &cmd, int me) override;
    RemotaAG get_estado_remota();
};
