"""Shared label anchors for the editor canvas and SVG export."""

from math import cos, pi, sin

from items import Aguja, CvLineal, Point, Señal, Topera


# Dimensions declared by editor/symbols/señal.svg (SVG user units).
SIGNAL_ICON_WIDTH = 51.75
SIGNAL_ICON_HEIGHT = 22.5


def signal_foot_position(item):
    normal = Point(sin(item.rotation.angle), -cos(item.rotation.angle))
    return item.position + normal * item.offset_pie


def signal_label_anchor(item):
    """Return the left edge / vertical center of the signal name label."""
    angle = item.rotation.angle
    ux, uy = cos(angle), sin(angle)
    foot = signal_foot_position(item)
    return (
        foot.x + ux * SIGNAL_ICON_WIDTH + uy * SIGNAL_ICON_HEIGHT / 2,
        foot.y + uy * SIGNAL_ICON_WIDTH - ux * SIGNAL_ICON_HEIGHT / 2,
    )


def label_anchor(item):
    """Return the label center, except signal labels use their left edge."""
    if isinstance(item, CvLineal):
        center = item.get_linear_position(item.offset)
        point = item.position + center.get_rotated(item.rotation)
        return point.x, point.y
    if isinstance(item, Aguja):
        return item.position.x, item.position.y
    if isinstance(item, Señal):
        return signal_label_anchor(item)
    if isinstance(item, Topera):
        angle = item.rotation.angle
        return item.position.x + cos(angle) * 2, item.position.y + sin(angle) * 2
    raise TypeError(f"No hay posición de etiqueta para {type(item).__name__}.")


def label_angle(item):
    """Return the local track angle where this item's name is placed."""
    if isinstance(item, CvLineal):
        angle = item.get_linear_angle(item.offset).angle
    else:
        angle = item.rotation.angle
    while angle > pi / 2:
        angle -= pi
    while angle <= -pi / 2:
        angle += pi
    return angle


def label_text_anchor(item):
    """Return the horizontal alignment needed at the label anchor."""
    if not isinstance(item, Señal):
        return "middle"
    raw_angle = item.rotation.angle
    normalized_angle = label_angle(item)
    angle_delta = (raw_angle - normalized_angle) % (2 * pi)
    return "end" if abs(angle_delta - pi) <= 1e-9 else "start"


def label_background_width(text):
    """Shared deterministic SVG/canvas label width, including side padding."""
    return len(text) * 7 + 4
