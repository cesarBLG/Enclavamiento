#pragma once
#include <enclavamiento.h>
#include "topology.h"
#include "remota.h"
#include "deslizamiento.h"
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
struct señal
{
    const id_elemento id;
    const Lado lado;
    const int pin;
    seccion_via * const seccion;
    ClaseSeñal clase;
    señal(const id_elemento &id, const json &j, ClaseSeñal clase_defecto);
    virtual ~señal() {}
};
class señal_fundamental : public señal, public estado_señal
{
public:
    const std::optional<id_elemento> bloqueo_asociado;
    const TipoSeñal tipo;
    frontera *frontera_entrada=nullptr;
    señal_fundamental(const id_elemento &id, const json &j, ClaseSeñal clase_defecto);
    aspecto_extendido get_state()
    {
        return aspecto;
    }
    virtual void message_señal(estado_señal est)
    {
        *((estado_señal*)this) = est;
    }
};
class señal_fundamental_impl : public señal_fundamental
{
public:
    const std::string topic;
protected:
    std::map<FocoSeñal,EstadoFocoSeñal> estado_foco_señal;
    std::vector<EstadoFocoSeñal> estado_pantalla;
    int64_t ultimo_cambio_focos = 0;
    bool apagada = false;
    std::set<Aspecto> aspectos_disponibles;
    std::vector<velocidad_agujas> aspectos_pantalla;
    int pajarita;
    std::map<Aspecto, std::vector<std::map<FocoSeñal, EstadoFocoSeñal>>> combinaciones_focos;
    ReconocimientoAnuncioPrecaucion aprec_reconocido;
    señal_fundamental *sig_señal=nullptr;
    señal_fundamental *señal_siguiente_aprec = nullptr;
    aspecto_extendido aspecto_bloqueado = Aspecto::ViaLibre;
    std::map<id_elemento,std::map<aspecto_extendido, aspecto_extendido>> aspectos_maximos_anterior_señal;
public:
    señal_fundamental_impl(const id_elemento &id, const json &j, ClaseSeñal clase_defecto= ClaseSeñal::AbsolutaFocos);
protected:
    static inline Aspecto get_aspecto_degradado(Aspecto asp)
    {
        switch (asp) {
            case Aspecto::ViaLibre:
                return Aspecto::ViaLibreCondicional;
            case Aspecto::ViaLibreCondicional:
                return Aspecto::PreanuncioParada;
            case Aspecto::PreanuncioParada:
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
    aspecto_extendido get_aspecto_degradado(aspecto_extendido asp)
    {
        if (asp.velocidad.velocidad > 0) {
            velocidad_agujas current;
            if (aspecto == Aspecto::AnuncioPrecaucion || aspecto == Aspecto::PreanuncioParada) {
                for (auto &v : aspectos_pantalla) {
                    if (v.posicion != asp.velocidad.posicion || v.velocidad >= asp.velocidad.velocidad) continue;
                    if (v.velocidad > current.velocidad) {
                        current = v;
                    }
                }
            }
            if (aspecto == Aspecto::AnuncioPrecaucion && current.velocidad < pajarita && (pajarita < asp.velocidad.velocidad || (pajarita == asp.velocidad.velocidad && asp.velocidad.posicion != LadoAguja::Recto)))
                current = {pajarita, LadoAguja::Recto};
            return {asp.aspecto, current};
        }
        Aspecto newasp = get_aspecto_degradado(asp.aspecto);
        if (newasp == asp.aspecto) return asp;
        return {newasp, velocidad_agujas::max()};
    }
    bool is_aspecto_disponible(aspecto_extendido asp)
    { 
        if (!aspectos_disponibles.contains(asp.aspecto)) return false;
        if (asp.aspecto == Aspecto::AnuncioPrecaucion && asp.velocidad.velocidad == pajarita && asp.velocidad.posicion == LadoAguja::Recto) return true;
        if (asp.velocidad.velocidad > 0) {
            if (asp.aspecto == Aspecto::PreanuncioParada || asp.aspecto == Aspecto::AnuncioPrecaucion) {
                for (auto &v : aspectos_pantalla) {
                    if (v == asp.velocidad) return true;
                }
            }
            return false;
        } else if (aspecto == Aspecto::PreanuncioParada || aspecto == Aspecto::AnuncioPrecaucion) {
            return false;
        }
        return true;
    }
    aspecto_extendido get_aspecto_anterior(aspecto_extendido asp, señal_fundamental *señal_sig)
    {
        id_elemento id;
        if (señal_sig != nullptr) id = señal_sig->id_señal_focos.id != "" ? señal_sig->id_señal_focos : señal_sig->id;
        if (aspectos_maximos_anterior_señal.find(id) == aspectos_maximos_anterior_señal.end()) id = id_elemento();
        return (--aspectos_maximos_anterior_señal[id].upper_bound(asp))->second;
    }
    void determinar_focos();
    bool normalizar_fusion();
public:
    void message_señal(estado_señal est) override {}
    void message_señal_campo(unsigned int focos_mask)
    {
        for (auto &[foco, estado] : estado_foco_señal) {
            auto estado_campo = (EstadoFocoSeñal)((focos_mask>>(2*(int)foco)) & 3);
            if (estado_campo > EstadoFocoSeñal::Apagado || estado != EstadoFocoSeñal::Fundido)
                estado = estado_campo;
        }
        int pantalla = focos_mask>>(2*(int)FocoSeñal::Pantalla);
        for (int i=0; i<estado_pantalla.size(); i++) {
            estado_pantalla[i] = pantalla == i - 1 ? EstadoFocoSeñal::Encendido : EstadoFocoSeñal::Apagado;
        }
    }
};
class señal_impl : public señal_fundamental_impl
{
public:
    const std::string topic_inicio;
    seccion_via * const seccion_prev;
    const Lado lado_prev;

    proximidad proximidad_señal;
    std::map<TipoMovimiento, ruta_deslizamiento*> deslizamientos;
protected:
    std::map<EstadoCanton, aspecto_extendido> aspecto_maximo_ocupacion;

    bloqueo *bloqueo_asociado_obj=nullptr;

    bool me_pendiente = false;
    bool rebasada;

    int64_t ultimo_paso_abierta;
    bool paso_circulacion = false;

    estado_inicio_ruta estado_inicio;

    bool itinerarios_desviada = false;
    ReconocimientoAnuncioPrecaucion aprec_anterior_reconocido = ReconocimientoAnuncioPrecaucion::Inactivo;
    int64_t inicio_aprec = 0;
    Aspecto aspecto_desviada = Aspecto::AnuncioParada;
    bool aprec_anterior = true;

    bool is_aspecto_disponible(aspecto_extendido asp);

public:
    bool clear_request=false;
    movimiento *ruta_activa=nullptr;

    bool ruta_necesaria = true;
    bool cierre_stick = true;
    bool abierta_desbloqueo = false;
    bool abierta_bloqueo_receptor = false;

    movimiento *ruta_fin=nullptr;
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
public:
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
    RespuestaMando mando(const std::string &cmd, int me);
    RemotaSIG get_estado_remota_sig();
    RemotaIMV get_estado_remota_imv();
    RemotaPV get_estado_remota_pv();
    estado_inicio_ruta get_estado_inicio();
    ruta_deslizamiento *get_deslizamiento(movimiento *m);
    bool is_rebasada() { return rebasada; }
    void set_reconocimiento_aprec(ReconocimientoAnuncioPrecaucion rec)
    {
        // FIXME: el topic no está incluido en el gestor de conexiones, por lo que no es seguro si se pierde la comunicación con la señal anterior.
        aprec_anterior_reconocido = rec;
    }
};
class señal_avanzada_sin_parada : public señal_fundamental_impl
{
    aspecto_extendido aspecto_minimo = Aspecto::AnuncioParada;
    void determinar_aspecto();
    bool is_aspecto_disponible(aspecto_extendido asp)
    {
        if (asp < aspecto_minimo) return false;
        return señal_fundamental_impl::is_aspecto_disponible(asp); 
    }
    void send_state()
    {
        send_message(topic, json(*(estado_señal*)this).dump());
    }
public:
    señal_avanzada_sin_parada(const id_elemento &id, const json &j);
    void update();
};
