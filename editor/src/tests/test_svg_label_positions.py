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
    label_text_anchor,
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
    label_text = next((candidate for candidate in group.findall(
        f".//{{{SVG_NS}}}text[@{{{INKSCAPE_NS}}}label='nombre']"
    ) if candidate.text == item.id.id_corto), None)
    if label_text is None:
        raise AssertionError(f"No se exportó la etiqueta de {item.id.id}")
    transform = label_text.get("transform")
    if transform is None:
        label_group = next(candidate for candidate in group.findall(
            f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='etiquetaNombre']"
        ) if candidate.find(f".//{{{SVG_NS}}}text").text == item.id.id_corto)
        transform = label_group.get("transform")
    values = [float(value) for value in transform[7:-1].split()]
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
                label_text = next(candidate for candidate in root.findall(
                    f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='{item.id.id}']//"
                    f"{{{SVG_NS}}}text[@{{{INKSCAPE_NS}}}label='nombre']"
                ) if candidate.text == item.id.id_corto)
                transform = label_text.get("transform")
                if transform is None:
                    label_group = next(candidate for candidate in root.findall(
                        f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='{item.id.id}']//"
                        f"{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='etiquetaNombre']"
                    ) if candidate.find(f".//{{{SVG_NS}}}text").text == item.id.id_corto)
                    transform = label_group.get("transform")
                matrix = [float(value) for value in transform[7:-1].split()]
                self.assertAlmostEqual(math.cos(label_angle(item)), matrix[0], places=12)
                self.assertAlmostEqual(math.sin(label_angle(item)), matrix[1], places=12)

    def test_signal_id_is_left_aligned_to_the_icon_edge(self):
        for angle in (0, math.pi / 4, math.pi / 2, 1.17, 2.2, -2.2):
            item = Señal(
                IdElemento("S1", "TEST"), Point(210, 105), TipoSeñal.Entrada,
                offset_pie=8, rotation=Angle(angle),
            )
            with self.subTest(angle=angle):
                root = ElementTree.fromstring(export_svg([item]))
                group = root.find(
                    f".//{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='{item.id.id}']"
                )
                text = group.find(
                    f".//{{{SVG_NS}}}text[@{{{INKSCAPE_NS}}}label='nombre']"
                )
                rect = group.find(
                    f".//{{{SVG_NS}}}rect[@{{{INKSCAPE_NS}}}label='fondoNombre']"
                )
                matrix = [float(value) for value in text.get("transform")[7:-1].split()]
                self.assertIsNotNone(text)
                expected_anchor = label_text_anchor(item)
                self.assertEqual(expected_anchor, text.get("text-anchor"))
                self.assertEqual("0", text.get("x"))
                self.assertEqual("0", text.get("y"))
                normalized = label_angle(item)
                self.assertAlmostEqual(math.cos(normalized), matrix[0], places=12)
                self.assertAlmostEqual(math.sin(normalized), matrix[1], places=12)
                self.assertAlmostEqual(label_anchor(item)[0] - item.position.x, matrix[4], places=12)
                self.assertAlmostEqual(label_anchor(item)[1] - item.position.y, matrix[5], places=12)
                self.assertEqual(str(label_background_width(item.id.id_corto)), rect.get("width"))
                expected_left = (
                    -label_background_width(item.id.id_corto)
                    if expected_anchor == "end" else 0
                )
                self.assertEqual(str(expected_left), rect.get("x"))
                self.assertAlmostEqual(SIGNAL_ICON_HEIGHT, float(rect.get("height")), places=12)

    def test_signal_imv_is_exported_in_its_own_layer_inside_the_port(self):
        item = Señal(
            IdElemento("S1", "TEST"), Point(210, 105), TipoSeñal.Entrada,
            offset_pie=8, rotation=Angle(1.17),
        )
        root = ElementTree.fromstring(export_svg([item]))
        imv_layer = root.find(
            f"./{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='IMV']"
        )
        self.assertIsNotNone(imv_layer)
        imv = imv_layer.find(f"./{{{SVG_NS}}}use")
        self.assertIsNotNone(imv)
        self.assertEqual(item.id.id, imv.get(f"{{{INKSCAPE_NS}}}label"))
        self.assertEqual("#IMV", imv.get("{http://www.w3.org/1999/xlink}href"))
        center = item.position + Point(math.cos(item.rotation.angle) * 11,
                                      math.sin(item.rotation.angle) * 11)
        x, y = (float(value) for value in imv.get("transform")[10:-1].split())
        self.assertAlmostEqual(center.x - 5.9999995, x, places=12)
        self.assertAlmostEqual(center.y - 5.999996, y, places=12)

    def test_signal_and_topera_fmv_are_exported_outside_their_ports(self):
        signal = Señal(
            IdElemento("S1", "TEST"), Point(210, 105), TipoSeñal.Entrada,
            offset_pie=8, rotation=Angle(1.17),
        )
        topera = Topera(IdElemento("F1", "TEST"), Point(300, 160), Angle(2.02))
        root = ElementTree.fromstring(export_svg([signal, topera]))
        fmv_layer = root.find(
            f"./{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='FMV']"
        )
        self.assertIsNotNone(fmv_layer)
        fmvs = fmv_layer.findall(f"./{{{SVG_NS}}}use")
        self.assertEqual({signal.id.id, topera.id.id}, {
            node.get(f"{{{INKSCAPE_NS}}}label") for node in fmvs
        })
        for item in (signal, topera):
            fmv = next(node for node in fmvs
                       if node.get(f"{{{INKSCAPE_NS}}}label") == item.id.id)
            center = item.position - Point(math.cos(item.rotation.angle) * 11,
                                      math.sin(item.rotation.angle) * 11)
            x, y = (float(value) for value in fmv.get("transform")[10:-1].split())
            self.assertAlmostEqual(center.x - 6, x, places=5)
            self.assertAlmostEqual(center.y - 6, y, places=5)

    def test_signal_route_options_control_its_symbols_but_not_topera_fmv(self):
        signal = Señal(
            IdElemento("S1", "TEST"), Point(210, 105), TipoSeñal.Entrada,
            rotation=Angle(1.17), inicio_ruta=False, final_ruta=False,
        )
        topera = Topera(IdElemento("F1", "TEST"), Point(300, 160), Angle(2.02))
        root = ElementTree.fromstring(export_svg([signal, topera]))
        imv_layer = root.find(f"./{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='IMV']")
        fmv_layer = root.find(f"./{{{SVG_NS}}}g[@{{{INKSCAPE_NS}}}label='FMV']")
        self.assertEqual([], imv_layer.findall(f"./{{{SVG_NS}}}use"))
        self.assertEqual([topera.id.id], [
            node.get(f"{{{INKSCAPE_NS}}}label")
            for node in fmv_layer.findall(f"./{{{SVG_NS}}}use")
        ])

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
            label_text = next(candidate for candidate in group.findall(
                f".//{{{SVG_NS}}}text[@{{{INKSCAPE_NS}}}label='nombre']"
            ) if candidate.text == name)
            matrix = [float(v) for v in label_text.get("transform")[7:-1].split()]
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

    def test_travesia_uses_zero_length_tip_and_special_svg_generator(self):
        item = Aguja(
            IdElemento("A1", "TEST"), Point(120, 60), Angle(0.73),
            punta=[],
            normal=[Point(0, 0), Point(22, 0)],
            invertida=[Point(0, 0), Point(18, 12)],
            angulo=math.pi / 4,
            travesia=True,
        )
        self.assertEqual(item.points_punta, [])
        self.assertEqual(item.get_outs()[2][0], item.position)
        self.assertTrue(item.get_outs()[2][1].close_to(item.rotation))
        data = item.serialize()
        self.assertTrue(data["travesia"])
        root = ElementTree.fromstring(export_svg([item]))
        labels = {
            node.get(f"{{{INKSCAPE_NS}}}label")
            for node in root.iter()
            if node.get(f"{{{INKSCAPE_NS}}}label")
        }
        self.assertIn(item.id.id, labels)
        self.assertIn("t3Ra", labels)

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
