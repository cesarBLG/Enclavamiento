#pragma once
#include "enums.h"
#include <vector>
#include <map>
#include "lado.h"
#include "mqtt.h"
#include "cv.h"
#include "log.h"
struct estado_bloqueo
{
    EstadoBloqueo estado;
    bool ocupado;
    lados<bool> prohibido;
    lados<bool> escape;
    lados<TipoMovimiento> ruta;
};
void to_json(json &j, const estado_bloqueo &estado);
void from_json(const json &j, estado_bloqueo &estado);
class bloqueo
{
    EstadoBloqueo estado;
    std::vector<std::string> cvs;
    std::map<std::string, EstadoCV> estado_cvs;
    bool ocupado;
    lados<std::string> estaciones;
    std::string via;
    lados<bool> prohibido;
    lados<bool> escape;
    lados<TipoMovimiento> ruta;
    lados<bool> maniobra_compatible;

    Lado ultimo_lado;

public:
    const std::string id;
    const std::string topic;
    bloqueo(const json &j) : estaciones(j["Estaciones"]), via(j.value("Vía", "")), cvs(j["CVs"]), id(estaciones[Lado::Impar]+"-"+estaciones[Lado::Par]+(via != "" ? "_" : "")+via), topic("bloqueo/"+id+"/state")
    {
        ocupado = false;
        for (auto lado : {Lado::Impar, Lado::Par}) {
            prohibido[lado] = false;
            escape[lado] = false;
            ruta[lado] = TipoMovimiento::Ninguno;
            maniobra_compatible[lado] = false;
            estado = EstadoBloqueo::Desbloqueo;
        }
    }

    estado_bloqueo get_state()
    {
        return estado_bloqueo({estado, ocupado, prohibido, escape, ruta});
    }

    void send_state()
    {
        json j = get_state();
        send_message(topic, j.dump(), 1);
    }

    bool establecer(Lado lado)
    {
        if (estado == EstadoBloqueo::Desbloqueo && !ocupado && !escape[Lado::Impar] && !escape[Lado::Par] && !prohibido[lado] && ruta[opp_lado(lado)] != TipoMovimiento::Itinerario && (ruta[opp_lado(lado)] != TipoMovimiento::Maniobra || maniobra_compatible[opp_lado(lado)])) {
            estado = lado == Lado::Impar ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar;
            ultimo_lado = lado;
            log(id, "establecido", LOG_DEBUG);
            return true;
        }
        return false;
    }

    void update()
    {
        if (ruta[Lado::Impar] == TipoMovimiento::Itinerario) establecer(Lado::Impar);
        if (ruta[Lado::Par] == TipoMovimiento::Itinerario) establecer(Lado::Par);
        send_state();
    }

    void message_cv(const std::string &id, estado_cv ecv)
    {
        int index = -1;
        for (int i=0; i<cvs.size(); i++) {
            if (cvs[i] == id) {
                index = i;
                break;
            }
        }
        if (index < 0) return;
        EstadoCV est_cv = ecv.estado;
        if (estado_cvs[id] == est_cv) return;

        bool ocupado = false;
        bool liberar = false;
        
        bool cvEntradaPar = index == cvs.size()-1;
        bool cvEntradaImpar = index == 0;

        if (estado == EstadoBloqueo::BloqueoImpar && cvEntradaPar && estado_cvs[id] == EstadoCV::OcupadoImpar && (!ecv.evento || ecv.evento->first == Lado::Par)) liberar = true;
        else if (estado == EstadoBloqueo::BloqueoPar && cvEntradaImpar && estado_cvs[id] == EstadoCV::OcupadoPar && (!ecv.evento || ecv.evento->first == Lado::Impar)) liberar = true;
        
        if (ecv.evento) {
            bool ocupacion = ecv.evento->second;
            Lado lado = ecv.evento->first;
            if (ocupacion && (ecv.estado_previo == EstadoCV::Libre || ecv.estado_previo == EstadoCV::Prenormalizado) && (ecv.estado != EstadoCV::Libre && ecv.estado != EstadoCV::Prenormalizado)) {
                bool esc=false;
                if (index == (ecv.evento->first == Lado::Impar ? 0 : cvs.size()-1)) {
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

        estado_cvs[id] = est_cv;
        for (auto &kvp : estado_cvs) {
            if (kvp.second != EstadoCV::Libre && kvp.second != EstadoCV::Prenormalizado) {
                ocupado = true;
                break;
            }
        }

        liberar = liberar && !ocupado;
        if (estado == EstadoBloqueo::BloqueoImpar && !ocupado && ruta[Lado::Impar] == TipoMovimiento::Itinerario && !prohibido[Lado::Impar]) liberar = false;
        if (estado == EstadoBloqueo::BloqueoPar && !ocupado && ruta[Lado::Par] == TipoMovimiento::Itinerario && !prohibido[Lado::Par]) liberar = false;
        if (liberar) {
            estado = EstadoBloqueo::Desbloqueo;
            log(this->id, "desbloqueo");
        }
    
        if (ocupado && !this->ocupado) log(this->id, "ocupado");
        this->ocupado = ocupado;
        update();
    }

    bool mando(const std::string &est1, const std::string &est2, const std::string &cmd)
    {
        bool aceptado = false;
        Lado lado;
        if (est1 == estaciones[Lado::Impar] && est2 == estaciones[Lado::Par]+via) lado = Lado::Impar;
        else if (est2 == estaciones[Lado::Impar]+via && est1 == estaciones[Lado::Par]) lado = Lado::Par;
        else return false;
        if (cmd == "B") aceptado = establecer(lado);
        else if (cmd == "AB") {
            if (lado == Lado::Impar && estado == EstadoBloqueo::BloqueoImpar && ruta[Lado::Impar] != TipoMovimiento::Itinerario) {
                estado = EstadoBloqueo::Desbloqueo;
                aceptado = true;
                log(id, "desbloqueo", LOG_DEBUG);
            } else if (lado == Lado::Par && estado == EstadoBloqueo::BloqueoPar && ruta[Lado::Par] != TipoMovimiento::Itinerario) {
                estado = EstadoBloqueo::Desbloqueo;
                aceptado = true;
                log(id, "desbloqueo", LOG_DEBUG);
            }
            if (!ocupado && escape[opp_lado(lado)]) {
                escape[opp_lado(lado)] = false;
                aceptado = true;
                log(id, "escape normalizado", LOG_INFO);
            }
        } else if (cmd == "PB") {
            prohibido[lado] = true;
            aceptado = true;
        } else if (cmd == "APB") {
            prohibido[lado] = false;
            aceptado = true;
        } else if (cmd == "NB") {}
        update();
        return aceptado;
    }

    void message_ruta(const std::string &estacion, TipoMovimiento tipo)
    {
        Lado lado;
        if (estacion == estaciones[Lado::Impar]) lado = Lado::Impar;
        else if (estacion == estaciones[Lado::Par]) lado = Lado::Par;
        else return;
        ruta[lado] = tipo;
        update();
    }
};
