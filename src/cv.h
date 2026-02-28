#pragma once
#include <set>
#include <map>
#include <vector>
#include <enclavamiento.h>
#include "mqtt.h"
#include "time.h"
#include "log.h"
#include "remota.h"
#include <optional>
class ruta;
class señal;
class seccion_via;
class cv : protected estado_cv
{
protected:
    bool me_pendiente=false;
public:
    const std::string id;
    std::set<seccion_via*> secciones;
    bool ocupacion_intempestiva = false;
    cv(const std::string &id) : id(id) {}
    virtual ~cv() = default;
    
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

    bool is_averia() { return averia; }
    bool is_btv() { return btv; }
    bool is_perdida_secuencia() { return perdida_secuencia; }

    virtual void message_cv(estado_cv ecv)
    {
        *((estado_cv*)this) = ecv;
        remota_cambio_elemento(ElementoRemota::CV, id);
        if (estado <= EstadoCV::Prenormalizado) ocupacion_intempestiva = false;
    }

    virtual RespuestaMando mando(const std::string &cmd, int me)
    {
        RespuestaMando aceptado = RespuestaMando::OrdenRechazada;
        if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
        bool pend = me_pendiente;
        me_pendiente = false;
        if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        if (cmd == "LC" && estado > EstadoCV::Prenormalizado) {
            if (me) {
                send_message("cv/"+id_to_mqtt(id)+"/action", "Prenormalizar");
                aceptado = RespuestaMando::Aceptado;
            } else {
                me_pendiente = true;
                aceptado = RespuestaMando::MandoEspecialNecesario;
                remota_cambio_elemento(ElementoRemota::CV, id);
            }
        }
        return aceptado;
    }

    RemotaCV get_estado_remota();
};
class cv_impl : public cv
{
public:
    struct cejes_position
    {
        Lado lado;
        bool reverse;
        bool ocupar=true;
        bool liberar=true;
        std::string cv_colateral;
    };
protected:
    std::map<std::string, cejes_position> cejes;
    std::set<std::string> averia_cejes;
    bool topera = false;

    int64_t tiempo_auto_prenormalizacion;
    int64_t tiempo_auto_prenormalizacion_tren;
    double fraccion_ejes_prenormalizacion;

    std::shared_ptr<timer> timer_auto_prenormalizacion;
    std::shared_ptr<timer> timer_auto_prenormalizacion_tren;

    std::shared_ptr<timer> timer_liberacion;

    lados<int> num_ejes;
    lados<std::vector<int>> num_trenes;
    lados<int64_t> ultimo_eje;
    lados<std::pair<int64_t,double>> ultimo_tren_liberado;
    EstadoCV estado_raw;

