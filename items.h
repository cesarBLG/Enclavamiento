#pragma once
#include "cv.h"
#include "signal.h"
#include "bloqueo.h"
#include "ruta.h"
#include "topology.h"
#include "dependencia.h"
#include "pn_enclavado.h"
#include "nlohmann/json.hpp"
using json = nlohmann::json;
extern std::map<std::string, cv*> cvs;
extern std::map<std::string, señal*> señales;
extern std::map<std::string, señal_impl*> señal_impls;
extern std::map<std::string, bloqueo*> bloqueos;
extern std::set<ruta*> rutas;
extern std::map<std::string, destino_ruta*> destinos_ruta;
extern std::set<std::string> managed_topics;
extern std::map<std::string, seccion_via*> secciones;
extern std::map<std::string, aguja*> agujas;
extern std::map<std::string, dependencia*> dependencias;
extern std::map<std::string, pn_enclavado*> pns;
extern parametros_predeterminados parametros;
void init_items(const json &j);
void loop_items();
