#include <regex>
#include <sstream>
#include "items.h"
#include "log.h"
#include "remota.h"

std::map<std::string, cv*> cvs;
std::map<std::string, cv_impl*> cv_impls;
std::map<std::string, señal*> señales;
std::set<bloqueo*> bloqueos;
std::set<ruta*> rutas;
std::map<std::string, destino_ruta*> destinos_ruta;
std::map<std::string, gestor_rutas*> grutas;
std::map<std::string, seccion_via*> secciones;

std::set<std::string> comandos_ruta = {"I","R","M","FAI","AFA"};
std::set<std::string> comandos_señal = {"CS","CSEÑ","NPS","BS","ABS","DS","SA","ASA","DAI"};
std::set<std::string> comandos_destino = {"BDE","BD","ABDE","ABD","SA","ASA","BDS","ABDS","DEI"};
std::set<std::string> comandos_bloqueo = {"B","AB","CSB","NSB","PB","APB","NB","AS","AAS"};
std::set<std::string> comandos_seccion = {"BV","BIV","ABV","DIV","FO","AFO"};
std::set<std::string> comandos_cv = {"BTV","ABTV","DTV","LC"};

std::map<std::string, std::string> mandos_especiales_pendientes;

RespuestaMando mando(const std::vector<std::string> &ordenes, int me)
{
    if (ordenes.size() <= 0) return RespuestaMando::OrdenDesconocida;
    std::string cmd = ordenes[0];
    if (comandos_cv.find(cmd) != comandos_cv.end()) {
        if (ordenes.size() == 3) {
            std::string id = ordenes[1]+":"+ordenes[2];
            auto it = cv_impls.find(id);
            if (it != cv_impls.end()) return it->second->mando(ordenes[0], me);
        }
    }
    if (comandos_bloqueo.find(cmd) != comandos_bloqueo.end()) {
        if (ordenes.size() == 3) {
            for (auto *b : bloqueos) {
                auto resp = b->mando(ordenes[1], ordenes[2], ordenes[0], me);
                if (resp != RespuestaMando::OrdenNoAplicable) return resp;
            }
        }
    }
    if (comandos_ruta.find(cmd) != comandos_ruta.end()) {
        auto it = grutas.find(ordenes[1]);
        if (it != grutas.end()) {
            if (ordenes.size() == 4) return it->second->mando(ordenes[2] ,ordenes[3], ordenes[0]);
        }
    }
    if (comandos_destino.find(cmd) != comandos_destino.end()) {
        std::string id = ordenes[1]+":"+ordenes[2];
        auto it = destinos_ruta.find(id);
        if (it != destinos_ruta.end()) return it->second->mando(ordenes[0], me);
    }
    if (comandos_señal.find(cmd) != comandos_señal.end()) {
        if (ordenes.size() == 3) {
            auto it = señales.find(ordenes[1]+":"+ordenes[2]);
            if (it != señales.end()) return it->second->mando(ordenes[0], me);
        }
    }
    if (comandos_seccion.find(cmd)  != comandos_seccion.end()) {
        if (ordenes.size() == 3) {
            auto it = secciones.find(ordenes[1]+":"+ordenes[2]);
            if (it != secciones.end()) return it->second->mando(ordenes[0], me);
        }
    }
    return RespuestaMando::OrdenRechazada;
}
RespuestaMando procesar_mando(const std::string &client, std::string payload)
{
    bool me = false;
    if (mandos_especiales_pendientes[client] != "") {
        if (payload == "ME") {
            payload = mandos_especiales_pendientes[client];
            me = true;
        } else {
            mando(split(mandos_especiales_pendientes[client], ' '), -1);
        }
        mandos_especiales_pendientes[client] = "";
    }
    auto ordenes = split(payload, ' ');
    if (ordenes.empty()) return RespuestaMando::OrdenDesconocida;
    RespuestaMando res = mando(ordenes, me ? 1 : 0);
    if (res == RespuestaMando::Aceptado) {
        if (me) log(payload, "mando especial confirmado", LOG_WARNING);
    } else if (res == RespuestaMando::MandoEspecialNecesario) {
        log(payload, "mando especial necesario", LOG_INFO);
        mandos_especiales_pendientes[client] = payload;
    } else if (res == RespuestaMando::MandoEspecialEnCurso) {
        log(payload, "mando especial en curso", LOG_WARNING);
    } else {
        if (me) log(payload, "mando especial no permitido", LOG_WARNING);
        else log(payload, "mando rechazado", LOG_WARNING);
    }
    return res;
}
void handle_message(const std::string &topic, const std::string &payload)
{
    std::smatch match;

    std::regex mandoPattern(R"(^mando/([a-zA-Z0-9_-]+)$)");
    if (std::regex_match(topic, match, mandoPattern)) {
        procesar_mando(match[1], payload);
        return;
    }

    std::regex cejesEventPattern(R"(^cejes/([a-zA-Z0-9_-]+/[a-zA-Z0-9_-]+)/event$)");
    if (std::regex_match(topic, match, cejesEventPattern)) {
        std::string id = id_from_mqtt(match[1]);
        for (auto &kvp : cv_impls) {
            kvp.second->message_cejes(id, payload);
        }
        return;
    }
    std::regex cvStatePattern(R"(^cv/([a-zA-Z0-9_-]+/[a-zA-Z0-9_-]+)/state$)");
    if (std::regex_match(topic, match, cvStatePattern)) {
        std::string id = id_from_mqtt(match[1]);
        estado_cv ecv(json::parse(payload));
        for (auto &kvp : cvs) {
            if (id == kvp.first) kvp.second->message_cv(ecv);
        }
        for (auto *bloqueo : bloqueos) {
            bloqueo->message_cv(id, ecv);
        }
        for (auto &kvp : secciones) {
            kvp.second->message_cv(id, ecv);
        }
        if (ecv.evento) {
            for (auto &kvp : señales) {
                kvp.second->message_cv(id, ecv);
            }
            for (auto *ruta : rutas) {
                ruta->message_cv(id, ecv);
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
        for (auto &kvp : secciones) {
            kvp.second->message_bloqueo(match[1], eb);
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

    if (topic == "remota/fec")
    {
        json j = json::parse(payload);
        handle_message_fec(j);
        return;
    }
}

void init_items(const json &j)
{
    if (j.contains("CVs")) {
        std::map<std::string,std::vector<std::string>> cejes_to_cvs;
        for (auto &[estacion, jcvs] : j["CVs"].items()) {
            for (auto &[id, jcv] : jcvs.items()) {
                std::string ic = estacion+":"+id;
                cvs[ic] = new cv(ic);
                if (jcv.contains("ContadoresEjes")) {
                    cv_impls[ic] = new cv_impl(ic, jcv);
                    for (auto &[key,val] : jcv["ContadoresEjes"].items()) {
                        cejes_to_cvs[key].push_back(ic);
                    }
                }
            }
        }
        for (auto &[id, cv] : cv_impls) {
            cv->asignar_cejes(cejes_to_cvs);
        }
    }
    if (j.contains("Secciones")) {
        for (auto &[estacion, jsecs] : j["Secciones"].items()) {
            for (auto &[id, jsec] : jsecs.items()) {
                std::string is = estacion+":"+id;
                secciones[is] = new seccion_via(is, jsec);
            }
        }
    }
    if (j.contains("Señales")) {
        for (auto &[estacion, jseñales] : j["Señales"].items()) {
            for (auto &[id, js] : jseñales.items()) {
                std::string is = estacion+":"+id;
                señales[is] = new señal(is, js);
            }
        }
    }
    if (j.contains("Bloqueos")) {
        for (auto &jb : j["Bloqueos"]) {
            bloqueos.insert(new bloqueo(jb));
        }
    }
    if (j.contains("DestinosRuta")) {
        for (auto &[estacion, jdestinos] : j["DestinosRuta"].items()) {
            for (auto &[id, tipo] : jdestinos.items()) {
                std::string idd = estacion+":"+id;
                destinos_ruta[idd] = new destino_ruta(idd, tipo);
            }
        }
    }
    if (j.contains("Rutas")) {
        for (auto &[estacion, jrutas] : j["Rutas"].items()) {
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
        remota_sendall();
        last_sent_state = now;
    }
    update_remota();
}
