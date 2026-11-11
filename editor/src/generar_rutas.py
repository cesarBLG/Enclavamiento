#!/usr/bin/env python3
"""Genera rutas de una dependencia a partir de su topología.

Ejemplo:
    python3 utils/generar_rutas.py dist/villalba.json \
        --dependencia VG \
        --inicios E1,E3 \
        --destinos 'S1/1:CV1,S1/3:CV3' \
        --tipos Itinerario,Rebase

Cada destino se expresa como ``id_destino:id_seccion_fin``. La salida es
únicamente un array JSON, listo para copiar dentro de ``Rutas``.
"""

import argparse
import json
import sys
from collections import defaultdict, deque
from functools import partial
from pathlib import Path
try:
    from topology import connections_by_side, normalize_connection
except ImportError:
    from editor.src.topology import connections_by_side, normalize_connection


def parsear_lista(texto, nombre):
    valores = [valor.strip() for valor in texto.split(",") if valor.strip()]
    if not valores:
        raise ValueError(f"La lista {nombre} no puede estar vacía")
    return valores


def parsear_destinos(texto):
    destinos = []
    for elemento in parsear_lista(texto, "destinos"):
        if ":" not in elemento:
            raise ValueError(
                f"Destino inválido {elemento!r}; usa id_destino:id_seccion_fin"
            )
        destino, fin = elemento.split(":", 1)
        if not destino or not fin:
            raise ValueError(
                f"Destino inválido {elemento!r}; usa id_destino:id_seccion_fin"
            )
        destinos.append((destino, fin))
    return destinos


def referencia_id(referencia, dependencia):
    normalizada = normalize_connection(referencia, dependencia)
    if normalizada is None:
        return None
    return normalizada["Id"]


def conexiones_seccion(seccion, lado, dependencia):
    """Devuelve pares (sección cualificada, pin), conservando el orden."""
    referencias = connections_by_side(seccion, dependencia)[lado]
    resultado = []
    for pin, referencia in enumerate(referencias):
        ident = referencia_id(referencia, dependencia)
        if ident is not None:
            resultado.append((ident, pin))
    return resultado


