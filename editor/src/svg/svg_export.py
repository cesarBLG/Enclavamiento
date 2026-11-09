"""Exportación del modelo geométrico del editor a SVG."""

from math import cos, degrees, pi, radians, sin
from operator import invert
from pathlib import Path
import sys
from typing import Iterable
from xml.etree import ElementTree

from rich_click import group
from tomlkit import item

from items import Aguja, CvLineal, Item, Point, Señal, Topera
from label_positions import (
	SIGNAL_ICON_HEIGHT, SIGNAL_ICON_WIDTH, label_anchor, label_angle,
	label_background_width,
)


INKEX_UTILS_PATH = Path(__file__).resolve().parent / "inkex-utils"
if str(INKEX_UTILS_PATH) not in sys.path:
	sys.path.insert(0, str(INKEX_UTILS_PATH))
from svg.generate_track import generate_track_svg  # pyright: ignore[reportMissingImports]
from svg.generate_junction import generate_junction  # pyright: ignore[reportMissingImports]


class Symbol:
	def __init__(self, path: Path, label: str):
		root = ElementTree.parse(path).getroot()
		self.label = label
		self.width = float(root.attrib["width"])
		self.height = float(root.attrib["height"])
		svg_namespace = "http://www.w3.org/2000/svg"
		ElementTree.register_namespace("", svg_namespace)
		group = root.find(f".//svg:g[@inkscape:label='{label}']", {
			"svg": svg_namespace,
			"inkscape": "http://www.inkscape.org/namespaces/inkscape",
		})
		self.symbol = ElementTree.Element("{http://www.w3.org/2000/svg}symbol", {
			"id": label
		})
		self.symbol.append(group)
	def use(self):
		return ElementTree.Element("{http://www.w3.org/2000/svg}use", {
			"{http://www.w3.org/1999/xlink}href": f"#{self.label}"})

simbolos = {
	label: Symbol(Path(__file__).resolve().parent.parent.parent / "symbols" / f"{label}.svg", label)
	for label in ("señal", "bloqueoVia", "bloqueoDestino", "IMV", "FMV", "bloqueo")
}


def _track_group(item: CvLineal):
	group = generate_track_svg([point.get_rotated(item.rotation) for point in item.points], name=item.id.id)
	group.set("transform", f"translate({item.position.x:.15g} {item.position.y:.15g})")
	anchor_x, anchor_y = label_anchor(item)
	label_center = Point(anchor_x - item.position.x, anchor_y - item.position.y)
	angle = label_angle(item)
	_add_item_label(group, item.id.id_corto, label_center.x, label_center.y,
	                angle=angle, top_offset=6)
	block = simbolos["bloqueoDestino"].use()
	block_symbol = simbolos["bloqueoDestino"]
	block_angle = degrees(item.get_linear_angle(item.offset).angle)
	block.set("transform", (
		f"translate({label_center.x:.15g} {label_center.y:.15g}) "
		f"rotate({block_angle:.15g}) "
		f"translate({-block_symbol.width / 2:.15g} {-block_symbol.height / 2:.15g})"
	))
	block.set("{http://www.inkscape.org/namespaces/inkscape}label", "bloqueoDestino")
	group.append(block)
	_add_item_label(group, "*0", label_center.x, label_center.y - 12, angle=angle)
	_add_item_label(group, "AVERÍA", label_center.x, label_center.y - 20, angle=angle)
	return group


def _add_item_label(group, item_id, x, y, width=None, height=14, angle=0, top_offset=0):
	inkscape_namespace = "http://www.inkscape.org/namespaces/inkscape"
	width = width if width is not None else len(item_id) * 7 + 4
	label_group = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		"transform": (
			f"matrix({cos(angle):.15g} {sin(angle):.15g} "
			f"{-sin(angle):.15g} {cos(angle):.15g} {x:.15g} {y:.15g})"
		),
		f"{{{inkscape_namespace}}}label": "etiquetaNombre",
	})
	background = ElementTree.Element("{http://www.w3.org/2000/svg}rect", {
		"x": f"{-width / 2:.15g}",
		"y": f"{top_offset:.15g}",
		"width": f"{width:.15g}",
		"height": f"{height:.15g}",
		"style": "fill:#fff;fill-opacity:0.8",
		f"{{{inkscape_namespace}}}label": "fondoNombre",
	})
	text = ElementTree.Element("{http://www.w3.org/2000/svg}text", {
		"x": "0",
		"y": f"{top_offset + height / 2:.15g}",
		"text-anchor": "middle",
		"dominant-baseline": "middle",
		f"{{{inkscape_namespace}}}label": "nombre",
	})
	text.text = item_id
	label_group.extend((background, text))
	group.append(label_group)