    bool normalizado;

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
            estado_raw = EstadoCV::Ocupado;
        } else if (ocupado[Lado::Impar]) {
            estado_raw = EstadoCV::OcupadoImpar;
        } else if (ocupado[Lado::Par]) {
            estado_raw = EstadoCV::OcupadoPar;
        } else {
            estado_raw = normalizado ? EstadoCV::Libre : EstadoCV::Prenormalizado;
        }
        averia = !averia_cejes.empty();
        if (estado_raw > EstadoCV::Prenormalizado && !averia) {
            bool normalizar = true;
            for (auto lado : {Lado::Impar, Lado::Par}) {
                if (num_ejes[lado] == 0) continue;
                auto &arr = num_trenes[lado];
                if (arr.empty()) continue;
                if (arr.size() > 1 || arr[0] - num_ejes[lado] < fraccion_ejes_prenormalizacion * arr[0]) {
                    normalizar = false;
                    break;
                }
            }
            if (normalizar) estado_raw = EstadoCV::Prenormalizado;
        }
        if (estado_raw >= estado) {
            estado = estado_raw;
            send_state();
        } else if (timer_liberacion == nullptr) {
            timer_liberacion = set_timer([this]() {
                estado = estado_raw;
                timer_liberacion = nullptr;
                send_state();
            }, 1000);
        }
    }

    void send_state()
    {
        remota_cambio_elemento(ElementoRemota::CV, id);
        if (estado <= EstadoCV::Prenormalizado) ocupacion_intempestiva = false;
        json msg(*((estado_cv*)this));
        send_message(topic, msg.dump());

        evento = {};
        estado_previo = estado;
    }

    void message_cejes(const std::string &id, std::string payload)
    {
        auto it = cejes.find(id);
        if (it == cejes.end()) return;
        if (payload == "Error" || payload == "desconexion") {
            if (it->second.ocupar) {
                log(id, "avería contador ejes", LOG_DEBUG);
                averia_cejes.insert(id);
                normalizado = false;
                update();
            }
        } else {
            std::string msg;
            int num;
            size_t pos = payload.find(':');
            if (pos != std::string::npos) { // if ':' was found
                msg = payload.substr(0, pos);
                num = atoi(payload.substr(pos + 1).c_str());
            } else {
                msg = payload;
                num = 1;
            }
            Lado lado = it->second.lado;
            if (it->second.reverse != (lado == Lado::Par)) {
                if (msg == "Nominal") msg = "Reverse";
                else if (msg == "Reverse") msg = "Nominal";
            }
            auto now = get_milliseconds();
            for (int i=0; i<num; i++) {
                if (msg == "Nominal" && it->second.ocupar) {
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
                } else if (msg == "Reverse" && it->second.liberar) {
                    if (num_ejes[lado] > 0) {
                        --num_ejes[lado];
                        auto &arr = num_trenes[lado];
                        if (!arr.empty()) {
                            int prev = arr.size() > 1 ? arr[arr.size() - 2] : 0;
                            int ejes = num_ejes[lado] - prev;
                            if (topera && num_ejes[lado] == 0 && arr[0] >= 2) normalizado = true;
                            if (ejes <= 0) {
                                ultimo_tren_liberado[lado] = {get_milliseconds(), arr.back() - prev};
                                arr.pop_back();
                            }
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
                                    ultimo_tren_liberado[opLado] = {get_milliseconds(), val0};
                                } else if (get_milliseconds() - ultimo_tren_liberado[opLado].first < 15000 && diff < ultimo_tren_liberado[opLado].second * (1 - fraccion_ejes_prenormalizacion)) {
                                    for (int &n : arr) {
                                        n -= diff;
                                    }
                                    ultimo_tren_liberado[opLado].second -= diff / (1-fraccion_ejes_prenormalizacion);
                                }
                            }
                        }
                        //else normalizado = false;
                    }
                }
            }
            if (num_ejes[Lado::Impar] == 0) num_trenes[Lado::Impar].clear();
            if (num_ejes[Lado::Par] == 0) num_trenes[Lado::Par].clear();
            clear_timer(timer_auto_prenormalizacion);
            clear_timer(timer_auto_prenormalizacion_tren);
            
            if (tiempo_auto_prenormalizacion > 0) {
                timer_auto_prenormalizacion = set_timer([this]() {
                    if (num_ejes[Lado::Impar] > 0 || num_ejes[Lado::Par] > 0) {
                        prenormalizar();
                        log(this->id, "timer prenormalizacion", LOG_WARNING);
                        update();
                    }
                }, tiempo_auto_prenormalizacion);
            }
            set_timer_auto_prenormalizacion_tren();
            averia_cejes.erase(id);
            evento = {lado, msg == "Nominal", it->second.cv_colateral};
            update();
        }
    }

    void message_cv(const std::string &msg)
    {
        if (msg == "Normalizar") normalizar();
        else if (msg == "Prenormalizar") prenormalizar();
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

    void message_cv(estado_cv ecv) override {}

    RespuestaMando mando(const std::string &cmd, int me) override
    {
        RespuestaMando aceptado = RespuestaMando::OrdenRechazada;
        if (me_pendiente && me == 0) return RespuestaMando::MandoEspecialEnCurso;
        bool pend = me_pendiente;
        me_pendiente = false;
        if (me < 0) return pend ? RespuestaMando::Aceptado : RespuestaMando::OrdenRechazada;
        if (cmd == "LC" && estado > EstadoCV::Prenormalizado) {
            if (me) {
                log(id, "prenormalizar", LOG_DEBUG);
                averia_cejes.clear();
                prenormalizar();
                aceptado = RespuestaMando::Aceptado;
            } else {
                me_pendiente = true;
                aceptado = RespuestaMando::MandoEspecialNecesario;
                send_state();
            }
        } else if (cmd == "BTV") {
            if (!btv) {
                log(id, "btv", LOG_DEBUG);
                btv = true;
                aceptado = RespuestaMando::Aceptado;
                send_state();
            }
        } else if (cmd == "ABTV" || cmd == "DTV") {
            if (btv) {
                if (me) {
                    log(id, "anulación btv", LOG_DEBUG);
                    btv = false;
                    aceptado = RespuestaMando::Aceptado;
                    send_state();
                } else {
                    me_pendiente = true;
                    aceptado = RespuestaMando::MandoEspecialNecesario;
                    send_state();
                }
            }
        }
        return aceptado;
    }

    void set_timer_auto_prenormalizacion_tren()
    {
        if (tiempo_auto_prenormalizacion_tren <= 0) return;
        timer_auto_prenormalizacion_tren = set_timer([this]() {
            bool changed = false;
            for (auto lado : {Lado::Impar, Lado::Par}) {
                if (num_ejes[lado] == 0) continue;
                auto &arr = num_trenes[lado];
                if (arr.empty()) continue;
                int diff = arr[arr.size() - 1] - num_ejes[lado];
                int val0 = arr[0];
                if (diff >= fraccion_ejes_prenormalizacion * val0) {
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
