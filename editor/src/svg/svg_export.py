"""Exportación del modelo geométrico del editor a SVG."""

from math import cos, degrees, pi, radians, sin
from operator import invert
from pathlib import Path
import sys
from typing import Iterable
from xml.etree import ElementTree

from rich_click import group
from tomlkit import item

from items import Aguja, CvLineal, Item, Point, Señal


INKEX_UTILS_PATH = Path(__file__).resolve().parent / "inkex-utils"
if str(INKEX_UTILS_PATH) not in sys.path:
	sys.path.insert(0, str(INKEX_UTILS_PATH))
from svg.generate_track import generate_track_svg  # pyright: ignore[reportMissingImports]
from svg.generate_junction import generate_junction  # pyright: ignore[reportMissingImports]


SIGNAL_SVG_PATH = Path(__file__).resolve().parent.parent.parent / "symbols" / "señal.svg"


def _load_signal_symbol():
	root = ElementTree.parse(SIGNAL_SVG_PATH).getroot()
	width = float(root.attrib["width"])
	height = float(root.attrib["height"])
	svg_namespace = "http://www.w3.org/2000/svg"
	ElementTree.register_namespace("", svg_namespace)
	children = []
	for child in root:
		if child.tag.rsplit("}", 1)[-1] == "namedview":
			continue
		children.append(ElementTree.tostring(child, encoding="unicode"))
	symbol = (f'<symbol id="señal">'
			  f'{"".join(children)}</symbol>')
	return symbol, width, height


SIGNAL_SYMBOL, SIGNAL_WIDTH, SIGNAL_HEIGHT = _load_signal_symbol()


def _absolute_points(item: CvLineal) -> list[Point]:
	"""Devuelve los puntos de un tramo en coordenadas del layout."""
	return [item.position + point.get_rotated(item.rotation)
			for point in item.points]


def _track_group(item: CvLineal):
	return generate_track_svg([point.get_rotated(item.rotation) for point in item.points], name=item.id.id, transform=f"translate({item.position.x:g} {item.position.y:g})")


def _junction_group(item: Aguja):
	group = ElementTree.Element("{http://www.w3.org/2000/svg}g")
	group.set("{http://www.inkscape.org/namespaces/inkscape}label", item.id.id)
	invert = item.points_invertida[-1][1] < 0
	center = item.points_punta[0]
	group.set("transform", f"translate({(item.position.x):g} {item.position.y:g})")
	transform = f"rotate({degrees(item.rotation.angle):g}) translate({(center.x):g} {(center.y):g})"
	if invert:
		transform += " scale(1,-1)"
	for child in generate_junction(radians(item.angulo), transform=transform):
		group.append(child)
	for points, label in ((item.points_punta, "t1a"),
						 (item.points_normal, "t3ra"),
						 (item.points_invertida, "t3la")):
		group.append(generate_track_svg([point.get_rotated(item.rotation) for point in points], inicio_recto=label=="t3la", name=label))
	return group


def _signal_foot(item: Señal) -> Point:
	"""Devuelve la esquina inferior izquierda del símbolo de la señal."""
	normal = Point(sin(item.rotation.angle), -cos(item.rotation.angle))
	return item.position + normal * item.offset_pie


def _signal_use(item: Señal) -> tuple[str, list[Point]]:
	foot = _signal_foot(item)
	angle = degrees(item.rotation.angle)
	use = (f'  <use href="#señal" transform="translate({foot.x:g} {foot.y:g}) '
	       f'rotate({angle:g}) translate(0 {-SIGNAL_HEIGHT:g})" />')

	# The symbol is anchored at its lower-left corner before rotation.
	local_points = (Point(0, -SIGNAL_HEIGHT), Point(SIGNAL_WIDTH, -SIGNAL_HEIGHT),
	                Point(SIGNAL_WIDTH, 0), Point(0, 0))
	absolute = [foot + point.get_rotated(item.rotation.angle)
	            for point in local_points]
	return use, absolute


def export_svg(items: Iterable[Item]) -> str:
	"""Genera un SVG con los tramos lineales contenidos en ``items``.

	Los puntos de cada ``CvLineal`` se transforman desde sus coordenadas
	locales a las coordenadas del layout antes de formar su ``path``.
	Elementos aún no soportados se omiten.
	"""
	track_groups = []
	points = []
	uses = []
	for item in items:
		if isinstance(item, Señal):
			use, signal_points = _signal_use(item)
			uses.append(use)
			points.extend(signal_points)
			continue
		if isinstance(item, Aguja):
			absolute = [item.position + point.get_rotated(item.rotation)
			            for point in (item.points_punta + item.points_normal[1:]
			                          + item.points_invertida[1:])]
			points.extend(absolute)
			track_groups.append(_junction_group(item))
			continue
		if not isinstance(item, CvLineal):
			continue
		absolute = _absolute_points(item)
		if not absolute:
			continue
		points.extend(absolute)
		track_groups.append(_track_group(item))

	if points:
		min_x = min(point.x for point in points)
		min_y = min(point.y for point in points)
		max_x = max(point.x for point in points)
		max_y = max(point.y for point in points)
		width = max_x - min_x or 1
		height = max_y - min_y or 1
		view_box = f"{min_x:g} {min_y:g} {width:g} {height:g}"
	else:
		view_box = "0 0 1 1"

	body = "\n".join(ElementTree.tostring(group, encoding="unicode")
				 for group in track_groups)
	if uses:
		body = f"  <defs>{SIGNAL_SYMBOL}</defs>\n" + body
		body = f"{body}\n" + "\n".join(uses)
	return (f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="{view_box}">\n'
			f'{body}\n</svg>\n')
