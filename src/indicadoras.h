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
    virtual void update() = 0;
};
class indicadora_salida : public señal_indicadora
{
    señal_fundamental *señal_salida = nullptr;
public:
    indicadora_salida(const id_elemento &id, const json &j) : señal_indicadora(id, j, TipoIndicadora::Salida) {}
    void update() override;
};
/*struct direccion
{
    id_elemento destino_propio; // Destino de la ruta que asegura la sección de la indicadora
    std::vector<std::pair<señal_fundamental*,id_elemento>> destinos_posteriores;
    bool activo(ruta *r)
    {
        if (r != nullptr && destino_propio.id != "" && r->id_destino != destino_propio) return false;
        for (auto &[sig, dest] : destinos_posteriores) {
            auto it = destinos_ruta.find(dest);
            if (it == destinos_ruta.end()) continue;
            if (it->second->ruta_activa == nullptr || it->second->ruta_activa->get_señal_inicio() != sig) return false;
        }
        return true;
    }
};*/
class indicadora_direccion : public señal_indicadora
{
    std::map<id_elemento, std::string> aspectos;
public:
    indicadora_direccion(const id_elemento &id, const json &j) : señal_indicadora(id, j, TipoIndicadora::Direccion) {}
    void update() override;
};
class indicadora_agujas : public señal_indicadora
{
    std::vector<std::pair<velocidad_agujas, std::string>> aspectos;
    std::vector<std::pair<std::map<seccion_via*, lados<int>>,std::string>> aspectos_aparatos;
public:
    indicadora_agujas(const id_elemento &id, const json &j) : señal_indicadora(id, j, TipoIndicadora::Agujas)
    {
        aspectos.push_back({{0, LadoAguja::Recto}, "↑"}); // ↖↗
    }
    void update() override;
};
