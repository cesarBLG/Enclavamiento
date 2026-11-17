"""Exportación del modelo geométrico del editor a SVG."""

from math import cos, degrees, hypot, pi, radians, sin
from copy import deepcopy
from operator import invert
from pathlib import Path
import sys
from typing import Iterable
from xml.etree import ElementTree

from rich_click import group
from tomlkit import item

from export import get_conexiones
from items import Aguja, Bloqueo, Cruzamiento, CvLineal, Item, Point, Señal, SeccionVia, Topera
from label_positions import (
	LABEL_FONT_SIZE, LABEL_SIDE_PADDING, SIGNAL_ICON_HEIGHT, SIGNAL_ICON_WIDTH,
	label_anchor, label_angle, label_background_width, label_text_anchor,
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
		if group is None:
			group = root.find(f".//*[@inkscape:label='{label}']", {
				"inkscape": "http://www.inkscape.org/namespaces/inkscape",
			})
		self.symbol = ElementTree.Element("{http://www.w3.org/2000/svg}symbol", {
			"id": label,
			"width": f"{self.width:.15g}",
			"height": f"{self.height:.15g}",
			"viewBox": f"0 0 {self.width:.15g} {self.height:.15g}",
			"overflow": "hidden",
		})
		self.symbol.append(group)
	def use(self):
		return ElementTree.Element("{http://www.w3.org/2000/svg}use", {
			"{http://www.w3.org/1999/xlink}href": f"#{self.label}",
			"width": f"{self.width:.15g}",
			"height": f"{self.height:.15g}",
		})

simbolos = {
	label: Symbol(Path(__file__).resolve().parent.parent.parent / "symbols" / f"{label}.svg", label)
	for label in ("signal", "bloqueoVia", "IMV", "FMV", "bloqueo",
	             "csb", "prohibirBloqueo")
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


def _track_group(item: CvLineal, points=None, *, include_label=True):
	points = item.points if points is None else points
	group = generate_track_svg([point.get_rotated(item.rotation) for point in points], name=item.id.id)
	group.set("transform", f"translate({item.position.x:.15g} {item.position.y:.15g})")
	anchor_x, anchor_y = label_anchor(item)
	label_center = Point(anchor_x - item.position.x, anchor_y - item.position.y)
	angle = label_angle(item)
	if include_label:
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
	if include_label:
		_add_item_label_group(group, "*0", label_center.x, label_center.y,
		                      label="numerador", angle=angle, top_offset=-7)
		_add_item_label_group(group, "AVERÍA", label_center.x, label_center.y,
		                      label="averia", angle=angle, top_offset=-7)
	return group


def _add_item_label(group, item_id, x, y, width=None, height=14, angle=0, top_offset=0,
					font_size=LABEL_FONT_SIZE):
	inkscape_namespace = "http://www.inkscape.org/namespaces/inkscape"
	width = width if width is not None else label_background_width(item_id)
	transform = (
		f"matrix({cos(angle):.15g} {sin(angle):.15g} "
		f"{-sin(angle):.15g} {cos(angle):.15g} {x:.15g} {y:.15g})"
	)
	background = ElementTree.Element("{http://www.w3.org/2000/svg}rect", {
		"x": f"{-width / 2:.15g}",
		"y": f"{top_offset:.15g}",
		"width": f"{width:.15g}",
		"height": f"{height:.15g}",
		"style": "fill:#fff",
		"transform": transform,
		f"{{{inkscape_namespace}}}label": "fondo_nombre",
	})
	text = ElementTree.Element("{http://www.w3.org/2000/svg}text", {
		"x": "0",
		"y": f"{top_offset + height / 2:.15g}",
		"text-anchor": "middle",
		"dominant-baseline": "middle",
		"font-size": str(font_size),
		"font-family": "sans-serif",
		"textLength": f"{width - LABEL_SIDE_PADDING:.15g}",
		"lengthAdjust": "spacingAndGlyphs",
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


def _junction_group(item: Aguja, points_by_port=None, *, include_label=True):
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
	if include_label:
		anchor_x, anchor_y = label_anchor(item)
		_add_item_label(group, item.id.id_corto,
		                anchor_x - item.position.x, anchor_y - item.position.y,
		                angle=label_angle(item),
		                top_offset=_aguja_label_offset(item))
	return group


def _travesia_group(item: Aguja, points_by_port=None, *, include_label=True):
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
	if include_label:
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
		"style": "fill:#fff",
		"transform": label_transform,
		"{http://www.inkscape.org/namespaces/inkscape}label": "fondo_nombre",
	})
	text = ElementTree.Element("{http://www.w3.org/2000/svg}text", {
		"x": "0", "y": "0", "text-anchor": text_anchor,
		"dominant-baseline": "middle", "font-size": str(LABEL_FONT_SIZE),
		"font-family": "sans-serif",
		"textLength": f"{width - LABEL_SIDE_PADDING:.15g}",
		"lengthAdjust": "spacingAndGlyphs",
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


def _bloqueo_group(item: Bloqueo, cv: CvLineal | None,
				   secciones=None) -> tuple[ElementTree.Element, ElementTree.Element, ElementTree.Element]:
	inkscape_namespace = "http://www.inkscape.org/namespaces/inkscape"
	link_namespace = "http://www.w3.org/1999/xlink"
	svg_rotation = degrees(item.rotation.angle)
	display_side = item.display_side()
	side = display_side
	group = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		"transform": (
			f"translate({item.position.x:.15g} {item.position.y:.15g}) "
			f"rotate({svg_rotation:.15g})"
		),
		f"{{{inkscape_namespace}}}label": item.id.id,
	})
	destination = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		"transform": (
			f"translate({item.position.x:.15g} {item.position.y:.15g}) "
			f"rotate({degrees(item.rotation.angle):.15g})"
		),
		f"{{{inkscape_namespace}}}label": item.id.id,
	})
	fmv_group = ElementTree.Element("{http://www.w3.org/2000/svg}g", {
		"transform": (
			f"translate({item.position.x:.15g} {item.position.y:.15g}) "
			f"rotate({degrees(item.rotation.angle):.15g})"
		),
		f"{{{inkscape_namespace}}}label": item.id.id,
	})
	label_rotation = label_angle(item)
	label_top_offset = 0 if display_side * cos(
		label_rotation - item.rotation.angle
	) > 0 else -18
	label_center_local = display_side * (
		75 - simbolos["bloqueo"].height / 2 - 5 - 18 / 2
	)
	label_anchor = Point(0, label_center_local) - Point(
		0, label_top_offset + 9
	).get_rotated(label_rotation - item.rotation.angle)
	_add_item_label(
		destination,
		item.id.id_corto,
		label_anchor.x,
		label_anchor.y,
		width=label_background_width(item.id.id_corto) * 16 / LABEL_FONT_SIZE,
		height=18,
		angle=label_rotation - item.rotation.angle,
		top_offset=label_top_offset,
		font_size=16,
	)

	def append_symbol(name, x, y, label, rotation=0, center=True):
		symbol = simbolos[name]
		use = symbol.use()
		if center:
			transform = (
				f"translate({x - symbol.width / 2:.15g} {y - symbol.height / 2:.15g}) "
				f"rotate({rotation:.15g} {symbol.width / 2:.15g} {symbol.height / 2:.15g})"
			)
		else:
			transform = (
				f"translate({x:.15g} {y - symbol.height / 2:.15g}) "
				f"rotate({rotation:.15g} 0 {symbol.height / 2:.15g})"
			)
		use.set("transform", transform)
		use.set(f"{{{inkscape_namespace}}}label", label)
		group.append(use)

	label_width = label_background_width(item.id.id_corto)
	actc_width = 32
	actc_x = label_width / 2 + actc_width / 2 + 5
	actc_angle = degrees(label_rotation - item.rotation.angle)
	actc_text = ElementTree.Element("{http://www.w3.org/2000/svg}text", {
		"x": "0",
		"y": "0",
		"text-anchor": "middle",
		"dominant-baseline": "middle",
		"font-size": str(LABEL_FONT_SIZE),
		"font-family": "sans-serif",
		"fill": "#00aa00",
		"transform": (
			f"translate({actc_x:.15g} {side * (
				75 - simbolos["bloqueo"].height / 2 - 5 - 18 / 2
			):.15g}) "
			f"rotate({actc_angle:.15g})"
		),
		f"{{{inkscape_namespace}}}label": "actc",
	})
	ElementTree.SubElement(actc_text, "{http://www.w3.org/2000/svg}tspan").text = "A/CTC"
	group.append(actc_text)
	append_symbol("bloqueo", 0, side * 75, "principal")
	lock_half_width = simbolos["bloqueo"].width / 2
	csb_gap = 5
	append_symbol("csb", -lock_half_width - csb_gap, side * 75, "csbPropio", 180, center=False)
	append_symbol("csb", lock_half_width + csb_gap, side * 75, "csbColateral", center=False)
	append_symbol("prohibirBloqueo", 0, side * (75 + simbolos["bloqueo"].height / 2 + simbolos["prohibirBloqueo"].height / 2 + 5), "prohibir")

	if cv is None:
		return group, destination, fmv_group
	port = next(
		(index for index, (position, angle) in enumerate(cv.get_outs())
		 if position.close_to(item.position) and angle.close_to(item.rotation + pi)),
		None,
	)
	if port is None:
		return group, destination, fmv_group
	other_port = 1 - port
	other_position, other_angle = cv.get_outs()[other_port]
	inside = other_position - Point(cos(other_angle.angle) * 11, sin(other_angle.angle) * 11)
	inside_local = (inside - item.position).get_rotated(-item.rotation.angle)
	neighbor = None
	neighbor_port = None
	if secciones is not None:
		neighbor, neighbor_port = get_conexiones(cv, secciones)[port]
	fmv = simbolos["FMV"]
	fmv_positions = [("trayecto", inside_local)]
	if neighbor is not None:
		neighbor_position, neighbor_angle = neighbor.get_outs()[neighbor_port]
		station_position = neighbor_position - Point(
			cos(neighbor_angle.angle) * 11,
			sin(neighbor_angle.angle) * 11,
		)
		station_local = (station_position - item.position).get_rotated(-item.rotation.angle)
		fmv_positions.append(("estacion", station_local))
	for label, center in fmv_positions:
		use = fmv.use()
		use.set("transform", f"translate({center.x - fmv.width / 2:.15g} {center.y - fmv.height / 2:.15g})")
		use.set(f"{{{inkscape_namespace}}}label", label)
		fmv_group.append(use)
	return group, destination, fmv_group


