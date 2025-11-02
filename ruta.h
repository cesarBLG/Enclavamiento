#pragma once
#include <vector>
#include "enums.h"
#include "bloqueo.h"
#include "cv.h"
#include "signal.h"
#include "log.h"
class ruta
{
public:
    const std::string estacion;
    const TipoMovimiento tipo;
    const std::string inicio;
    const std::string destino;
    const std::string id;
    const std::string bloqueo_salida;
    bool maniobra_compatible = false;
    bool valid = false;
protected:
    std::vector<cv*> cvs;
    std::set<cv*> cvs_asegurados;
    std::shared_ptr<timer> diferimetro_dai;
    std::shared_ptr<timer> diferimetro_dei;
    std::vector<cv*> proximidad;
    señal *señal_inicio;
    señal *señal_fin;
    estado_bloqueo bloqueo_act;
    Lado lado;
    Lado lado_bloqueo;

    bool mandada = false;
    bool formada = false;
    bool supervisada = false;
    bool ocupada = false;
    bool diferimetro_cancelado = false;

    bool bloqueo_destino = false;
    bool bloqueo_señal = false;

    bool sucesion_automatica = false;

    void disolver()
    {
        if (señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
        mandada = false;
        formada = false;
        supervisada = false;
        ocupada = false;
        diferimetro_dai = nullptr;
        diferimetro_dei = nullptr;
        diferimetro_cancelado = false;
        for (auto *cv : cvs_asegurados) {
            cv->ruta_asegurada = nullptr;
        }
        cvs_asegurados.clear();
        log(id, "disuelta");
    }
public:
    ruta(const std::string &estacion, const json &j);
    bool establecer()
    {
        if (bloqueo_destino || bloqueo_señal) return false;
        clear_timer(diferimetro_dai);
        diferimetro_dai = nullptr;
        if (mandada && !ocupada) {
            señal_inicio->clear_request = true;
            log(id, "re-mandada", LOG_DEBUG);
            return true;
        }
        if (bloqueo_salida != "" && (tipo == TipoMovimiento::Itinerario || (tipo == TipoMovimiento::Maniobra && !maniobra_compatible)) && bloqueo_act.estado == (lado_bloqueo == Lado::Par ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar))
            return false;
        if (bloqueo_salida != "" && bloqueo_act.ruta[lado] != tipo && bloqueo_act.ruta[lado] != TipoMovimiento::Ninguno)
            return false;
        for (auto *cv : cvs) {
            if (cv->ruta_asegurada != this) return false;
        }
        log(id, "mandada", LOG_DEBUG);
        clear_timer(diferimetro_dei);
        diferimetro_dei = nullptr;
        mandada = formada = true;
        señal_inicio->clear_request = true;
        señal_inicio->ruta_activa = this;
        for (auto *cv : cvs) {
            cvs_asegurados.insert(cv);
            cv->ruta_asegurada = this;
        }
        return true;
    }
    void update()
    {
        if (!mandada) return;
        if (ocupada && !sucesion_automatica) {
            for (int i=0; i<cvs.size(); i++) {
                if ((i == 0 || cvs[i-1]->ruta_asegurada == nullptr) && cvs[i]->get_state() <= EstadoCV::Prenormalizado) {
                    cvs[i]->ruta_asegurada = nullptr;
                    cvs_asegurados.erase(cvs[i]);
                    if (i == cvs.size() - 1) disolver();
                }
            }
            if (cvs.empty()) disolver();
        }
        if (ocupada && sucesion_automatica) {
            bool libre = true;
            for (int i=0; i<cvs.size(); i++) {
                if (cvs[i]->get_state() > EstadoCV::Prenormalizado) {
                    libre = false;
                    break;
                }
            }
            if (libre) {
                ocupada = false;
                supervisada = false;
            }
        }
        if (formada && !supervisada && señal_inicio->get_state() != Aspecto::Parada) {
            supervisada = true;
            log(id, "supervisada");
        }
    }
    bool dai()
    {
        if (ocupada || !mandada) return false;
        log(id, "dai", LOG_DEBUG);
        if (!supervisada) {
            disolver();
            return true;
        }
        bool proximidad_libre = true;
        for (auto *cv : proximidad) {
            auto e = cv->get_state();
            if (e > EstadoCV::Prenormalizado) {
                proximidad_libre = false;
                break;
            }
        }
        if (proximidad_libre && proximidad.size() > 0) {
            disolver();
        } else if (diferimetro_dai == nullptr) {
            diferimetro_dai = set_timer([this]() {
                diferimetro_dai = nullptr;
                disolver();
            }, 15000);
        }
        return true;
    }
    bool dei()
    {
        if (!ocupada || !supervisada) return false;
        log(id, "dai", LOG_DEBUG);
        if (diferimetro_dei == nullptr) {
            diferimetro_dei = set_timer([this]() {
                diferimetro_dei = nullptr;
                disolver();
            }, 3*60*1000);
        }
        return true;
    } 

    bool is_mandada()
    {
        return mandada;
    }
    bool dai_activo()
    {
        return diferimetro_dai != nullptr;
    }

    void message_bloqueo(std::string id, estado_bloqueo estado)
    {
        if (id != bloqueo_salida) return;
        bloqueo_act = estado;
        update();
    }

    bool mando(const std::string &inicio, const std::string &fin, const std::string &cmd) {
        if (this->inicio != inicio || this->destino != fin) return false;

        if (cmd == "I" && tipo == TipoMovimiento::Itinerario) return establecer();
        else if (cmd == "M" && tipo == TipoMovimiento::Maniobra) return establecer();
        else if (cmd == "ISA" && tipo == TipoMovimiento::Itinerario) {
            if (!establecer()) return false;
            sucesion_automatica = true;
            return true;
        }
        return false;
    }
    bool mando(const std::string &pos, const std::string &cmd) {
        if (pos == inicio) {
            if (cmd == "DAI") {
                return dai();
            } else if (cmd == "BS") {
                bloqueo_señal = true;
                return true;
            } else if (cmd == "ABS" || cmd == "DS") {
                bloqueo_señal = false;
                return true;
            } else if (cmd == "SA" && !ocupada) {
                sucesion_automatica = true;
                return true;
            } else if (cmd == "DASA") {
                if (!dai()) return false;
                sucesion_automatica = false;
                return true;
            } else if (cmd == "ASA" && !ocupada) sucesion_automatica = true;
        } else if (pos == destino) {
            if (cmd == "DEI") {
                return dei();
            } else if (cmd == "BD" || cmd == "BDS" || cmd == "BDE") {
                bloqueo_destino = true;
                return true;
            } else if (cmd == "ABD" || cmd == "ABDS" || cmd == "ABDE") {
                bloqueo_destino = false;
                return true;
            }
        }
        return false;
    }
    void message_cv(const std::string &id, estado_cv ecv)
    {
        if (id != señal_inicio->get_cv_señal()->id) return;
        if (!mandada) return;
        if (ecv.evento && ecv.evento->second && ecv.evento->first == lado) {
            if (!supervisada) {
                supervisada = true;
                log(this->id, "supervisada");
            }
            if (!ocupada) { 
                ocupada = true;
                if (diferimetro_dai != nullptr) {
                    clear_timer(diferimetro_dai);
                    diferimetro_dai = nullptr;
                    diferimetro_cancelado = true;
                    log(this->id, "ocupada con dai activo", LOG_WARNING);
                } else {
                    log(this->id, "ocupada");
                }
                if (!sucesion_automatica) señal_inicio->ruta_activa = nullptr;
            }
        }
    }
};
struct gestor_rutas
{
    const std::string estacion;
    std::set<ruta*> rutas;
    std::map<std::string, TipoMovimiento> movimiento_bloqueos;
    gestor_rutas(std::string estacion) : estacion(estacion)
    {

    }
    void update()
    {
        std::map<std::string, TipoMovimiento> movimiento_bloqueos_new;
        for (auto *ruta : rutas) {
            movimiento_bloqueos_new[ruta->bloqueo_salida] = TipoMovimiento::Ninguno;
        }
        for (auto *ruta : rutas) {
            ruta->update();
            if (ruta->is_mandada()) {
                movimiento_bloqueos_new[ruta->bloqueo_salida] = ruta->tipo;
            }
        }
        for (auto &kvp : movimiento_bloqueos_new) {
            if (kvp.first == "" || movimiento_bloqueos[kvp.first] == kvp.second) continue;
            json j = kvp.second;
            send_message("bloqueo/"+kvp.first+"/ruta/"+estacion, j.dump());
        }
        movimiento_bloqueos = movimiento_bloqueos_new;
    }
    bool mando(const std::string &pos, const std::string &cmd) {
        bool aceptado = false;
        for (auto *ruta : rutas) {
            aceptado |= ruta->mando(pos, cmd);
        }
        return aceptado;
    }
    bool mando(const std::string &inicio, const std::string &fin, const std::string &cmd) {
        bool aceptado = false;
        for (auto *ruta : rutas) {
            aceptado |= ruta->mando(inicio, fin, cmd);
        }
        return aceptado;
    }
};