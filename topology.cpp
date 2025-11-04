#include "topology.h"
#include "ruta.h"
#include "items.h"
seccion_via::seccion_via(const std::string &id, const json &j) : id(id)
{
    active_outs[Lado::Impar][0] = 0;
    active_outs[Lado::Par][0] = 0;
    route_outs = {-1, -1};
    cv_seccion = cvs[j.value("CV", id)];
    if (j.contains("Conexiones")) {
        siguientes_secciones = j["Conexiones"];
    }
}
void seccion_via::asegurar(ruta *ruta, seccion_via *prev, seccion_via *next, Lado dir)
{
    int in = get_in(prev, dir);
    int out = get_out(next, dir);
    if (in < 0 || out < 0) return;
    ruta_asegurada = ruta;
    route_outs[dir] = out;
    route_outs[opp_lado(dir)] = in;
}
señal *seccion_via::señal_inicio(Lado lado)
{
    if (señales[lado] != "") return ::señales[señales[lado]];
    return nullptr;
}
seccion_via* seccion_via::siguiente_seccion(seccion_via *prev, Lado &dir)
{
    int in = get_in(prev, dir);
    if (in < 0) return nullptr;
    int out = active_outs[dir][in];
    if (out < 0) out = route_outs[dir];
    if (out < 0 || siguientes_secciones[dir].empty()) return nullptr;
    auto p = siguientes_secciones[dir][out];
    if (p.invertir_paridad) dir = opp_lado(dir);
    return secciones[p.id];
}

std::pair<seccion_via*,Lado> seccion_via::get_seccion_in(Lado dir, int pin)
{
    Lado lado = opp_lado(dir);
    auto &sigs = siguientes_secciones[lado];
    if (pin >= sigs.size()) return {nullptr, Lado::Impar};
    return {secciones[sigs[pin].id],sigs[pin].invertir_paridad ? lado : dir};
}

void seccion_via::prev_secciones(seccion_via *next, Lado dir_fwd, std::vector<std::pair<seccion_via*, Lado>> &secciones)
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
        auto p = siguientes_secciones[lado][in];
        secciones.push_back({::secciones[p.id], p.invertir_paridad ? lado : dir_fwd});
    }
}

int seccion_via::get_in(seccion_via* prev, Lado dir)
{
    Lado lado = opp_lado(dir);
    for (int i=0; i<siguientes_secciones[lado].size(); i++) {
        auto p = siguientes_secciones[lado][i];
        if ((prev != nullptr && p.id == prev->id) || (i == 0 && siguientes_secciones[lado].size() == 1)) {
            return i;
        }
    }
    return -1;
}
int seccion_via::get_out(seccion_via* next, Lado dir)
{
    for (int i=0; i<siguientes_secciones[dir].size(); i++) {
        auto p = siguientes_secciones[dir][i];
        if ((next != nullptr && p.id == next->id) || (i == 0 && siguientes_secciones[dir].size() == 1)) {
            return i;
        }
    }
    return -1;
}
void from_json(const json &j, seccion_via::conexion &conex)
{
    conex.id = j["Id"];
    conex.invertir_paridad = j.value("Reverse", false);
}
