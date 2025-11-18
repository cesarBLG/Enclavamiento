#pragma once
#include "enums.h"
template<class T>
struct lados
{
    T impar;
    T par;
    T& operator[](Lado l)
    {
        return l == Lado::Par ? par : impar;
    }
    const T&operator[](Lado l) const
    {
        return l == Lado::Par ? par : impar;
    }
    bool operator==(const lados<T> &o) const
    {
        return par == o.par && impar == o.impar;
    }
};
#ifndef WITHOUT_JSON
#include "json.h"
template<class T>
void to_json(json &j, const lados<T> &l)
{
    j["Impar"] = l[Lado::Impar];
    j["Par"] = l[Lado::Par];
}
template<class T>
void from_json(const json &j, lados<T> &l)
{
    l.impar = j["Impar"];
    l.par = j["Par"];
}
#endif
