#pragma once
#include <vector>
#include <variant>
#include "enums.h"
#include "bloqueo.h"
#include "cv.h"
#include "signal.h"
#include "deslizamiento.h"
#include "log.h"
#include "remota.h"
class movimiento;
class ruta;
class frontera;
class destino_ruta
{
protected:
    estado_fin_ruta estado;
public:
    const id_elemento id;
    const std::string topic;
    const TipoDestino tipo;
    bool bloqueo_destino = false;
    bool me_pendiente = false;
    ruta *ruta_activa = nullptr;
    señal_impl *señal_fin = nullptr;
    std::map<TipoMovimiento, ruta_deslizamiento*> deslizamientos;
    destino_ruta(const id_elemento &id, const json &j);
    RespuestaMando mando(const std::string &cmd, int me);
    RemotaFMV get_estado_remota();
    estado_fin_ruta get_estado();
    void send_state()
    {
        send_message(topic, json(estado).dump());
    }
    void update()
    {
        if (señal_fin != nullptr) {
            señal_fin->ruta_fin = ruta_activa;
        }
        auto prev_estado = estado;
        estado = get_estado();
        if (estado != prev_estado) send_state();
    }
    frontera* get_frontera();
};
class movimiento
{
public:
    const TipoMovimiento tipo;
    const bool ertms;
    const bool es_ruta;
    const std::string estacion;
    const std::string id;
    bool valid = false;
    CompatibilidadManiobra maniobra_compatible = CompatibilidadManiobra::IncompatibleBloqueo;

    std::map<ruta_deslizamiento*,int> deslizamientos_afectados;
protected:
    std::map<seccion_via*, EstadoCanton> ocupacion_maxima_secciones;
    std::map<seccion_via*, lados<int>> posicion_aparatos;
    std::vector<señal_impl*> señales;
    std::vector<elemento_ruta> secciones;
    std::set<seccion_via*> secciones_aseguradas;
    std::vector<std::pair<pn_enclavado*, Lado>> pn_afectados;

    bool mandada = false;
    bool formada = false;
public:
    movimiento(const std::string &estacion, TipoMovimiento tipo, const std::string &id, bool es_ruta=true, bool ertms=false) : estacion(estacion), tipo(tipo), id((tipo == TipoMovimiento::Itinerario ? (ertms ? "ER " : "I ") : (tipo == TipoMovimiento::Rebase ? "R " : (es_ruta ? "M " : "ML ")))+id), es_ruta(es_ruta), ertms(ertms) {}
    virtual bool establecer(bool msg=false);
    virtual bool posible_establecer(bool msg=false);
    virtual void disolver();
    virtual void update();
    bool is_mandada()
    {
        return mandada;
    }
    bool is_formada()
    {
        return formada;
    }
    void mover_agujas();
    const std::vector<elemento_ruta> &get_secciones()
    {
        return secciones;
    }
    const std::vector<señal_impl*> &get_señales()
    {
        return señales;
    }
    const std::map<seccion_via*,EstadoCanton> &get_ocupacion_maxima_secciones()
    {
        return ocupacion_maxima_secciones;
    }
};
class ruta : public movimiento
{
public:
    const std::string id_inicio;
    const std::string id_destino;
    const std::optional<id_elemento> bloqueo_salida;

    bool anulacion_bloqueo_pendiente = false;
protected:
    std::shared_ptr<timer> diferimetro_dai;
    std::shared_ptr<timer> diferimetro_dei;
    std::shared_ptr<timer> diferimetro_deslizamiento;
    señal_impl *señal_inicio;
    destino_ruta *destino;
    ruta_deslizamiento *deslizamiento = nullptr;
    Lado lado;
    Lado lado_bloqueo;
    estado_bloqueo bloqueo_act;

    bool supervisada = false;
    bool ocupada = false;
    bool diferimetro_cancelado = false;

    bool desactivar_diferimetro;
    int64_t temporizador_dai1;
    int64_t temporizador_dai2;
    int64_t temporizador_dei;
    int64_t temporizador_deslizamiento;

    seccion_via *seccion_inicio_temporizador_deslizamiento = nullptr;