def _crossing_group(item: Cruzamiento, points_by_port=None, *, include_label=True):
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
	if include_label:
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


def generate_item_svg_components(item: Item, related_items=()):
	"""Devuelve elementos SVG locales agrupados por layer, listos para cachear."""
	if isinstance(item, SeccionVia):
		# Only labels are cached for track sections; their geometry and labels are
		# local to this item, so connection discovery is unnecessary here.
		points = _points_for_ports(item, [])
		if isinstance(item, CvLineal):
			group = _track_group(item, points[0])
			layer = "CV"
		elif isinstance(item, Aguja):
			group = (_travesia_group(item, points) if item.is_travesia
					 else _junction_group(item, points))
			layer = "Aguja"
		else:
			group = _crossing_group(item, points)
			layer = "Cruzamientos"
		labels = ElementTree.Element("{http://www.w3.org/2000/svg}g")
		inkscape_label = "{http://www.inkscape.org/namespaces/inkscape}label"
		def collect_labels(parent):
			for child in parent:
				label = child.attrib.get(inkscape_label)
				if label in {"numerador", "averia"}:
					labels.append(deepcopy(child))
				elif label in {"fondo_nombre", "nombre"}:
					labels.append(deepcopy(child))
				else:
					collect_labels(child)
		collect_labels(group)
		components = {layer: list(labels)}
	else:
		components = {}
		if isinstance(item, Señal):
			group, _ = _signal_group(item)
			components["Señal"] = [group]
			if item.inicio_ruta:
				components["IMV"] = [_imv_group(item)]
			if item.final_ruta:
				components["FMV"] = [_fmv_group(item)]
		elif isinstance(item, Topera):
			components["Destino"] = [_topera_group(item)]
			components["FMV"] = [_fmv_group(item)]
		elif isinstance(item, Bloqueo):
			cv = next((candidate for candidate in related_items
				if isinstance(candidate, CvLineal)
				and candidate.matches(item.position, item.rotation + pi)), None)
			sections = [candidate for candidate in related_items if isinstance(candidate, SeccionVia)]
			lock, destination, fmv = _bloqueo_group(item, cv, sections)
			components.update(Bloqueo=[lock], Destino=[destination])
			if len(fmv):
				components["FMV"] = [fmv]
	position = item.position
	for layer, elements in components.items():
		for element in elements:
			if isinstance(item, SeccionVia):
				continue  # Labels are local children of the section's original group.
			if isinstance(item, Señal):
				if layer == "Señal":
					element.set("transform", "")
				elif layer == "IMV":
					element.set("transform", f"translate({cos(item.rotation.angle) * 11 - simbolos['IMV'].width / 2:.15g} {sin(item.rotation.angle) * 11 - simbolos['IMV'].height / 2:.15g})")
				elif layer == "FMV":
					element[0].set("transform", f"translate({-cos(item.rotation.angle) * 11 - simbolos['FMV'].width / 2:.15g} {-sin(item.rotation.angle) * 11 - simbolos['FMV'].height / 2:.15g})")
			elif isinstance(item, Topera):
				if layer == "Destino":
					element.set("transform", "")
				else:
					element[0].set("transform", f"translate({-cos(item.rotation.angle) * 11 - simbolos['FMV'].width / 2:.15g} {-sin(item.rotation.angle) * 11 - simbolos['FMV'].height / 2:.15g})")
			elif isinstance(item, Bloqueo):
				element.set("transform", f"rotate({degrees(item.rotation.angle):.15g})")
	return components


