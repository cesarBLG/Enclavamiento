from typing import Dict, List, Tuple
from enum import Enum
import json
from pathlib import Path

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

def get_next_position(pos: Tuple[int,int], dir: Direccion):
    if dir == Direccion.E:
        return (pos[0]+1, pos[1])
    elif dir == Direccion.SE:
        return (pos[0]+1, pos[1]-1)
    elif dir == Direccion.S:
        return (pos[0], pos[1]-1)
    elif dir == Direccion.SW:
        return (pos[0]-1, pos[1]-1)
    elif dir == Direccion.W:
        return (pos[0]-1, pos[1])
    elif dir == Direccion.NW:
        return (pos[0]-1, pos[1]+1)
    elif dir == Direccion.N:
        return (pos[0], pos[1]+1)
    elif dir == Direccion.NE:
        return (pos[0]+1, pos[1]+1)

class TipoSeñal(Enum):
    Entrada = 0
    Salida = 1
    Avanzada = 2
    Intermedia = 3
    Maniobra = 4
    Retroceso = 5

class IdElemento:
    def __init__(self, id: str, dep: str = None):
        idx = id.find(':')
        if idx != -1:
            self.id = id
            self.dep = id[:idx]
            self.id_corto = id[idx+1:]
        elif dep is not None:
            self.id = dep+':'+id
            self.dep = dep
            self.id_corto = id
        else:
            raise ValueError(f"Dependencia desconocida para {id}")
    def __eq__(self, other):
        if isinstance(other, IdElemento):
            return self.id == other.id
        return False

class Item:
    def __init__(self, id: IdElemento, tipo: TipoElemento, position: Tuple[int,int]):
        self.id = id
        self.tipo = tipo
        self.position = position
        self.rotation = 0
    def rotate(self, rot: int):
        self.rotation = (self.rotation + rot) % 8

    def serialize(self) -> dict:
        return {
            "id": self.id.id,
            "posicion": list(self.position),
            "rotacion": self.rotation,
        }

    @staticmethod
    def _deserialize_base(data: dict, dependency: str):
        if not isinstance(data, dict):
            raise ValueError("Cada elemento debe ser un objeto JSON.")
        position = tuple(data["posicion"])
        if len(position) != 2 or any(not isinstance(value, int) for value in position):
            raise ValueError("La posición debe contener dos coordenadas enteras.")
        ident = IdElemento(data["id"], dependency)
        rotation = data.get("rotacion", 0)
        if not isinstance(rotation, int) or not 0 <= rotation < 8:
            raise ValueError(f"Rotación inválida para {ident.id}.")
        return ident, position, rotation

    @staticmethod
    def deserialize(data: dict, dependency: str) -> "Item":
        if not isinstance(data, dict):
            raise ValueError("Cada elemento debe ser un objeto JSON.")
        deserializers = {
            "CvLineal": CvLineal.deserialize,
            "Aguja": Aguja.deserialize,
            "Señal": Señal.deserialize,
        }
        tipo = data.get("tipo")
        if tipo not in deserializers:
            raise ValueError(f"Tipo de elemento desconocido: {tipo!r}.")
        return deserializers[tipo](data, dependency)

class CvLineal(Item):
    def __init__(self, id: IdElemento, position: Tuple[int,int], angle: int = 4):
        super().__init__(id, TipoElemento.CV_LINEAL, position)
        self.angle = angle
        self.lado = None
    def get_directions(self) -> Tuple[Direccion, Direccion]:
        return (Direccion(self.rotation), Direccion((self.rotation+self.angle)%8))
    def get_cv(self) -> IdElemento:
        return self.id
    def serialize(self) -> dict:
        return {
            **super().serialize(),
            "tipo": "CvLineal",
            "angulo": self.angle,
            "lado": self.lado.name if self.lado else None,
        }
    @classmethod
    def deserialize(cls, data: dict, dependency: str) -> "CvLineal":
        id, position, rotation = Item._deserialize_base(data, dependency)
        angle = data["angulo"]
        if angle not in (2, 3, 4):
            raise ValueError(f"Ángulo inválido para {id.id}.")
        item = cls(id, position, angle)
        side = data.get("lado")
        if side is not None:
            try:
                item.lado = Lado[side]
            except (KeyError, TypeError):
                raise ValueError(f"Lado inválido para {id.id}.") from None
        item.rotation = rotation
        return item

