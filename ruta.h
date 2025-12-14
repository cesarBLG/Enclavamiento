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
    const bool ertms = false;
    const std::string id_inicio;
    const std::string id_destino;
    const std::string id;
    const std::string bloqueo_salida;
    CompatibilidadManiobra maniobra_compatible;
    bool valid = false;

    bool anulacion_bloqueo_pendiente = false;
protected:
    std::vector<std::pair<seccion_via*,Lado>> secciones;
    std::map<seccion_via*, std::pair<int, int>> posicion_aparatos;
    std::set<seccion_via*> secciones_aseguradas;
    std::shared_ptr<timer> diferimetro_dai;
    std::shared_ptr<timer> diferimetro_dei;
    std::vector<std::pair<seccion_via*,Lado>> proximidad;
    std::set<std::string> ultimos_cvs_proximidad;
    señal_impl *señal_inicio;
    destino_ruta *destino;
    Lado lado;
    Lado lado_bloqueo;
    estado_bloqueo bloqueo_act;

    bool mandada = false;
    bool formada = false;
    bool supervisada = false;
    bool ocupada = false;
    bool diferimetro_cancelado = false;

    int64_t temporizador_dai1;
    int64_t temporizador_dai2;
    int64_t temporizador_dei;

    bool sucesion_automatica = false;
    enum struct EstadoFAI
    {
        EnEspera,
        Solicitud,
        AperturaNoPosible,
        AperturaNoPosibleReconocida,
        Activo,
        Cancelado
    };
    EstadoFAI estado_fai = EstadoFAI::EnEspera;
    bool fai = false;
    bool fai_disparo_unico = false;
    int64_t inicio_temporizacion_fai = 0;
    int64_t tiempo_espera_fai;

    // Disolución completa de la ruta
    void disolver()
    {
        if (señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
        mandada = false;
        formada = false;
        supervisada = false;
        ocupada = false;
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
    // Disolución parcial de la ruta hasta el primer CV ocupado
    void disolucion_parcial(bool anular_bloqueo=false)
    {
        if (!ocupada) {
            anulacion_bloqueo_pendiente = anular_bloqueo;
            disolver();
            return;
        }
        if (señal_inicio->ruta_activa == this) señal_inicio->ruta_activa = nullptr;
        if (estado_fai == EstadoFAI::Activo) {
            estado_fai = EstadoFAI::EnEspera;
            inicio_temporizacion_fai = 0;
        }
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
    bool establecer();
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
    void update();
    // Disolución artificial de ruta
    bool dai(bool anular_bloqueo=false)
    {
        if (!mandada) return false;
        log(id, "dai", LOG_DEBUG);
        // Si la señal no llegó a abrir, la disolución es inmediata
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
        // Si la proximidad está libre, la disolución es inmediata
        // Si no hay secciones aseguradas por la ruta, ni un bloqueo tomado, no es necesario temporizar
        if ((proximidad_libre && proximidad.size() > 0) || (secciones.size() == 0 && bloqueo_salida == "")) {
            disolucion_parcial(anular_bloqueo);
        } else if (diferimetro_dai == nullptr) {
            int64_t temporizador = temporizador_dai1;
            // Temporización larga si se cumplen todas:
            // - Existe movimiento con fin en la señal o es la señal de entrada desde un bloqueo
            // - El aspecto de la señal condiciona señales anteriores
            // - Está ocupada la proximidad más allá del circuito anterior a la señal
            if (tipo == TipoMovimiento::Itinerario && señal_inicio->condiciona_anteriores() && 
            (señal_inicio->tipo == TipoSeñal::Entrada || señal_inicio->tipo == TipoSeñal::PostePuntoProtegido || señal_inicio->ruta_fin != nullptr)
            && (proximidad.empty() || proximidad[0].first->get_cv()->get_state() <= EstadoCV::Prenormalizado)) {
                temporizador = temporizador_dai2;
            }
            diferimetro_dai = set_timer([this, anular_bloqueo]() {
                diferimetro_dai = nullptr;
                disolucion_parcial(anular_bloqueo);
            }, temporizador);
        }
        return true;
    }
    bool cancelar_fai()
    {
        if (estado_fai != EstadoFAI::EnEspera && estado_fai != EstadoFAI::Cancelado) {
            estado_fai = EstadoFAI::Cancelado;
            fai_disparo_unico = false;
            return true;
        }
        return false;
    }
    // Disolución de emergencia
    // Temporización larga, disuelve secciones aseguradas aunque no estén libres
    bool dei()
    {
        if (!ocupada || !supervisada) return false;
        log(id, "dei", LOG_DEBUG);
        if (diferimetro_dei == nullptr) {
            diferimetro_dei = set_timer([this]() {
                diferimetro_dei = nullptr;
                disolver();
            }, temporizador_dei);
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
    const std::vector<std::pair<seccion_via*,Lado>> &get_secciones()
    {
        return secciones;
    }
    int get_estado_fai()
    {
        if (!fai && !fai_disparo_unico) return 0;
        if (estado_fai == EstadoFAI::AperturaNoPosible || estado_fai == EstadoFAI::AperturaNoPosibleReconocida || estado_fai == EstadoFAI::Cancelado) return 3;
        if (estado_fai == EstadoFAI::Solicitud) return 2;
        return 1;
    }

    RespuestaMando mando(const std::string &inicio, const std::string &fin, const std::string &cmd)
    {
        if (inicio != id_inicio || fin != id_destino) return RespuestaMando::OrdenNoAplicable;
        if ((cmd == "I" || cmd == "FAI" || cmd == "AFA" || cmd == "ID") && (tipo != TipoMovimiento::Itinerario || ertms)) return RespuestaMando::OrdenNoAplicable;
        if (cmd == "M" && tipo != TipoMovimiento::Maniobra) return RespuestaMando::OrdenNoAplicable;
        if (cmd == "R" && tipo != TipoMovimiento::Rebase) return RespuestaMando::OrdenNoAplicable;
        if (cmd == "ER" && !ertms) return RespuestaMando::OrdenNoAplicable;
        if (cmd == "I" || cmd == "ER" || cmd == "R" || cmd == "M") return establecer() ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        else if (cmd == "ID") {
            if (establecer()) return RespuestaMando::Aceptado;
            if (!fai_disparo_unico && (estado_fai == EstadoFAI::Cancelado || estado_fai == EstadoFAI::EnEspera)) {
                fai_disparo_unico = true;
                señal_inicio->ruta_fai = this;
                estado_fai = EstadoFAI::Solicitud;
                inicio_temporizacion_fai = get_milliseconds();
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "FAI") {
            if (!fai && señal_inicio->ruta_fai == nullptr) {
                fai = true;
                señal_inicio->ruta_fai = this;
                return RespuestaMando::Aceptado;
            }
        } else if (cmd == "AFA") {
            if ((fai || fai_disparo_unico) && señal_inicio->ruta_fai == this) {
                fai = false;
                fai_disparo_unico = false;
                señal_inicio->ruta_fai = nullptr;
                return RespuestaMando::Aceptado;
            }
        }
        return RespuestaMando::OrdenRechazada;
    }

    void message_cv(const std::string &id, estado_cv ecv)
    {
        if (!mandada) return;
        // Ocupación del primer CV de la ruta
        if (id == señal_inicio->seccion->get_cv()->id && ecv.evento && ecv.evento->ocupacion && ecv.evento->lado == lado) {
            if (!supervisada) {
                // La ruta pasa a estar supervisada aunque la señal no haya llegado a abrir
                supervisada = true;
                log(this->id, "supervisada");
            }
            if (!ocupada) {
                // Comprobación de si la ruta debe mantenerse enclavada por sucesión automática
                sucesion_automatica = tipo == TipoMovimiento::Itinerario && señal_inicio->sucesion_automatica;
                ocupada = true;
                // Si la ruta se ocupa con diferímetro de disolución, se cancela la disolución
                if (diferimetro_dai != nullptr) {
                    clear_timer(diferimetro_dai);
                    diferimetro_dai = nullptr;
                    diferimetro_cancelado = true;
                    log(this->id, "ocupada con dai activo", LOG_WARNING);
                } else {
                    log(this->id, "ocupada");
                }
            }
            // Rearme de FAI
            if (estado_fai == EstadoFAI::Activo) estado_fai = EstadoFAI::EnEspera;
            // Impedir nueva activación de FAI hasta que transcurra cierto tiempo
            if (estado_fai == EstadoFAI::EnEspera) inicio_temporizacion_fai = get_milliseconds();

            // Con el paso de la circulación se cierra la señal, salvo en maniobras
            if (!sucesion_automatica && tipo != TipoMovimiento::Maniobra) señal_inicio->ruta_activa = nullptr;
        }
    }
    void message_bloqueo(const std::string &id, estado_bloqueo eb)
    {
        if (id != bloqueo_salida) return;
        bloqueo_act = eb;
    }
    void messsage_aguja(const std::string &id, estado_aguja estado)
    {
        for (auto &[sec, d] : proximidad) {
            if (sec->id == id) {
                construir_proximidad();
                break;
            }
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