def construir_ruta(dependencia_id, dependencia, inicio, fin, dependencias=None):
    if dependencias is None:
        dependencias = {dependencia_id: dependencia}
    senales = dependencia.get("Señales", {})
    if inicio not in senales:
        raise ValueError(f"No existe la señal de inicio {inicio!r}")

    señal = senales[inicio]
    origen = referencia_id(señal.get("Sección"), dependencia_id)
    lado = señal.get("Lado")
    if origen is None:
        raise ValueError(f"La señal {inicio!r} referencia una sección inválida")
    if lado not in ("Par", "Impar"):
        raise ValueError(f"La señal {inicio!r} no tiene un Lado Par o Impar")
    destino = referencia_id(fin, dependencia_id)
    if destino is None:
        raise ValueError(f"La sección final {fin!r} es inválida")
    destino_dep, destino_id = destino.split(":", 1)
    if destino_dep not in dependencias or destino_id not in dependencias[destino_dep].get("Secciones", {}):
        raise ValueError(f"No existe la sección final {fin!r}")

    # Cada estado registra la sección actual, el sentido de marcha y la anterior.
    # En las agujas se prueban las salidas existentes hasta encontrar el destino.
    # FIXME: calcular seccion previa, sabiendo que el pin de entrada a origen está en señal["Pin"] (por defecto 0)
    origen_dep, origen_id = origen.split(":", 1)
    inicial = (origen_dep, origen_id, lado, None, None, None)
    anteriores = {inicial: None}
    cola = deque([inicial])
    final_estado = None
    while cola:
        dep_actual, actual, dir_marcha, previa_dep, previa, pin_entrada = cola.popleft()
        if f"{dep_actual}:{actual}" == destino:
            final_estado = (dep_actual, actual, dir_marcha, previa_dep, previa, pin_entrada)
            break
        secciones = dependencias[dep_actual].get("Secciones", {})
        seccion = secciones[actual]
        salidas = conexiones_seccion(seccion, dir_marcha, dep_actual)
        entradas = conexiones_seccion(seccion, "Par" if dir_marcha == "Impar" else "Impar", dep_actual)
        pin_in = señal.get("Pin", 0) if previa is None else next(
            (pin for vecino, pin in entradas if vecino == f"{previa_dep}:{previa}"), None
        )
        for siguiente, pin_out in salidas:
            siguiente_dep, siguiente_id = siguiente.split(":", 1)
            if siguiente_dep not in dependencias or siguiente_id not in dependencias[siguiente_dep].get("Secciones", {}):
                continue
            # La paridad puede invertirse en conexiones declaradas como objeto.
            refs = connections_by_side(seccion, dep_actual)[dir_marcha]
            ref_salida = refs[pin_out] if pin_out < len(refs) else None
            invierte = ref_salida.get("InvertirParidad", False) if isinstance(ref_salida, dict) else False
            siguiente_dir = ("Par" if dir_marcha == "Impar" else "Impar") if invierte else dir_marcha
            estado = (siguiente_dep, siguiente_id, siguiente_dir, dep_actual, actual, pin_out)
            if estado not in anteriores:
                anteriores[estado] = ((dep_actual, actual, dir_marcha, previa_dep, previa, pin_entrada), pin_in, pin_out)
                cola.append(estado)

    if final_estado is None:
        raise ValueError(
            f"No hay camino topológico desde {inicio!r} ({origen}) hasta {fin!r}"
        )

    estados = []
    estado = final_estado
    while estado is not None:
        estados.append(estado)
        registro = anteriores[estado]
        estado = registro[0] if registro is not None else None
    estados.reverse()

    posiciones = {}
    for indice, estado in enumerate(estados):
        seccion_dep, seccion_id, dir_actual, previa_dep, previa, _ = estado
        seccion = dependencias[seccion_dep]["Secciones"][seccion_id]
        if seccion.get("Tipo") != "Aguja":
            continue
        in_lado = "Par" if dir_actual == "Impar" else "Impar"
        entradas = conexiones_seccion(seccion, in_lado, seccion_dep)
        salidas = conexiones_seccion(seccion, dir_actual, seccion_dep)
        pin_in = señal.get("Pin", 0) if previa is None else next(
            (pin for vecino, pin in entradas if vecino == f"{previa_dep}:{previa}"), None
        )
        siguiente_estado = estados[indice + 1] if indice + 1 < len(estados) else None
        if siguiente_estado is None:
            pin_out = 0
        else:
            siguiente_dep, siguiente_id = siguiente_estado[0], siguiente_estado[1]
            pin_out = next((pin for vecino, pin in salidas if vecino == f"{siguiente_dep}:{siguiente_id}"), None)
        if pin_in is None or pin_out is None:
            raise ValueError(f"No se pudieron determinar los pines de la aguja {seccion_id}")
        # lados<int> se serializa como [Par, Impar].
        pines = {"Par": 0, "Impar": 0}
        pines[in_lado] = pin_in
        pines[dir_actual] = pin_out
        posicion_id = seccion_id if seccion_dep == dependencia_id else f"{seccion_dep}:{seccion_id}"
        posiciones[posicion_id] = [pines["Par"], pines["Impar"]]

    return posiciones


def generar(dependencia_id, dependencia, inicios, destinos, tipos, params_ruta=None, dependencias=None):
    rutas = []
    for inicio in inicios:
        for destino, fin in destinos:
            posiciones = construir_ruta(dependencia_id, dependencia, inicio, fin, dependencias)
            for tipo in tipos:
                ruta = {
                    "Inicio": inicio,
                    "Tipo": tipo,
                    "Destino": destino,
                    "SecciónFin": fin,
                }
                if posiciones:
                    ruta["PosiciónAparatos"] = posiciones
                if params_ruta:
                    ruta.update(**params_ruta)
                rutas.append(ruta)
    return rutas