def _junction_group(item: Aguja):
	group = ElementTree.Element("{http://www.w3.org/2000/svg}g")
	group.set("{http://www.inkscape.org/namespaces/inkscape}label", item.id.id)
	invert = item.points_invertida[-1][1] < 0
	center = item.points_punta[0]
	group.set("transform", f"translate({item.position.x:.15g} {item.position.y:.15g})")
	transform = f"rotate({degrees(item.rotation.angle):g}) translate({(center.x):g} {(center.y):g})"
	if invert:
		transform += " scale(1,-1)"
	for child in generate_junction(radians(item.angulo), transform=transform):
		group.append(child)
	for points, label in ((item.points_punta, "t1a"),
						 (item.points_normal, "t3Ra"),
						 (item.points_invertida, "t3La")):
		group.append(generate_track_svg([point.get_rotated(item.rotation) for point in points], inicio_recto=label=="t3La", name=label))
	anchor_x, anchor_y = label_anchor(item)
	_add_item_label(group, item.id.id_corto,
	                anchor_x - item.position.x, anchor_y - item.position.y,
	                angle=label_angle(item))
	return group

def _signal_group(item: Señal) -> tuple[ElementTree.Element, list[Point]]:
	normal = Point(sin(item.rotation.angle), -cos(item.rotation.angle))
	foot = item.position + normal * item.offset_pie
	angle = degrees(item.rotation.angle)
	signal = simbolos["señal"]
	use = signal.use()
	use.set("transform", f"translate({foot.x - item.position.x:.15g} {foot.y - item.position.y:.15g}) rotate({angle:.15g}) translate(0 {-signal.height:.15g})")
	use.set("{http://www.inkscape.org/namespaces/inkscape}label", "señal")

	# The symbol is anchored at its lower-left corner before rotation.
	local_points = (Point(0, -signal.height), Point(signal.width, -signal.height),
	                Point(signal.width, 0), Point(0, 0))
	absolute = [foot + point.get_rotated(item.rotation.angle)
	            for point in local_points]
	ux, uy = cos(item.rotation.angle), sin(item.rotation.angle)
	label_x, label_y = label_anchor(item)
	label_x -= item.position.x
	label_y -= item.position.y
	group = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		"transform": f"translate({item.position.x:.15g} {item.position.y:.15g})",
		"{http://www.inkscape.org/namespaces/inkscape}label": item.id.id,
	})
	group.append(use)
	angle_radians = item.rotation.angle
	label = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		"transform": (
			f"matrix({cos(angle_radians):.15g} {sin(angle_radians):.15g} "
			f"{-sin(angle_radians):.15g} {cos(angle_radians):.15g} "
			f"{label_x:.15g} {label_y:.15g})"
		),
		"{http://www.inkscape.org/namespaces/inkscape}label": "etiquetaNombre",
	})
	width = label_background_width(item.id.id_corto)
	background = ElementTree.Element("{http://www.w3.org/2000/svg}rect", {
		"x": "0", "y": f"{-SIGNAL_ICON_HEIGHT / 2:.15g}",
		"width": f"{width:.15g}", "height": f"{SIGNAL_ICON_HEIGHT:.15g}",
		"style": "fill:#fff;fill-opacity:0.8",
		"{http://www.inkscape.org/namespaces/inkscape}label": "fondoNombre",
	})
	text = ElementTree.Element("{http://www.w3.org/2000/svg}text", {
		"x": "0", "y": "0", "text-anchor": "start",
		"dominant-baseline": "middle", "font-size": "12",
		"font-family": "sans-serif",
		"{http://www.inkscape.org/namespaces/inkscape}label": "nombre",
	})
	text.text = item.id.id_corto
	label.extend((background, text))
	group.append(label)
	return group, absolute


