#include <regex>
#include <sstream>
#include "items.h"
#include "log.h"
#include "remota.h"

std::map<std::string, cv*> cvs;
std::map<std::string, cv_impl*> cv_impls;
std::map<std::string, señal*> señales;
std::map<std::string, señal_impl*> señal_impls;
std::map<std::string, bloqueo*> bloqueos;
std::set<ruta*> rutas;
std::map<std::string, destino_ruta*> destinos_ruta;
std::map<std::string, seccion_via*> secciones;
std::map<std::string, aguja*> agujas;
std::map<std::string, dependencia*> dependencias;
std::map<std::string, pn_enclavado*> pns;
parametros_predeterminados parametros;

std::set<std::string> comandos_ruta = {"I","R","M","FAI","ID"};
std::set<std::string> comandos_señal = {"CS","CSEÑ","NPS","BS","ABS","DS","SA","ASA","DAI","DAB","AFA"};
std::set<std::string> comandos_destino = {"BDE","BD","ABDE","ABD","BDS","ABDS","DEI"};
std::set<std::string> comandos_bloqueo = {"B","AB","CSB","NSB","PB","APB","NB","AS","AAS"};
std::set<std::string> comandos_seccion = {"BV","BIV","ABV","DIV","FO","AFO"};
std::set<std::string> comandos_aguja = {"MA","AN","AI","MAT","ATN","ATI","BA","ABA","BIA","DIA"};
std::set<std::string> comandos_cv = {"BTV","ABTV","DTV","LC"};
std::set<std::string> comandos_ignorar_mando = {"C", "TML", "TME", "CML", "RML", "ME"};
std::set<std::string> comandos_ctc = {"C", "L", "AS", "AAS"};
std::set<std::string> comandos_local = {"TML", "TME", "CML", "RML"};
std::set<std::string> comandos_pn = {"APN", "CPN"};