def load_dependencia(config, dependencia_id=None):
    if isinstance(config, (str, Path)):
        with Path(config).open(encoding="utf-8") as archivo:
            config = json.load(archivo)
    dependencias = config.get("Dependencias", {})
    if dependencia_id is None:
        if len(dependencias) != 1:
            raise ValueError("Indica dependencia_id cuando hay cero o varias dependencias")
        dependencia_id = next(iter(dependencias))
    if dependencia_id not in dependencias:
        raise ValueError(f"No existe la dependencia {dependencia_id!r}")
    return dependencia_id, dependencias[dependencia_id]


def main(config=None, inicios=None, destinos=None, tipos=None, dependencia_id=None, params_ruta=None):
    """Genera rutas desde Python o desde la línea de comandos.

    Desde Python, config puede ser un dict ya cargado o una ruta; inicios
    y tipos son listas y destinos una lista de pares (id, sección_fin).
    Si se llama sin argumentos, se leen las opciones CLI como antes.
    """
    if all(valor is None for valor in (config, inicios, destinos, tipos, dependencia_id)):
        parser = argparse.ArgumentParser(description=__doc__)
        parser.add_argument("config", type=Path, help="JSON de configuración")
        parser.add_argument("--dependencia", help="id de dependencia (si hay varias)")
        parser.add_argument("--inicios", required=True, help="ids separados por coma")
        parser.add_argument("--destinos", required=True, help="pares destino:sección_fin separados por coma")
        parser.add_argument("--tipos", required=True, help="tipos separados por coma")
        args = parser.parse_args()
        config = args.config
        dependencia_id = args.dependencia
        inicios = args.inicios
        destinos = args.destinos
        tipos = args.tipos

    try:
        if isinstance(config, (str, Path)):
            with Path(config).open(encoding="utf-8") as archivo:
                config = json.load(archivo)
        dependencia_id, dependencia = load_dependencia(config, dependencia_id)
        dependencias = config.get("Dependencias", {})
        if isinstance(inicios, str):
            inicios = parsear_lista(inicios, "inicios")
        if isinstance(destinos, str):
            destinos = parsear_destinos(destinos)
        if isinstance(tipos, str):
            tipos = parsear_lista(tipos, "tipos")
        if not inicios or not destinos or not tipos:
            raise ValueError("inicios, destinos y tipos no pueden estar vacíos")
        destinos = [(destino, fin) for destino, fin in destinos]
        tipos_validos = {"Itinerario", "Maniobra", "Rebase"}
        invalidos = [tipo for tipo in tipos if tipo not in tipos_validos]
        if invalidos:
            raise ValueError(f"Tipos de itinerario no válidos: {', '.join(invalidos)}")
        rutas = generar(dependencia_id, dependencia, inicios, destinos, tipos, params_ruta, dependencias)
        json.dump(rutas, sys.stdout, ensure_ascii=False, indent=4)
        sys.stdout.write("\n")
        return 0
    except (OSError, json.JSONDecodeError, ValueError, KeyError) as exc:
        print(f"Error: {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    #raise SystemExit(main())
    rutas = []
    dep = "TMB"
    if dep == "VG":
        config = json.loads(Path("dist/villalba.json").read_text(encoding="utf-8"))
        dependencia_id, dependencia = load_dependencia(config, dependencia_id="VG")
        generar = partial(generar, dependencias=config["Dependencias"])

        # tipo señales_inicio destinos
        # I,R E1,E3 S1/1,CV1;S1/2,CV2;S1/3,CV3;S1/4,CV4;S1/5,CV5
        rutas += generar(dependencia_id, dependencia, inicios=["E1", "E3"], destinos=[(f"S1/{i}", f"CV{i}") for i in range(1,6)]+[("FV6", "CV6")], tipos=["Itinerario", "Rebase"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["E1", "E3"], destinos=[("S1/7-11", "A19")], tipos=["Rebase"])
        rutas += generar(dependencia_id, dependencia, inicios=["R3"], destinos=[(f"S1/{i}", f"CV{i}") for i in range(1,6)]+[("FV6", "CV6")], tipos=["Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["R3"], destinos=[("S1/7-11", "A19")], tipos=["Maniobra"])

        rutas += generar(dependencia_id, dependencia, inicios=["E2", "E4", "E6"], destinos=[(f"S2/{i}", f"CV{i}") for i in [1,2,3,4,5]], tipos=["Itinerario", "Rebase"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["R4"], destinos=[(f"S2/{i}", f"CV{i}") for i in [1,2,3,4,5]], tipos=["Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["R2"], destinos=[(f"S2/{i}", f"CV{i}") for i in [1,3,5]], tipos=["Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["E2", "E4", "E6"], destinos=[("S2/7-9", "A22")], tipos=["Rebase"])
        rutas += generar(dependencia_id, dependencia, inicios=["E2", "R2", "R4"], destinos=[("S2/7-9", "A22")], tipos=["Maniobra"])

        rutas += generar(dependencia_id, dependencia, inicios=[f"S1/{i}" for i in [1,2,3,4,5]]+["S1/7-11"], destinos=[("ES1", "A2"), ("ES2", "CVE2"), ("CR", "CVE4")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S2/{i}" for i in range(1,7)]+["S2/7-9"], destinos=[("TR1", "CVE1"),("TR2","CVE3")], tipos=["Itinerario"])

        rutas += generar(dependencia_id, dependencia, inicios=[f"S1/{i}" for i in [1,3,5]]+["S1/7-11"], destinos=[("ES1", "A2")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S1/{i}" for i in [1,2,3,4,5]]+["S1/7-11"], destinos=[("CR", "CVE4")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S2/{i}" for i in range(1,7)]+["S2/7-9"], destinos=[("TR2","CVE3")], tipos=["Maniobra"])
    elif dep == "CTL":
        config = json.loads(Path("../../dist/ENCE_FCD.json").read_text(encoding="utf-8"))
        dependencia_id, dependencia = load_dependencia(config, dependencia_id="CTL")
        generar = partial(generar, dependencias=config["Dependencias"])
        rutas += generar(dependencia_id, dependencia, inicios=["E1"], destinos=[(f"S1/{i}", f"CV{i}") for i in range(1,5)], tipos=["Itinerario", "Rebase"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["R1"], destinos=[(f"S1/{i}", f"CV{i}") for i in range(1,5)], tipos=["Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["E2","E4"], destinos=[(f"S2/{i}", f"CV{i}") for i in range(1,5)], tipos=["Itinerario", "Rebase", "Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=[f"S1/{i}" for i in range(1,5)], destinos=[("TOR:E1", "CVCTLTOR")], tipos=["Itinerario","Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S1/{i}" for i in range(1,5)], destinos=[("CAR","A2")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S1/{i}" for i in range(1,5)], destinos=[("CAR","CVCTLCAR")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S2/{i}" for i in range(1,5)], destinos=[("TMB", "CVE1")], tipos=["Itinerario", "Maniobra"])
    elif dep == "ARB":
        config = json.loads(Path("../../dist/ENCE_FCD.json").read_text(encoding="utf-8"))
        dependencia_id, dependencia = load_dependencia(config, dependencia_id="ARB")
        generar = partial(generar, dependencias=config["Dependencias"])
        rutas += generar(dependencia_id, dependencia, inicios=["E1"], destinos=[(f"S1/{i}", f"CV{i}") for i in range(1,4)], tipos=["Itinerario", "Rebase", "Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["E1"], destinos=[("TMB:ARB", "TMB:A1")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=["E1"], destinos=[("TMB:ARB", "TMB:CVPROXE1")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=["E2","S2/4"], destinos=[(f"S2/{i}", f"CV{i}") for i in range(1,4)], tipos=["Itinerario", "Rebase", "Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=[f"S1/{i}" for i in range(1,4)], destinos=[("TMB", "A2")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S1/{i}" for i in range(1,4)], destinos=[("TMB", "CVPROXE2")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S1/{i}" for i in range(1,4)], destinos=[("FV4", "CV4")], tipos=["Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["S1/3"], destinos=[("FV5", "CV5")], tipos=["Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["M4"], destinos=[("S2/3", "CV3")], tipos=["Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["M2"], destinos=[("S2/4", "CV4")], tipos=["Maniobra"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=[f"S2/{i}" for i in range(1,4)], destinos=[("CDM", "A1")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S2/{i}" for i in range(1,4)], destinos=[("CDM", "CVCDMARB")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=[f"S2/{i}" for i in [1,3]], destinos=[("TMB:S1/1", "TMB:CV1")], tipos=["Itinerario","Maniobra","Rebase"], params_ruta={"DiferímetroDeslizamiento": {}})
    elif dep == "TMB":
        config = json.loads(Path("../../dist/ENCE_FCD.json").read_text(encoding="utf-8"))
        dependencia_id, dependencia = load_dependencia(config, dependencia_id="TMB")
        generar = partial(generar, dependencias=config["Dependencias"])
        rutas += generar(dependencia_id, dependencia, inicios=["E1"], destinos=[("ARB:CDM", "ARB:A1")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=["E1"], destinos=[("ARB:CDM", "ARB:CVCDMARB")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=["E1"], destinos=[("S1/1", "CV1")], tipos=["Itinerario","Maniobra","Rebase"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["S1/1"], destinos=[("CTL", "A2")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=["S1/1"], destinos=[("CTL", "CVTMBCTL")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=["S2/2"], destinos=[("ARB", "A1")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=["S2/2"], destinos=[("ARB", "CVPROXE1")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=["S2/2"], destinos=[(f"ARB:S1/{i}", f"ARB:CV{i}") for i in [1,3]], tipos=["Itinerario","Maniobra","Rebase"], params_ruta={"DiferímetroDeslizamiento": {}})
    elif dep == "CDM":
        config = json.loads(Path("../../dist/ENCE_FCD.json").read_text(encoding="utf-8"))
        dependencia_id, dependencia = load_dependencia(config, dependencia_id="CDM")
        generar = partial(generar, dependencias=config["Dependencias"])
        rutas += generar(dependencia_id, dependencia, inicios=["S1/1"], destinos=[("ARB", "A2")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=["S1/1"], destinos=[("ARB", "ARB:CVCDMARB")], tipos=["Maniobra"])
    elif dep == "CAR":
        config = json.loads(Path("../../dist/ENCE_FCD.json").read_text(encoding="utf-8"))
        dependencia_id, dependencia = load_dependencia(config, dependencia_id="CAR")
        generar = partial(generar, dependencias=config["Dependencias"])
        rutas += generar(dependencia_id, dependencia, inicios=["S1/1","S1/3"], destinos=[("CDM1", "A2")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=["S1/1","S1/3"], destinos=[("CDM1", "CVCARCDM1")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=["S2/2"], destinos=[("CTL", "A1")], tipos=["Itinerario"])
        rutas += generar(dependencia_id, dependencia, inicios=["S2/2"], destinos=[("CTL", "CTL:CVCTLCAR")], tipos=["Maniobra"])
    elif dep == "TOR":
        config = json.loads(Path("../../dist/ENCE_FCD.json").read_text(encoding="utf-8"))
        dependencia_id, dependencia = load_dependencia(config, dependencia_id="TOR")
        generar = partial(generar, dependencias=config["Dependencias"])
        rutas += generar(dependencia_id, dependencia, inicios=["E1"], destinos=[("CAR:S1/1", "CAR:CV1")], tipos=["Itinerario","Maniobra","Rebase"], params_ruta={"DiferímetroDeslizamiento": {}})
        rutas += generar(dependencia_id, dependencia, inicios=["E1"], destinos=[("FTOR", "A1")], tipos=["Maniobra"])
        rutas += generar(dependencia_id, dependencia, inicios=["S2"], destinos=[("CTL:E4", "CTL:CVCTLTOR")], tipos=["Itinerario"])

    print(json.dumps(rutas, ensure_ascii=False, indent=None))
