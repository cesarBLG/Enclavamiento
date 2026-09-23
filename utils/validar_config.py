#!/usr/bin/env python3
"""
Validador del JSON de configuración del enclavamiento.

Uso:
    python3 validar_config.py config.json

Devuelve el código de salida 0 si no hay errores y 1 en caso contrario.
"""

import json
import sys
from pathlib import Path


# --------------------------------------------------------------------------- #
# Enumerados admitidos (coinciden con include/enums.h y la lectura en src/)
# --------------------------------------------------------------------------- #

LADO = {"Par", "Impar"}

TIPO_SECCION = {"Lineal", "Aguja", "Cruzamiento"}
TIPO_CV = {"Lineal", "Aguja", "Cruzamiento"}
TIPO_SENAL = {
    "Entrada",
    "Salida",
    "Avanzada",
    "Maniobra",
    "Retroceso",
    "Intermedia",
    "PostePuntoProtegido",
}
ASPECTOS = {
    "Parada",
    "RebaseAutorizado",
    "RebaseAutorizadoDestellos",
    "MovimientoAutorizado",
    "ParadaSelectiva",
    "ParadaSelectivaDestellos",
    "ParadaDiferida",
    "Precaución",
    "AnuncioParada",
    "AnuncioPrecaución",
    "VíaLibre",
}
ESTADO_CANTON = {
    "Libre",
    "Prenormalizado",
    "OcupadoMismoSentido",
    "Ocupado",
}
TIPO_MOVIMIENTO_RUTA = {"Itinerario", "Maniobra", "Rebase"}
COMPATIBILIDAD_MANIOBRA = {
    "Incompatible",
    "IncompatibleBloqueo",
    "IncompatibleMovimiento",
    "IncompatibleItinerario",
    "Compatible",
}
TIPO_BLOQUEO = {"BAU", "BAD", "BAB", "BLAU", "BLAD", "BLAB"}
TIPO_PN = {"Automático", "Afectado", "Enclavado"}
TIPO_DESTINO = {"Señal", "Colateral", "FinalVía"}
TIPO_FAI = {"Proximidad", "Reserva"}

# Prefijos de mando de ruta tal y como se construye en src/ruta.h (movimiento::id).
RUTA_ID_PREFIXES = {
    ("Itinerario", False): "I",
    ("Itinerario", True): "ER",   # ERTMS
    "Rebase": "R",
    "Maniobra": "M",
}

# Campos de ParámetrosPredeterminados con su tipo esperado (src/serialization.cpp).
PARAMETROS = {
    "DiferímetroDAI1": ("number", None),
    "DiferímetroDAI2": ("number", None),
    "DiferímetroDEI": ("number", None),
    "PrenormalizaciónCV": ("number", None),
    "PrenormalizaciónCVTren": ("number", None),
    "EspaciadoFAI": ("number", None),
    "FracciónEjesPrenormalización": ("number", None),
    "DeslizamientoBloqueo": ("bool", None),
    "AspectoDesviada": (ASPECTOS, None),
    "AnuncioPrecaución": ("bool", None),
}


# --------------------------------------------------------------------------- #
# Construcción del registro de elementos por dependencia
# --------------------------------------------------------------------------- #

def _obj_keys(obj):
    """Claves de un objeto; vacío si no es un diccionario.
    El tipo incorrecto se reporta aparte como error de validación."""
    if isinstance(obj, dict):
        return set(obj.keys())
    return set()


def build_secciones(dep):
    return _obj_keys(dep.get("Secciones", {}))


def build_cvs(dep):
    return _obj_keys(dep.get("CVs", {}))


def build_senales(dep):
    return _obj_keys(dep.get("Señales", {}))


def build_pns(dep):
    return _obj_keys(dep.get("PNs", {}))


def build_destinos(dep):
    return _obj_keys(dep.get("DestinosRuta", {}))

def build_bloqueos(dep):
    """Identificador de un bloqueo: 'Colateral'+'Vía'.
    Un mismo id puede estar referenciado como 'Id' o 'Dep:Id', donde Id es
    exactamente la concatenación Colateral+Vía (src/bloqueo.cpp)."""
    bloqs = dep.get("Bloqueos", [])
    if not isinstance(bloqs, list):
        return set()
    return {
        b.get("Colateral", "") + b.get("Vía", "")
        for b in bloqs
        if isinstance(b, dict)
    }


def build_agujas(dep):
    """Agujas: secciones cuyo Tipo es 'Aguja' (obligatorio para referencias Escape)."""
    secs = dep.get("Secciones", {})
    if not isinstance(secs, dict):
        return set()
    return {
        sid
        for sid, s in secs.items()
        if isinstance(s, dict) and s.get("Tipo") == "Aguja"
    }


