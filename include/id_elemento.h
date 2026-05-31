#pragma once
#include <string>
struct id_elemento
{
    std::string dependencia;
    std::string id;
    std::string id_corto;
    id_elemento() = default;
    id_elemento(const std::string &id) : id(id)
    {
        auto pos = id.find_first_of(':');
        dependencia = id.substr(0, pos);
        if (pos != std::string::npos) id_corto = id.substr(pos+1);
    }
    id_elemento(const std::string &dep, const std::string &id_corto) : dependencia(dep), id_corto(id_corto), id(dep+":"+id_corto)
    {

    }
    auto operator<=>(const id_elemento &o) const
    {
        return id <=> o.id;
    }
    bool operator==(const id_elemento &o) const
    {
        return id == o.id;
    }
    bool operator!=(const id_elemento &o) const
    {
        return id != o.id;
    }
    static id_elemento from_default_dep(const std::string &id, const std::string &dep)
    {
        if (id.find_first_of(':') != std::string::npos) return id_elemento(id);
        return id_elemento(dep, id);
    }
};
