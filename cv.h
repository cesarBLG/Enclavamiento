#pragma once
#include <set>
#include <map>
#include <vector>
#include "lado.h"
#include "enums.h"
#include "mqtt.h"
#include "time.h"
#include "log.h"
#include "remota.h"
#include <optional>
struct evento_cv
{
    Lado lado;
    bool ocupacion;
    std::string cv_colateral;
};
struct estado_cv
{
    EstadoCV estado;
    EstadoCV estado_previo;
    std::optional<evento_cv> evento;
    bool averia;
    bool btv;
    bool perdida_secuencia;
    bool sin_datos=false;
    bool me_pendiente=false;
};
void to_json(json &j, const estado_cv &estado);
void from_json(const json &j, estado_cv &estado);
void from_json(const json &j, evento_cv &ev);
void to_json(json &j, const evento_cv &ev);
class ruta;
class señal;
class seccion_via;
class cv : public estado_cv
{
public:
    const std::string id;
    std::set<seccion_via*> secciones;
    bool ocupacion_intempestiva = false;
    cv(const std::string &id) : id(id) {}
    
    EstadoCV get_state()
    {
        return estado;
    }

    EstadoCanton get_ocupacion(Lado lado)
    {
        if (estado == EstadoCV::Libre) return EstadoCanton::Libre;
        else if (estado == EstadoCV::Prenormalizado) return EstadoCanton::Prenormalizado;
        else if (estado == EstadoCV::OcupadoImpar && lado == Lado::Impar) return EstadoCanton::OcupadoMismoSentido;
        else if (estado == EstadoCV::OcupadoPar && lado == Lado::Par) return EstadoCanton::OcupadoMismoSentido;
        else return EstadoCanton::Ocupado;
    }

    bool is_asegurada(ruta *ruta=nullptr);
    bool is_averia() { return averia; }
    bool is_btv() { return btv; }
    bool is_perdida_secuencia() { return perdida_secuencia; }

    void message_cv(estado_cv ecv)
    {
        *((estado_cv*)this) = ecv;
        remota_cambio_elemento(ElementoRemota::CV, id);
        if (estado <= EstadoCV::Prenormalizado) ocupacion_intempestiva = false;
    }

    RemotaCV get_estado_remota();
};
class cv_impl : public estado_cv
{
public:
    struct cejes_position
    {
        Lado lado;
        bool reverse;
        std::string cv_colateral;
    };
    const std::string id;
protected:
    std::map<std::string, cejes_position> cejes;
    std::set<std::string> averia_cejes;

    int64_t tiempo_auto_prenormalizacion = 180000;
    int64_t tiempo_auto_prenormalizacion_tren = 20000;

    std::shared_ptr<timer> timer_auto_prenormalizacion;
    std::shared_ptr<timer> timer_auto_prenormalizacion_tren;

    lados<int> num_ejes;
    lados<std::vector<int>> num_trenes;
    lados<int64_t> ultimo_eje;

    bool normalizado;

    bool me_pendiente = false;

public:
    const std::string topic;
    cv_impl(const std::string &id, const json &j);

    void update()
    {
        lados<bool> ocupado;
        ocupado[Lado::Impar] = num_ejes[Lado::Impar] > 0;
        ocupado[Lado::Par] = num_ejes[Lado::Par] > 0;
        for (auto &id : averia_cejes) {
            ocupado[cejes[id].lado] = true;
        }
        if (ocupado[Lado::Impar] && ocupado[Lado::Par]) {
            estado = EstadoCV::Ocupado;
        } else if (ocupado[Lado::Impar]) {
            estado = EstadoCV::OcupadoImpar;
        } else if (ocupado[Lado::Par]) {
            estado = EstadoCV::OcupadoPar;
        } else {
            estado = normalizado ? EstadoCV::Libre : EstadoCV::Prenormalizado;
        }
        averia = !averia_cejes.empty();
        send_state();
    }

    void send_state()
    {
        json msg(*this);
        send_message(topic, msg.dump(), evento ? 2 : 1);

        evento = {};
        estado_previo = estado;
    }

