#include <regex>
#include <fstream>
#include "items.h"
#include "log.h"

std::map<std::string, cv*> cvs;
std::map<std::string, cv_impl*> cv_impls;
std::map<std::string, señal*> señales;
std::set<bloqueo*> bloqueos;
std::set<ruta*> rutas;
std::map<std::string, gestor_rutas*> grutas;

std::set<std::string> comandos_ruta = {"I","R","M","DAI","DEI","BS","ABS","DS","BDE","BD","ABDE","ABD","SA","ASA","FAI","AFA","DEI","BDS","ABDS"};
std::set<std::string> comandos_señal = {"CS","CSEÑ","NPS"};
std::set<std::string> comandos_bloqueo = {"B","AB","CSB","NSB","PB","APB","NB","AS","AAS"};
std::set<std::string> comandos_cv = {"BV","BIV","ABV","DIV","BTV","ABTV","DTV","FO","AFO","LC"};
std::set<std::string> mandos_especiales = {"DEI","DS","ABDE","ABD","MAE","ANE","AIE","EMA","ABA","DIA","AM","AAM","RTA","MCE","CCE","CUE","ABC","DIC","NSB","APB","NB","TME","RM","LD","LN","DCA","LE","ABV","DIV","ABTV","DTV","LC","AMLE"};

int mando(const std::vector<std::string> &ordenes)
{
    if (ordenes.size() <= 0) return 1;
    std::string cmd = ordenes[0];
    if (mandos_especiales.find(cmd) != mandos_especiales.end()) {
        //return 2;
    }
    bool aceptado = false;
    if (comandos_cv.find(cmd) != comandos_cv.end()) {
        if (ordenes.size() == 3) {
            std::string id = ordenes[1]+"/"+ordenes[2];
            auto it = cv_impls.find(id);
            if (it != cv_impls.end()) aceptado |= it->second->mando(ordenes[0]);
        }
    }
    if (comandos_bloqueo.find(cmd) != comandos_bloqueo.end()) {
        if (ordenes.size() == 3) {
            for (auto *b : bloqueos) {
                aceptado |= b->mando(ordenes[1], ordenes[2], ordenes[0]);
            }
        }
    }
    if (comandos_ruta.find(cmd) != comandos_ruta.end()) {
        auto it = grutas.find(ordenes[1]);
        if (it != grutas.end()) {
            if (ordenes.size() == 4) aceptado |= it->second->mando(ordenes[2] ,ordenes[3], ordenes[0]);
            else if (ordenes.size() == 3) aceptado |= it->second->mando(ordenes[2], ordenes[0]);
        }
    }
    if (comandos_señal.find(cmd) != comandos_señal.end()) {
        if (ordenes.size() == 3) {
            auto it = señales.find(ordenes[1]+"/"+ordenes[2]);
            if (it != señales.end()) aceptado |= it->second->mando(ordenes[0]);
        }
    }
    return aceptado ? 0 : 1;
}
void handle_message(const std::string &topic, const std::string &payload)
{
    if (topic == "mando") {
        auto ordenes = split(payload, ' ');
        int res = mando(ordenes);
        if (res == 1) {
            log(payload, "mando rechazado", LOG_WARNING);
        } else if (res == 2) {
            log(payload, "mando especial necesario", LOG_INFO);
        }
        return;
    }

    std::smatch match;

    std::regex cejesEventPattern(R"(^cejes/([a-zA-Z0-9_-]+/[a-zA-Z0-9_-]+)/event$)");
    if (std::regex_match(topic, match, cejesEventPattern)) {
        for (auto &kvp : cv_impls) {
            kvp.second->message_cejes(match[1], payload);
        }
        return;
    }
    std::regex cvStatePattern(R"(^cv/([a-zA-Z0-9_-]+/[a-zA-Z0-9_-]+)/state$)");
    if (std::regex_match(topic, match, cvStatePattern)) {
        estado_cv ecv(json::parse(payload));
        for (auto &kvp : cvs) {
            kvp.second->message_cv(ecv);
        }
        for (auto *bloqueo : bloqueos) {
            bloqueo->message_cv(match[1], ecv);
        }
        if (ecv.evento) {
            for (auto &kvp : señales) {
                kvp.second->message_cv(match[1], ecv);
            }
            for (auto *ruta : rutas) {
                ruta->message_cv(match[1], ecv);
            }
        }
        return;
    }
    std::regex bloqueoStatePattern(R"(^bloqueo/([a-zA-Z0-9_-]+)/state$)");
    if (std::regex_match(topic, match, bloqueoStatePattern)) {
        estado_bloqueo eb(json::parse(payload));
        for (auto &kvp : señales) {
            kvp.second->message_bloqueo(match[1], eb);
        }
        for (auto *ruta : rutas) {
            ruta->message_bloqueo(match[1], eb);
        }
        return;
    }
    std::regex bloqueoRutaPattern(R"(^bloqueo/([a-zA-Z0-9_-]+)/ruta/([a-zA-Z0-9_-]+)$)");
    if (std::regex_match(topic, match, bloqueoRutaPattern)) {
        for (auto *bloqueo : bloqueos) {
            if (bloqueo->id == match[1]) {
                json j = json::parse(payload);
                bloqueo->message_ruta(match[2], j);
            }
        }
        return;
    }
}

