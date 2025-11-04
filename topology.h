#pragma once
#include <set>
#include <map>
#include <vector>
#include "lado.h"
#include "enums.h"
#include "mqtt.h"
#include "time.h"
#include "log.h"
#include "cv.h"
#include <optional>
class ruta;
class señal;
class seccion_via
{
public:
    struct conexion
    {
        std::string id;
        bool invertir_paridad;
    };
    const std::string id;
protected:
    lados<std::string> señales;
    cv *cv_seccion;

    lados<std::vector<conexion>> siguientes_secciones;
    lados<std::map<int,int>> active_outs;
    lados<int> route_outs;
    ruta *ruta_asegurada;
public:
    seccion_via(const std::string &id, const json &j);
    seccion_via* siguiente_seccion(seccion_via *prev, Lado &dir);
    std::pair<seccion_via*,Lado> get_seccion_in(Lado dir, int pin=0);
    void prev_secciones(seccion_via *next, Lado dir_fwd, std::vector<std::pair<seccion_via*, Lado>> &secciones);
    señal *señal_inicio(Lado lado);
    cv *get_cv()
    {
        return cv_seccion;
    }
    void asegurar(ruta *ruta, seccion_via *prev, seccion_via *next, Lado dir);
    void liberar(ruta *ruta)
    {
        if (ruta_asegurada == ruta) {
            ruta_asegurada = nullptr;
            route_outs = {-1, -1};
        }
    }
    bool is_asegurada(ruta *ruta)
    {
        return ruta_asegurada == ruta;
    }
    bool is_asegurada()
    {
        return ruta_asegurada != nullptr;
    }
protected:
    int get_in(seccion_via* prev, Lado dir);
    int get_out(seccion_via* next, Lado dir);
};
void from_json(const json &j, seccion_via::conexion &conex);