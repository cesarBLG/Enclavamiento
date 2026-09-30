"""Modelo geométrico de los elementos de vía.

Las posiciones y los puntos se expresan en las mismas unidades continuas. Los
puntos guardados en cada sección son locales respecto a su posición.
"""

from enum import Enum
import json
import math
from pathlib import Path
from typing import Dict, List, Tuple


class TipoElemento(Enum):
    CV_LINEAL = 1
    AGUJA = 2
    CRUZAMIENTO = 3
    TOPERA = 4
    SEÑAL = 5


class Lado(Enum):
    Par = 0
    Impar = 1

    def opp_lado(self):
        return Lado.Par if self == Lado.Impar else Lado.Impar


class Direccion(Enum):
    E = 0
    SE = 1
    S = 2
    SW = 3
    W = 4
    NW = 5
    N = 6
    NE = 7


def get_next_position(pos, direction):
    """Ayuda heredada para las herramientas del editor basadas en celdas."""
    offsets = ((1, 0), (1, 1), (0, 1), (-1, 1),
               (-1, 0), (-1, -1), (0, -1), (1, -1))
    dx, dy = offsets[direction.value if isinstance(direction, Direccion) else int(direction)]
    return (pos[0] + dx, pos[1] + dy)


class Point:
    __slots__ = ("x", "y")

    def __init__(self, x=0.0, y=0.0):
        self.x, self.y = float(x), float(y)

    def __add__(self, other):
        return Point(self.x + other.x, self.y + other.y)

    def __sub__(self, other):
        return Point(self.x - other.x, self.y - other.y)

    def __mul__(self, scalar):
        return Point(self.x * scalar, self.y * scalar)

    __rmul__ = __mul__

    def __truediv__(self, scalar):
        return Point(self.x / scalar, self.y / scalar)

    def __iter__(self):
        yield self.x
        yield self.y

    def __getitem__(self, index):
        return (self.x, self.y)[index]

    def __hash__(self):
        return hash((self.x, self.y))

    def __eq__(self, other):
        return isinstance(other, Point) and self.close_to(other)

    def close_to(self, other: "Point", tol=1e-6):
        return abs(self.x - other.x) <= tol and abs(self.y - other.y) <= tol

    def serialize(self):
        return [self.x, self.y]

    def get_rotated(self, angle):
        if isinstance(angle, Angle):
            angle = angle.angle
        c, s = math.cos(angle), math.sin(angle)
        return Point(self.x * c - self.y * s, self.x * s + self.y * c)

    @classmethod
    def deserialize(cls, data):
        if not isinstance(data, (list, tuple)) or len(data) != 2:
            raise ValueError("Un punto debe contener dos coordenadas.")
        if any(not isinstance(value, (int, float)) or not math.isfinite(value) for value in data):
            raise ValueError("Las coordenadas deben ser números finitos.")
        return cls(*data)


class Angle:
    __slots__ = ("angle",)

    def __init__(self, angle=0.0):
        self.angle = float(angle) % (2 * math.pi)

    def __add__(self, other):
        return Angle(self.angle + (other.angle if isinstance(other, Angle) else other))

    def __radd__(self, other):
        return self + other

    def __sub__(self, other):
        return Angle(self.angle - (other.angle if isinstance(other, Angle) else other))

    def __mul__(self, scalar):
        return Angle(self.angle * scalar)

    __rmul__ = __mul__

    def __float__(self):
        return self.angle

    def opposite(self) -> Angle:
        return Angle(self.angle+math.pi)

    def close_to(self, other: "Angle", tol=1e-6):
        diff = abs(self.angle - other.angle) % (2 * math.pi)
        return min(diff, 2 * math.pi - diff) <= tol

    def serialize(self):
        return self.angle


def get_angle(point1: Point, point2: Point) -> Angle:
    return Angle(math.atan2(point2.y - point1.y, point2.x - point1.x))


class TipoSeñal(Enum):
    Entrada = 0
    Salida = 1
    Avanzada = 2
    Intermedia = 3
    Maniobra = 4
    Retroceso = 5


class IdElemento:
    def __init__(self, id: str, dep: str = None):
        idx = id.find(":")
        if idx != -1:
            self.id, self.dep, self.id_corto = id, id[:idx], id[idx + 1:]
        elif dep is not None:
            self.id, self.dep, self.id_corto = dep + ":" + id, dep, id
        else:
            raise ValueError(f"Dependencia desconocida para {id}")

    def __eq__(self, other):
        return isinstance(other, IdElemento) and self.id == other.id

    def __hash__(self):
        return hash(self.id)