REGISTRIES = {
    "Secciones": build_secciones,
    "CVs": build_cvs,
    "Señales": build_senales,
    "PNs": build_pns,
    "DestinosRuta": build_destinos,
    "Bloqueos": build_bloqueos,
}


def build_registry(data):
    registry = {}
    for dep_name, dep in data.get("Dependencias", {}).items():
        if not isinstance(dep, dict):
            continue
        registry[dep_name] = {
            ns: builder(dep)
            for ns, builder in REGISTRIES.items()
        }
        # Registro auxiliar de agujas (no es una referencia genérica).
        registry[dep_name]["Agujas"] = build_agujas(dep)

    # Rutas por dependencia y sus ids de mando para validar FAI/ItinerariosApertura.
    for dep_name, dep in data.get("Dependencias", {}).items():
        if not isinstance(dep, dict):
            continue
        rutas_cfg = dep.get("Rutas", [])
        ruta_ids = set()
        for jr in (rutas_cfg if isinstance(rutas_cfg, list) else []):
            rid = ruta_command_id(dep_name, jr)
            if rid:
                ruta_ids.add(rid)
        registry[dep_name]["IdsRuta"] = ruta_ids

    return registry


def split_reference(ref, current_dependency):
    """Devuelve (dependencia, identificador) de una referencia.
    Acepta 'Id' o 'Dep:Id'. Detecta referencias con formato inválido."""
    if isinstance(ref, str) and ":" in ref:
        dep, _, ident = ref.partition(":")
        return dep, ident
    return current_dependency, ref


def exists(registry, dependency, namespace, ident):
    try:
        return (
            dependency in registry
            and namespace in registry[dependency]
            and isinstance(ident, str)
            and ident in registry[dependency][namespace]
        )
    except TypeError:
        return False


# --------------------------------------------------------------------------- #
# Utilidades de validación por tipo de dato
# --------------------------------------------------------------------------- #

def check_reference(errors, path, ref, current_dep, namespace):
    """Valida que una referencia (str) exista en el namespace indicado."""
    if not isinstance(ref, str) or ref == "":
        errors.append(f"{path}: referencia vacía o no textual a {namespace}")
        return
    dep, ident = split_reference(ref, current_dep)
    if ident == "":
        errors.append(
            f"{path}: '{ref}' es una referencia inválida (identificador vacío)"
        )
        return
    if not exists(registry_global(), dep, namespace, ident):
        errors.append(f"{path}: '{ref}' no existe en {dep}.{namespace}")


def check_reference_list(errors, path, refs, current_dep, namespace):
    """Valida una lista de referencias (str)."""
    if not isinstance(refs, list):
        return
    for i, ref in enumerate(refs):
        check_reference(
            errors,
            f"{path}[{i}]",
            ref,
            current_dep,
            namespace,
        )


def check_lados_object(errors, path, obj):
    """Valida que un objeto tenga claves de Lado válidas ('Par'/'Impar')."""
    if not isinstance(obj, dict):
        return
    for key in obj:
        if key not in LADO:
            errors.append(
                f"{path}/{key}: lado inválido (esperaba 'Par' o 'Impar')"
            )


def check_position_aparatos(errors, path, pos):
    """Valida un par [pin_salida_par, pin_salida_impar]."""
    if not isinstance(pos, list) or len(pos) != 2:
        errors.append(f"{path}: posición de aparato debe ser [par, impar]")
        return
    for v in pos:
        if isinstance(v, bool) or not isinstance(v, int):
            errors.append(
                f"{path}: los pines deben ser números enteros"
            )
            break


def check_positions_map(errors, path, obj, current_dep, namespace="Secciones"):
    """Valida un mapa sección -> [par, impar] (PosiciónAparatos y similares)."""
    if not isinstance(obj, dict):
        return
    for sec_id, pos in obj.items():
        check_reference(
            errors,
            f"{path}/{sec_id}",
            sec_id,
            current_dep,
            namespace,
        )
        check_position_aparatos(errors, f"{path}/{sec_id}", pos)


def check_bool(errors, path, value):
    if not isinstance(value, bool):
        errors.append(f"{path}: se esperaba un booleano")


# Referencia global al registro (se rellena en main()).
_REGISTRY = {}


def registry_global():
    return _REGISTRY


# --------------------------------------------------------------------------- #
# Validación por tipo de elemento
# --------------------------------------------------------------------------- #

