from typing import List, Dict
from items import *
import json
def get_seccion_pin_lado(seccion_by_coords: Dict[Tuple[int,int], Item], pos: Tuple[int,int], dir: Direccion) -> Tuple[str, int, Lado]:
    if not pos in seccion_by_coords:
        return None, None, None
    sec = seccion_by_coords[pos]
    if sec.lado is None:
        return None, None, None
    odir = Direccion((dir.value + 4) % 8)
    dirs = sec.get_directions()
    id = sec.id
    if isinstance(sec, Aguja):
        if dirs[0] == odir:
            return id, 0, sec.lado.opp_lado()
        elif dirs[1] == odir:
            return id, 1, sec.lado.opp_lado()
        else:
            return id, 0, sec.lado
    elif isinstance(sec, CvLineal):
        if id.id_corto != "":
            return id, 0, sec.lado if dirs[1] == odir else sec.lado.opp_lado()
        newdir = dirs[1] if dirs[0] == odir else dirs[0]
        newpos = get_next_position(pos, newdir)
        sec2 = seccion_by_coords.get(newpos)
        if sec2 is not None:
            return get_seccion_pin_lado(seccion_by_coords, newpos, newdir)
    return None, None, None

def get_siguientes_secciones(seccion_by_coords: Dict[Tuple[int,int], Item], pos: Tuple[int,int], lado: Lado) -> List[Tuple[IdElemento, bool]]:
    if not pos in seccion_by_coords:
        return []
    sec = seccion_by_coords[pos]
    if sec.lado is None:
        return []
    dirs = sec.get_directions()
    if isinstance(sec, Aguja):
        lst = []
        for i in range(2 if lado == sec.lado else 1):
            newdir = dirs[i] if lado == sec.lado else dirs[2]
            newpos = get_next_position(pos, newdir)
            sec2 = seccion_by_coords.get(newpos)
            if sec2 is None or sec2.lado is None:
                continue
            newlado = lado_siguiente(sec2, newdir)
            invert = lado != newlado
            while len(lst) < i:
                lst.append(None)
            if isinstance(sec2, CvLineal) and sec2.id.id_corto == "":
                lst.extend(
                    (ident, invert ^ child_invert)
                    for ident, child_invert in get_siguientes_secciones(
                        seccion_by_coords, newpos, newlado
                    )
                )
            else:
                lst.append((sec2.id, invert))
        return lst
    elif isinstance(sec, CvLineal):
        newdir = dirs[0] if lado == sec.lado else dirs[1]
        newpos = get_next_position(pos, newdir)
        sec2 = seccion_by_coords.get(newpos)
        if sec2 is None:
            return []
        if sec2.lado is None:
            return []
        newlado = lado_siguiente(sec2, newdir)
        invert = lado != newlado
        if isinstance(sec2, CvLineal) and sec2.id.id_corto == "":
            return [
                (ident, invert ^ child_invert)
                for ident, child_invert in get_siguientes_secciones(
                    seccion_by_coords, newpos, newlado
                )
            ]
        return [(sec2.id, invert)]
    return None

def lado_siguiente(section: Item, dir_entrada: Direccion) -> Lado:
    """Lado desde el que continuar tras entrar por el puerto indicado."""
    odir = Direccion((dir_entrada.value + 4) % 8)
    directions = section.get_directions()
    if isinstance(section, Aguja):
        if odir in directions[:2]:  # entrada por cualquiera de los talones
            return section.lado.opp_lado()
        if odir == directions[2]:  # entrada por la punta
            return section.lado
    elif isinstance(section, CvLineal):
        if odir == directions[1]:
            return section.lado
        if odir == directions[0]:
            return section.lado.opp_lado()
    return section.lado


def serialize_connection(connection):
    """Convierte (IdElemento, invertir) en el formato JSON de conexiones."""
    if isinstance(connection, tuple):
        ident, invert = connection
    else:
        ident, invert = connection, False
    if ident is None:
        return None
    if invert:
        return {"Id": ident.id, "InvertirParidad": True}
    return ident.id


