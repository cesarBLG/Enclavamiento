#pragma once
#include <chrono>
struct parametros_predeterminados
{
    int64_t diferimetro_dai1;
    int64_t diferimetro_dai2;
    int64_t diferimetro_dei;
    int64_t diferimetro_prenormalizacion_cv;
    int64_t diferimetro_prenormalizacion_cv_tren;
    int64_t tiempo_espera_fai;
    double fraccion_ejes_prenormalizacion;
    bool deslizamiento_bloqueo;
};
#ifndef WITHOUT_JSON
#include "json.h"
void from_json(const json &j, parametros_predeterminados &params);
#endif