class Item:
    def __init__(self, id: IdElemento, tipo: TipoElemento, position: Point,
                 rotation: Angle = None):
        self.id = id
        self.tipo = tipo
        self.posicion = position if isinstance(position, Point) else Point(*position)
        self.rotacion = rotation if isinstance(rotation, Angle) else Angle(rotation or 0)

    @property
    def position(self):
        return self.posicion

    @position.setter
    def position(self, value):
        self.posicion = value if isinstance(value, Point) else Point(*value)

    @property
    def rotation(self):
        return self.rotacion

    @rotation.setter
    def rotation(self, value):
        self.rotacion = value if isinstance(value, Angle) else Angle(value)

    def rotate(self, amount):
        # amount se expresa en radianes en el nuevo modelo.
        delta = amount if isinstance(amount, Angle) else Angle(
            amount * math.pi / 4 if isinstance(amount, int) else amount
        )
        self.rotacion = self.rotacion + delta

    def serialize(self):
        return {"id": self.id.id, "posicion": self.position.serialize(),
                "rotacion": self.rotation.serialize()}

    @staticmethod
    def deserialize(data, dependency):
        if not isinstance(data, dict):
            raise ValueError("Cada elemento debe ser un objeto JSON.")
        constructors = {"CvLineal": CvLineal.deserialize,
                        "Aguja": Aguja.deserialize,
                        "Señal": Señal.deserialize}
        kind = data.get("tipo")
        if kind not in constructors:
            raise ValueError(f"Tipo de elemento desconocido: {kind!r}.")
        return constructors[kind](data, dependency)

    @staticmethod
    def _base(data, dependency):
        if not isinstance(data.get("id"), str):
            raise ValueError("Falta el identificador del elemento.")
        ident = IdElemento(data["id"], dependency)
        position = Point.deserialize(data.get("posicion"))
        rotation = data.get("rotacion", 0.0)
        if not isinstance(rotation, (int, float)) or not math.isfinite(rotation):
            raise ValueError(f"Rotación inválida para {ident.id}.")
        return ident, position, Angle(rotation)


class SeccionVia(Item):
    def matches(self, pos: Point, direction: Angle) -> bool:
        return any(p.close_to(pos) and a.close_to(direction) for p, a in self.get_outs())

    def get_directions(self):
        # Compatibilidad temporal con utilidades antiguas; la geometría nueva y
        # el exportador trabajan con get_outs().
        return tuple(Direccion(round(angle.angle / (math.pi / 4)) % 8)
                     for _, angle in self.get_outs())


class CvLineal(SeccionVia):
    def __init__(self, id: IdElemento, position: Point,
                 points: List[Point] = None, rotation: Angle = None):
        super().__init__(id, TipoElemento.CV_LINEAL, position, rotation)
        if isinstance(points, int):  # firma anterior del editor: ángulo en octavos
            points = None
        self.points = [Point(0, 0)] + [p if isinstance(p, Point) else Point(*p)
                                      for p in (points or [Point(30, 0)])]
        self.lado = None

    @staticmethod
    def get_connections():
        return [(0, 1)]

    def get_oriented_connections(self, reverse):
        if reverse:
            return [1]
        else:
            return [0]

    def get_outs(self):
        points = [point.get_rotated(self.rotation) for point in self.points]
        return [
            (self.position + points[0], get_angle(points[1], points[0])),
            (self.position + points[-1], get_angle(points[-2], points[-1])),
        ]

    def get_cv(self):
        return self.id

    def serialize(self):
        return {**super().serialize(), "tipo": "CvLineal",
                "points": [p.serialize() for p in self.points[1:]],
                "lado": self.lado.name if self.lado else None}

    @classmethod
    def deserialize(cls, data, dependency):
        ident, position, rotation = Item._base(data, dependency)
        item = cls(ident, position, [Point.deserialize(p) for p in data.get("points", [])], rotation)
        side = data.get("lado")
        if side is not None:
            try:
                item.lado = Lado[side]
            except (KeyError, TypeError):
                raise ValueError(f"Lado inválido para {ident.id}.") from None
        return item


