"""Normalize railway section connections into a common Python representation."""


SIDES = ("Par", "Impar")


def normalize_connection(connection, dependency):
    """Return a connection as {Id, InvertirParidad}, with a qualified Id."""
    if isinstance(connection, str):
        ident = connection
        invert = False
    elif isinstance(connection, dict):
        ident = connection.get("Id")
        invert = connection.get("InvertirParidad", False)
    else:
        return None
    if not isinstance(ident, str) or not ident:
        return None
    if ":" not in ident:
        ident = f"{dependency}:{ident}"
    return {"Id": ident, "InvertirParidad": invert}


def connections_by_side(section, dependency):
    """Return both sides as lists of normalized connection objects.

    Accepts named sides or the positional ``[Par, Impar]`` representation.
    For switches, derives the sides from SecciónPunta and SeccionesTalón.
    """
    result = {side: [] for side in SIDES}
    if not isinstance(section, dict):
        return result

    if section.get("Tipo") == "Aguja":
        side = section.get("Lado")
        if side not in SIDES:
            return result
        opposite = "Impar" if side == "Par" else "Par"
        talones = section.get("SeccionesTalón", [])
        if not isinstance(talones, list):
            talones = [talones]
        punta = section.get("SecciónPunta")
        values = {side: talones, opposite: [] if punta is None else [punta]}
    else:
        values = section.get("Conexiones", {})
        if isinstance(values, list):
            if len(values) != 2:
                return result
            # lados<T>::from_json assigns {j[1], j[0]} to (impar, par).
            values = {"Par": values[0], "Impar": values[1]}
        if not isinstance(values, dict):
            return result

    for side in SIDES:
        value = values.get(side)
        if value is None:
            continue
        if not isinstance(value, list):
            value = [value]
        result[side] = [
            normalized
            for item in value
            if (normalized := normalize_connection(item, dependency)) is not None
        ]
    return result


def local_connection_id(connection, dependency):
    """Return a connection's local ID, or None when it targets another dep."""
    normalized = normalize_connection(connection, dependency)
    if normalized is None:
        return None
    dep, ident = normalized["Id"].split(":", 1)
    return ident if dep == dependency else None