def validate_cv(errors, path, jcv):
    if not isinstance(jcv, dict):
        errors.append(f"{path}: el CV debe ser un objeto")
        return

    tipo = jcv.get("Tipo")
    if tipo is not None and tipo not in TIPO_CV:
        errors.append(
            f"{path}/Tipo: '{tipo}' no es válido (esperaba {sorted(TIPO_CV)})"
        )

    cejes = jcv.get("ContadoresEjes")
    if cejes is not None:
        if not isinstance(cejes, dict):
            errors.append(f"{path}/ContadoresEjes: debe ser un objeto")
            return
        for cid, pos in cejes.items():
            cpath = f"{path}/ContadoresEjes/{cid}"
            if not isinstance(pos, dict):
                errors.append(f"{cpath}: el contador debe ser un objeto")
                continue
            lado = pos.get("Lado")
            if lado is None:
                errors.append(f"{cpath}/Lado: campo obligatorio")
            elif lado not in LADO:
                errors.append(
                    f"{cpath}/Lado: '{lado}' no es válido (Par/Impar)"
                )
            for field, default in (
                ("Reverse", False),
                ("Liberar", True),
                ("Ocupar", True),
            ):
                if field in pos and not isinstance(pos[field], bool):
                    errors.append(f"{cpath}/{field}: debe ser booleano")


def validate_conexion(errors, path, conex, current_dep):
    """Valida una entrada de Conexiones (string o {Id, InvertirParidad})."""
    if isinstance(conex, str):
        check_reference(errors, path, conex, current_dep, "Secciones")
        return
    if not isinstance(conex, dict):
        errors.append(f"{path}: conexión debe ser un texto o un objeto")
        return
    ident = conex.get("Id")
    if ident is None:
        errors.append(f"{path}/Id: campo obligatorio en la conexión")
    else:
        check_reference(errors, f"{path}/Id", ident, current_dep, "Secciones")
    if "InvertirParidad" in conex and not isinstance(conex["InvertirParidad"], bool):
        errors.append(f"{path}/InvertirParidad: debe ser booleano")


def validate_conexiones_lado(errors, path, value, current_dep):
    """Valida el objeto de lados de 'Conexiones'."""
    if not isinstance(value, dict):
        return
    check_lados_object(errors, path, value)
    for lado, conexs in value.items():
        if not isinstance(conexs, list):
            errors.append(f"{path}/{lado}: debe ser una lista de conexiones")
            continue
        for i, conex in enumerate(conexs):
            validate_conexion(errors, f"{path}/{lado}[{i}]", conex, current_dep)


def validate_flanco(errors, path, jf, current_dep):
    if not isinstance(jf, dict):
        errors.append(f"{path}: flanco debe ser un objeto")
        return
    lado = jf.get("Lado")
    if lado is None:
        errors.append(f"{path}/Lado: campo obligatorio")
    elif lado not in LADO:
        errors.append(f"{path}/Lado: '{lado}' no es válido (Par/Impar)")
    pin = jf.get("Pin")
    if isinstance(pin, bool) or (pin is not None and not isinstance(pin, int)):
        errors.append(f"{path}/Pin: debe ser un entero")
    check_reference_list(
        errors,
        f"{path}/Límite",
        jf.get("Límite", []),
        current_dep,
        "Secciones",
    )
    if "PosiciónAparatos" in jf:
        check_positions_map(
            errors, f"{path}/PosiciónAparatos", jf["PosiciónAparatos"], current_dep
        )


def validate_punto_negro(errors, path, jg, current_dep):
    if not isinstance(jg, dict):
        errors.append(f"{path}: punto negro debe ser un objeto")
        return
    check_reference(errors, f"{path}/Id", jg.get("Id"), current_dep, "Secciones")
    for side in ("Afectado", "Causante"):
        if side not in jg:
            continue
        sub = jg[side]
        spath = f"{path}/{side}"
        if not isinstance(sub, dict):
            errors.append(f"{spath}: debe ser un objeto")
            continue
        lado = sub.get("Lado")
        if lado is not None and lado not in LADO:
            errors.append(f"{spath}/Lado: '{lado}' no es válido (Par/Impar)")
        pin = sub.get("Pin")
        if isinstance(pin, bool) or (pin is not None and not isinstance(pin, int)):
            errors.append(f"{spath}/Pin: debe ser un entero")