RespuestaMando mando(const std::vector<std::string> &ordenes, int me)
{
    std::string cmd = ordenes[0];
    if (comandos_cv.find(cmd) != comandos_cv.end()) {
        std::string id = "";
        if (ordenes.size() == 3) {
            id = ordenes[1]+":"+ordenes[2];
        } else if (ordenes.size() == 4) {
            std::string colat = ordenes[3];
            for (auto &[idd, dest] : destinos_ruta) {
                if (dest->tipo != TipoDestino::Colateral) continue;
                if (idd.starts_with(ordenes[1]+":"+colat)) {
                    id = colat+":"+ordenes[2];
                    break;
                }
            }
        }
        auto it = cvs.find(id);
        if (it != cvs.end()) return it->second->mando(ordenes[0], me);
    }
    if (comandos_bloqueo.find(cmd) != comandos_bloqueo.end()) {
        if (ordenes.size() == 3) {
            std::string id = ordenes[1]+":"+ordenes[2];
            auto it = bloqueos.find(id);
            if (it != bloqueos.end()) return it->second->mando(ordenes[0], me);
        }
    }
    if (comandos_ruta.find(cmd) != comandos_ruta.end()) {
        auto it = dependencias.find(ordenes[1]);
        if (it != dependencias.end()) {
            if (ordenes.size() == 4) return it->second->mando_ruta(ordenes[2] ,ordenes[3], ordenes[0]);
        }
    }
    if (comandos_destino.find(cmd) != comandos_destino.end()) {
        std::string id = ordenes[1]+":"+ordenes[2];
        auto it = destinos_ruta.find(id);
        if (it != destinos_ruta.end()) return it->second->mando(ordenes[0], me);
    }
    if (comandos_señal.find(cmd) != comandos_señal.end()) {
        if (ordenes.size() == 3) {
            auto it = señal_impls.find(ordenes[1]+":"+ordenes[2]);
            if (it != señal_impls.end()) return it->second->mando(ordenes[0], me);
        }
    }
    if (comandos_seccion.find(cmd)  != comandos_seccion.end()) {
        if (ordenes.size() == 3) {
            auto it = secciones.find(ordenes[1]+":"+ordenes[2]);
            if (it != secciones.end()) return it->second->mando(ordenes[0], me);
        }
    }
    if (comandos_aguja.find(cmd)  != comandos_aguja.end()) {
        if (ordenes.size() == 3) {
            auto it = agujas.find(ordenes[1]+":"+ordenes[2]);
            if (it != agujas.end()) {
                auto resp = it->second->mando(ordenes[0], me);
                if (resp == RespuestaMando::Aceptado) dependencias[ordenes[1]]->calcular_vinculacion_bloqueos();
                return resp;
            }
        }
    }
    if (comandos_pn.find(cmd)  != comandos_pn.end()) {
        if (ordenes.size() == 3) {
            auto it = pns.find(ordenes[1]+":"+ordenes[2]);
            if (it != pns.end()) return it->second->mando(ordenes[0]);
        }
    }
    return RespuestaMando::OrdenRechazada;
}
RespuestaMando procesar_mando(const std::string &client, std::string payload, bool from_ctc)
{
    if (payload == "desconexion") {
        /*for (auto &[id, dep] : dependencias) {
            estado_mando est = dep->mando_actual;
            if (est.central == from_ctc && est.puesto == client && !est.cedido) {
                est.cedido = "";
                dep->set_mando(est);
                log(id, "mando cedido a local por desconexion");
            }
        }*/
        return RespuestaMando::Aceptado;
    }
    auto ordenes = split(payload, ' ');
    if (ordenes.size() < 2) return RespuestaMando::OrdenDesconocida;

    auto it_dep = dependencias.find(ordenes[1]);
    if (it_dep == dependencias.end()) return RespuestaMando::OrdenDesconocida;
    log(client, payload, LOG_DEBUG);
    bool ignorar_mando = comandos_ignorar_mando.find(ordenes[0]) != comandos_ignorar_mando.end();
    if ((it_dep->second->mando_actual.central != from_ctc || it_dep->second->mando_actual.puesto != client) && !ignorar_mando) {
        log(payload, "no tiene el mando", LOG_WARNING);
        return RespuestaMando::NoMando;
    }

    if ((from_ctc && comandos_local.find(ordenes[0]) != comandos_local.end()) || (!from_ctc && comandos_ctc.find(ordenes[0]) != comandos_ctc.end())) {
        log(payload, "mando rechazado", LOG_WARNING);
        return RespuestaMando::OrdenRechazada;
    }

    bool me = false;
    if (ordenes[0] == "ME") {
        if (it_dep->second->mando_especial_pendiente != "") {
            payload = it_dep->second->mando_especial_pendiente;
            ordenes = split(payload, ' ');
            me = true;
            it_dep->second->mando_especial_pendiente = "";
        } else {
            return RespuestaMando::MandoEspecialNoPermitido;
        }
    } else if (it_dep->second->mando_especial_pendiente != "") {
        mando(split(it_dep->second->mando_especial_pendiente, ' '), -1);
        it_dep->second->mando_especial_pendiente = "";
        return RespuestaMando::MandoEspecialEnCurso;
    }

    RespuestaMando res = RespuestaMando::OrdenRechazada;
    if (ordenes[0] == "C") {
        if (from_ctc) {
            it_dep->second->set_mando({true, client, std::nullopt, false});
            log(ordenes[1], "mando central");
            res = RespuestaMando::Aceptado;
        }
    } else if (ordenes[0] == "TML") {
        if (!from_ctc && (it_dep->second->mando_actual.cedido == "" || it_dep->second->mando_actual.cedido == client)) {
            it_dep->second->set_mando({false, client, std::nullopt, false});
            log(ordenes[1], "mando local");
            res = RespuestaMando::Aceptado;
        }
    } else if (ordenes[0] == "TME") {
        if (!from_ctc && (it_dep->second->mando_actual.central || it_dep->second->mando_actual.puesto != client)) {
            if (me) {
                it_dep->second->set_mando({false, client, std::nullopt, true});
                log(ordenes[1], "mando local por emergencia");
                res = RespuestaMando::Aceptado;
            } else {
                res = RespuestaMando::MandoEspecialNecesario;
            }
        }
    } else if (ordenes[0] == "L") {
        if (from_ctc && !it_dep->second->mando_actual.cedido) {
            estado_mando est = it_dep->second->mando_actual;
            est.cedido = ordenes.size() > 2 ? ordenes[2] : "";
            it_dep->second->set_mando(est);
            log(ordenes[1], "mando cedido");
            res = RespuestaMando::Aceptado;
        }
    } else if (ordenes[0] == "CML") {
        if (from_ctc) {
        } else if (it_dep->second->mando_actual.central || it_dep->second->mando_actual.puesto != client) {
            log(payload, "no tiene el mando", LOG_WARNING);
            return RespuestaMando::NoMando;
        } else if(!it_dep->second->mando_actual.cedido) {
            estado_mando est = it_dep->second->mando_actual;
            est.cedido = ordenes.size() > 2 ? ordenes[2] : "";
            it_dep->second->set_mando(est);
            log(ordenes[1], "mando cedido");
            res = RespuestaMando::Aceptado;
        }
    } else if (ordenes[0] == "RML") {
        if (from_ctc) {
        } else if (it_dep->second->mando_actual.central) {
            log(payload, "no tiene el mando", LOG_WARNING);
            return RespuestaMando::NoMando;
        } else if (it_dep->second->mando_actual.puesto != client) {
            it_dep->second->set_mando({false, client, std::nullopt, false});
            log(ordenes[1], "mando local");
            res = RespuestaMando::Aceptado;
        }
    } else {
        res = mando(ordenes, me ? 1 : 0);
    }

    if (res == RespuestaMando::Aceptado) {
        if (me) log(payload, "mando especial confirmado", LOG_WARNING);
    } else if (res == RespuestaMando::MandoEspecialNecesario) {
        log(payload, "mando especial necesario", LOG_INFO);
        it_dep->second->mando_especial_pendiente = payload;
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
        auto ordenes = split(payload, '\n');
        for (auto &orden : ordenes) {
            procesar_mando(match[1], orden, false);
        }
        return;
    }

    std::regex cejesEventPattern(R"(^cejes/([a-zA-Z0-9_-]+/[a-zA-Z0-9_'-]+)/event$)");
    if (std::regex_match(topic, match, cejesEventPattern)) {
        std::string id = id_from_mqtt(match[1]);
        for (auto &kvp : cv_impls) {
            kvp.second->message_cejes(id, payload);
        }
        return;
    }
    std::regex cvStatePattern(R"(^cv/([a-zA-Z0-9_-]+/[a-zA-Z0-9_'-]+)/state$)");
    if (std::regex_match(topic, match, cvStatePattern)) {
        std::string id = id_from_mqtt(match[1]);
        estado_cv ecv(json::parse(payload));
        auto it = cvs.find(id);
        if (it != cvs.end()) it->second->message_cv(ecv);
        for (auto &kvp : secciones) {
            kvp.second->message_cv(id, ecv);
        }
        for (auto &kvp : bloqueos) {
            kvp.second->message_cv(id, ecv);
        }
        if (ecv.evento) {
            for (auto &kvp : señal_impls) {
                kvp.second->message_cv(id, ecv);
            }
            for (auto *ruta : rutas) {
                ruta->message_cv(id, ecv);
            }
        }
        return;
    }
    std::regex cvActionPattern(R"(^cv/([a-zA-Z0-9_-]+/[a-zA-Z0-9_'-]+)/action$)");
    if (std::regex_match(topic, match, cvActionPattern)) {
        std::string id = id_from_mqtt(match[1]);
        auto it = cv_impls.find(id);
        if (it != cv_impls.end()) it->second->message_cv(payload);
        return;
    }
    std::regex signalStatePattern(R"(^signal/([a-zA-Z0-9_-]+/[a-zA-Z0-9_'-]+)/state$)");
    if (std::regex_match(topic, match, signalStatePattern)) {
        std::string id = id_from_mqtt(match[1]);
        auto it = señales.find(id);
        if (it != señales.end()) it->second->message_señal(json::parse(payload));
        return;
    }
    std::regex bloqueoStatePattern(R"(^bloqueo/([a-zA-Z0-9_-]+/[a-zA-Z0-9_'-]+)/state$)");
    if (std::regex_match(topic, match, bloqueoStatePattern)) {
        estado_bloqueo eb(json::parse(payload));
        std::string id = id_from_mqtt(match[1]);
        for (auto &kvp : señal_impls) {
            kvp.second->message_bloqueo(id, eb);
        }
        for (auto *ruta : rutas) {
            ruta->message_bloqueo(id, eb);
        }
        for (auto &kvp : secciones) {
            kvp.second->message_bloqueo(id, eb);
        }
        return;
    }
    std::regex bloqueoColateralPattern(R"(^bloqueo/([a-zA-Z0-9_-]+/[a-zA-Z0-9_'-]+)/colateral$)");
    if (std::regex_match(topic, match, bloqueoColateralPattern)) {
        std::string id = id_from_mqtt(match[1]);
        auto it = bloqueos.find(id);
        if (it != bloqueos.end()) it->second->message_colateral(json::parse(payload));
        return;
    }
    std::regex comprobacionPNPattern(R"(^pn/([a-zA-Z0-9_-]+/[a-zA-Z0-9_'-]+)/comprobacion$)");
    if (std::regex_match(topic, match, comprobacionPNPattern)) {
        std::string id = id_from_mqtt(match[1]);
        auto it = pns.find(id);
        if (it != pns.end()) it->second->message_pn(payload == "true");
        return;
    }

    std::regex fecPattern(R"(^fec/([a-zA-Z0-9_-]+)$)");
    if (std::regex_match(topic, match, fecPattern)) {
        json j = json::parse(payload);
        handle_message_fec(match[1], j);
        return;
    }
}
std::map<std::string,std::vector<std::string>> cejes_to_cvs;
void init_items_ordered(const json &j, std::string tipo)
{
    for (auto &[estacion, jdep] : j.items()) {
        if (!jdep.contains(tipo)) continue;
        if (tipo == "CVs") {
            for (auto &[id, jcv] : jdep["CVs"].items()) {
                std::string ic = estacion+":"+id;
                if (jcv.contains("ContadoresEjes")) {
                    cv_impls[ic] = new cv_impl(ic, jcv);
                    for (auto &[key,val] : jcv["ContadoresEjes"].items()) {
                        cejes_to_cvs[key].push_back(ic);
                    }
                    cvs[ic] = cv_impls[ic];
                } else {
                    cvs[ic] = new cv(ic);
                }
            }
        }
        if (tipo == "Secciones") {
            for (auto &[id, jsec] : jdep["Secciones"].items()) {
                std::string is = estacion+":"+id;
                if (jsec.contains("Tipo") && jsec["Tipo"] == "Aguja") {
                    auto *ag = new aguja(is, jsec);
                    secciones[is] = ag;
                    agujas[is] = ag;
                } else {
                    secciones[is] = new seccion_via(is, jsec);
                }
            }
        }
        if (tipo == "PNs") {
            for (auto &[id, jpn] : jdep["PNs"].items()) {
                std::string ipn = estacion+":"+id;
                pns[ipn] = new pn_enclavado(ipn, jpn);
            }
        }
        if (tipo == "Señales") {
            for (auto &[id, js] : jdep["Señales"].items()) {
                std::string is = estacion+":"+id;
                if (dependencias.find(estacion) == dependencias.end()) {
                    auto *señ = new señal(is, js);
                    señales[is] = señ;
                } else {
                    auto *señ = new señal_impl(is, js);
                    señales[is] = señ;
                    señal_impls[is] = señ;
                }
            }
        }
        if (dependencias.find(estacion) == dependencias.end()) continue;
        if (tipo == "Bloqueos") {
            for (auto &jb : jdep["Bloqueos"]) {
                auto *b = new bloqueo(estacion, jb);
                bloqueos[b->id] = b;
                dependencias[estacion]->bloqueos[b->id] = b;
            }
        }
        if (tipo == "DestinosRuta") {
            for (auto &[id, tipo] : jdep["DestinosRuta"].items()) {
                std::string idd = estacion+":"+id;
                destinos_ruta[idd] = new destino_ruta(idd, tipo);
            }
        }
        if (tipo == "Rutas") {
            for (auto &jr : jdep["Rutas"]) {
                ruta *r = new ruta(estacion, jr);
                if (r->valid) dependencias[estacion]->rutas.insert(r);
            }
        }
    }
}
void init_items(const json &j)
{
    parametros = j["ParámetrosPredeterminados"];
    if (j.contains("Dependencias")) {
        auto jdeps = j["Dependencias"];
        for (auto &[estacion, jdep] : jdeps.items()) {
            if (jdep.value("Controlada", true)) dependencias[estacion] = new dependencia(estacion);
        }
        init_items_ordered(jdeps, "CVs");
        init_items_ordered(jdeps, "Secciones");
        init_items_ordered(jdeps, "PNs");
        init_items_ordered(jdeps, "Señales");
        init_items_ordered(jdeps, "Bloqueos");
        init_items_ordered(jdeps, "DestinosRuta");
        init_items_ordered(jdeps, "Rutas");
    }
    for (auto &[id, cv] : cv_impls) {
        cv->asignar_cejes(cejes_to_cvs);
    }
    for (auto &kvp : dependencias) {
        for (auto *ruta : kvp.second->rutas) {
            rutas.insert(ruta);
        }
        kvp.second->calcular_vinculacion_bloqueos();
    }

    std::stringstream managed_topics;
    for (auto &kvp : cv_impls) {
        managed_topics<<kvp.second->topic<<'\n';
    }
    for (auto &kvp : señal_impls) {
        managed_topics<<kvp.second->topic<<'\n';
    }
    for (auto &kvp : bloqueos) {
        managed_topics<<kvp.second->topic<<'\n';
        managed_topics<<kvp.second->topic_colateral<<'\n';
    }
    for (auto &kvp : pns) {
        managed_topics<<kvp.second->topic<<'\n';
    }
    managed_topics<<"remota/"<<name<<'\n';
    send_message("desconexion/"+name, managed_topics.str(), 1, true);
}
int64_t last_sent_state;
void loop_items()
{
    for (auto &kvp : señal_impls) {
        kvp.second->update();
    }
    for (auto &kvp : dependencias) {
        kvp.second->update();
    }
    int64_t now = get_milliseconds();
    if (now - last_sent_state > 30000) {
        for (auto &kvp : cv_impls) {
            kvp.second->send_state();
        }
        for (auto &kvp : señal_impls) {
            kvp.second->send_state();
        }
        for (auto &kvp : bloqueos) {
            kvp.second->send_state();
        }
        for (auto &kvp : dependencias) {
            kvp.second->send_state();
        }
        for (auto &kvp : pns) {
            kvp.second->send_state();
        }
        remota_sendall();
        last_sent_state = now;
    }
    update_remota();
}