class Aguja(SeccionVia):
    def __init__(self, id: IdElemento, position: Point, rotation: Angle = None,
                 punta: List[Point] = None, normal: List[Point] = None,
                 invertida: List[Point] = None, angulo: float = None):
        super().__init__(id, TipoElemento.AGUJA, position, rotation)
        if angulo is None or not isinstance(angulo, (int, float)) or not math.isfinite(angulo):
            raise ValueError(f"Falta el ángulo de la aguja {id.id}.")
        self.angulo = float(angulo)
        self.points_punta = self._path(punta or [Point(-15, 0)])
        self.points_normal = self._path(normal or [Point(15, 0)])
        self.points_invertida = self._path(invertida or [Point(15, 15)])
        self.lado = None
        self.cv = None

    @staticmethod
    def _path(points):
        values = [p if isinstance(p, Point) else Point(*p) for p in points]
        if not values or not values[0].close_to(Point(0, 0)):
            values.insert(0, Point(0, 0))
        return values

    @staticmethod
    def get_connections():
        return [(0, 2), (1, 2)]

    def get_oriented_connections(self, reverse):
        if reverse:
            return [2]
        else:
            return [0,1]

    def get_outs(self):
        outs = []
        for path in (self.points_normal, self.points_invertida, self.points_punta):
            points = [point.get_rotated(self.rotation) for point in path]
            outs.append((self.position + points[-1],
                         get_angle(points[-2], points[-1])))
        return outs

    def get_cv(self):
        return self.cv

    def serialize(self):
        return {**super().serialize(), "tipo": "Aguja",
                "points_normal": [p.serialize() for p in self.points_normal[1:]],
                "points_invertida": [p.serialize() for p in self.points_invertida[1:]],
                "points_punta": [p.serialize() for p in self.points_punta[1:]],
                    "angulo": self.angulo,
                "cv": self.cv.id if self.cv else None,
                "lado": self.lado.name if self.lado else None}

    @classmethod
    def deserialize(cls, data, dependency):
        ident, position, rotation = Item._base(data, dependency)
        item = cls(ident, position, rotation,
                   [Point.deserialize(p) for p in data.get("points_punta", [])],
                   [Point.deserialize(p) for p in data.get("points_normal", [])],
                   [Point.deserialize(p) for p in data.get("points_invertida", [])],
                   data["angulo"] if "angulo" in data else None)
        cv = data.get("cv")
        if cv:
            item.cv = IdElemento(cv, dependency)
        side = data.get("lado")
        if side is not None:
            try:
                item.lado = Lado[side]
            except (KeyError, TypeError):
                raise ValueError(f"Lado inválido para {ident.id}.") from None
        return item


class Señal(Item):
    def __init__(self, id: IdElemento, position: Point, tipo: TipoSeñal,
                 offset_pie: float = 6.0, rotation: Angle = None):
        super().__init__(id, TipoElemento.SEÑAL, position, rotation)
        self.tipo_señal = tipo
        self.offset_pie = float(offset_pie)

    def get_outs(self):
        return [(self.position, self.rotation)]

    def get_directions(self):
        return (Direccion(round(self.rotation.angle / (math.pi / 4)) % 8),)

    def serialize(self):
        return {**super().serialize(), "tipo": "Señal",
                "tipo_señal": self.tipo_señal.name,
                "offset_pie": self.offset_pie}

    @classmethod
    def deserialize(cls, data, dependency):
        ident, position, rotation = Item._base(data, dependency)
        try:
            signal_type = TipoSeñal[data["tipo_señal"]]
        except (KeyError, TypeError):
            raise ValueError(f"Tipo de señal inválido para {ident.id}.") from None
        offset = data.get("offset_pie", 6.0)
        if not isinstance(offset, (int, float)) or not math.isfinite(offset):
            raise ValueError(f"Desplazamiento del pie inválido para {ident.id}.")
        return cls(ident, position, signal_type, offset, rotation)


def _validate_layout(dependencies):
    # Los elementos pueden compartir coordenadas o cruzarse; las relaciones de
    # conexión se determinan por puertos geométricos y no por ocupación de celdas.
    for dependency, items in dependencies.items():
        for item in items:
            if item.id.dep != dependency:
                raise ValueError(f"El elemento {item.id.id} está en la dependencia incorrecta.")
            if isinstance(item, Señal):
                associated = [track for track in dependencies[dependency]
                              if isinstance(track, SeccionVia)
                              and any(p.close_to(item.position)
                                      and a.close_to(item.rotation + Angle(math.pi))
                                      for p, a in track.get_outs())]
                if not associated:
                    raise ValueError(f"La señal {item.id.id} no coincide con una salida de vía.")


def serialize_layout(dependencies: Dict[str, List[Item]]) -> dict:
    _validate_layout(dependencies)
    return {"version": 3, "dependencias": {
        dep: {"elementos": [item.serialize() for item in items]}
        for dep, items in dependencies.items()
    }}


def deserialize_layout(data: dict) -> Dict[str, List[Item]]:
    if not isinstance(data, dict) or data.get("version") != 3:
        raise ValueError("El archivo no es un layout geométrico compatible (versión 3).")
    dependencies = data.get("dependencias")
    if not isinstance(dependencies, dict):
        raise ValueError("El mapa de dependencias no es válido.")
    result = {}
    for dependency, value in dependencies.items():
        if not isinstance(dependency, str) or not dependency.strip():
            raise ValueError("Hay un nombre de dependencia no válido.")
        records = value.get("elementos") if isinstance(value, dict) else None
        if not isinstance(records, list):
            raise ValueError(f"La lista de elementos de {dependency} no es válida.")
        result[dependency] = [Item.deserialize(record, dependency) for record in records]
    _validate_layout(result)
    return result