void init_items(std::string path)
{
    std::ifstream cvs_cfg(path+"cvs.json");
    if (!cvs_cfg.fail()) {
        json j;
        cvs_cfg>>j;
        for (auto &[estacion, jcvs] : j.items()) {
            for (auto &[id, jcv] : jcvs.items()) {
                std::string ic = estacion+"/"+id;
                cvs[ic] = new cv(ic, jcv);
                if (jcv.contains("ContadoresEjes")) cv_impls[ic] = new cv_impl(ic, jcv);
            }
        }
    }
    std::ifstream señales_cfg(path+"signals.json");
    if (!señales_cfg.fail()) {
        json j;
        señales_cfg>>j;
        for (auto &[estacion, jseñales] : j.items()) {
            for (auto &[id, js] : jseñales.items()) {
                std::string is = estacion+"/"+id;
                señales[is] = new señal(is, js);
            }
        }
    }
    std::ifstream bloqueos_cfg(path+"bloqueos.json");
    if (!bloqueos_cfg.fail()) {
        json j;
        bloqueos_cfg>>j;
        for (auto &jb : j) {
            bloqueos.insert(new bloqueo(jb));
        }
    }
    std::ifstream rutas_cfg(path+"rutas.json");
    if (!rutas_cfg.fail()) {
        json j;
        rutas_cfg>>j;
        for (auto &[estacion, jrutas] : j.items()) {
            if (grutas.find(estacion) == grutas.end()) grutas[estacion] = new gestor_rutas(estacion);
            for (auto &jr : jrutas) {
                ruta *r = new ruta(estacion, jr);
                if (r->valid) grutas[estacion]->rutas.insert(r);
            }
        }
        for (auto &kvp : grutas) {
            for (auto *ruta : kvp.second->rutas) {
                rutas.insert(ruta);
            }
        }
    }

    std::stringstream managed_topics;
    for (auto &kvp : cv_impls) {
        managed_topics<<kvp.second->topic<<'\n';
    }
    for (auto &kvp : señales) {
        managed_topics<<kvp.second->topic<<'\n';
    }
    for (auto *b : bloqueos) {
        managed_topics<<b->topic<<'\n';
    }
    send_message("desconexion/main", managed_topics.str(), 1, true);
}
int64_t last_sent_state;
void loop_items()
{
    for (auto &kvp : señales) {
        kvp.second->update();
    }
    for (auto &kvp : grutas) {
        kvp.second->update();
    }
    int64_t now = get_milliseconds();
    if (now - last_sent_state > 30000) {
        for (auto &kvp : cv_impls) {
            kvp.second->send_state();
        }
        for (auto &kvp : señales) {
            kvp.second->send_state();
        }
        for (auto *b : bloqueos) {
            b->send_state();
        }
        last_sent_state = now;
    }
}