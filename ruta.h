#pragma once
#include <vector>
#include <variant>
#include "enums.h"
#include "bloqueo.h"
#include "cv.h"
#include "signal.h"
#include "log.h"
#include "remota.h"
class ruta;
class destino_ruta
{
public:
    const std::string id;
    const TipoDestino tipo;
    bool bloqueo_destino = false;
    bool sucesion_automatica = false;
    bool me_pendiente = false;
    ruta *ruta_activa = nullptr;
    destino_ruta(const std::string &id, TipoDestino tipo) : id(id), tipo(tipo) {}
    RespuestaMando mando(const std::string &cmd, int me);
    RemotaFMV get_estado_remota();
};
class ruta
{
public:
    const std::string estacion;
    const TipoMovimiento tipo;
    const std::string id_inicio;
    const std::string id_destino;
    const std::string id;
    const std::string bloqueo_salida;
    bool maniobra_compatible;
    bool valid = false;

    bool anulacion_bloqueo_pendiente = false;
protected:
    std::vector<std::pair<seccion_via*,Lado>> secciones;
    std::set<seccion_via*> secciones_aseguradas;
    std::shared_ptr<timer> diferimetro_dai;
    std::shared_ptr<timer> diferimetro_dei;
    std::vector<std::pair<seccion_via*,Lado>> proximidad;
    std::set<std::string> ultimos_cvs_proximidad;
    seccion_via *ultima_seccion_señal;
    señal_impl *señal_inicio;
    destino_ruta *destino;
    estado_bloqueo bloqueo_act;
    Lado lado;
    Lado lado_bloqueo;

    bool mandada = false;
    bool formada = false;
    bool supervisada = false;
    bool ocupada = false;
    bool diferimetro_cancelado = false;

    bool sucesion_automatica = false;
    bool fai = false;
    bool fai_lanzado = false;

