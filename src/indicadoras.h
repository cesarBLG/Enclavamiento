#pragma once
#include "signal.h"
class señal_indicadora : public señal
{
public:
    const std::string topic;
    const TipoIndicadora tipo;
protected:
    std::string_view aspecto = "Apagada";
public:
    señal_indicadora(const id_elemento &id, const json &j, TipoIndicadora tipo) : señal(id, j, ClaseSeñal::Indicadora), tipo(tipo), topic("signal/"+id_to_mqtt(id.id)+"/state") {}
    void update()
    {
        auto prev_aspecto = aspecto;
        determinar_aspecto();
        if (prev_aspecto != aspecto) send_state();
    }
    virtual void determinar_aspecto() = 0;
    static señal_indicadora *from_json(const id_elemento &id, const json &j);
    void send_state()
    {
        send_message(topic, std::string(aspecto));
    }
};
class indicadora_salida : public señal_indicadora
{
    señal_fundamental *señal_salida = nullptr;
public:
    indicadora_salida(const id_elemento &id, const json &j) : señal_indicadora(id, j, TipoIndicadora::Salida) {}
    void determinar_aspecto() override;
};
class indicadora_direccion : public señal_indicadora
{
    struct ruta_compuesta
    {
        id_elemento destino_propio;
        std::vector<std::pair<id_elemento,id_elemento>> rutas;
        std::string aspecto;
    };
    std::vector<ruta_compuesta> aspectos;
    std::map<id_elemento, estado_fin_ruta> destinos;
public:
    indicadora_direccion(const id_elemento &id, const json &j);
    void determinar_aspecto() override;
    void message_destino(const id_elemento &id, const estado_fin_ruta &est)
    {
        auto it = destinos.find(id);
        if (it != destinos.end()) it->second = est;
    }
};
class indicadora_agujas : public señal_indicadora
{
    std::vector<std::string> aspectos;
    std::vector<velocidad_agujas> velocidades;
    std::vector<std::pair<std::map<seccion_via*, lados<int>>,velocidad_agujas>> aspectos_aparatos;
public:
    indicadora_agujas(const id_elemento &id, const json &j);
    void determinar_aspecto() override;
};
