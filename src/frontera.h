#pragma once
#include <enclavamiento.h>
#include "signal.h"
#include "ruta.h"
struct estado_frontera_entrada
{
    bool autorizacion_entrada;
    Aspecto aspecto_señal;
    TipoMovimiento tipo_movimiento;
    bool dei;
};
struct estado_frontera_salida
{
    bool autorizacion_salida;
    TipoMovimiento tipo_movimiento;
    bool dai;
    bool ocupada;
    bool supervisada;
};
class frontera
{
    bool autorizacion_entrada = true;
    bool autorizacion_salida = true;
    estado_frontera_entrada entrada_colateral;
    estado_frontera_salida salida_colateral;
    señal_impl *señal_entrada;
    señal_impl *señal_salida;
    ruta *ruta_entrada = nullptr;
    ruta *ruta_salida = nullptr;
    public:
    const std::string id;
    void send_entrada(bool dei=false)
    {
        update_rutas();
        estado_frontera_entrada e;
        e.autorizacion_entrada = autorizacion_entrada;
        if (ruta_salida != nullptr) {
            e.aspecto_señal = ruta_salida->get_señal_inicio()->get_state();
            e.tipo_movimiento = ruta_salida->tipo;
        } else {
            e.aspecto_señal = Aspecto::Parada;
            e.tipo_movimiento = TipoMovimiento::Ninguno;
        }
        e.dei = dei;
    }
    void send_salida(bool dai=false)
    {
        update_rutas();
        estado_frontera_salida s;
        s.autorizacion_salida = autorizacion_salida;
        if (ruta_entrada != nullptr) {
            s.ocupada = ruta_entrada->is_ocupada();
            s.supervisada = ruta_entrada->is_supervisada();
            s.tipo_movimiento = ruta_entrada->tipo;
        } else {
            s.ocupada = s.supervisada = false;
            s.tipo_movimiento = TipoMovimiento::Ninguno;
        }
        s.dai = dai;
    }
    void update_rutas()
    {
        ruta_entrada = señal_entrada->ruta_fin;
        ruta_salida = señal_salida->ruta_activa;
    }
    bool salida_permitida(ruta* r)
    {
        update_rutas();
        if (!entrada_colateral.autorizacion_entrada || !autorizacion_salida || ruta_salida != nullptr || (ruta_entrada != nullptr && ruta_entrada != r)) {
            return false;
        }
        if (r->tipo != salida_colateral.tipo_movimiento) {
            return false;
        }
        return true;
    }
    bool entrada_permitida(ruta *r)
    {
        update_rutas();
        if (!salida_colateral.autorizacion_salida || !autorizacion_entrada || ruta_entrada != nullptr || (ruta_salida != nullptr && ruta_salida != r)) {
            return false;
        }
        if (entrada_colateral.tipo_movimiento != TipoMovimiento::Ninguno && r->tipo != entrada_colateral.tipo_movimiento) {
            return false;
        }
        return true;
    }
    bool dai_salida_permitido()
    {
        return salida_colateral.tipo_movimiento == TipoMovimiento::Ninguno || !salida_colateral.supervisada;
    }
    bool dei_salida_permitido()
    {
        return salida_colateral.tipo_movimiento != TipoMovimiento::Ninguno && salida_colateral.supervisada && salida_colateral.ocupada;
    }
    bool dei_entrada_permitido()
    {
        return entrada_colateral.tipo_movimiento == TipoMovimiento::Ninguno;
    }
    void disolver_salida()
    {
        send_salida(true);
    }
    void disolver_entrada()
    {
        send_entrada(true);
    }
    Aspecto get_aspecto_entrada()
    {
        if (!entrada_colateral.autorizacion_entrada) return Aspecto::Parada;
        return entrada_colateral.aspecto_señal;
    }
};
