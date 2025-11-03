#include "cv.h"
#include "items.h"
cv::cv(const std::string &id, const json &j) : id(id)
{
    active_outs[Lado::Impar][0] = 0;
    active_outs[Lado::Par][0] = 0;
    route_outs = {-1, -1};
    if (j.contains("Conexiones")) {
        siguientes_cvs = j["Conexiones"];
    }
}
señal *cv::señal_inicio(cv *prev, Lado lado)
{
    int in = get_in(prev, lado);
    if (in < 0) return nullptr;
    auto it = señales[lado].find(in);
    if (it != señales[lado].end()) return ::señales[it->second];
    return nullptr;
}
cv* cv::siguiente_cv(cv *prev, Lado &dir)
{
    int in = get_in(prev, dir);
    if (in < 0) return nullptr;
    int out = active_outs[dir][in];
    if (out < 0) out = route_outs[dir];
    if (out < 0 || siguientes_cvs[dir].empty()) return nullptr;
    auto p = siguientes_cvs[dir][out];
    if (p.invertir_paridad) dir = opp_lado(dir);
    return cvs[p.id];
}

std::pair<cv*,Lado> cv::get_cv_in(Lado dir, int pin)
{
    Lado lado = opp_lado(dir);
    auto &sigs = siguientes_cvs[lado];
    if (pin >= sigs.size()) return {nullptr, Lado::Impar};
    return {cvs[sigs[pin].id],sigs[pin].invertir_paridad ? lado : dir};
}

void cv::prev_cvs(cv *next, Lado dir_fwd, std::vector<std::pair<cv*, Lado>> &cvs)
{
    Lado lado = opp_lado(dir_fwd);
    int out = get_out(next, dir_fwd);
    if (out < 0) return;
    std::set<int> ins;
    for (auto &[in, out2] : active_outs[dir_fwd]) {
        if (out2 == out) {
            ins.insert(in);
        }
    }
    if (route_outs[dir_fwd] == out) {
        ins.insert(route_outs[lado]);
    }
    for (int in : ins) {
        auto p = siguientes_cvs[lado][in];
        cvs.push_back({::cvs[p.id], p.invertir_paridad ? lado : dir_fwd});
    }
}

int cv::get_in(cv* prev, Lado dir)
{
    Lado lado = opp_lado(dir);
    for (int i=0; i<siguientes_cvs[lado].size(); i++) {
        auto p = siguientes_cvs[lado][i];
        if ((prev != nullptr && p.id == prev->id) || (i == 0 && siguientes_cvs[lado].size() == 1)) {
            return i;
        }
    }
    return -1;
}
int cv::get_out(cv* next, Lado dir)
{
    for (int i=0; i<siguientes_cvs[dir].size(); i++) {
        auto p = siguientes_cvs[dir][i];
        if ((next != nullptr && p.id == next->id) || (i == 0 && siguientes_cvs[dir].size() == 1)) {
            return i;
        }
    }
    return -1;
}
cv_impl::cv_impl(const std::string &id, const json &j) : id(id), topic("cv/"+id+"/state"), cejes(j["ContadoresEjes"])
{
    normalizado = false;
    estado = prev_estado = EstadoCV::Prenormalizado;
}
void from_json(const json &j, cv::conexion_cv &conex)
{
    conex.id = j["CV"];
    conex.invertir_paridad = j.value("Reverse", false);
}
void to_json(json &j, const estado_cv &estado)
{
    j["Estado"] = estado.estado;
    j["EstadoPrevio"] = estado.estado_previo;
    j["BIV"] = estado.biv;
    j["BTV"] = estado.btv;
    if (estado.evento) {
        j["Evento"] = *estado.evento;
    }
}
void from_json(const json &j, estado_cv &estado)
{
    if (j == "desconexion") {
        return;
    }
    estado.estado = j["Estado"];
    estado.estado_previo = j["EstadoPrevio"];
    estado.biv = j["BIV"];
    estado.btv = j["BTV"];
    if (j.contains("Evento")) {
        estado.evento = j["Evento"];
    }
}
void from_json(const json &j, cv_impl::cejes_position &position)
{
    position.lado = j["Lado"];
    position.reverse = j.value("Reverse", false);
    position.pin = j.value("Pin", 0);
}
void from_json(const json &j, evento_cv &ev)
{
    ev.lado = j["Lado"];
    ev.ocupacion = j["Ocupación"];
    ev.pin = j["Pin"];
}
void to_json(json &j, const evento_cv &ev)
{
    j["Lado"] = ev.lado;
    j["Ocupación"] = ev.ocupacion;
    j["Pin"] = ev.pin;
}