#pragma once
#include <enclavamiento.h>
#include "topology.h"
#include "remota.h"
class ruta;
class frontera;
class señal_impl;
class bloqueo;
struct proximidad
{
    señal_impl *señal_inicio;
    std::map<seccion_via*,std::pair<Lado,seccion_via*>> proximidad0;
    std::map<seccion_via*,std::pair<Lado,seccion_via*>> proximidad1;
    std::set<id_elemento> ultimos_cvs_proximidad;
    proximidad(señal_impl *señal_inicio) : señal_inicio(señal_inicio) {}
    void construir();
    void construir0(seccion_via *next, seccion_via *sec, Lado dir);
    const std::map<seccion_via*,std::pair<Lado,seccion_via*>> &get(TipoMovimiento tipo)
    {
        if (tipo == TipoMovimiento::Maniobra || tipo == TipoMovimiento::Rebase) return proximidad0;
        return proximidad1;
    }
};
class señal : public estado_señal
{
public:
    const id_elemento id;
    const std::optional<id_elemento> bloqueo_asociado;
    const Lado lado;
    const TipoSeñal tipo;
    const bool señal_virtual=false;
    const int pin;
    seccion_via * const seccion;
    seccion_via * const seccion_prev;
    const Lado lado_prev;
    frontera *frontera_entrada=nullptr;
    señal(const id_elemento &id, const json &j);
    virtual ~señal() {}
    Aspecto get_state()
    {
        return aspecto;
    }
    virtual void message_señal(estado_señal est)
    {
        *((estado_señal*)this) = est;
    }
};
class señal_impl : public señal
{
public:
    const std::string topic;
    const std::string topic_inicio;
    proximidad proximidad_señal;
protected:
    std::map<EstadoCanton, Aspecto> aspecto_maximo_ocupacion;
    std::map<Aspecto, Aspecto> aspectos_maximos_anterior_señal;

    bloqueo *bloqueo_asociado_obj=nullptr;
    señal *sig_señal=nullptr;

    bool me_pendiente = false;
    bool rebasada;

    int64_t ultimo_paso_abierta;
    bool paso_circulacion = false;

    estado_inicio_ruta estado_inicio;

    bool itinerarios_desviada = false;
    ReconocimientoAnuncioPrecaucion aprec_anterior_reconocido = ReconocimientoAnuncioPrecaucion::Inactivo;
    señal *señal_siguiente_aprec = nullptr;
    ReconocimientoAnuncioPrecaucion aprec_reconocido = ReconocimientoAnuncioPrecaucion::Inactivo;
    int64_t inicio_aprec = 0;
    Aspecto aspecto_desviada = Aspecto::AnuncioParada;
    bool aprec_anterior = true;

    std::map<FocoSeñal,EstadoFocoSeñal> estado_foco_señal;
    int64_t ultimo_cambio_focos = 0;
    bool apagada = false;
    std::set<Aspecto> aspectos_disponibles;
    std::map<Aspecto, std::vector<std::map<FocoSeñal, EstadoFocoSeñal>>> combinaciones_focos;
    static inline Aspecto get_aspecto_degradado(Aspecto asp)
    {
        switch (asp) {
            case Aspecto::ViaLibre:
                return Aspecto::ViaLibreCondicional;
            case Aspecto::ViaLibreCondicional:
            case Aspecto::AnuncioPrecaucion:
                return Aspecto::AnuncioParada;
            case Aspecto::AnuncioParada:
                return Aspecto::Precaucion;
            case Aspecto::Precaucion:
                return Aspecto::ParadaSelectivaDestellos;
            case Aspecto::ParadaSelectivaDestellos:
                return Aspecto::ParadaSelectiva;
            case Aspecto::IndicadoraDesviada:
            case Aspecto::IndicadoraDirecta:
            case Aspecto::MovimientoAutorizado:
                return asp;
            default:
                return Aspecto::Parada;
        }
    }
    bool is_aspecto_disponible(Aspecto asp);
    bool normalizar_fusion();

public:
    bool clear_request=false;
    movimiento *ruta_activa=nullptr;
    Aspecto aspecto_bloqueado = Aspecto::ViaLibre;

    bool ruta_necesaria = true;
    bool cierre_stick;
    bool abierta_desbloqueo = false;
    bool abierta_bloqueo_receptor = false;

    ruta *ruta_fin=nullptr;
    ruta *ruta_fai=nullptr;
    bool bloqueo_señal = false;
    bool sucesion_automatica = false;

    frontera *frontera_salida=nullptr;

    señal_impl(const id_elemento &id, const json &j);
    void send_state(bool aspecto=true, bool inicio=true)
    {
        if (aspecto) send_message(topic, json(*(estado_señal*)this).dump());
        if (inicio) send_message(topic_inicio, json(estado_inicio).dump());
    }
    void determinar_aspecto();
    void update();
    void message_cv(const id_elemento &id, estado_cv ev);
    cv* get_cv_inicio();
    void ruta_mandada(movimiento *m)
    {
        ruta_activa = m;
        clear_request = true;
        aspecto_bloqueado = Aspecto::ViaLibre;
        normalizar_fusion();
    }
    void message_señal(estado_señal est) override {}
    void message_señal_campo(unsigned int focos_mask)
    {
        for (auto &[foco, estado] : estado_foco_señal) {
            auto estado_campo = (EstadoFocoSeñal)((focos_mask>>(2*(int)foco)) & 3);
            if (estado_campo > EstadoFocoSeñal::Apagado || estado != EstadoFocoSeñal::Fundido)
                estado = estado_campo;
        }
    }
    void determinar_focos();
    RespuestaMando mando(const std::string &cmd, int me);
    RemotaSIG get_estado_remota_sig();
    RemotaIMV get_estado_remota_imv();
    RemotaPV get_estado_remota_pv();
    estado_inicio_ruta get_estado_inicio();
    bool is_rebasada() { return rebasada; }
    void set_reconocimiento_aprec(ReconocimientoAnuncioPrecaucion rec)
    {
        // FIXME: el topic no está incluido en el gestor de conexiones, por lo que no es seguro si se pierde la comunicación con la señal anterior.
        aprec_anterior_reconocido = rec;
    }
};
