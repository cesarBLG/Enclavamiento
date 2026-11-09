"""Regression tests keeping canvas label anchors in sync with SVG exports."""

import math
from pathlib import Path
import sys
import unittest
from xml.etree import ElementTree


EDITOR_SRC = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(EDITOR_SRC))

from items import Aguja, Angle, CvLineal, IdElemento, Point, Señal, TipoSeñal, Topera
from label_positions import (
    SIGNAL_ICON_HEIGHT, label_anchor, label_angle, label_background_width,
)
from svg.svg_export import export_svg


SVG_NS = "http://www.w3.org/2000/svg"
INKSCAPE_NS = "http://www.inkscape.org/namespaces/inkscape"


def exported_label_anchor(root, item):
    group = root.find(
        f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='{item.id.id}']"
    )
    if group is None:
        raise AssertionError(f"No se exportó el grupo SVG para {item.id.id}")
    label_group = next((candidate for candidate in group.findall(
        f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='etiquetaNombre']"
    ) if candidate.find(f".//{{{SVG_NS}}}text") is not None
      and candidate.find(f".//{{{SVG_NS}}}text").text == item.id.id_corto), None)
    if label_group is None:
        raise AssertionError(f"No se exportó la etiqueta de {item.id.id}")
    values = [float(value) for value in label_group.get("transform")[7:-1].split()]
    tx = ty = 0.0
    transform = group.get("transform", "")
    if transform.startswith("translate("):
        tx, ty = (float(value) for value in transform[10:-1].replace(",", " ").split())
    return tx + values[4], ty + values[5]


class SvgLabelPositionTests(unittest.TestCase):
    def make_items(self):
        dep = "TEST"
        return [
            CvLineal(
                IdElemento("CV1", dep), Point(20, 40),
                [Point(18, 7), Point(35, 24), Point(58, 15)], Angle(0.41),
            ),
            Aguja(
                IdElemento("A1", dep), Point(120, 60), Angle(0.73),
                punta=[Point(0, 0), Point(22, 0)],
                normal=[Point(0, 0), Point(22, 0)],
                invertida=[Point(0, 0), Point(18, 12)],
                angulo=math.pi / 4,
            ),
            Señal(
                IdElemento("S1", dep), Point(210, 105), TipoSeñal.Entrada,
                offset_pie=8, rotation=Angle(1.17),
            ),
            Topera(IdElemento("F1", dep), Point(300, 160), Angle(2.02)),
        ]

    def test_canvas_label_anchors_match_exported_svg_for_every_item_type(self):
        items = self.make_items()
        root = ElementTree.fromstring(export_svg(items))
        for item in items:
            with self.subTest(kind=type(item).__name__):
                canvas_x, canvas_y = label_anchor(item)
                svg_x, svg_y = exported_label_anchor(root, item)
                self.assertAlmostEqual(canvas_x, svg_x, places=5)
                self.assertAlmostEqual(canvas_y, svg_y, places=5)
                label_group = next(candidate for candidate in root.findall(
                    f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='{item.id.id}']/"
                    f"{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='etiquetaNombre']"
                ) if candidate.find(f".//{{{SVG_NS}}}text").text == item.id.id_corto)
                matrix = [float(value) for value in label_group.get("transform")[7:-1].split()]
                self.assertAlmostEqual(math.cos(label_angle(item)), matrix[0], places=12)
                self.assertAlmostEqual(math.sin(label_angle(item)), matrix[1], places=12)

    def test_signal_id_is_left_aligned_to_the_icon_edge(self):
        for angle in (0, math.pi / 4, math.pi / 2, 1.17):
            item = Señal(
                IdElemento("S1", "TEST"), Point(210, 105), TipoSeñal.Entrada,
                offset_pie=8, rotation=Angle(angle),
            )
            with self.subTest(angle=angle):
                root = ElementTree.fromstring(export_svg([item]))
                label_group = root.find(
                    f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='etiquetaNombre']"
                )
                matrix = [float(value) for value in label_group.get("transform")[7:-1].split()]
                text = label_group.find(
                    f".//{{{SVG_NS}}}text[@{{{INKSCAPE_NS}}}label='nombre']"
                )
                rect = label_group.find(
                    f".//{{{SVG_NS}}}rect[@{{{INKSCAPE_NS}}}label='fondoNombre']"
                )
                self.assertIsNotNone(text)
                self.assertEqual("start", text.get("text-anchor"))
                self.assertEqual("0", text.get("x"))
                self.assertEqual("0", text.get("y"))
                self.assertAlmostEqual(math.cos(angle), matrix[0], places=12)
                self.assertAlmostEqual(math.sin(angle), matrix[1], places=12)
                self.assertAlmostEqual(label_anchor(item)[0] - item.position.x, matrix[4], places=12)
                self.assertAlmostEqual(label_anchor(item)[1] - item.position.y, matrix[5], places=12)
                self.assertEqual(str(label_background_width(item.id.id_corto)), rect.get("width"))
                self.assertAlmostEqual(SIGNAL_ICON_HEIGHT, float(rect.get("height")), places=12)

    def test_cv_fixed_decorations_follow_the_common_linear_offset(self):
        item = self.make_items()[0]
        item.offset = item.length() * 0.25
        root = ElementTree.fromstring(export_svg([item]))
        group = root.find(
            f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='{item.id.id}']"
        )
        texts = {node.text for node in group.findall(f".//{{{SVG_NS}}}text")}
        expected = label_anchor(item)
        tx, ty = (float(v) for v in group.get("transform")[10:-1].split())
        self.assertEqual({"CV1", "*0", "AVERÍA"}, texts)
        for name, dy in (("CV1", 0), ("*0", -12), ("AVERÍA", -20)):
            label_group = next(candidate for candidate in group.findall(
                f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='etiquetaNombre']"
            ) if candidate.find(f".//{{{SVG_NS}}}text").text == name)
            matrix = [float(v) for v in label_group.get("transform")[7:-1].split()]
            self.assertAlmostEqual(expected[0], tx + matrix[4], places=12)
            self.assertAlmostEqual(expected[1] + dy, ty + matrix[5], places=12)
            self.assertAlmostEqual(math.cos(label_angle(item)), matrix[0], places=12)
            self.assertAlmostEqual(math.sin(label_angle(item)), matrix[1], places=12)
        destination_lock = group.find(
            f".//{{{SVG_NS}}}use[@{{{INKSCAPE_NS}}}label='bloqueoDestino']"
        )
        self.assertIsNotNone(destination_lock)
        self.assertIn("bloqueoDestino", destination_lock.get("{http://www.w3.org/1999/xlink}href"))
        lock_x, lock_y = (float(v) for v in destination_lock.get("transform").split("translate(")[1].split(")")[0].split())
        self.assertAlmostEqual(expected[0], tx + lock_x, places=12)
        self.assertAlmostEqual(expected[1], ty + lock_y, places=12)

    def test_legacy_train_distance_is_migrated_to_the_shared_offset(self):
        item = self.make_items()[0]
        data = item.serialize()
        data["distancia_tren"] = item.length() * 0.3
        data.pop("offset")
        restored = CvLineal.deserialize(data, item.id.dep)
        self.assertAlmostEqual(item.length() * 0.3, restored.offset)
        self.assertIn("offset", restored.serialize())
        self.assertNotIn("distancia_tren", restored.serialize())


if __name__ == "__main__":
    unittest.main()
