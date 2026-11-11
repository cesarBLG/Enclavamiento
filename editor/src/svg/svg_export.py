"""Exportación del modelo geométrico del editor a SVG."""

from math import cos, degrees, hypot, pi, radians, sin
from operator import invert
from pathlib import Path
import sys
from typing import Iterable
from xml.etree import ElementTree

from rich_click import group
from tomlkit import item

from export import get_conexiones
from items import Aguja, Cruzamiento, CvLineal, Item, Point, Señal, SeccionVia, Topera
from label_positions import (
	SIGNAL_ICON_HEIGHT, SIGNAL_ICON_WIDTH, label_anchor, label_angle,
	label_background_width, label_text_anchor,
)


INKEX_UTILS_PATH = Path(__file__).resolve().parent / "inkex-utils"
if str(INKEX_UTILS_PATH) not in sys.path:
	sys.path.insert(0, str(INKEX_UTILS_PATH))
from svg.generate_track import generate_track_svg  # pyright: ignore[reportMissingImports]
from svg.generate_junction import generate_junction  # pyright: ignore[reportMissingImports]
from svg.generate_travesia import generate_travesia  # pyright: ignore[reportMissingImports]
from svg.generate_crossing import generate_crossing  # pyright: ignore[reportMissingImports]


def _aguja_label_offset(item: Aguja):
	inverted_start = item.position + item.points_invertida[0].get_rotated(item.rotation)
	normal_start = item.position + item.points_normal[0].get_rotated(item.rotation)
	return 5 if inverted_start.y < normal_start.y else -19


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
	for label in ("signal", "bloqueoVia", "bloqueoDestino", "IMV", "FMV", "bloqueo")
}


def _shorten_last_segment(points, from_start=False):
	points = list(points)
	if len(points) < 2:
		return points
	endpoint_index = 0 if from_start else -1
	previous_index = 1 if from_start else -2
	endpoint = points[endpoint_index]
	previous = points[previous_index]
	length = hypot(endpoint.x - previous.x, endpoint.y - previous.y)
	if length == 0:
		return points
	distance = min(2.5, length-0.5)
	points[endpoint_index] = endpoint + (previous - endpoint) * (distance / length)
	return points


def _points_for_ports(item, connections):
	if isinstance(item, CvLineal):
		points = list(item.points)
		circuit = item.get_cv()
		for port, (connected, _) in enumerate(connections):
			if connected is not None and circuit != connected.get_cv():
				points = _shorten_last_segment(points, from_start=port == 0)
		return {0: points, 1: points}
	if isinstance(item, Cruzamiento):
		paths = {
			0: item.points_p1, 1: item.points_p2,
			2: item.points_p3, 3: item.points_p4,
		}
	else:
		paths = {
			0: item.points_normal,
			1: item.points_invertida,
			2: item.points_punta,
		}
	result = {port: list(points) for port, points in paths.items()}
	circuit = item.get_cv()
	for port, (connected, _) in enumerate(connections):
		if connected is not None and circuit != connected.get_cv():
			result[port] = _shorten_last_segment(result[port])
	return result


def _track_group(item: CvLineal, points=None):
	points = item.points if points is None else points
	group = generate_track_svg([point.get_rotated(item.rotation) for point in points], name=item.id.id)
	group.set("transform", f"translate({item.position.x:.15g} {item.position.y:.15g})")
	anchor_x, anchor_y = label_anchor(item)
	label_center = Point(anchor_x - item.position.x, anchor_y - item.position.y)
	angle = label_angle(item)
	_add_item_label(group, item.id.id_corto, label_center.x, label_center.y,
	                angle=angle, top_offset=6)
	block = simbolos["bloqueoVia"].use()
	block_symbol = simbolos["bloqueoVia"]
	block_angle = degrees(item.get_linear_angle(item.offset).angle)
	block.set("transform", (
		f"translate({label_center.x:.15g} {label_center.y:.15g}) "
		f"rotate({block_angle:.15g}) "
		f"translate({-block_symbol.width / 2:.15g} {-block_symbol.height / 2:.15g})"
	))
	block.set("{http://www.inkscape.org/namespaces/inkscape}label", "bv")
	group.append(block)
	_add_item_label_group(group, "*0", label_center.x, label_center.y,
	                      label="numerador", angle=angle, top_offset=-7)
	_add_item_label_group(group, "AVERÍA", label_center.x, label_center.y,
	                      label="averia", angle=angle, top_offset=-7)
	return group


