"""Exporta el grafo geométrico de vías al formato de enclavamiento."""

from typing import List
from items import Aguja, CvLineal, Lado, Señal, SeccionVia

def conectados(first, second):
    p1, a1 = first
    p2, a2 = second
    return p1.close_to(p2) and a1.close_to(a2.opposite())

def get_conexiones(sec, secciones):
    """Devuelve las parejas (sección vecina, puerto vecino) de cada puerto."""
    conex = []
    for out, pos in enumerate(sec.get_outs()):
        for sec2 in secciones:
            if sec2 is sec:
                continue
            for out2, pos2 in enumerate(sec2.get_outs()):
                if conectados(pos, pos2):
                    conex.append((sec2, out2))
                    break
            else:
                continue
            break
        else:
            conex.append((None,None))
    return conex

def lado_out(item, out):
    if item.lado is None:
        return None
    if out in item.get_oriented_connections(False):
        return item.lado
    else:
        return item.lado.opp_lado()

def lado_in(item, out):
    if item.lado is None:
        return None
    return lado_out(item, out).opp_lado()


def asignar_lado(secciones, conexiones):
    roots = [sec for sec in secciones if sec.lado is not None]

    def visit(sec):
        for lado in (Lado.Impar, Lado.Par):
            outs = sec.get_oriented_connections(lado != sec.lado)

            for out in outs:
                sec2, out2 = conexiones[sec][out]
                if sec2 is None or sec2.lado is not None:
                    continue
                if out2 in sec2.get_oriented_connections(False):
                    sec2.lado = lado.opp_lado()
                else:
                    sec2.lado = lado
                visit(sec2)

    for root in roots:
        visit(root)


def get_siguientes_secciones(sec, lado, secciones, conexiones):
    """Obtiene las secciones siguientes desde el lado solicitado.

    Los CV sin identificador se tratan como tramos auxiliares y se atraviesan.
    """
    outs = sec.get_oriented_connections(lado != sec.lado)
    sigs = []
    for i, out in enumerate(outs):
        sec2, out2 = conexiones[sec][out]
        if sec2 is None or sec2.lado is None:
            continue
        lado2 = lado_in(sec2, out2)
        while len(sigs) < i:
            sigs.append(None)
        if isinstance(sec2, CvLineal) and not sec2.id.id_corto:
            tmp = get_siguientes_secciones(sec2, lado2, secciones, conexiones)
            if len(tmp) > 0 and tmp[0] is not None:
                sigs.append((tmp[0][0], tmp[0][1] ^ (lado != lado2)))
        else:
            sigs.append((sec2.id, lado != lado2))
    return sigs


def serialize_connection(connection):
    if connection is None:
        return None
    if isinstance(connection, tuple):
        ident, invert = connection
    else:
        ident, invert = connection, False
    if ident is None:
        return None
    return {"Id": ident.id, "InvertirParidad": True} if invert else ident.id


def serialize_connections(connections):
    return [serialize_connection(connection) for connection in connections]

def get_seccion_pin_lado(out, sec, conexiones):
    for rev in (False, True):
        outs = sec.get_oriented_connections(rev)
        if out not in outs:
            continue
        idx = outs.index(out)
        if isinstance(sec, CvLineal) and not sec.id.id_corto:
            out_opp = sec.get_oriented_connections(not rev)[0]
            sec2, out2 = conexiones[sec][out_opp]
            if not sec2:
                return None, None, None
            return get_seccion_pin_lado(out2, sec2, conexiones)
        else:
            return sec, idx, sec.lado if rev else sec.lado.opp_lado()
    return None, None, None


def generar_config_ence(itemlist: List[SeccionVia]):
    secciones = [item for item in itemlist if isinstance(item, SeccionVia)]
    conexiones = {item: get_conexiones(item, secciones) for item in secciones}
    asignar_lado(secciones, conexiones)

    config = {}

    def get_config(dep, kind):
        return config.setdefault(dep, {}).setdefault(kind, {})

    for item in itemlist:
        if isinstance(item, Señal):
            for sec in secciones:
                for out, pos in enumerate(sec.get_outs()):
                    if conectados(item.get_outs()[0], pos):
                        section, pin, lado = get_seccion_pin_lado(out, sec, conexiones)
                        if section is None or lado is None:
                            continue
                        get_config(item.id.dep, "Señales")[item.id.id_corto] = {
                            "Tipo": item.tipo_señal.name,
                            "Sección": section.id.id,
                            "Pin": pin,
                            "Lado": lado.name,
                        }
                        break
                else:
                    continue
                break
        elif isinstance(item, CvLineal):
            if item.lado is None or not item.id.id_corto:
                continue
            sec = {"Conexiones": {}}
            for lado in (Lado.Impar, Lado.Par):
                sec["Conexiones"][lado.name] = serialize_connections(get_siguientes_secciones(item, lado, secciones, conexiones))
            get_config(item.id.dep, "Secciones")[item.id.id_corto] = sec
            get_config(item.id.dep, "CVs")[item.get_cv().id_corto] = {}
        elif isinstance(item, Aguja):
            if item.lado is None:
                continue
            sec = {"Tipo": "Aguja", "Lado": item.lado.name}
            if item.get_cv() is not None:
                sec["CV"] = item.get_cv().id
            tip = get_siguientes_secciones(item, item.lado.opp_lado(), secciones, conexiones)
            if tip and tip[0] is not None:
                sec["SecciónPunta"] = serialize_connection(tip[0])
            sec["SeccionesTalón"] = serialize_connections(
                get_siguientes_secciones(item, item.lado, secciones, conexiones)
            )
            get_config(item.id.dep, "Secciones")[item.id.id_corto] = sec
            if item.get_cv() is not None:
                get_config(item.id.dep, "CVs")[item.get_cv().id_corto] = {}
    return config