def validate_seccion(errors, path, jsec, current_dep):
    if not isinstance(jsec, dict):
        errors.append(f"{path}: la sección debe ser un objeto")
        return

    tipo = jsec.get("Tipo", "Lineal")
    if tipo not in TIPO_SECCION:
        errors.append(
            f"{path}/Tipo: '{tipo}' no es válido (esperaba {sorted(TIPO_SECCION)})"
        )

    # CV opcional; si se indica debe existir.
    if "CV" in jsec:
        check_reference(errors, f"{path}/CV", jsec["CV"], current_dep, "CVs")

    # Bloqueo opcional; si se indica debe existir.
    if "Bloqueo" in jsec:
        check_reference(
            errors, f"{path}/Bloqueo", jsec["Bloqueo"], current_dep, "Bloqueos"
        )

    for field in ("Trayecto",):
        if field in jsec and not isinstance(jsec[field], bool):
            errors.append(f"{path}/{field}: debe ser booleano")

    # Conexiones (objeto de lados).
    if "Conexiones" in jsec:
        validate_conexiones_lado(
            errors, f"{path}/Conexiones", jsec["Conexiones"], current_dep
        )

    # Aguja.
    if tipo == "Aguja":
        lado = jsec.get("Lado")
        if lado is None:
            errors.append(f"{path}/Lado: obligatorio para sección aguja")
        elif lado not in LADO:
            errors.append(
                f"{path}/Lado: '{lado}' no es válido (Par/Impar)"
            )
        punta = jsec.get("SecciónPunta")
        if punta is not None:
            validate_conexion(errors, f"{path}/SecciónPunta", punta, current_dep)
        talon = jsec.get("SeccionesTalón")
        if talon is not None:
            if not isinstance(talon, list):
                errors.append(f"{path}/SeccionesTalón: debe ser una lista")
            else:
                for i, t in enumerate(talon):
                    validate_conexion(
                        errors, f"{path}/SeccionesTalón[{i}]", t, current_dep
                    )
        if "Talonable" in jsec and not isinstance(jsec["Talonable"], bool):
            errors.append(f"{path}/Talonable: debe ser booleano")
        muelle = jsec.get("PosiciónMuelle")
        if muelle is not None and (isinstance(muelle, bool) or muelle not in (0, 1)):
            errors.append(
                f"{path}/PosiciónMuelle: debe ser 0 (normal) o 1 (invertida)"
            )
        # Escape hace referencia a otra aguja.
        if "Escape" in jsec:
            check_reference(errors, f"{path}/Escape", jsec["Escape"], current_dep, "Agujas")

    # Protecciones de flanco y puntos negros (topology.cpp).
    for i, jf in enumerate(jsec.get("Flanco", [])):
        validate_flanco(errors, f"{path}/Flanco[{i}]", jf, current_dep)
    for i, jg in enumerate(jsec.get("PuntosNegros", [])):
        validate_punto_negro(errors, f"{path}/PuntosNegros[{i}]", jg, current_dep)


def validate_pn(errors, path, jpn, current_dep):
    if not isinstance(jpn, dict):
        errors.append(f"{path}: el PN debe ser un objeto")
        return
    check_reference(errors, f"{path}/Sección", jpn.get("Sección"), current_dep, "Secciones")

    if "TiempoApertura" in jpn:
        ta = jpn["TiempoApertura"]
        tpath = f"{path}/TiempoApertura"
        if isinstance(ta, dict):
            check_lados_object(errors, tpath, ta)
            for lado, val in ta.items():
                if isinstance(val, bool) or not isinstance(val, (int, float)):
                    errors.append(f"{tpath}/{lado}: debe ser un número")
    if "Tipo" in jpn:
        t = jpn["Tipo"]
        tpath = f"{path}/Tipo"
        if isinstance(t, dict):
            check_lados_object(errors, tpath, t)
            for lado, val in t.items():
                if val not in TIPO_PN:
                    errors.append(
                        f"{tpath}/{lado}: '{val}' no es válido "
                        f"(esperaba {sorted(TIPO_PN)})"
                    )


def validate_senal(errors, path, js, current_dep):
    if not isinstance(js, dict):
        errors.append(f"{path}: la señal debe ser un objeto")
        return

    lado = js.get("Lado")
    if lado is None:
        errors.append(f"{path}/Lado: campo obligatorio")
    elif lado not in LADO:
        errors.append(f"{path}/Lado: '{lado}' no es válido (Par/Impar)")

    tipo = js.get("Tipo")
    if tipo is None:
        errors.append(f"{path}/Tipo: campo obligatorio")
    elif tipo not in TIPO_SENAL:
        errors.append(
            f"{path}/Tipo: '{tipo}' no es válido (esperaba {sorted(TIPO_SENAL)})"
        )

    check_reference(errors, f"{path}/Sección", js.get("Sección"), current_dep, "Secciones")

    pin = js.get("Pin")
    if isinstance(pin, bool) or (pin is not None and not isinstance(pin, int)):
        errors.append(f"{path}/Pin: debe ser un entero")

    if "Bloqueo" in js:
        check_reference(errors, f"{path}/Bloqueo", js["Bloqueo"], current_dep, "Bloqueos")

    # AspectoCanton: clave EstadoCanton -> valor Aspecto.
    ac = js.get("AspectoCanton")
    if isinstance(ac, dict):
        for est, asp in ac.items():
            if est not in ESTADO_CANTON:
                errors.append(
                    f"{path}/AspectoCanton/{est}: estado de cantón inválido "
                    f"(esperaba {sorted(ESTADO_CANTON)})"
                )
            if asp not in ASPECTOS:
                errors.append(f"{path}/AspectoCanton/{est}: '{asp}' no es un aspecto válido")

    # AspectoAnteriorSeñal: clave/valor de aspectos.
    aas = js.get("AspectoAnteriorSeñal")
    if isinstance(aas, dict):
        for k, v in aas.items():
            for where, val in ((f"{path}/AspectoAnteriorSeñal/{k}", k), (f"{path}/AspectoAnteriorSeñal/{k}", v)):
                if val not in ASPECTOS:
                    errors.append(f"{where}: '{val}' no es un aspecto válido")
                    break

    check_reference_list(
        errors,
        f"{path}/LímiteProximidad",
        js.get("LímiteProximidad", []),
        current_dep,
        "CVs",
    )

    for field in ("RutaNecesaria", "ItinerariosDesviada"):
        if field in js and not isinstance(js[field], bool):
            errors.append(f"{path}/{field}: debe ser booleano")