def _add_item_label(group, item_id, x, y, width=None, height=14, angle=0, top_offset=0):
	inkscape_namespace = "http://www.inkscape.org/namespaces/inkscape"
	width = width if width is not None else len(item_id) * 7 + 4
	transform = (
		f"matrix({cos(angle):.15g} {sin(angle):.15g} "
		f"{-sin(angle):.15g} {cos(angle):.15g} {x:.15g} {y:.15g})"
	)
	background = ElementTree.Element("{http://www.w3.org/2000/svg}rect", {
		"x": f"{-width / 2:.15g}",
		"y": f"{top_offset:.15g}",
		"width": f"{width:.15g}",
		"height": f"{height:.15g}",
		"style": "fill:#fff;fill-opacity:0.8",
		"transform": transform,
		f"{{{inkscape_namespace}}}label": "fondo_nombre",
	})
	text = ElementTree.Element("{http://www.w3.org/2000/svg}text", {
		"x": "0",
		"y": f"{top_offset + height / 2:.15g}",
		"text-anchor": "middle",
		"dominant-baseline": "middle",
		"transform": transform,
		f"{{{inkscape_namespace}}}label": "nombre",
	})
	tspan = ElementTree.SubElement(text, "{http://www.w3.org/2000/svg}tspan")
	tspan.text = item_id
	group.extend((background, text))


def _add_item_label_group(group, item_id, x, y, label, width=None, height=14,
						  angle=0, top_offset=0):
	inkscape_namespace = "http://www.inkscape.org/namespaces/inkscape"
	label_group = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		f"{{{inkscape_namespace}}}label": label,
	})
	_add_item_label(label_group, item_id, x, y, width=width, height=height,
					angle=angle, top_offset=top_offset)
	group.append(label_group)


def _junction_group(item: Aguja, points_by_port=None):
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
	for port, (points, label) in enumerate(((item.points_punta, "t1a"),
									 (item.points_normal, "t3Ra"),
									 (item.points_invertida, "t3La"))):
		if points_by_port is not None:
			points = points_by_port[{0: 2, 1: 0, 2: 1}[port]]
		group.append(generate_track_svg([point.get_rotated(item.rotation) for point in points], inicio_recto=label=="t3La", name=label))
	anchor_x, anchor_y = label_anchor(item)
	_add_item_label(group, item.id.id_corto,
	                anchor_x - item.position.x, anchor_y - item.position.y,
	                angle=label_angle(item),
	               top_offset=_aguja_label_offset(item))
	return group


def _travesia_group(item: Aguja, points_by_port=None):
	group = ElementTree.Element("{http://www.w3.org/2000/svg}g")
	group.set("{http://www.inkscape.org/namespaces/inkscape}label", item.id.id)
	group.set("transform", f"translate({item.position.x:.15g} {item.position.y:.15g})")
	transform = f"rotate({degrees(item.rotation.angle):g})"
	if item.points_invertida[-1][1] < 0:
		transform += " scale(1,-1)"
	for child in generate_travesia(radians(item.angulo), transform=transform):
		group.append(child)
	for port, (points, label) in enumerate(((item.points_normal, "t3Ra"),
									 (item.points_invertida, "t3La"))):
		if points_by_port is not None:
			points = points_by_port[port]
		group.append(generate_track_svg(
			[point.get_rotated(item.rotation) for point in points], name=label,
		))
	anchor_x, anchor_y = label_anchor(item)
	_add_item_label(group, item.id.id_corto,
					anchor_x - item.position.x, anchor_y - item.position.y,
					angle=label_angle(item),
					top_offset=_aguja_label_offset(item))
	return group

def _signal_group(item: Señal) -> tuple[ElementTree.Element, list[Point]]:
	normal = Point(sin(item.rotation.angle), -cos(item.rotation.angle))
	foot = item.position + normal * item.offset_pie
	angle = degrees(item.rotation.angle)
	signal = simbolos["signal"]
	use = signal.use()
	use.set("transform", f"translate({foot.x - item.position.x:.15g} {foot.y - item.position.y:.15g}) rotate({angle:.15g}) translate(0 {-signal.height:.15g})")
	use.set("{http://www.inkscape.org/namespaces/inkscape}label", "objeto")

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
	angle_radians = label_angle(item)
	label_transform = (
		f"matrix({cos(angle_radians):.15g} {sin(angle_radians):.15g} "
		f"{-sin(angle_radians):.15g} {cos(angle_radians):.15g} "
		f"{label_x:.15g} {label_y:.15g})"
	)
	width = label_background_width(item.id.id_corto)
	text_anchor = label_text_anchor(item)
	label_left = -width if text_anchor == "end" else 0
	background = ElementTree.Element("{http://www.w3.org/2000/svg}rect", {
		"x": f"{label_left:.15g}", "y": f"{-SIGNAL_ICON_HEIGHT / 2:.15g}",
		"width": f"{width:.15g}", "height": f"{SIGNAL_ICON_HEIGHT:.15g}",
		"style": "fill:#fff;fill-opacity:0.8",
		"transform": label_transform,
		"{http://www.inkscape.org/namespaces/inkscape}label": "fondo_nombre",
	})
	text = ElementTree.Element("{http://www.w3.org/2000/svg}text", {
		"x": "0", "y": "0", "text-anchor": text_anchor,
		"dominant-baseline": "middle", "font-size": "12",
		"font-family": "sans-serif",
		"transform": label_transform,
		"{http://www.inkscape.org/namespaces/inkscape}label": "nombre",
	})
	tspan = ElementTree.SubElement(text, "{http://www.w3.org/2000/svg}tspan")
	tspan.text = item.id.id_corto
	group.extend((background, text))
	return group, absolute


