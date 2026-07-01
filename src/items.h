#pragma once
#include "cv.h"
#include "signal.h"
#include "bloqueo.h"
#include "ruta.h"
#include "deslizamiento.h"
#include "topology.h"
#include "dependencia.h"
#include "pn_enclavado.h"
#include "frontera.h"
#include "aguja.h"
#include "nlohmann/json.hpp"
using json = nlohmann::json;
extern std::map<id_elemento, cv*> cvs;
extern std::map<id_elemento, cv_impl*> cv_impls;
extern std::map<id_elemento, señal*> señales;
extern std::map<id_elemento, señal_impl*> señal_impls;
extern std::map<id_elemento, bloqueo*> bloqueos;
extern std::set<ruta*> rutas;
extern std::map<id_elemento, destino_ruta*> destinos_ruta;
extern std::set<std::string> managed_topics;
extern std::map<id_elemento, seccion_via*> secciones;
extern std::map<id_elemento, aguja*> agujas;
extern std::map<std::string, dependencia*> dependencias;
extern std::map<id_elemento, pn_enclavado*> pns;
extern parametros_predeterminados parametros;
void init_items(const json &j);
void loop_items();