    void message_cejes(const std::string &id, std::string msg)
    {
        auto it = cejes.find(id);
        if (it == cejes.end()) return;
        if (msg == "Error" || msg == "desconexion") {
            averia_cejes.insert(id);
            normalizado = false;
            update();
        } else {
            EstadoCV prev = estado;
            Lado lado = it->second.lado;
            if (it->second.reverse != (lado == Lado::Par)) {
                if (msg == "Nominal") msg = "Reverse";
                else if (msg == "Reverse") msg = "Nominal";
            }
            auto now = get_milliseconds();
            if (msg == "Nominal") {
                auto &arr = num_trenes[lado];
                if (!arr.empty()) {
                    int diff = arr[arr.size()-1] - num_ejes[lado];
                    if (diff > 0) {
                        for (auto it2 = arr.begin(); it2 != arr.end(); ) {
                            if (*it2 <= diff) {
                                it2 = arr.erase(it2);
                            } else {
                                *it2 -= diff;
                                ++it2;
                            }
                        }
                    }
                    if (now - ultimo_eje[lado] < tiempo_auto_prenormalizacion_tren) arr.pop_back();
                }
                ++num_ejes[lado];
                arr.push_back(num_ejes[lado]);
                ultimo_eje[lado] = now;
            } else if (msg == "Reverse") {
                if (num_ejes[lado] > 0) {
                    --num_ejes[lado];
                    auto &arr = num_trenes[lado];
                    if (!arr.empty()) {
                        int prev = arr.size() > 1 ? arr[arr.size() - 2] : 0;
                        int ejes = num_ejes[lado] - prev;
                        if (ejes <= 0) arr.pop_back();
                    }
                } else {
                    Lado opLado = opp_lado(lado);
                    if (num_ejes[opLado] > 0) {
                        --num_ejes[opLado];
                        if (num_ejes[opLado] == 0/* && num_ejes[lado] == 0*/) normalizado = true;
                        auto &arr = num_trenes[opLado];
                        if (!arr.empty()) {
                            int diff = arr[arr.size()-1] - num_ejes[opLado];
                            if (diff >= arr[0]) {
                                int val0 = arr[0];
                                arr.erase(arr.begin());
                                for (int &n : arr) {
                                    n -= val0;
                                }
                            }
                        }
                    }
                    //else normalizado = false;
                }
            } else {
                return;
            }
            if (num_ejes[Lado::Impar] == 0) num_trenes[Lado::Impar].clear();
            if (num_ejes[Lado::Par] == 0) num_trenes[Lado::Par].clear();
            clear_timer(timer_auto_prenormalizacion);
            clear_timer(timer_auto_prenormalizacion_tren);

            timer_auto_prenormalizacion = set_timer([this]() {
                if (num_ejes[Lado::Impar] > 0 || num_ejes[Lado::Par] > 0) {
                    prenormalizar();
                    log(this->id, "timer prenormalizacion", LOG_WARNING);
                    update();
                }
            }, tiempo_auto_prenormalizacion);
            set_timer_auto_prenormalizacion_tren();
            averia_cejes.erase(id);
            evento = {lado, msg == "Nominal", it->second.cv_colateral};
            update();
        }
    }

    void message_cv(const std::string &msg)
    {
        if (msg == "Normalizar") normalizar();
        else if (msg == "NormalizarSecuencia") perdida_secuencia = false;
        else if (msg == "PérdidaSecuencia") perdida_secuencia = true;
    }

    void prenormalizar()
    {
        num_ejes[Lado::Impar] = 0;
        num_ejes[Lado::Par] = 0;
        num_trenes[Lado::Impar].clear();
        num_trenes[Lado::Par].clear();
        normalizado = false;
        update();
    }

    void normalizar()
    {
        if (normalizado) return;
        normalizado = true;
        update();
    }

    RespuestaMando mando(const std::string &cmd, int me)
    {
        RespuestaMando aceptado = RespuestaMando::OrdenRechazada;
        if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
        bool pend = me_pendiente;
        me_pendiente = false;
        if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        if (cmd == "LC" && estado > EstadoCV::Prenormalizado) {
            if (me) {
                averia_cejes.clear();
                prenormalizar();
                aceptado = RespuestaMando::Aceptado;
            } else {
                me_pendiente = true;
                aceptado = RespuestaMando::MandoEspecialNecesario;
            }
        } else if (cmd == "BTV") {
            if (!btv) {
                btv = true;
                aceptado = RespuestaMando::Aceptado;
            }
        } else if (cmd == "ABTV" || cmd == "DTV") {
            if (btv) {
                if (me) {
                    btv = false;
                    aceptado = RespuestaMando::Aceptado;
                } else {
                    me_pendiente = true;
                    aceptado = RespuestaMando::MandoEspecialNecesario;
                }
            }
        }
        return aceptado;
    }

    void set_timer_auto_prenormalizacion_tren()
    {
        timer_auto_prenormalizacion_tren = set_timer([this]() {
            bool changed;
            for (auto lado : {Lado::Impar, Lado::Par}) {
                if (num_ejes[lado] == 0) continue;
                auto &arr = num_trenes[lado];
                if (arr.empty()) continue;
                int diff = arr[arr.size() - 1] - num_ejes[lado];
                int val0 = arr[0];
                if (diff >= 0.5 * val0) {
                    arr.erase(arr.begin());
                    num_ejes[lado] = arr.empty() ? 0 : arr[arr.size()-1] - val0;
                    for (int &n : arr) {
                        n -= val0;
                    }
                    normalizado = false;
                    changed = true;
                    log(id, "timer prenormalizacion tren", LOG_WARNING);
                }
            }
            if (changed) {
                set_timer_auto_prenormalizacion_tren();
                update();
            }
        }, tiempo_auto_prenormalizacion_tren);
    }

    void asignar_cejes(std::map<std::string,std::vector<std::string>> &cejes_to_cvs)
    {
        for (auto &[idce, pos] : cejes) {
            auto it = cejes_to_cvs.find(idce);
            if (it != cejes_to_cvs.end()) {
                for (auto &idcv : it->second) {
                    if (idcv != id) {
                        pos.cv_colateral = idcv;
                    }
                }
            }
        }
    }
};
void from_json(const json &j, cv_impl::cejes_position &position);
