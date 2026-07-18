#include "remota.h"
#include "nlohmann/json.hpp"
#include "items.h"
#include <set>
using nlohmann::json;
std::set<std::pair<std::string, id_elemento>> update_components;
std::map<id_elemento, json> sigs;
std::map<id_elemento, json> imvs;
std::map<id_elemento, json> fmvs;
bool sendall = false;
void remota_cambio_elemento(std::string tipo, const id_elemento &id)
{
    update_components.insert({tipo, id});
}
void push(json &j, std::pair<ElementoRemota,id_elemento> comp, json j2)
{
    j2["Id"] = comp.second.id;
    j2["Tipo"] = (int)comp.first;
    j.push_back(j2);
}
void send_remota(std::string tipo, const json &msg)
{
    json j;
    j["Tipo"] = tipo;
    j["Mensaje"] = msg;
    send_message("remota/"+name, j.dump());
}
void remota_sendall()
{
    sendall = true;
}
RespuestaMando procesar_mando(const std::string &client, std::string payload, bool from_ctc);
void handle_message_fec(const std::string client, const json &j)
{
    if (j == "desconexion") {
        procesar_mando(client, "desconexion", true);
        return;
    }
    if (j["Tipo"] == "EnvíoÓrdenes") {
        RespuestaMando res = procesar_mando(client, j["Mensaje"], true);
        json j1;
        switch (res) {
            case RespuestaMando::Aceptado:
                j1["Respuesta"] = 0;
                j1["Rechazo"] = 0;
                break;
            case RespuestaMando::MandoEspecialNecesario:
                j1["Respuesta"] = 1;
                j1["Rechazo"] = 0;
                break;
            case RespuestaMando::MandoEspecialNoPermitido:
                j1["Respuesta"] = 2;
                j1["Rechazo"] = 3;
                break;
            case RespuestaMando::MandoEspecialEnCurso:
                j1["Respuesta"] = 2;
                j1["Rechazo"] = 4;
                break;
            case RespuestaMando::OrdenDesconocida:
                j1["Respuesta"] = 2;
                j1["Rechazo"] = 2;
                break;
            default:
                j1["Respuesta"] = 2;
                j1["Rechazo"] = 1;
                break;
        }
        j1["Comando"] = j["Mensaje"];
        send_remota("RespuestaÓrdenes", j1);
    } else if (j["Tipo"] == "PeticiónEstadoCompleto") {
        sendall = true;
    } else if (j["Tipo"] == "ConfirmaciónMandoEspecial") {
        procesar_mando(client, "ME", true);
    } else if (j["Tipo"] == "CancelaciónMandoEspecial") {
        procesar_mando(client, "BL", true);
    }
}
void update_remota()
{
    json j;
    for (auto &[id, destino] : destinos_ruta) {
        auto r = json(destino->get_estado_remota());
        if (sendall || r != fmvs[id]) push(j, {ElementoRemota::FMV, id}, r);
        fmvs[id] = r;
    }
    for (auto &[id, sig] : señal_impls) {
        auto p = sig->get_estado_remota();
        auto r = json(p.first);
        if (sendall || r != sigs[id]) push(j, {ElementoRemota::SIG, id}, r);
        sigs[id] = r;
        r = json(p.second);
        if (sendall || r != imvs[id]) push(j, {ElementoRemota::IMV, id}, r);
        imvs[id] = r;
    }
    for (auto &[id, sec] : secciones) {
        std::pair<std::string,id_elemento> comp1("sec", id);
        std::pair<std::string,id_elemento> comp2("cv", sec->id_cv);
        std::pair<std::string,id_elemento> comp3("blq", sec->bloqueo_asociado ? *sec->bloqueo_asociado : id_elemento(""));
        if (sendall || update_components.find(comp1) != update_components.end() || update_components.find(comp2) != update_components.end() || update_components.find(comp3) != update_components.end()) {
            if (sec->tipo == TipoSeccion::Aguja) {
                push(j, {ElementoRemota::AG, id}, json(((aguja*)sec)->get_estado_remota()));
                update_components.insert(comp2);
            } else if (sec->tipo == TipoSeccion::Cruzamiento) {
                push(j, {ElementoRemota::CVX, id}, json(((cruzamiento*)sec)->get_estado_remota()));
            } else {
                if (cvs.find(sec->id_cv) != cvs.end() && cv_impls.find(sec->id_cv) == cv_impls.end()) continue;
                push(j, {ElementoRemota::CV, id}, json(sec->get_estado_remota()));
            }
        }
    }
    for (auto &[id, cv] : cv_impls) {
        std::pair<std::string,id_elemento> comp("cv", id);
        if (sendall || update_components.find(comp) != update_components.end()) {
            if (cv->tipo == TipoSeccion::Aguja)
                push(j, {ElementoRemota::CVA, id}, json(cv->get_estado_remota_agujas()));
        }
    }
    for (auto &[id, bloq] : bloqueos) {
        std::pair<std::string,id_elemento> comp("blq", bloq->id);
        if (sendall || update_components.find(comp) != update_components.end()) {
            auto eb = json(bloq->get_estado_remota());
            push(j, {ElementoRemota::BLQ, id}, eb);
        }
    }
    for (auto &[id, dep] : dependencias) {
        std::pair<std::string,id_elemento> comp("dep", id+":DEP");
        if (sendall || update_components.find(comp) != update_components.end()) {
            auto r = json(dep->get_estado_remota());
            push(j, {ElementoRemota::DEP, comp.second}, r);
        }
    }
    for (auto &[id, pn] : pns) {
        std::pair<std::string,id_elemento> comp("pn", id);
        if (sendall || update_components.find(comp) != update_components.end()) {
            auto r = json(pn->get_estado_remota());
            push(j, {ElementoRemota::PN, comp.second}, r);
        }
    }
    update_components.clear();
    if (!j.empty()) send_remota(sendall ? "EstadoCompleto" : "CambioEstado", j);
    sendall = false;
}