def validate_bloqueo(errors, path, jb, current_dep):
    if not isinstance(jb, dict):
        errors.append(f"{path}: el bloqueo debe ser un objeto")
        return

    colateral = jb.get("Colateral")
    if not colateral:
        errors.append(f"{path}/Colateral: campo obligatorio")

    lado = jb.get("Lado")
    if lado is None:
        errors.append(f"{path}/Lado: campo obligatorio")
    elif lado not in LADO:
        errors.append(f"{path}/Lado: '{lado}' no es válido (Par/Impar)")

    tipo = jb.get("Tipo", "BAU")
    if tipo not in TIPO_BLOQUEO:
        errors.append(
            f"{path}/Tipo: '{tipo}' no es válido (esperaba {sorted(TIPO_BLOQUEO)})"
        )

    # SentidoPreferente obligatorio salvo en BAU/BLAU (bloqueo.cpp).
    if tipo not in ("BAU", "BLAU"):
        sp = jb.get("SentidoPreferente")
        if sp is None:
            errors.append(
                f"{path}/SentidoPreferente: obligatorio para bloqueos {tipo}"
            )
        elif sp not in LADO:
            errors.append(f"{path}/SentidoPreferente: '{sp}' no es válido (Par/Impar)")

    # CVs: lista de referencias a SECCIONES del trayecto (bloqueo.cpp usa
    # secciones[id_elemento(cv)]). Obligatorio.
    cvs = jb.get("CVs")
    if cvs is None:
        errors.append(f"{path}/CVs: campo obligatorio")
    else:
        check_reference_list(
            errors, f"{path}/CVs", cvs, current_dep, "Secciones"
        )

    nce = jb.get("CVsEntrada")
    if nce is not None and (isinstance(nce, bool) or not isinstance(nce, int)):
        errors.append(f"{path}/CVsEntrada: debe ser un entero")

    if "Vía" in jb and not isinstance(jb["Vía"], str):
        errors.append(f"{path}/Vía: debe ser una cadena")


def validate_destino_ruta(errors, path, idd, jdest, current_dep):
    if not isinstance(jdest, dict):
        errors.append(f"{path}: el destino de ruta debe ser un objeto")
        return

    tipo = jdest.get("Tipo")
    if tipo is None:
        errors.append(f"{path}/Tipo: campo obligatorio")
    elif tipo not in TIPO_DESTINO:
        errors.append(
            f"{path}/Tipo: '{tipo}' no es válido (esperaba {sorted(TIPO_DESTINO)})"
        )

    # Para destino de tipo Señal, la clave debe coincidir con una señal local.
    if tipo == "Señal":
        check_reference(errors, path + "/[clave]", idd, current_dep, "Señales")

    desliz = jdest.get("Deslizamiento")
    if isinstance(desliz, dict):
        limite = desliz.get("Límite")
        if limite is None:
            errors.append(f"{path}/Deslizamiento/Límite: campo obligatorio")
        else:
            check_reference_list(
                errors,
                f"{path}/Deslizamiento/Límite",
                limite,
                current_dep,
                "Secciones",
            )
        if isinstance(desliz.get("DeslizamientosOrientados"), list):
            for i, jo in enumerate(desliz["DeslizamientosOrientados"]):
                check_positions_map(
                    errors,
                    f"{path}/Deslizamiento/DeslizamientosOrientados[{i}]",
                    jo,
                    current_dep,
                )