class Aguja(Item):
    def __init__(self, id: IdElemento, position: Tuple[int,int], angle: int = 1):
        super().__init__(id, TipoElemento.AGUJA, position)
        self.angle = angle
        self.lado: Lado = None
        self.cv = None
    def get_directions(self) -> Tuple[Direccion, Direccion, Direccion]:
        return (Direccion(self.rotation), Direccion((self.rotation+self.angle)%8), Direccion((self.rotation+4)%8))
    def get_cv(self) -> IdElemento:
        return self.cv
    def serialize(self) -> dict:
        return {
            **super().serialize(),
            "tipo": "Aguja",
            "angulo": self.angle,
            "cv": self.cv.id if self.cv else None,
            "lado": self.lado.name if self.lado else None,
        }
    @classmethod
    def deserialize(cls, data: dict, dependency: str) -> "Aguja":
        id, position, rotation = Item._deserialize_base(data, dependency)
        angle = data["angulo"]
        if angle not in (1, -1):
            raise ValueError(f"Ángulo inválido para {id.id}.")
        cv = data.get("cv")
        if not isinstance(cv, str) or not cv:
            raise ValueError(f"Falta el circuito de vía de {id.id}.")
        item = cls(id, position, angle)
        item.cv = IdElemento(cv, dependency)
        side = data.get("lado")
        if side is not None:
            try:
                item.lado = Lado[side]
            except (KeyError, TypeError):
                raise ValueError(f"Lado inválido para {id.id}.") from None
        item.rotation = rotation
        return item

class Señal(Item):
    def __init__(self, id: IdElemento, position: Tuple[int,int], tipo: TipoSeñal):
        super().__init__(id, TipoElemento.SEÑAL, position)
        self.tipo_señal = tipo
    def get_directions(self) -> Tuple[Direccion]:
        return (Direccion(self.rotation),)
    def serialize(self) -> dict:
        return {
            **super().serialize(),
            "tipo": "Señal",
            "tipo_señal": self.tipo_señal.name,
        }
    @classmethod
    def deserialize(cls, data: dict, dependency: str) -> "Señal":
        id, position, rotation = Item._deserialize_base(data, dependency)
        try:
            tipo = TipoSeñal[data["tipo_señal"]]
        except (KeyError, TypeError):
            raise ValueError(f"Tipo de señal inválido para {id.id}.") from None
        item = cls(id, position, tipo)
        item.rotation = rotation
        return item


def _valid_cell_overlap(first: Item, first_dependency: str,
                        second: Item, second_dependency: str) -> bool:
    track_types = (CvLineal, Aguja)
    if first_dependency != second_dependency:
        return False
    if isinstance(first, Señal) and isinstance(second, track_types):
        return first.get_directions()[0] in second.get_directions()
    if isinstance(second, Señal) and isinstance(first, track_types):
        return second.get_directions()[0] in first.get_directions()
    return False


def _validate_layout_cells(dependencies: Dict[str, List[Item]]) -> None:
    positions = {}
    for dependency, items in dependencies.items():
        for item in items:
            cell_items = positions.setdefault(item.position, [])
            if cell_items:
                other_dependency, other_item = cell_items[0]
                if len(cell_items) != 1 or not _valid_cell_overlap(
                    other_item, other_dependency, item, dependency
                ):
                    raise ValueError(
                        f"La celda {item.position} está ocupada por {other_item.id.id} "
                        f"({other_dependency}) y {item.id.id} ({dependency})."
                    )
            cell_items.append((dependency, item))

    for position, cell_items in positions.items():
        signals = [item for _, item in cell_items if isinstance(item, Señal)]
        if not signals:
            continue
        tracks = [item for _, item in cell_items if isinstance(item, (CvLineal, Aguja))]
        if len(cell_items) != 2 or len(signals) != 1 or len(tracks) != 1:
            raise ValueError(f"La señal de la celda {position} debe compartirla con una única vía.")
        signal, track = signals[0], tracks[0]
        if signal.get_directions()[0] not in track.get_directions():
            raise ValueError(
                f"La dirección de la señal {signal.id.id} no coincide con una dirección "
                f"de la vía {track.id.id}."
            )


def serialize_layout(dependencies: Dict[str, List[Item]]) -> dict:
    _validate_layout_cells(dependencies)
    return {
        "version": 2,
        "dependencias": {
            dependency: {"elementos": [item.serialize() for item in items]}
            for dependency, items in dependencies.items()
        },
    }


def deserialize_layout(data: dict) -> Dict[str, List[Item]]:
    if not isinstance(data, dict):
        raise ValueError("El layout debe ser un objeto JSON.")
    version = data.get("version")
    if version == 1:
        dependency = data.get("dependencia")
        records = data.get("elementos")
        dependencies = {dependency: records}
    elif version == 2:
        dependencies = data.get("dependencias")
    else:
        raise ValueError("El archivo no es un layout compatible (versión 1 o 2).")
    if not isinstance(dependencies, dict):
        raise ValueError("El mapa de dependencias no es válido.")

    result: Dict[str, List[Item]] = {}
    for dependency, value in dependencies.items():
        if not isinstance(dependency, str) or not dependency.strip():
            raise ValueError("Hay un nombre de dependencia no válido.")
        records = value.get("elementos") if isinstance(value, dict) else value
        if not isinstance(records, list):
            raise ValueError(f"La lista de elementos de {dependency} no es válida.")
        items = [Item.deserialize(record, dependency) for record in records]
        result[dependency] = items
    _validate_layout_cells(result)
    return result
