#pragma once
#include "topology.h"
#include "remota.h"
class pn_enclavado
{
public:
    const std::string id;
    const std::string topic;
protected:
    seccion_via *seccion;
    lados<TipoPN> tipo;
    lados<bool> aviso_cierre={false, false};
    bool cierre_manual=false;
    bool mando_cierre=false;
    bool protegido=false;
    bool perdida_comprobacion=false;

    bool ocupado;

    bool prev_state=false;
    std::shared_ptr<timer> temporizador_apertura;
    lados<int64_t> tiempo_apertura;
public:
    pn_enclavado(const std::string &id, const json &j);
    void send_state()
    {
        if (mando_cierre != prev_state) log(id, mando_cierre ? "cierre" : "apertura");
        send_message(topic, json(mando_cierre).dump());
        prev_state = mando_cierre;
        remota_cambio_elemento(ElementoRemota::PN, id);
    }
    bool permiso_apertura()
    {
        return !cierre_manual && !aviso_cierre.impar && !aviso_cierre.par;
    }
    void activar_ruta(Lado lado)
    {
        if (aviso_cierre[lado]) return;
        if (tipo[lado] != TipoPN::Automatico) {
            aviso_cierre[lado] = true;
            mando_cierre = true;
            update();
        }
    }
    void desactivar_ruta(Lado lado)
    {
        if (!aviso_cierre[lado]) return;
        if (tipo[lado] != TipoPN::Automatico) {
            aviso_cierre[lado] = false;
            update();
        }
    }
    void update()
    {
        if (permiso_apertura() && seccion->get_cv()->get_state() <= EstadoCV::Prenormalizado) {
            mando_cierre = false;
        }
        if (!mando_cierre && perdida_comprobacion) perdida_comprobacion = false;
        if (!mando_cierre && temporizador_apertura != nullptr) {
            clear_timer(temporizador_apertura);
            temporizador_apertura = nullptr;
        }
        send_state();
    }
    RespuestaMando mando(const std::string &cmd)
    {
        if (cmd == "CPN") {
            if (!cierre_manual) {
                cierre_manual = true;
                mando_cierre = true;
                update();
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "APN") {
            if (cierre_manual) {
                cierre_manual = false;
                update();
                return RespuestaMando::Aceptado;
            }
        }
        return RespuestaMando::OrdenRechazada;
    }
    void message_pn(bool protegido)
    {
        if (this->protegido == protegido) return;
        this->protegido = protegido;
        if (!protegido && mando_cierre) perdida_comprobacion = true;
        if (perdida_comprobacion && protegido) perdida_comprobacion = false;
        log(id, protegido ? "protegido" : "sin proteccion");
        update();
    }
    void message_cv(estado_cv ecv)
    {
        if (ecv.evento && ecv.evento->ocupacion) {
            if (tipo[ecv.evento->lado] != TipoPN::Enclavado) aviso_cierre[ecv.evento->lado] = false;
            mando_cierre = true;
            clear_timer(temporizador_apertura);
            int64_t tiempo = tiempo_apertura[ecv.evento->lado];
            if (tiempo > 0) {
                temporizador_apertura = set_timer([this] {
                    if (permiso_apertura()) {
                        mando_cierre = false;
                        update();
                    }
                }, tiempo);
            }
        }
        if (ecv.estado_previo <= EstadoCV::Prenormalizado && ecv.estado > EstadoCV::Prenormalizado) {
            for (Lado l : {Lado::Impar, Lado::Par}) {
                if (tipo[l] != TipoPN::Enclavado) aviso_cierre[l] = false;
            }
            mando_cierre = true;
        }
        ocupado = ecv.estado > EstadoCV::Prenormalizado;
        update();
    }
    RemotaPN get_estado_remota()
    {
        RemotaPN r;
        r.PN_DAT = 1;
        r.PN_FUN = cierre_manual ? 1 : 0;
        r.PN_EST = perdida_comprobacion ? 3 : (mando_cierre != protegido ? 2 : (protegido ? 1 : 0));
        r.PN_ENC1 = ((protegido || perdida_comprobacion) && ((aviso_cierre.impar && tipo.impar == TipoPN::Enclavado) || (aviso_cierre.par && tipo.par == TipoPN::Enclavado))) ? 2 : (mando_cierre ? 1 : 0);
        r.PN_ENC2 = 0;
        r.PN_ENC3 = 0;
        r.PN_ENC4 = 0;
        r.PN_FLC = 0;
        r.PN_FLP = 0;
        r.PN_FAC = 0;
        r.PN_PRX1 = aviso_cierre.impar ? 1 : 0;
        r.PN_PRX2 = aviso_cierre.par ? 1 : 0;
        return r;
    }
    bool is_protegido()
    {
        return protegido;
    }
    TipoPN get_tipo(Lado lado)
    {
        return tipo[lado];
    }
};