def validate_ruta(errors, path, jr, estacion, registry):
    if not isinstance(jr, dict):
        errors.append(f"{path}: la ruta debe ser un objeto")
        return

    tipo = jr.get("Tipo")
    if tipo is None:
        errors.append(f"{path}/Tipo: campo obligatorio")
    elif tipo not in TIPO_MOVIMIENTO_RUTA:
        errors.append(
            f"{path}/Tipo: '{tipo}' no es válido (esperaba {sorted(TIPO_MOVIMIENTO_RUTA)})"
        )

    # Inicio -> señal. src/ruta.cpp la busca en señal_impls del mismo estación.
    inicio = jr.get("Inicio")
    if not inicio:
        errors.append(f"{path}/Inicio: campo obligatorio")
    else:
        check_reference(errors, f"{path}/Inicio", inicio, estacion, "Señales")

    destino = jr.get("Destino")
    if not destino:
        errors.append(f"{path}/Destino: campo obligatorio")
    else:
        check_reference(errors, f"{path}/Destino", destino, estacion, "DestinosRuta")

    if "Bloqueo" in jr:
        check_reference(errors, f"{path}/Bloqueo", jr["Bloqueo"], estacion, "Bloqueos")
    if "SecciónFin" in jr:
        check_reference(
            errors, f"{path}/SecciónFin", jr["SecciónFin"], estacion, "Secciones"
        )
    if "SeñalLiberación" in jr:
        check_reference(
            errors,
            f"{path}/SeñalLiberación",
            jr["SeñalLiberación"],
            estacion,
            "Señales",
        )

    if "PosiciónAparatos" in jr:
        check_positions_map(
            errors, f"{path}/PosiciónAparatos", jr["PosiciónAparatos"], estacion
        )

    comp = jr.get("Compatible")
    if comp is not None and comp not in COMPATIBILIDAD_MANIOBRA:
        errors.append(
            f"{path}/Compatible: '{comp}' no es válido "
            f"(esperaba {sorted(COMPATIBILIDAD_MANIOBRA)})"
        )

    for field, kind in (
        ("DiferímetroDAI1", "number"),
        ("DiferímetroDAI2", "number"),
        ("DiferímetroDEI", "number"),
    ):
        val = jr.get(field)
        if val is not None and (isinstance(val, bool) or not isinstance(val, (int, float))):
            errors.append(f"{path}/{field}: debe ser un número")

    for field in ("DesactivarDiferímetroDAI", "FAI", "ERTMS"):
        if field in jr and not isinstance(jr[field], bool):
            errors.append(f"{path}/{field}: debe ser booleano")

    tfa = jr.get("TipoFAI")
    if tfa is not None and tfa not in TIPO_FAI:
        errors.append(
            f"{path}/TipoFAI: '{tfa}' no es válido (esperaba {sorted(TIPO_FAI)})"
        )

    # DiferímetroDeslizamiento.
    dd = jr.get("DiferímetroDeslizamiento")
    if isinstance(dd, dict):
        if "InicioTemporizador" in dd:
            check_reference(
                errors,
                f"{path}/DiferímetroDeslizamiento/InicioTemporizador",
                dd["InicioTemporizador"],
                estacion,
                "Secciones",
            )
        val = dd.get("Valor")
        if val is not None and (isinstance(val, bool) or not isinstance(val, (int, float))):
            errors.append(f"{path}/DiferímetroDeslizamiento/Valor: debe ser un número")


def ruta_command_id(estacion, jr):
    """Id de mando de una ruta tal como lo construye src/ruta.h."""
    if not isinstance(jr, dict):
        return None
    tipo = jr.get("Tipo")
    ertms = bool(jr.get("ERTMS", False))
    inicio = jr.get("Inicio")
    destino = jr.get("Destino")
    if tipo not in TIPO_MOVIMIENTO_RUTA or not inicio or not destino:
        return None
    if tipo == "Itinerario":
        prefix = RUTA_ID_PREFIXES[("Itinerario", ertms)]
    else:
        prefix = RUTA_ID_PREFIXES[tipo]
    return f"{prefix} {estacion} {inicio} {destino}"


def validate_servicio_intermitente(errors, path, si, current_dep):
    if not isinstance(si, dict):
        errors.append(f"{path}: debe ser un objeto")
        return
    if "Activo" in si and not isinstance(si["Activo"], bool):
        errors.append(f"{path}/Activo: debe ser booleano")

    ids_ruta = registry_global()[current_dep].get("IdsRuta", set())
    for field, descr in (("FAI", "rutas FAI"), ("ItinerariosApertura", "itinerarios de apertura")):
        arr = si.get(field)
        if not isinstance(arr, list):
            continue
        for i, rid in enumerate(arr):
            ipath = f"{path}/{field}[{i}]"
            if not isinstance(rid, str) or rid == "":
                errors.append(f"{ipath}: id de ruta inválido")
            elif rid not in ids_ruta:
                errors.append(
                    f"{ipath}: '{rid}' no coincide con ninguna ruta definida "
                    f"({sorted(ids_ruta)})"
                )

    if "PosiciónAparatos" in si:
        check_positions_map(
            errors, f"{path}/PosiciónAparatos", si["PosiciónAparatos"], current_dep
        )

    # Secciones: mapa sección -> [entrada, salida].
    secs = si.get("Secciones")
    if isinstance(secs, dict):
        for sid, pair in secs.items():
            spath = f"{path}/Secciones/{sid}"
            check_reference(errors, spath + "/[clave]", sid, current_dep, "Secciones")
            if not isinstance(pair, list) or len(pair) != 2:
                errors.append(f"{spath}: debe ser [entrada, salida]")

    sa = si.get("SeñalesAbiertas")
    if isinstance(sa, dict):
        for sid, cfg in sa.items():
            spath = f"{path}/SeñalesAbiertas/{sid}"
            check_reference(errors, spath + "/[clave]", sid, current_dep, "Señales")
            if not isinstance(cfg, dict):
                errors.append(f"{spath}: debe ser un objeto")
                continue
            for field in ("RutaNecesaria", "Desbloqueo", "BloqueoReceptor"):
                if field in cfg and not isinstance(cfg[field], bool):
                    errors.append(f"{spath}/{field}: debe ser booleano")


