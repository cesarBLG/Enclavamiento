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
    std::vector<seccion_via*> secciones;
    std::set<seccion_via*> secciones_aseguradas;
    std::shared_ptr<timer> diferimetro_dai;
    std::shared_ptr<timer> diferimetro_dei;
    std::vector<std::pair<seccion_via*,Lado>> proximidad;
    std::set<std::string> ultimos_cvs_proximidad;
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
    bool fai = false;

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
        for (auto *sec : secciones_aseguradas) {
            sec->liberar(this);
        }
        secciones_aseguradas.clear();
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
        for (auto *sec : secciones) {
            if (sec->is_asegurada() && !sec->is_asegurada(this)) return false;
        }
        log(id, "mandada", LOG_DEBUG);
        clear_timer(diferimetro_dei);
        diferimetro_dei = nullptr;
        mandada = formada = true;
        señal_inicio->clear_request = true;
        señal_inicio->ruta_activa = this;
        for (auto *sec : secciones) {
            secciones_aseguradas.insert(sec);
            sec->liberar(this);
        }
        return true;
    }
    void update()
    {
        if (fai) {
            bool proximidad_ocupada = false;
            for (auto [sec, dir] : proximidad) {
                auto e = sec->get_cv()->get_state();
                if (e > EstadoCV::Prenormalizado && (e != (dir == Lado::Impar ? EstadoCV::OcupadoPar : EstadoCV::OcupadoImpar))) {
                    proximidad_ocupada = true;
                    break;
                }
            }
            if (proximidad_ocupada && !mandada) {
                establecer();
            }
        }
        if (!mandada) return;
        if (ocupada && !sucesion_automatica) {
            if (tipo == TipoMovimiento::Maniobra && proximidad.size() > 0 && (proximidad[0].first->get_cv()->get_state() <= EstadoCV::Prenormalizado || proximidad[0].first->get_cv()->get_state() == (proximidad[0].second == Lado::Impar ? EstadoCV::OcupadoPar : EstadoCV::OcupadoImpar))) {
                if (señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
            }
            for (int i=0; i<secciones.size(); i++) {
                if ((i == 0 || !secciones[i-1]->is_asegurada(this)) && secciones[i]->get_cv()->get_state() <= EstadoCV::Prenormalizado) {
                    if (i == 0 && señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
                    secciones[i]->liberar(this);
                    secciones_aseguradas.erase(secciones[i]);
                    if (i == secciones.size() - 1) disolver();
                }
            }
            if (señal_inicio->ruta_activa != this && secciones.empty()) disolver();
        }
        if (ocupada && sucesion_automatica) {
            bool libre = true;
            for (int i=0; i<secciones.size(); i++) {
                if (secciones[i]->get_cv()->get_state() > EstadoCV::Prenormalizado) {
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
        for (auto [sec, dir] : proximidad) {
            auto e = sec->get_cv()->get_state();
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
    bool is_formada()
    {
        return formada;
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
        } else if (cmd == "FAI" && tipo == TipoMovimiento::Itinerario) {
            fai = true;
            return true;
        } else if (cmd == "AFA") {
            fai = false;
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
            } else if (cmd == "SA" && !ocupada && tipo == TipoMovimiento::Itinerario) {
                sucesion_automatica = true;
                return true;
            } else if (cmd == "DASA") {
                if (!dai()) return false;
                sucesion_automatica = false;
                return true;
            } else if (cmd == "ASA" && !ocupada) {
                sucesion_automatica = false;
                return true;
            }
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
        if (!mandada) return;
        if (id == señal_inicio->get_seccion_señal()->get_cv()->id && ecv.evento && ecv.evento->ocupacion && ecv.evento->lado == lado) {
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
                if (!sucesion_automatica && tipo != TipoMovimiento::Maniobra) señal_inicio->ruta_activa = nullptr;
            }
        }
    }
    void construir_proximidad(seccion_via *inicio, seccion_via *next, Lado dir_fwd, std::vector<std::pair<seccion_via*, Lado>> &secciones)
    {
        if (inicio == nullptr) return;
        if (ultimos_cvs_proximidad.find(inicio->get_cv()->id) != ultimos_cvs_proximidad.end()) return;
        int i0 = secciones.size();
        inicio->prev_secciones(next, dir_fwd, secciones);
        for (int i=i0; i<secciones.size(); i++) {
            construir_proximidad(secciones[i].first, inicio, secciones[i].second, secciones);
        }
    }
    void construir_proximidad()
    {
        proximidad.clear();
        auto *inicio = señal_inicio->get_seccion_señal();
        auto p = inicio->get_seccion_in(lado);
        if (p.first == nullptr || ultimos_cvs_proximidad.empty()) return;
        proximidad.push_back({p.first, opp_lado(p.second)});
        construir_proximidad(p.first, inicio, opp_lado(p.second), proximidad);
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