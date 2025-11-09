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
    j2["Type"] = (int)comp.first;
    j.push_back(j2);
}
void send_remota(std::string tipo, const json &msg)
{
    json j;
    j["Tipo"] = tipo;
    j["Mensaje"] = msg;
    send_message("remota", j.dump(), 0);
}
void remota_sendall()
{
    sendall = true;
}
RespuestaMando procesar_mando(const std::string &client, std::string payload, bool from_ctc);
void handle_message_fec(const json &j)
{
    if (j["Tipo"] == "EnvíoÓrdenes") {
        RespuestaMando res = procesar_mando("fec", j["Mensaje"], true);
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
        procesar_mando("fec", "ME", true);
    } else if (j["Tipo"] == "CancelaciónMandoEspecial") {
        procesar_mando("fec", "", true);
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
    for (auto *bloq : bloqueos) {
        std::pair<ElementoRemota,std::string> comp(ElementoRemota::BLQ, bloq->id);
        if (sendall || update_components.find(comp) != update_components.end()) {
            auto eb = bloq->get_estado_remota();
            for (auto &lado : {Lado::Impar, Lado::Par}) {
                auto r = json(eb[lado]);
                push(j, {ElementoRemota::BLQ, bloq->estaciones[lado]+":"+bloq->estaciones[opp_lado(lado)]+bloq->via}, r);
            }
        }
    }
    update_components.clear();
    if (!j.empty()) send_remota(sendall ? "EstadoCompleto" : "CambioEstado", j);
    sendall = false;
}

// ---------------- RemotaSIG ----------------

void to_json(json& j, const RemotaSIG& p)
{
    j = json{
        {"SIG_DAT", p.SIG_DAT},
        {"SIG_TIPO", p.SIG_TIPO},
        {"SIG_EAR", p.SIG_EAR},
        {"SIG_IND", p.SIG_IND},
        {"SIG_FOCO_R", p.SIG_FOCO_R},
        {"SIG_FOCO_BL_C", p.SIG_FOCO_BL_C},
        {"SIG_FOCO_BL_V", p.SIG_FOCO_BL_V},
        {"SIG_FOCO_BL_H", p.SIG_FOCO_BL_H},
        {"SIG_FOCO_AZ", p.SIG_FOCO_AZ},
        {"SIG_FOCO_AM", p.SIG_FOCO_AM},
        {"SIG_FOCO_V", p.SIG_FOCO_V},
        {"SIG_ME", p.SIG_ME},
        {"SIG_B", p.SIG_B},
        {"SIG_UIC", p.SIG_UIC},
        {"SIG_SA", p.SIG_SA},
        {"SIG_FAI", p.SIG_FAI},
        {"SIG_GRP_ARS", p.SIG_GRP_ARS}
    };
}

void from_json(const json& j, RemotaSIG& p)
{
    p.SIG_DAT      = j.value("SIG_DAT", 0);
    p.SIG_TIPO     = j.value("SIG_TIPO", 0);
    p.SIG_EAR      = j.value("SIG_EAR", 0);
    p.SIG_IND      = j.value("SIG_IND", 0);
    p.SIG_FOCO_R   = j.value("SIG_FOCO_R", 0);
    p.SIG_FOCO_BL_C= j.value("SIG_FOCO_BL_C", 0);
    p.SIG_FOCO_BL_V= j.value("SIG_FOCO_BL_V", 0);
    p.SIG_FOCO_BL_H= j.value("SIG_FOCO_BL_H", 0);
    p.SIG_FOCO_AZ  = j.value("SIG_FOCO_AZ", 0);
    p.SIG_FOCO_AM  = j.value("SIG_FOCO_AM", 0);
    p.SIG_FOCO_V   = j.value("SIG_FOCO_V", 0);
    p.SIG_ME       = j.value("SIG_ME", 0);
    p.SIG_B        = j.value("SIG_B", 0);
    p.SIG_UIC      = j.value("SIG_UIC", 0);
    p.SIG_SA       = j.value("SIG_SA", 0);
    p.SIG_FAI      = j.value("SIG_FAI", 0);
    p.SIG_GRP_ARS  = j.value("SIG_GRP_ARS", 0);
}

// ---------------- RemotaPV ----------------

void to_json(json& j, const RemotaPV& p)
{
    j = json{
        {"PV_DAT", p.PV_DAT},
        {"PV_IND", p.PV_IND},
        {"PV_CS_IND", p.PV_CS_IND}
    };
}

void from_json(const json& j, RemotaPV& p)
{
    p.PV_DAT   = j.value("PV_DAT", 0);
    p.PV_IND   = j.value("PV_IND", 0);
    p.PV_CS_IND= j.value("PV_CS_IND", 0);
}

// ---------------- RemotaCV ----------------

void to_json(json& j, const RemotaCV& p)
{
    j = json{
        {"CV_DAT", p.CV_DAT},
        {"CV_ME", p.CV_ME},
        {"CV_BV", p.CV_BV},
        {"CV_OCUP_TIPO", p.CV_OCUP_TIPO},
        {"CV_EST", p.CV_EST},
        {"CV_DES", p.CV_DES},
        {"CV_CEJES_AV", p.CV_CEJES_AV},
        {"CV_CEJES_PREN", p.CV_CEJES_PREN},
        {"CV_UC", p.CV_UC},
        {"CV_NSEC", p.CV_NSEC}
    };
}

void from_json(const json& j, RemotaCV& p)
{
    p.CV_DAT        = j.value("CV_DAT", 0);
    p.CV_ME         = j.value("CV_ME", 0);
    p.CV_BV         = j.value("CV_BV", 0);
    p.CV_OCUP_TIPO  = j.value("CV_OCUP_TIPO", 0);
    p.CV_EST        = j.value("CV_EST", 0);
    p.CV_DES        = j.value("CV_DES", 0);
    p.CV_CEJES_AV   = j.value("CV_CEJES_AV", 0);
    p.CV_CEJES_PREN = j.value("CV_CEJES_PREN", 0);
    p.CV_UC         = j.value("CV_UC", 0);
    p.CV_NSEC       = j.value("CV_NSEC", 0);
}

// ---------------- RemotaIMV ----------------

void to_json(json& j, const RemotaIMV& p)
{
    j = json{
        {"IMV_DAT", p.IMV_DAT},
        {"IMV_EST", p.IMV_EST},
        {"IMV_DIF_VAL", p.IMV_DIF_VAL}
    };
}

void from_json(const json& j, RemotaIMV& p)
{
    p.IMV_DAT     = j.value("IMV_DAT", 0);
    p.IMV_EST     = j.value("IMV_EST", 0);
    p.IMV_DIF_VAL = j.value("IMV_DIF_VAL", 0);
}

// ---------------- RemotaFMV ----------------

void to_json(json& j, const RemotaFMV& p)
{
    j = json{
        {"FMV_DAT", p.FMV_DAT},
        {"FMV_EST", p.FMV_EST},
        {"FMV_DIF_VAL", p.FMV_DIF_VAL},
        {"FMV_DIF_ELEM", p.FMV_DIF_ELEM},
        {"FMV_ME", p.FMV_ME},
        {"FMV_BD", p.FMV_BD}
    };
}

void from_json(const json& j, RemotaFMV& p)
{
    p.FMV_DAT      = j.value("FMV_DAT", 0);
    p.FMV_EST      = j.value("FMV_EST", 0);
    p.FMV_DIF_VAL  = j.value("FMV_DIF_VAL", 0);
    p.FMV_DIF_ELEM = j.value("FMV_DIF_ELEM", 0);
    p.FMV_ME       = j.value("FMV_ME", 0);
    p.FMV_BD       = j.value("FMV_BD", 0);
}

// ---------------- RemotaBLQ ----------------

void to_json(json& j, const RemotaBLQ& p)
{
    j = json{
        {"BLQ_DAT", p.BLQ_DAT},
        {"BLQ_PROHI_SAL", p.BLQ_PROHI_SAL},
        {"BLQ_EST_SAL", p.BLQ_EST_SAL},
        {"BLQ_EST_ENT", p.BLQ_EST_ENT},
        {"BLQ_PROHI_ENT", p.BLQ_PROHI_ENT},
        {"BLQ_COL_MAN", p.BLQ_COL_MAN},
        {"BLQ_ACTC", p.BLQ_ACTC},
        {"BLQ_CSB", p.BLQ_CSB},
        {"BLQ_NB", p.BLQ_NB},
        {"BLQ_ME", p.BLQ_ME},
        {"BLQ_PROX", p.BLQ_PROX}
    };
}

void from_json(const json& j, RemotaBLQ& p)
{
    p.BLQ_DAT       = j.value("BLQ_DAT", 0);
    p.BLQ_PROHI_SAL = j.value("BLQ_PROHI_SAL", 0);
    p.BLQ_EST_SAL   = j.value("BLQ_EST_SAL", 0);
    p.BLQ_EST_ENT   = j.value("BLQ_EST_ENT", 0);
    p.BLQ_PROHI_ENT = j.value("BLQ_PROHI_ENT", 0);
    p.BLQ_COL_MAN   = j.value("BLQ_COL_MAN", 0);
    p.BLQ_ACTC      = j.value("BLQ_ACTC", 0);
    p.BLQ_CSB       = j.value("BLQ_CSB", 0);
    p.BLQ_NB        = j.value("BLQ_NB", 0);
    p.BLQ_ME        = j.value("BLQ_ME", 0);
    p.BLQ_PROX      = j.value("BLQ_PROX", 0);
}