def serialize_connections(connections):
    return [serialize_connection(connection) for connection in connections]

def asignar_lado(seccion_by_coords: Dict[Tuple[int,int], Item], pos: Tuple[int,int], lado: Lado, dir: Direccion = None):
    if not pos in seccion_by_coords:
        return
    sec = seccion_by_coords[pos]
    if dir is not None:
        if sec.lado is not None:
            return
        odir = Direccion((dir.value + 4) % 8)
        if (isinstance(sec, Aguja) and odir != sec.get_directions()[2]) or (isinstance(sec, CvLineal) and odir != sec.get_directions()[1]):
            lado = lado.opp_lado()
    sec.lado = lado
    dirs = sec.get_directions()
    for l in [Lado.Impar, Lado.Par]:
        if isinstance(sec, Aguja):
            for i in range(2 if l == sec.lado else 1):
                newdir = dirs[i] if l == sec.lado else dirs[2]
                newpos = get_next_position(pos, newdir)
                asignar_lado(seccion_by_coords, newpos, l, newdir)
        elif isinstance(sec, CvLineal):
            newdir = dirs[0] if l == sec.lado else dirs[1]
            newpos = get_next_position(pos, newdir)
            asignar_lado(seccion_by_coords, newpos, l, newdir)

def generar_config_ence(itemlist: List[Item]):
    seccion_by_coords: Dict[Tuple[int,int], Item] = dict()
    secciones_paridad: List[Item] = []
    for item in itemlist:
        if isinstance(item, CvLineal) or isinstance(item, Aguja):
            seccion_by_coords[item.position] = item
            if item.lado is not None:
                secciones_paridad.append(item)

    for item in secciones_paridad:
        asignar_lado(seccion_by_coords, item.position, item.lado)

    config = dict()
    def get_config(dep, tipo):
        if dep not in config:
            config[dep] = dict()
        if tipo not in config[dep]:
            config[dep][tipo] = dict()
        return config[dep][tipo]
    for item in itemlist:
        if isinstance(item, Señal):
            sec, pin, lado = get_seccion_pin_lado(seccion_by_coords, item.position, item.get_directions()[0])
            if sec is None:
                continue
            sig = dict()
            sig["Tipo"] = item.tipo_señal.name
            sig["Sección"] = sec.id
            sig["Pin"] = pin
            sig["Lado"] = lado.name
            get_config(item.id.dep, "Señales")[item.id.id_corto] = sig
        elif isinstance(item, CvLineal):
            if not item.lado:
                continue
            if item.id.id_corto == "":
                continue
            secciones = get_config(item.id.dep, "Secciones")
            sec = secciones.setdefault(
                item.id.id_corto,
                {"Conexiones": {"Impar": [], "Par": []}},
            )
            for lado in [Lado.Impar, Lado.Par]:
                connections = serialize_connections(
                    get_siguientes_secciones(seccion_by_coords, item.position, lado)
                )
                current = sec["Conexiones"][lado.name]
                current.extend(connection for connection in connections if connection not in current)
            if item.get_cv() is not None:
                get_config(item.id.dep, "CVs")[item.get_cv().id_corto] = dict()
        elif isinstance(item, Aguja):
            if not item.lado:
                continue
            aguja = dict()
            aguja["Tipo"] = "Aguja"
            aguja["Lado"] = item.lado.name
            if item.get_cv() is not None:
                aguja["CV"] = item.get_cv().id
            punta = get_siguientes_secciones(
                seccion_by_coords, item.position, item.lado.opp_lado()
            )
            if punta:
                aguja["SecciónPunta"] = serialize_connection(punta[0])
            aguja["SeccionesTalón"] = serialize_connections(
                get_siguientes_secciones(seccion_by_coords, item.position, item.lado)
            )
            get_config(item.id.dep, "Secciones")[item.id.id_corto] = aguja
            if item.get_cv() is not None:
                get_config(item.id.dep, "CVs")[item.get_cv().id_corto] = dict()
    return config