def _export_cached_components(item, components):
	result = {}
	for layer, elements in components.items():
		result[layer] = [deepcopy(element) for element in elements]
		if isinstance(item, SeccionVia):
			continue
		for component in result[layer]:
			if isinstance(item, (Señal, Topera)):
				if layer == "Señal" or (layer == "Destino" and isinstance(item, Topera)):
					component.set("transform", f"translate({item.position.x:.15g} {item.position.y:.15g})")
				elif layer == "FMV":
					component[0].set("transform", f"translate({item.position.x - cos(item.rotation.angle) * 11 - simbolos['FMV'].width / 2:.15g} {item.position.y - sin(item.rotation.angle) * 11 - simbolos['FMV'].height / 2:.15g})")
				elif layer == "IMV":
					component.set("transform", f"translate({item.position.x + cos(item.rotation.angle) * 11 - simbolos['IMV'].width / 2:.15g} {item.position.y + sin(item.rotation.angle) * 11 - simbolos['IMV'].height / 2:.15g})")
			elif isinstance(item, Bloqueo):
				component.set("transform", f"translate({item.position.x:.15g} {item.position.y:.15g}) rotate({degrees(item.rotation.angle):.15g})")
	return result


def export_svg(items: Iterable[Item]) -> str:
	"""Genera un SVG con los tramos lineales contenidos en ``items``.

	Los puntos de cada ``CvLineal`` se transforman desde sus coordenadas
	locales a las coordenadas del layout antes de formar su ``path``.
	Elementos aún no soportados se omiten.
	"""
	items = list(items)
	secciones = [item for item in items if isinstance(item, SeccionVia)]
	connections = {id(item): get_conexiones(item, secciones) for item in secciones}
	layers = {"CV": [], "Aguja": [], "Cruzamientos": [], "Señal": [], "IMV": [], "FMV": [], "Destino": [], "Bloqueo": []}
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
	def update_rotated_rect(center, width, height, angle=0, x_offset=0, y_offset=0):
		corners = (
			Point(-width / 2 + x_offset, y_offset),
			Point(width / 2 + x_offset, y_offset),
			Point(width / 2 + x_offset, height + y_offset),
			Point(-width / 2 + x_offset, height + y_offset),
		)
		update_bounds(center + corner.get_rotated(angle) for corner in corners)
	def update_label_bounds(item, top_offset=0, height=14, width=None):
		width = label_background_width(item.id.id_corto) if width is None else width
		if isinstance(item, Cruzamiento):
			anchor_x, anchor_y = item.position.x, item.position.y
		else:
			anchor_x, anchor_y = label_anchor(item)
		if isinstance(item, Señal):
			text_anchor = label_text_anchor(item)
			x_offset = -width if text_anchor == "end" else 0
			update_rotated_rect(
				Point(anchor_x, anchor_y), width, height, label_angle(item),
				x_offset=x_offset, y_offset=-height / 2,
			)
		else:
			update_rotated_rect(
				Point(anchor_x, anchor_y), width, height, label_angle(item),
				x_offset=0, y_offset=top_offset,
			)
	for item in items:
		cached = item.generate_svg_components(items)
		if isinstance(item, Bloqueo):
			for layer, elements in _export_cached_components(item, cached).items():
				layers[layer].extend(elements)
			update_bounds((item.position + Point(x, y).get_rotated(item.rotation)
				for x, y in ((-90, -90), (90, -90), (90, 90), (-90, 90))))
		elif isinstance(item, Señal):
			_, signal_points = _signal_group(item)
			for layer, elements in _export_cached_components(item, cached).items():
				layers[layer].extend(elements)
			update_bounds(signal_points)
			update_label_bounds(item, height=SIGNAL_ICON_HEIGHT)
			if item.inicio_ruta:
				angle = item.rotation.angle
				update_rotated_rect(
					item.position + Point(cos(angle) * 11, sin(angle) * 11),
					simbolos["IMV"].width, simbolos["IMV"].height,
				)
			if item.final_ruta:
				angle = item.rotation.angle
				update_rotated_rect(
					item.position - Point(cos(angle) * 11, sin(angle) * 11),
					simbolos["FMV"].width, simbolos["FMV"].height,
				)
		elif isinstance(item, Topera):
			for layer, elements in _export_cached_components(item, cached).items():
				layers[layer].extend(elements)
			angle = item.rotation.angle
			center = item.position + Point(cos(angle) * 2, sin(angle) * 2)
			perpendicular = Point(-sin(angle) * 10, cos(angle) * 10)
			update_bounds([center - perpendicular, center + perpendicular])
			update_label_bounds(item)
			angle = item.rotation.angle
			update_rotated_rect(
				item.position - Point(cos(angle) * 11, sin(angle) * 11),
				simbolos["FMV"].width, simbolos["FMV"].height,
			)
		elif isinstance(item, Aguja):
			absolute = [item.position]
			absolute.extend(
				item.position + point.get_rotated(item.rotation)
				for points in (item.points_normal, item.points_invertida)
				for point in points[1:]
			)
			update_bounds(absolute)
			points_by_port = _points_for_ports(item, connections[id(item)])
			group = (_travesia_group(
				item, points_by_port, include_label=False) if item.is_travesia else _junction_group(
				item, points_by_port, include_label=False))
			group.extend(_export_cached_components(item, cached).get("Aguja", []))
			layers["Aguja"].append(group)
			update_label_bounds(item, top_offset=_aguja_label_offset(item))
		elif isinstance(item, Cruzamiento):
			absolute = [item.position + point.get_rotated(item.rotation)
			            for points in (item.points_p1, item.points_p2,
			                           item.points_p3, item.points_p4)
			            for point in points]
			update_bounds(absolute)
			group = _crossing_group(item, _points_for_ports(item, connections[id(item)]), include_label=False)
			group.extend(_export_cached_components(item, cached).get("Cruzamientos", []))
			layers["Cruzamientos"].append(group)
			update_label_bounds(item)
		elif isinstance(item, CvLineal):
			absolute = [item.position + point.get_rotated(item.rotation) for point in item.points]
			update_bounds(absolute)
			group = _track_group(item, _points_for_ports(item, connections[id(item)])[0], include_label=False)
			group.extend(_export_cached_components(item, cached).get("CV", []))
			layers["CV"].append(group)
			update_label_bounds(item, top_offset=6)
	if bounds[0] is not None:
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