    bool sucesion_automatica = false;
    EstadoFAI estado_fai = EstadoFAI::EnEspera;
    TipoFAI tipo_fai = TipoFAI::Proximidad;
    bool fai = false;
    bool fai_disparo_unico = false;
    int64_t inicio_temporizacion_fai = 0;
    int64_t tiempo_espera_fai;

    // Disolución completa de la ruta
    void disolver() override;
    // Disolución parcial de la ruta hasta el primer CV ocupado
    void disolucion_parcial(bool anular_bloqueo=false);
public:
    ruta(const std::string &estacion, const json &j);
    bool establecer(bool msg=false) override;
    bool posible_establecer(bool msg=false) override;
    estado_inicio_ruta get_estado_inicio()
    {
        estado_inicio_ruta e;
        e.ocupada_diferimetro = supervisada && diferimetro_cancelado;
        if (supervisada && diferimetro_dai != nullptr) e.fin_diferimetro = diferimetro_dai->time;
        e.formada = formada;
        e.tipo = tipo;
        return e;
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
    estado_fin_ruta get_estado_fin()
    {
        estado_fin_ruta e;
        e.supervisada = supervisada;
        e.bloqueo_destino = destino->bloqueo_destino;
        e.me_pendiente = destino->me_pendiente;
        e.tipo = tipo;
        if (diferimetro_dei) e.fin_diferimetro = diferimetro_dei->time;
        return e;
    }
    RemotaFMV get_estado_remota_fin()
    {
        RemotaFMV r;
        r.FMV_DAT = 1;
        if (!supervisada) r.FMV_EST = 0;
        else if (diferimetro_dei) r.FMV_EST = 6;
        else if (diferimetro_deslizamiento) r.FMV_EST = tipo == TipoMovimiento::Maniobra ? 5 : 4;
        else r.FMV_EST = tipo == TipoMovimiento::Maniobra ? 2 : 1;
        switch (destino->tipo) {
            case TipoDestino::Colateral:
                if (!secciones.empty() && secciones.back().seccion->is_trayecto()) r.FMV_DIF_ELEM = 3;
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
    void update() override;
    // Disolución artificial de ruta
    bool dai(bool anular_bloqueo=false);
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
    bool dei();

    bool is_ocupada()
    {
        return ocupada;
    }
    bool is_supervisada()
    {
        return supervisada;
    }
    bool is_sucesion_automatica()
    {
        return sucesion_automatica;
    }
    bool deslizamiento_asegurado()
    {
        for (auto &[desliz, id] : deslizamientos_afectados) {
            if (!desliz->is_asegurado(id)) return false;
        }
        return true;
    }
    std::optional<EstadoFAI> get_estado_fai()
    {
        if (!fai && !fai_disparo_unico) return {};
        return estado_fai;
    }
    int get_estado_fai_remota()
    {
        if (!fai && !fai_disparo_unico) return 0;
        if (estado_fai == EstadoFAI::AperturaNoPosible || estado_fai == EstadoFAI::AperturaNoPosibleReconocida || estado_fai == EstadoFAI::Cancelado) return 3;
        if (estado_fai == EstadoFAI::Solicitud) return 2;
        return 1;
    }
    señal_impl *get_señal_inicio()
    {
        return señal_inicio;
    }
    destino_ruta *get_destino()
    {
        return destino;
    }
    bool set_fai(bool activar) {
        if (activar) {
            if (!fai && señal_inicio->ruta_fai == nullptr) {
                fai = true;
                señal_inicio->ruta_fai = this;
                return true;
            }
        } else {
            if ((fai || fai_disparo_unico) && señal_inicio->ruta_fai == this) {
                fai = false;
                fai_disparo_unico = false;
                señal_inicio->ruta_fai = nullptr;
                return true;
            }
        }
        return false;
    }

    RespuestaMando mando(const std::string &inicio, const std::string &fin, const std::string &cmd);

    void message_cv(const id_elemento &id, estado_cv ecv);
    void message_bloqueo(const id_elemento &id, estado_bloqueo eb)
    {
        if (id != bloqueo_salida) return;
        bloqueo_act = eb;
    }
    void message_aguja(const id_elemento &id, estado_aguja estado)
    {
    }
protected:
    void activar_pns();
    void desactivar_pns();
};
