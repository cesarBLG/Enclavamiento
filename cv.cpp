#include "cv.h"
cv::cv(const std::string &id, const json &j) : id(id)
{
    if (j.contains("Señales")) señales = j["Señales"];
}
cv_impl::cv_impl(const std::string &id, const json &j) : id(id), topic("cv/"+id+"/state"), cejes(j["ContadoresEjes"])
{
    normalizado = false;
    estado = prev_estado = EstadoCV::Prenormalizado;
}
void to_json(json &j, const estado_cv &estado)
{
    j["Estado"] = estado.estado;
    j["EstadoPrevio"] = estado.estado_previo;
    j["BIV"] = estado.biv;
    j["BTV"] = estado.btv;
    if (estado.evento) {
        j["Lado"] = estado.evento->first;
        j["Ocupación"] = estado.evento->second;
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
    if (j.contains("Lado")) {
        estado.evento = {j["Lado"], j["Ocupación"]};
    }
}
void from_json(const json &j, cv_impl::cejes_position &position)
{
    position.lado = j["Lado"];
    position.reverse = j.value("Reverse", false);
}