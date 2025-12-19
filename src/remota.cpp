#include "remota.h"
#include "nlohmann/json.hpp"
#include "items.h"
#include <set>
using nlohmann::json;
std::set<std::pair<ElementoRemota, std::string>> update_components;
std::map<std::string, json> sigs;
std::map<std::string, json> imvs;
std::map<std::string, json> fmvs;
bool sendall = false;
void remota_cambio_elemento(ElementoRemota el, std::string id)
{
    update_components.insert({el, id});
}
void push(json &j, std::pair<ElementoRemota,std::string> comp, json j2)
{
    j2["Id"] = comp.second;
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
        procesar_mando(client, "", true);
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
    for (auto &[id, cv] : cvs) {
        std::pair<ElementoRemota,std::string> comp(ElementoRemota::CV, id);
        if (sendall || update_components.find(comp) != update_components.end()) {
            auto r = json(cv->get_estado_remota());
            push(j, comp, r);
        }
    }
    for (auto &[id, bloq] : bloqueos) {
        std::pair<ElementoRemota,std::string> comp(ElementoRemota::BLQ, bloq->id);
        if (sendall || update_components.find(comp) != update_components.end()) {
            auto eb = json(bloq->get_estado_remota());
            push(j, {ElementoRemota::BLQ, id}, eb);
        }
    }
    for (auto &[id, dep] : dependencias) {
        std::pair<ElementoRemota,std::string> comp(ElementoRemota::DEP, id+":DEP");
        if (sendall || update_components.find(comp) != update_components.end()) {
            auto r = json(dep->get_estado_remota());
            push(j, comp, r);
        }
    }
    for (auto &[id, pn] : pns) {
        std::pair<ElementoRemota,std::string> comp(ElementoRemota::PN, id);
        if (sendall || update_components.find(comp) != update_components.end()) {
            auto r = json(pn->get_estado_remota());
            push(j, comp, r);
        }
    }
    update_components.clear();
    if (!j.empty()) send_remota(sendall ? "EstadoCompleto" : "CambioEstado", j);
    sendall = false;
}