def _imv_group(item: Señal) -> ElementTree.Element:
	angle = item.rotation.angle
	center = item.position + Point(cos(angle) * 11, sin(angle) * 11)
	imv = simbolos["IMV"]
	use = imv.use()
	use.set("transform", f"translate({center.x - imv.width / 2:.15g} {center.y - imv.height / 2:.15g})")
	use.set("{http://www.inkscape.org/namespaces/inkscape}label", item.id.id)
	return use


def _fmv_group(item: Item) -> ElementTree.Element:
	angle = item.rotation.angle
	center = item.position - Point(cos(angle) * 11, sin(angle) * 11)
	fmv = simbolos["FMV"]
	use = fmv.use()
	use.set("transform", f"translate({center.x - fmv.width / 2:.15g} {center.y - fmv.height / 2:.15g})")
	use.set("{http://www.inkscape.org/namespaces/inkscape}label", "señal")
	group = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		"{http://www.inkscape.org/namespaces/inkscape}label": item.id.id,
	})
	group.append(use)
	return group


def _crossing_group(item: Cruzamiento, points_by_port=None):
	group = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		"transform": (
			f"translate({item.position.x:.15g} {item.position.y:.15g}) "
			f"rotate({degrees(item.rotation.angle):.15g})"
		),
		"{http://www.inkscape.org/namespaces/inkscape}label": item.id.id,
	})
	for child in generate_crossing(radians(item.angulo)):
		group.append(child)
	for port, (points, label) in enumerate((
		(item.points_p1, "p1"), (item.points_p2, "p2"),
		(item.points_p3, "p3"), (item.points_p4, "p4"),
	)):
		if points_by_port is not None:
			points = points_by_port[port]
		group.append(generate_track_svg(
			[point for point in points], name=label,
		))
	_add_item_label(group, item.id.id_corto, 0, 0)
	return group


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
	items = list(items)
	secciones = [item for item in items if isinstance(item, SeccionVia)]
	connections = {id(item): get_conexiones(item, secciones) for item in secciones}
	layers = {"CV": [], "Aguja": [], "Cruzamientos": [], "Señal": [], "IMV": [], "FMV": [], "Destino": []}
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
			layers["Señal"].append(group)
			if item.inicio_ruta:
				layers["IMV"].append(_imv_group(item))
			if item.final_ruta:
				layers["FMV"].append(_fmv_group(item))
			update_bounds(signal_points)
		elif isinstance(item, Topera):
			layers["Destino"].append(_topera_group(item))
			layers["FMV"].append(_fmv_group(item))
			angle = item.rotation.angle
			center = item.position + Point(cos(angle) * 2, sin(angle) * 2)
			perpendicular = Point(-sin(angle) * 10, cos(angle) * 10)
			update_bounds([center - perpendicular, center + perpendicular])
		elif isinstance(item, Aguja):
			absolute = [item.position]
			absolute.extend(
				item.position + point.get_rotated(item.rotation)
				for points in (item.points_normal, item.points_invertida)
				for point in points[1:]
			)
			update_bounds(absolute)
			points_by_port = _points_for_ports(item, connections[id(item)])
			layers["Aguja"].append(_travesia_group(
				item, points_by_port) if item.is_travesia else _junction_group(
				item, points_by_port))
		elif isinstance(item, Cruzamiento):
			absolute = [item.position + point.get_rotated(item.rotation)
			            for points in (item.points_p1, item.points_p2,
			                           item.points_p3, item.points_p4)
			            for point in points]
			update_bounds(absolute)
			layers["Cruzamientos"].append(
				_crossing_group(item, _points_for_ports(item, connections[id(item)])))
		elif isinstance(item, CvLineal):
			absolute = [item.position + point.get_rotated(item.rotation) for point in item.points]
			update_bounds(absolute)
			layers["CV"].append(_track_group(
				item, _points_for_ports(item, connections[id(item)])[0]))

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
			"{http://www.inkscape.org/namespaces/inkscape}label": layer_name,
			"{http://www.inkscape.org/namespaces/inkscape}groupmode": "layer",
		})
		for element in elements:
			layer_group.append(element)
		svg.append(layer_group)
	return ElementTree.tostring(svg, encoding="unicode", method="xml")