    void disolver()
    {
        if (señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
        mandada = false;
        formada = false;
        supervisada = false;
        ocupada = false;
        fai_lanzado = false;
        clear_timer(diferimetro_dai);
        clear_timer(diferimetro_dei);
        diferimetro_dai = nullptr;
        diferimetro_dei = nullptr;
        diferimetro_cancelado = false;
        for (auto *sec : secciones_aseguradas) {
            sec->liberar(this);
        }
        secciones_aseguradas.clear();
        log(id, "disuelta");
    }
    void disolucion_parcial(bool anular_bloqueo=false)
    {
        if (!ocupada) {
            anulacion_bloqueo_pendiente = anular_bloqueo;
            disolver();
            return;
        }
        if (señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
        fai_lanzado = false;
        clear_timer(diferimetro_dai);
        diferimetro_dai = nullptr;
        diferimetro_cancelado = false;
        log(id, "disolución parcial");
        for (int i=0; i<secciones.size(); i++) {
            auto *sec = secciones[i].first;
            auto *prev = i>0 ? secciones[i-1].first : señal_inicio->seccion_prev;
            if (!sec->get_cv()->is_asegurada(this)) break;
            if (sec->get_ocupacion(prev, secciones[i].second) > EstadoCanton::Prenormalizado) break;
            sec->liberar(this);
            secciones_aseguradas.erase(sec);
        }
    }
public:
    ruta(const std::string &estacion, const json &j);
    bool establecer()
    {
        if (destino->bloqueo_destino || señal_inicio->bloqueo_señal) return false;
        clear_timer(diferimetro_dai);
        diferimetro_dai = nullptr;
        if (mandada && !ocupada) {
            señal_inicio->clear_request = true;
            log(id, "re-mandada", LOG_DEBUG);
            return true;
        }
        if (bloqueo_salida != "") {
            if ((tipo == TipoMovimiento::Itinerario || (tipo == TipoMovimiento::Maniobra && !maniobra_compatible)) && bloqueo_act.estado == (lado_bloqueo == Lado::Par ? EstadoBloqueo::BloqueoImpar : EstadoBloqueo::BloqueoPar))
                return false;
            if (bloqueo_act.ruta[lado] != tipo && bloqueo_act.ruta[lado] != TipoMovimiento::Ninguno)
                return false;
        }
        for (int i=0; i<secciones.size(); i++) {
            auto *sec = secciones[i].first;
            auto *prev = i>0 ? secciones[i-1].first : señal_inicio->seccion_prev;
            if (sec->get_cv()->is_asegurada() && !sec->get_cv()->is_asegurada(this)) return false;
            if (sec->is_bloqueo_seccion()) return false;
            if (sec->get_ocupacion(prev, secciones[i].second) == EstadoCanton::Ocupado) return false;
        }
        log(id, "mandada", LOG_DEBUG);
        clear_timer(diferimetro_dei);
        diferimetro_dei = nullptr;
        mandada = formada = true;
        señal_inicio->clear_request = true;
        señal_inicio->ruta_activa = this;
        destino->ruta_activa = this;
        for (int i=0; i<secciones.size(); i++) {
            auto *sec = secciones[i].first;
            auto *prev = i>0 ? secciones[i-1].first : señal_inicio->seccion_prev;
            auto *next = i+1<secciones.size() ? secciones[i+1].first : nullptr;
            secciones_aseguradas.insert(sec);
            sec->asegurar(this, prev, next, secciones[i].second);
        }
        return true;
    }
    RemotaIMV get_estado_remota_inicio()
    {
        RemotaIMV r;
        r.IMV_DAT = 1;
        if (señal_inicio->is_rebasada()) r.IMV_EST = 7;
        else if (supervisada && diferimetro_cancelado) r.IMV_EST = 3;
        else if (supervisada && diferimetro_dai != nullptr) r.IMV_EST = 6;
        else if (supervisada) r.IMV_EST = tipo == TipoMovimiento::Maniobra ? 2 : 1;
        else if (formada) r.IMV_EST = tipo == TipoMovimiento::Maniobra ? 5 : 4;
        else r.IMV_EST = 0;
        if (diferimetro_dai != nullptr) {
            r.IMV_DIF_VAL = 1;
        } else {
            r.IMV_DIF_VAL = 0;
        }
        return r;
    }
    RemotaFMV get_estado_remota_fin()
    {
        RemotaFMV r;
        r.FMV_DAT = 1;
        if (!supervisada) r.FMV_EST = 0;
        else if (diferimetro_dei) r.FMV_EST = 6;
        else r.FMV_EST = tipo == TipoMovimiento::Maniobra ? 2 : 1;
        switch (destino->tipo) {
            case TipoDestino::Colateral:
                if (!secciones.empty() && secciones.back().first->is_trayecto()) r.FMV_DIF_ELEM = 3;
                else r.FMV_DIF_ELEM = 2;
                break;
            case TipoDestino::Señal:
                r.FMV_DIF_ELEM = 0;
                break;
            default:
                r.FMV_DIF_ELEM = 1;
        }
        r.FMV_ME = destino->me_pendiente ? 1 : 0;
        r.FMV_BD = destino->bloqueo_destino ? 1 : 0;
        return r;
    }
    void update()
    {
        bool fai_activo = false;
        if (fai) {
            bool proximidad_ocupada = false;
            for (auto [sec, dir] : proximidad) {
                auto e = sec->get_cv()->get_state();
                if (e > EstadoCV::Prenormalizado && (e != (dir == Lado::Impar ? EstadoCV::OcupadoPar : EstadoCV::OcupadoImpar))) {
                    proximidad_ocupada = true;
                    break;
                }
            }
            bool aut_salida = bloqueo_salida == "" || 
                (!bloqueo_act.cierre_señales[lado] && !bloqueo_act.prohibido[lado] && bloqueo_act.actc[lado] != ACTC::Denegada);
            if (aut_salida && proximidad_ocupada)
                fai_activo = true;
            else if ((!aut_salida || !proximidad_ocupada) && señal_inicio->ruta_activa == this && diferimetro_dai == nullptr && fai_lanzado)
                dai(true);
        }
        if (fai_activo && señal_inicio->ruta_activa != this) {
            fai_lanzado = true;
            establecer();
        }
        if (fai_activo && señal_inicio->ruta_activa == this && !señal_inicio->clear_request) {
            señal_inicio->clear_request = true;
        }
        if (!mandada) return;
        if (ocupada && !sucesion_automatica) {
            if (tipo == TipoMovimiento::Maniobra && proximidad.size() > 0 && (proximidad[0].first->get_cv()->get_state() <= EstadoCV::Prenormalizado || proximidad[0].first->get_cv()->get_state() == (proximidad[0].second == Lado::Impar ? EstadoCV::OcupadoPar : EstadoCV::OcupadoImpar))) {
                if (señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
            }
            for (int i=0; i<secciones.size(); i++) {
                if (secciones[i].first->get_cv()->get_state() <= EstadoCV::Prenormalizado) {
                    bool liberar = false;
                    if (i == 0) {
                        if (señal_inicio->ruta_activa == nullptr || tipo == TipoMovimiento::Maniobra) liberar = true;
                    } else if (!secciones[i-1].first->is_asegurada(this)) {
                        liberar = true;
                    }
                    if (liberar) {
                        secciones[i].first->liberar(this);
                        secciones_aseguradas.erase(secciones[i].first);
                        if (i == secciones.size() - 1) disolver();
                    }
                }
            }
            if (señal_inicio->ruta_activa != this && secciones.empty()) disolver();
        }
        if (ocupada && señal_inicio->ruta_activa == this) {
            bool libre = true;
            for (int i=0; i<secciones.size(); i++) {
                if (secciones[i].first->get_cv()->get_state() > EstadoCV::Prenormalizado) {
                    libre = false;
                    break;
                }
            }
            if (libre) {
                ocupada = false;
                supervisada = señal_inicio->get_state() != Aspecto::Parada;
            }
        }
        if (formada && !supervisada && señal_inicio->get_state() != Aspecto::Parada) {
            supervisada = true;
            log(id, "supervisada");
        }
    }
    bool dai(bool anular_bloqueo=false)
    {
        if (!mandada) return false;
        log(id, "dai", LOG_DEBUG);
        if (!supervisada) {
            disolucion_parcial(anular_bloqueo);
            return true;
        }
        bool proximidad_libre = true;
        for (auto [sec, dir] : proximidad) {
            auto e = sec->get_cv()->get_state();
            if (e != EstadoCV::Libre) {
                proximidad_libre = false;
                break;
            }
        }
        if (proximidad_libre && proximidad.size() > 0) {
            disolucion_parcial(anular_bloqueo);
        } else if (diferimetro_dai == nullptr) {
            diferimetro_dai = set_timer([this, anular_bloqueo]() {
                diferimetro_dai = nullptr;
                disolucion_parcial(anular_bloqueo);
            }, 15000);
        }
        return true;
    }
    bool dei()
    {
        if (!ocupada || !supervisada) return false;
        log(id, "dei", LOG_DEBUG);
        if (diferimetro_dei == nullptr) {
            diferimetro_dei = set_timer([this]() {
                diferimetro_dei = nullptr;
                disolver();
            }, 60*1000);
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
    seccion_via *get_ultima_seccion_señal()
    {
        return ultima_seccion_señal;
    }

    void message_bloqueo(std::string id, estado_bloqueo estado)
    {
        if (id != bloqueo_salida) return;
        bloqueo_act = estado;
        update();
    }

    RespuestaMando mando(const std::string &inicio, const std::string &fin, const std::string &cmd)
    {
        if (inicio != id_inicio || fin != id_destino) return RespuestaMando::OrdenNoAplicable;
        if ((cmd == "I" || cmd == "FAI" || cmd == "AFA") && tipo != TipoMovimiento::Itinerario) return RespuestaMando::OrdenNoAplicable;
        if (cmd == "M" && tipo != TipoMovimiento::Maniobra) return RespuestaMando::OrdenNoAplicable;
        if (cmd == "I") return establecer() ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        else if (cmd == "M") return establecer() ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        else if (cmd == "FAI") {
            if (!fai) {
                fai = true;
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "AFA") {
            if (fai) {
                fai = false;
                return RespuestaMando::Aceptado;
            }
        }
        return RespuestaMando::OrdenRechazada;
    }
    void message_cv(const std::string &id, estado_cv ecv)
    {
        if (!mandada) return;
        if (id == señal_inicio->seccion->get_cv()->id && ecv.evento && ecv.evento->ocupacion && ecv.evento->lado == lado) {
            if (!supervisada) {
                supervisada = true;
                log(this->id, "supervisada");
            }
            if (!ocupada) {
                sucesion_automatica = tipo == TipoMovimiento::Itinerario && señal_inicio->sucesion_automatica && destino->sucesion_automatica;
                ocupada = true;
                if (diferimetro_dai != nullptr) {
                    clear_timer(diferimetro_dai);
                    diferimetro_dai = nullptr;
                    diferimetro_cancelado = true;
                    log(this->id, "ocupada con dai activo", LOG_WARNING);
                } else {
                    log(this->id, "ocupada");
                }
            }
            fai_lanzado = false;
            if (!sucesion_automatica && tipo != TipoMovimiento::Maniobra) señal_inicio->ruta_activa = nullptr;
        }
    }
protected:
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
        auto *inicio = señal_inicio->seccion;
        auto p = inicio->get_seccion_in(lado, señal_inicio->pin);
        if (p.first == nullptr || ultimos_cvs_proximidad.empty()) return;
        proximidad.push_back({p.first, p.second});
        construir_proximidad(p.first, inicio, p.second, proximidad);
    }
};