def validate_dependency(errors, dep_name, jdep):
    reg = registry_global()

    # Controlada (opcional, booleana).
    if "Controlada" in jdep and not isinstance(jdep["Controlada"], bool):
        errors.append(f"Dependencias/{dep_name}/Controlada: debe ser booleano")

    for ns in ("CVs", "Secciones", "PNs", "Señales"):
        obj = jdep.get(ns, {})
        if obj is not None and not isinstance(obj, dict):
            errors.append(f"Dependencias/{dep_name}/{ns}: debe ser un objeto")
            continue
        for ident, val in (obj or {}).items():
            path = f"Dependencias/{dep_name}/{ns}/{ident}"
            if ns == "CVs":
                validate_cv(errors, path, val)
            elif ns == "Secciones":
                validate_seccion(errors, path, val, dep_name)
            elif ns == "PNs":
                validate_pn(errors, path, val, dep_name)
            elif ns == "Señales":
                validate_senal(errors, path, val, dep_name)

    # Bloqueos (array).
    bloqs = jdep.get("Bloqueos", [])
    if bloqs is not None and not isinstance(bloqs, list):
        errors.append(f"Dependencias/{dep_name}/Bloqueos: debe ser una lista")
    else:
        for i, jb in enumerate(bloqs or []):
            validate_bloqueo(errors, f"Dependencias/{dep_name}/Bloqueos[{i}]", jb, dep_name)

    # DestinosRuta.
    dests = jdep.get("DestinosRuta", {})
    if dests is not None and not isinstance(dests, dict):
        errors.append(f"Dependencias/{dep_name}/DestinosRuta: debe ser un objeto")
    else:
        for ident, jdest in (dests or {}).items():
            validate_destino_ruta(
                errors,
                f"Dependencias/{dep_name}/DestinosRuta/{ident}",
                ident,
                jdest,
                dep_name,
            )

    # Rutas.
    rutas = jdep.get("Rutas", [])
    if rutas is not None and not isinstance(rutas, list):
        errors.append(f"Dependencias/{dep_name}/Rutas: debe ser una lista")
    else:
        for i, jr in enumerate(rutas or []):
            validate_ruta(
                errors,
                f"Dependencias/{dep_name}/Rutas[{i}]",
                jr,
                dep_name,
                reg,
            )

    # ServicioIntermitente.
    if "ServicioIntermitente" in jdep:
        validate_servicio_intermitente(
            errors,
            f"Dependencias/{dep_name}/ServicioIntermitente",
            jdep["ServicioIntermitente"],
            dep_name,
        )


# --------------------------------------------------------------------------- #
# Topología: conexiones recíprocas (mantiene y amplía el comportamiento previo)
# --------------------------------------------------------------------------- #

def opposite_side(side):
    return "Par" if side == "Impar" else "Impar"


def virtual_connections(section, registry, current_dep):
    """Devuelve las conexiones efectivas de una sección por lado.
    Para agujas incluye punta (lado opuesto a Lado) y talones (lado de Lado)."""
    if not isinstance(section, dict):
        return {}
    if section.get("Tipo", "").lower() != "aguja":
        return section.get("Conexiones", {})

    result = {"Par": [], "Impar": []}
    side = section.get("Lado")
    if not side or side not in LADO:
        return result

    punta_side = opposite_side(side)
    punta = section.get("SecciónPunta")
    if isinstance(punta, dict):
        ids = [punta["Id"]]
        invs = [punta.get("InvertirParidad", False)]
    elif isinstance(punta, str):
        ids, invs = [punta], [False]
    else:
        ids, invs = [], []
    for ident, invert in zip(ids, invs):
        side_eff = opposite_side(punta_side) if invert else punta_side
        result[side_eff].append(ident)

    talon_side = side
    for talon in section.get("SeccionesTalón", []):
        if isinstance(talon, dict):
            ident = talon["Id"]
            invert = talon.get("InvertirParidad", False)
        elif isinstance(talon, str):
            ident, invert = talon, False
        else:
            continue
        side_eff = opposite_side(talon_side) if invert else talon_side
        result[side_eff].append(ident)

    return result


