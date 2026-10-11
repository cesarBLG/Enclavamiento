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
class indicadora_direccion : public señal_indicadora
{
    std::map<id_elemento, std::string> aspectos;
public:
    indicadora_direccion(const id_elemento &id, const json &j) : señal_indicadora(id, j, TipoIndicadora::Direccion) {}
    void update() override;
};
class indicadora_agujas : public señal_indicadora
{
    std::vector<std::pair<std::map<seccion_via*, lados<int>>,std::string>> aspectos;
public:
    indicadora_agujas(const id_elemento &id, const json &j) : señal_indicadora(id, j, TipoIndicadora::Agujas) {}
    void update() override;
};