def _topera_group(item: Topera):
	angle = item.rotation.angle
	ux, uy = cos(angle), sin(angle)
	anchor_x, anchor_y = label_anchor(item)
	center = Point(anchor_x - item.position.x, anchor_y - item.position.y)
	perpendicular = Point(-uy * 10, ux * 10)
	group = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		"transform": f"translate({item.position.x:g} {item.position.y:g})",
		"{http://www.inkscape.org/namespaces/inkscape}label": item.id.id,
	})
	line = ElementTree.Element("{http://www.w3.org/2000/svg}path", {
		"d": f"M {center.x - perpendicular.x:g} {center.y - perpendicular.y:g} L {center.x + perpendicular.x:g} {center.y + perpendicular.y:g}",
		"style": "fill:none;stroke:#222;stroke-width:5",
	})
	group.append(line)
	_add_item_label(group, item.id.id_corto, center.x, center.y,
	                angle=label_angle(item))
	return group


def export_svg(items: Iterable[Item]) -> str:
	"""Genera un SVG con los tramos lineales contenidos en ``items``.

	Los puntos de cada ``CvLineal`` se transforman desde sus coordenadas
	locales a las coordenadas del layout antes de formar su ``path``.
	Elementos aún no soportados se omiten.
	"""
	layers = {"CVs": [], "Agujas": [], "Señales": [], "Toperas": []}
	bounds = [None,None,None,None]
	def update_bounds(points):
		if isinstance(points, Point):
			points = [points]
		for point in points:
			nonlocal bounds
			if bounds[0] is None or point.x < bounds[0]:
				bounds[0] = point.x
			if bounds[1] is None or point.y < bounds[1]:
				bounds[1] = point.y
			if bounds[2] is None or point.x > bounds[2]:
				bounds[2] = point.x
			if bounds[3] is None or point.y > bounds[3]:
				bounds[3] = point.y
	for item in items:
		if isinstance(item, Señal):
			group, signal_points = _signal_group(item)
			layers["Señales"].append(group)
			update_bounds(signal_points)
		elif isinstance(item, Topera):
			layers["Toperas"].append(_topera_group(item))
			angle = item.rotation.angle
			center = item.position + Point(cos(angle) * 2, sin(angle) * 2)
			perpendicular = Point(-sin(angle) * 10, cos(angle) * 10)
			update_bounds([center - perpendicular, center + perpendicular])
		elif isinstance(item, Aguja):
			absolute = [item.position + point.get_rotated(item.rotation)
			            for point in (item.points_punta + item.points_normal[1:]
			                          + item.points_invertida[1:])]
			update_bounds(absolute)
			layers["Agujas"].append(_junction_group(item))
		elif isinstance(item, CvLineal):
			absolute = [item.position + point.get_rotated(item.rotation) for point in item.points]
			update_bounds(absolute)
			layers["CVs"].append(_track_group(item))

	if bounds[0]:
		min_x, min_y, max_x, max_y = bounds
		width = max_x - min_x or 1
		height = max_y - min_y or 1
		view_box = f"{min_x:g} {min_y:g} {width:g} {height:g}"
	else:
		view_box = "0 0 1 1"
	svg = ElementTree.Element("{http://www.w3.org/2000/svg}svg", {
		"viewBox": view_box
	})
	defs = ElementTree.Element("{http://www.w3.org/2000/svg}defs")
	for s in simbolos.values():
		defs.append(s.symbol)
	svg.append(defs)
	for layer_name, elements in layers.items():
		layer_group = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
			"{http://www.inkscape.org/namespaces/inkscape}label": layer_name
		})
		for element in elements:
			layer_group.append(element)
		svg.append(layer_group)
	return ElementTree.tostring(svg, encoding="unicode", method="xml")