def has_connection(section, registry, side, dependency, section_id):
    for conexion in virtual_connections(section, registry, dependency).get(side, []):
        ident = (
            conexion.get("Id")
            if isinstance(conexion, dict)
            else conexion
        )
        if not isinstance(ident, str):
            continue
        dep, sid = split_reference(ident, dependency)
        if dep == dependency and sid == section_id:
            return True
    return False


def validate_topology(data):
    errors = []
    dependencies = data.get("Dependencias", {})
    reg = registry_global()

    for dependency, dep_data in dependencies.items():
        sections = dep_data.get("Secciones", {})

        for section_id, section in sections.items():
            path = f"Dependencias/{dependency}/Secciones/{section_id}"

            conns = virtual_connections(section, reg, dependency)
            if not isinstance(conns, dict):
                continue

            for side, connection_list in conns.items():
                if side not in LADO or not isinstance(connection_list, list):
                    continue
                for conexion in connection_list:
                    # Una conexión puede ser un texto o un objeto {Id: ...}.
                    ident = (
                        conexion.get("Id")
                        if isinstance(conexion, dict)
                        else conexion
                    )
                    if not isinstance(ident, str) or ident == "":
                        continue
                    target_dep, target_id = split_reference(ident, dependency)
                    target_sections = dependencies.get(target_dep, {}).get("Secciones", {})
                    if not isinstance(target_sections, dict):
                        target_section = None
                    else:
                        target_section = target_sections.get(target_id)

                    if not isinstance(target_section, dict):
                        # La referencia a una sección inexistente ya la
                        # señala validate_seccion; aquí solo comprobamos el vínculo.
                        continue

                    opp_side = opposite_side(side)
                    if not has_connection(
                        target_section,
                        reg,
                        opp_side,
                        dependency,
                        section_id,
                    ):
                        errors.append(
                            f"{path}: conexión {side} hacia {ident} no tiene "
                            f"conexión recíproca {opp_side}"
                        )

    return errors


# --------------------------------------------------------------------------- #
# Estructura superior
# --------------------------------------------------------------------------- #

def validate_top_level(errors, data):
    # MQTT (main.cpp accede directamente a j["MQTT"]).
    mqtt = data.get("MQTT")
    if not isinstance(mqtt, dict):
        errors.append("MQTT: el objeto de configuración es obligatorio")
        return
    for field in ("Name", "Host"):
        val = mqtt.get(field)
        if not isinstance(val, str) or val == "":
            errors.append(f"MQTT/{field}: campo obligatorio (cadena no vacía)")

    # ParámetrosPredeterminados (main.cpp accede directamente).
    params = data.get("ParámetrosPredeterminados")
    if not isinstance(params, dict):
        errors.append(
            "ParámetrosPredeterminados: el objeto es obligatorio "
            "(puede estar vacío)"
        )
        return
    for field, (kind, _) in PARAMETROS.items():
        if field not in params:
            continue
        val = params[field]
        p = f"ParámetrosPredeterminados/{field}"
        if isinstance(kind, set):
            if val not in kind:
                errors.append(f"{p}: '{val}' no es un valor válido ({sorted(kind)})")
        elif kind == "bool":
            check_bool(errors, p, val)
        elif kind == "number":
            if isinstance(val, bool) or not isinstance(val, (int, float)):
                errors.append(f"{p}: debe ser un número")


# --------------------------------------------------------------------------- #
# Entrada
# --------------------------------------------------------------------------- #

def main():
    global _REGISTRY

    if len(sys.argv) != 2:
        print("Usage: python validar_config.py config.json")
        sys.exit(1)

    filename = Path(sys.argv[1])

    try:
        with filename.open("r", encoding="utf-8") as f:
            data = json.load(f)
    except Exception as e:
        print(f"JSON error: {e}")
        sys.exit(1)

    if not isinstance(data, dict):
        print("El documento raíz debe ser un objeto JSON.")
        sys.exit(1)

    errors = []

    validate_top_level(errors, data)

    _REGISTRY = build_registry(data)
    registry_global().update(_REGISTRY)

    deps = data.get("Dependencias", {})
    if not isinstance(deps, dict):
        if "Dependencias" in data:
            errors.append("Dependencias: debe ser un objeto")
        deps = {}

    for dep_name, jdep in deps.items():
        if not isinstance(jdep, dict):
            errors.append(f"Dependencias/{dep_name}: debe ser un objeto")
            continue
        validate_dependency(errors, dep_name, jdep)

    connection_errors = validate_topology(data)
    errors.extend(connection_errors)

    if errors:
        print(f"Found {len(errors)} error(s):")
        for error in sorted(set(errors)):
            print(" -", error)
        sys.exit(1)

    print("Validation successful.")


if __name__ == "__main__":
    main()
