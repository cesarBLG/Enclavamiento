#pragma once
#include <set>
#include <map>
#include <vector>
#include "lado.h"
#include "enums.h"
#include "mqtt.h"
#include "time.h"
#include "log.h"
#include <optional>
struct evento_cv
{
    Lado lado;
    bool ocupacion;
    int pin;
};
struct estado_cv
{
    EstadoCV estado;
    EstadoCV estado_previo;
    bool btv;
    bool biv;
    std::optional<evento_cv> evento;
};
void to_json(json &j, const estado_cv &estado);
void from_json(const json &j, estado_cv &estado);
void from_json(const json &j, evento_cv &ev);
void to_json(json &j, const evento_cv &ev);
class ruta;
class señal;
class cv
{
public:
    struct conexion_cv
    {
        std::string id;
        bool invertir_paridad;
    };
    const std::string id;
protected:
    lados<std::map<int,std::string>> señales;

    lados<std::vector<conexion_cv>> siguientes_cvs;
    lados<std::map<int,int>> active_outs;
    lados<int> route_outs;

    EstadoCV estado = EstadoCV::Prenormalizado;
    bool btv = false;
    bool biv = false;
public:
    ruta *ruta_asegurada;
    cv(const std::string &id, const json &j);

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

    void message_cv(estado_cv ecv)
    {
        estado = ecv.estado;
        biv = ecv.biv;
        btv = ecv.btv;
    }

    cv* siguiente_cv(cv *prev, Lado &dir);
    std::pair<cv*,Lado> get_cv_in(Lado dir, int pin);
    void prev_cvs(cv *next, Lado dir_fwd, std::vector<std::pair<cv*, Lado>> &cvs);
    señal *señal_inicio(cv *prev, Lado lado);
protected:
    int get_in(cv* prev, Lado dir);
    int get_out(cv* next, Lado dir);
};
void from_json(const json &j, cv::conexion_cv &conex);
class cv_impl
{
public:
    struct cejes_position
    {
        Lado lado;
        bool reverse;
        int pin;
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

    EstadoCV estado;
    EstadoCV prev_estado;
    bool normalizado;

    bool btv;
    bool biv;

    std::optional<evento_cv> evento_pendiente;

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
        send_state();
    }

    void send_state()
    {
        estado_cv ecv;
        ecv.estado_previo = prev_estado;
        ecv.estado = estado;
        ecv.evento = evento_pendiente;
        ecv.biv = biv;
        ecv.btv = btv;
    
        json msg(ecv);
        send_message(topic, msg.dump(), ecv.evento ? 2 : 1);

        evento_pendiente = {};
        prev_estado = estado;
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
                }
                ++num_ejes[lado];
                if (now - ultimo_eje[lado] < tiempo_auto_prenormalizacion_tren) arr.pop_back();
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
            evento_pendiente = {lado, msg == "Nominal", it->second.pin};
            update();
        }
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

    bool mando(const std::string &cmd)
    {
        if (cmd == "BV" || cmd == "BIV") biv = true;
        else if (cmd == "ABV" || cmd == "DIV") biv = false;
        else if (cmd == "BTV") btv = true;
        else if (cmd == "ABTV" || cmd == "DTV") btv = false;
        else if (cmd == "LC") prenormalizar();
        else return false;
        return true;
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
};
void from_json(const json &j, cv_impl::cejes_position &position);