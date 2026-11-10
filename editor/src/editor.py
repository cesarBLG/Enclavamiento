#!/usr/bin/env python3
"""Editor gráfico sencillo de topologías ferroviarias sobre una rejilla.

Requiere PyQt6 o PyQt5. Ejecutar con ``python3 editor.py`` desde este directorio.
"""

from __future__ import annotations

import sys
import json
import traceback
from copy import deepcopy
from math import atan2, cos, degrees, isfinite, pi, sin, sqrt, tan, radians
from pathlib import Path

try:
    from PyQt6.QtCore import QPointF, QRectF, QSize, Qt
    from PyQt6.QtGui import QBrush, QColor, QFont, QFontMetricsF, QIcon, QKeySequence, QPainter, QPainterPath, QPen, QPixmap, QPolygonF, QShortcut
    from PyQt6.QtSvg import QSvgRenderer
    from PyQt6.QtWidgets import (
        QApplication, QCheckBox, QComboBox, QFormLayout,
        QFileDialog, QGraphicsItemGroup, QGraphicsRectItem, QGraphicsScene,
        QGraphicsTextItem, QGraphicsView, QHBoxLayout,
        QLabel, QLineEdit, QMainWindow, QMessageBox,
        QListWidget, QPushButton, QSplitter, QToolButton, QVBoxLayout, QWidget,
    )
    LEFT_BUTTON = Qt.MouseButton.LeftButton
except ImportError:
    from PyQt5.QtCore import QPointF, QRectF, QSize, Qt
    from PyQt5.QtGui import QBrush, QColor, QFont, QFontMetricsF, QIcon, QKeySequence, QPainter, QPainterPath, QPen, QPixmap, QPolygonF
    from PyQt5.QtSvg import QSvgRenderer
    from PyQt5.QtWidgets import (
        QApplication, QCheckBox, QComboBox, QFormLayout,
        QFileDialog, QGraphicsItemGroup, QGraphicsRectItem, QGraphicsScene,
        QGraphicsTextItem, QGraphicsView, QHBoxLayout,
        QLabel, QLineEdit, QMainWindow, QMessageBox,
        QListWidget, QPushButton, QShortcut, QSplitter, QToolButton, QVBoxLayout, QWidget,
    )
    LEFT_BUTTON = Qt.LeftButton

from items import (
    Aguja, Angle, Cruzamiento, CvLineal, Direccion, GEOMETRY_TOLERANCE, IdElemento, Lado, Point, Señal, TipoSeñal,
    Topera, deserialize_layout, serialize_layout,
)
from export import generar_config_ence
from label_positions import (
    SIGNAL_ICON_HEIGHT, SIGNAL_ICON_WIDTH, label_anchor, label_angle,
    label_text_anchor, signal_foot_position, signal_label_anchor,
)
from svg.svg_export import export_svg
from svg.generate_junction import get_junction_bounds
from svg.generate_crossing import get_crossing_bounds


GRID_WIDTH = 170/2
GRID_HEIGHT = 85
COLS = 24
ROWS = 16
SNAP_DISTANCE = 10
PICK_TOLERANCE = 5.0
GRID_EXTENSION_TRIGGER = 2
GRID_EXTENSION_CELLS = 10
SIGNAL_ICON_PATH = Path(__file__).resolve().parent.parent / "symbols" / "señal.svg"
SECTION_TYPES = (CvLineal, Aguja, Cruzamiento)


class SignalIcon:
    def __init__(self, path):
        self.renderer = QSvgRenderer(str(path))
        self.size = self.renderer.defaultSize()

    @property
    def width(self):
        return SIGNAL_ICON_WIDTH

    @property
    def height(self):
        return SIGNAL_ICON_HEIGHT

    def pixmap(self):
        pixmap = QPixmap(self.size)
        pixmap.fill(Qt.GlobalColor.transparent if hasattr(Qt, "GlobalColor") else Qt.transparent)
        painter = QPainter(pixmap)
        self.renderer.render(painter, QRectF(0, 0, pixmap.width(), pixmap.height()))
        painter.end()
        return pixmap


SIGNAL_ICON = SignalIcon(SIGNAL_ICON_PATH)


def item_kind_name(item):
    if isinstance(item, CvLineal):
        return "Tramo de vía"
    if isinstance(item, Aguja):
        return "Cambio de agujas"
    if isinstance(item, Cruzamiento):
        return "Cruzamiento"
    if isinstance(item, Señal):
        return "Señal"
    if isinstance(item, Topera):
        return "Topera"
    return None


def direction_vector(direction, length):
    """Vector local de una dirección antigua en coordenadas cartesianas."""
    vectors = ((1, 0), (1, 1), (0, 1), (-1, 1),
               (-1, 0), (-1, -1), (0, -1), (1, -1))
    index = direction.value if isinstance(direction, Direccion) else int(direction)
    dx, dy = vectors[index % 8]
    return Point(dx * length, dy * length)


def item_screen_point(point):
    return point.x, point.y


class AddItemCommand:
    def __init__(self, editor, item):
        self.editor = editor
        self.item = item
        self.previous_selection = list(editor.selected_items)
        if isinstance(item, CvLineal):
            side = item.lado.name if item.lado else "None"
            self.text = f"CV {item.id.id} {side}"
        elif isinstance(item, Aguja):
            cv = item.get_cv().id if item.get_cv() else ""
            side = item.lado.name if item.lado else "None"
            self.text = f"Aguja {item.id.id} {cv} {side}"
        elif isinstance(item, Cruzamiento):
            self.text = f"Cruzamiento {item.id.id}"
        elif isinstance(item, Topera):
            self.text = f"Topera {item.id.id}"
        else:
            self.text = f"Señal {item.tipo_señal.name} {item.id.id}"

    def execute(self):
        if self.item not in self.editor.items:
            self.editor.items.append(self.item)
        self.editor.set_selection([self.item])

    def undo(self):
        if self.item in self.editor.items:
            self.editor.items.remove(self.item)
        self.editor.set_selection(self.previous_selection)


class RotateCommand:
    def __init__(self, editor, item, amount):
        self.editor = editor
        self.item = item
        self.amount = amount
        self.linked_signal = None
        if isinstance(item, (CvLineal, Aguja)):
            self.linked_signal = editor.signal_for_section(item)
        self.before = editor.capture_rotation_state(item, self.linked_signal)
        self.previous_selection = list(editor.selected_items)
        x, y = item.position
        self.text = f"{'RotCW' if amount > 0 else 'RotCCW'} {x} {y}"

    def execute(self):
        if self.editor.rotate_geometry(self.item, self.amount, self.linked_signal) is False:
            self.after = self.before
            return False
        self.after = self.editor.capture_rotation_state(self.item, self.linked_signal)
        self.editor.set_selection([self.item])

    def undo(self):
        self.editor.restore_rotation_state(self.item, self.linked_signal, self.before)
        self.editor.set_selection(self.previous_selection)


class ConvertTravesiaCommand:
    def __init__(self, editor, item):
        self.editor = editor
        self.item = item
        self.before = (item.is_travesia, list(item.points_punta))
        self.previous_selection = list(editor.selected_items)
        self.text = f"Convertir en travesía {item.id.id}"

    def execute(self):
        self.item.is_travesia = True
        self.item.points_punta = []
        self.editor.set_selection([self.item])

    def undo(self):
        self.item.is_travesia, self.item.points_punta = self.before
        self.editor.set_selection(self.previous_selection)


class ExtendSectionCommand:
    def __init__(self, editor, item):
        self.editor = editor
        self.item = item
        self.before = editor.capture_geometry_state(item)
        self.previous_selection = list(editor.selected_items)
        self.text = f"Alargar {item.id.id}"

    def execute(self):
        self.editor.extend_section(self.item)
        self.editor.set_selection([self.item])

    def undo(self):
        self.editor.restore_geometry_state(self.item, self.before)
        self.editor.set_selection(self.previous_selection)


class ResizeSectionCommand:
    def __init__(self, editor, item, before, after):
        self.editor = editor
        self.item = item
        self.before = before
        self.after = after
        self.previous_selection = list(editor.selected_items)
        self.text = f"Redimensionar {item.id.id}"

    def execute(self):
        self.editor.restore_geometry_state(self.item, self.after)
        self.editor.set_selection([self.item])

    def undo(self):
        self.editor.restore_geometry_state(self.item, self.before)
        self.editor.set_selection(self.previous_selection)


class MergeSectionsCommand:
    def __init__(self, editor, first, second):
        self.editor = editor
        self.first = first
        self.second = second
        self.destination, self.source = editor.merge_order(first, second)
        self.destination_before = editor.capture_geometry_state(self.destination)
        self.source_bucket = editor.items_by_dependency[self.source.id.dep]
        self.source_index = self.source_bucket.index(self.source)
        self.previous_selection = list(editor.selected_items)
        self.text = f"Unir {first.id.id} y {second.id.id}"

    def execute(self):
        self.editor.merge_sections(self.destination, self.source)
        self.editor.set_selection([self.destination])

    def undo(self):
        self.editor.restore_geometry_state(self.destination, self.destination_before)
        bucket = self.editor.items_by_dependency.setdefault(
            self.source.id.dep, self.source_bucket
        )
        if self.source not in bucket:
            bucket.insert(min(self.source_index, len(bucket)), self.source)
        self.editor.set_selection(self.previous_selection)


class DeleteItemsCommand:
    def __init__(self, editor, items):
        self.editor = editor
        self.items = list(items)
        self.indices = {
            item: editor.items_by_dependency[item.id.dep].index(item)
            for item in self.items
        }
        self.previous_selection = list(editor.selected_items)
        self.text = f"Delete {len(self.items)} item(s)"

    def execute(self):
        for item in self.items:
            bucket = self.editor.items_by_dependency.get(item.id.dep, [])
            if item in bucket:
                bucket.remove(item)
        self.editor.set_selection([])

    def undo(self):
        for item in self.items:
            bucket = self.editor.items_by_dependency.setdefault(item.id.dep, [])
            if item not in bucket:
                bucket.insert(min(self.indices[item], len(bucket)), item)
        self.editor.set_selection(self.previous_selection)


class MoveItemsCommand:
    def __init__(self, editor, items, before, after):
        self.editor = editor
        self.items = list(items)
        self.before = dict(before)
        self.after = dict(after)
        self.previous_selection = list(editor.selected_items)
        self.text = f"Move {len(self.items)} item(s)"

    def apply(self, positions):
        for item, state in positions.items():
            self.editor.restore_move_state(item, state)
        self.editor.set_selection(self.items)

    def execute(self):
        self.apply(self.after)

    def undo(self):
        self.apply(self.before)


class ReplaceLayoutCommand:
    def __init__(self, editor, layouts, description):
        self.editor = editor
        self.dependency = editor.current_dependency
        self.layouts = {dependency: list(items) for dependency, items in layouts.items()}
        self.description = description
        self.previous_layouts = {
            dependency: list(items)
            for dependency, items in editor.items_by_dependency.items()
        }
        self.previous_layouts.setdefault(self.dependency, list(editor.items))
        self.previous_histories = dict(editor.history_by_dependency)
        self.loaded_histories = {
            dependency: editor.history_by_dependency.get(dependency, ([], []))
            if dependency == self.dependency else ([], [])
            for dependency in self.layouts
        }
        self.loaded_histories.setdefault(
            self.dependency,
            editor.history_by_dependency.get(self.dependency, ([], [])),
        )
        self.previous_selection = list(editor.selected_items)
        self.text = f"LoadLayout {description}"

    def apply_layout(self, layouts, histories):
        replacement = {dependency: list(items) for dependency, items in layouts.items()}
        replacement.setdefault(self.dependency, [])
        self.editor.items_by_dependency = replacement
        self.editor.history_by_dependency = dict(histories)
        self.editor.undo_stack, self.editor.redo_stack = self.editor.history_by_dependency[
            self.dependency
        ]
        self.editor.items = self.editor.items_by_dependency[self.dependency]
        self.editor.sync_dependency_choices()
        self.editor.set_selection([])

    def execute(self):
        self.apply_layout(self.layouts, self.loaded_histories)

    def undo(self):
        self.apply_layout(self.previous_layouts, self.previous_histories)
        self.editor.set_selection(self.previous_selection)


class EditItemCommand:
    def __init__(self, editor, item, new_state, description):
        self.editor = editor
        self.item = item
        self.before = self.capture(item)
        self.after = new_state
        self.text = description

    @staticmethod
    def capture(item):
        state = {"id": item.id, "position": Point(item.position.x, item.position.y)}
        if isinstance(item, CvLineal):
            state["lado"] = item.lado
            state.update(tipo_cv=item.tipo_cv, offset=item.offset)
        elif isinstance(item, Aguja):
            state["cv"] = item.cv
            state["lado"] = item.lado
        elif isinstance(item, Señal):
            state["tipo_señal"] = item.tipo_señal
            state["focos"] = list(item.focos)
            state["aspectos_pantalla"] = list(item.aspectos_pantalla)
        return state

    def apply(self, state):
        self.item.id = state["id"]
        self.item.position = state["position"]
        if isinstance(self.item, CvLineal):
            self.item.lado = state["lado"]
            self.item.tipo_cv = state["tipo_cv"]
            self.item.offset = state["offset"]
        elif isinstance(self.item, Aguja):
            self.item.cv = state["cv"]
            self.item.lado = state["lado"]
        elif isinstance(self.item, Señal):
            self.item.tipo_señal = state["tipo_señal"]
            self.item.focos = list(state["focos"])
            self.item.aspectos_pantalla = list(state["aspectos_pantalla"])
        self.editor.set_selection([self.item])

    def execute(self):
        self.apply(self.after)

    def undo(self):
        self.apply(self.before)


class ItemConfigPanel(QWidget):
    def __init__(self, editor, parent=None):
        super().__init__(parent)
        self.editor = editor
        self.current_item = None
        self.current_kind = None
        self.name_edit = None
        self.position_x_edit = None
        self.position_y_edit = None
        self.cv_edit = None
        self.signal_type = None
        self.side_combo = None
        self.cv_type = None
        self.cv_offset = None
        self.signal_lamps = {}
        self.speed_list = None
        self.speed_edit = None

        layout = QVBoxLayout(self)
        self.title_label = QLabel("Configuración del elemento", self)
        self.title_label.setStyleSheet("font-weight: 600;")
        self.summary_label = QLabel(self)
        self.summary_label.setWordWrap(True)
        self.summary_label.setText("Selecciona un único elemento configurable para editarlo aquí.")

        self.form_widget = QWidget(self)
        self.form_layout = QFormLayout(self.form_widget)

        self.apply_button = QPushButton("Aplicar cambios", self)
        self.apply_button.clicked.connect(self.apply_changes)

        layout.addWidget(self.title_label)
        layout.addWidget(self.summary_label)
        layout.addWidget(self.form_widget)
        layout.addStretch(1)
        layout.addWidget(self.apply_button)

        self.setMinimumWidth(320)
        self.setFixedWidth(350)
        self.set_item(None)

    def clear_form(self):
        while self.form_layout.count():
            item = self.form_layout.takeAt(0)
            widget = item.widget() if item is not None else None
            if widget is not None:
                widget.deleteLater()
        self.name_edit = None
        self.position_x_edit = None
        self.position_y_edit = None
        self.cv_edit = None
        self.signal_type = None
        self.side_combo = None
        self.cv_type = None
        self.cv_offset = None
        self.signal_lamps = {}
        self.speed_list = None
        self.speed_edit = None
        self.current_kind = None

    def build_form(self, kind):
        self.clear_form()
        self.current_kind = kind

        self.name_edit = QLineEdit(self)
        if kind == "Tramo de vía":
            self.form_layout.addRow("Nombre del CV:", self.name_edit)
        else:
            self.form_layout.addRow("Nombre:", self.name_edit)

        position_widget = QWidget(self)
        position_layout = QHBoxLayout(position_widget)
        position_layout.setContentsMargins(0, 0, 0, 0)
        self.position_x_edit = QLineEdit(position_widget)
        self.position_x_edit.setPlaceholderText("X")
        self.position_y_edit = QLineEdit(position_widget)
        self.position_y_edit.setPlaceholderText("Y")
        position_layout.addWidget(self.position_x_edit)
        position_layout.addWidget(self.position_y_edit)
        self.form_layout.addRow("Posición (X, Y):", position_widget)

        if kind == "Cambio de agujas":
            self.cv_edit = QLineEdit(self)
            self.form_layout.addRow("Circuito de vía:", self.cv_edit)

        if kind in ("Tramo de vía", "Cambio de agujas"):
            self.side_combo = QComboBox(self)
            self.side_combo.addItem("", "")
            self.side_combo.addItem("Par", "Par")
            self.side_combo.addItem("Impar", "Impar")
            self.form_layout.addRow("Lado:", self.side_combo)

        if kind == "Tramo de vía":
            self.cv_type = QComboBox(self)
            self.cv_type.addItem("Sin tipo", None)
            self.cv_type.addItem("Estacionamiento", "Estacionamiento")
            self.cv_type.addItem("Trayecto", "Trayecto")
            self.form_layout.addRow("Tipo de CV:", self.cv_type)
            self.cv_offset = QLineEdit(self)
            self.cv_offset.setToolTip("Distancia desde la entrada del CV; desplaza juntas sus etiquetas y símbolos.")
            self.form_layout.addRow("Offset desde la entrada:", self.cv_offset)

        if kind == "Señal":
            self.signal_type = QComboBox(self)
            for signal_type in TipoSeñal:
                self.signal_type.addItem(signal_type.name, signal_type)
            self.form_layout.addRow("Tipo de señal:", self.signal_type)
            lamp_widget = QWidget(self)
            lamp_layout = QVBoxLayout(lamp_widget)
            lamp_layout.setContentsMargins(0, 0, 0, 0)
            for code, label in (("V", "Verde"), ("R", "Rojo"), ("B", "Blanco"), ("Az", "Azul"), ("A", "Amarillo")):
                checkbox = QCheckBox(label, lamp_widget)
                self.signal_lamps[code] = checkbox
                lamp_layout.addWidget(checkbox)
            self.form_layout.addRow("Focos:", lamp_widget)
            self.speed_list = QListWidget(self)
            self.form_layout.addRow("Velocidades pantalla:", self.speed_list)
            speed_controls = QWidget(self)
            speed_layout = QHBoxLayout(speed_controls)
            speed_layout.setContentsMargins(0, 0, 0, 0)
            self.speed_edit = QLineEdit(speed_controls)
            self.speed_edit.setPlaceholderText("I60, D30 o 90")
            add_speed = QPushButton("Añadir", speed_controls)
            remove_speed = QPushButton("Quitar", speed_controls)
            add_speed.clicked.connect(self.add_screen_speed)
            remove_speed.clicked.connect(lambda: self.speed_list.takeItem(self.speed_list.currentRow()) if self.speed_list.currentRow() >= 0 else None)
            speed_layout.addWidget(self.speed_edit)
            speed_layout.addWidget(add_speed)
            speed_layout.addWidget(remove_speed)
            self.form_layout.addRow("Editar velocidades:", speed_controls)

    def add_screen_speed(self):
        value = self.speed_edit.text().strip().upper() if self.speed_edit else ""
        if not value or not (value.isdigit() or (value[0] in "ID" and value[1:].isdigit())):
            QMessageBox.warning(self, "Velocidad inválida", "Usa una velocidad como I60, D30 o 90.")
            return
        if self.speed_list.findItems(value, Qt.MatchFlag.MatchExactly if hasattr(Qt, "MatchFlag") else Qt.MatchExactly):
            QMessageBox.warning(self, "Velocidad repetida", "Esa velocidad ya está en la lista.")
            return
        self.speed_list.addItem(value)
        self.speed_edit.clear()

    def set_item(self, item):
        self.current_item = item
        kind = item_kind_name(item) if item is not None else None
        if item is None or kind is None:
            self.clear_form()
            self.summary_label.setText("Selecciona un único elemento configurable para editarlo aquí.")
            self.apply_button.setEnabled(False)
            return

        if kind != self.current_kind:
            self.build_form(kind)

        self.summary_label.setText(f"Editando {kind.lower()} {item.id.id} en {item.id.dep}.")
        self.apply_button.setEnabled(True)
        self.name_edit.setText(item.id.id_corto)
        self.position_x_edit.setText(repr(item.position.x))
        self.position_y_edit.setText(repr(item.position.y))

        if isinstance(item, (CvLineal, Aguja)) and self.side_combo is not None:
            index = self.side_combo.findData(item.lado.name if item.lado else "")
            if index >= 0:
                self.side_combo.setCurrentIndex(index)
        if isinstance(item, CvLineal):
            self.cv_type.setCurrentIndex(max(0, self.cv_type.findData(item.tipo_cv)))
            self.cv_offset.setText(str(item.offset))

        if isinstance(item, Aguja) and self.cv_edit is not None:
            self.cv_edit.setText(item.get_cv().id_corto if item.get_cv() else "")

        if isinstance(item, Señal) and self.signal_type is not None:
            index = self.signal_type.findData(item.tipo_señal)
            if index >= 0:
                self.signal_type.setCurrentIndex(index)
            for code, checkbox in self.signal_lamps.items():
                checkbox.setChecked(code in item.focos)
            self.speed_list.clear()
            self.speed_list.addItems(item.aspectos_pantalla)

    def apply_changes(self):
        item = self.current_item
        if item is None:
            return
        short_id = self.name_edit.text().strip() if self.name_edit else ""
        try:
            position = Point(
                float(self.position_x_edit.text().strip().replace(",", ".")),
                float(self.position_y_edit.text().strip().replace(",", ".")),
            )
        except ValueError:
            QMessageBox.warning(self, "Posición inválida", "Las coordenadas deben ser numéricas.")
            return
        if not (isfinite(position.x) and isfinite(position.y)):
            QMessageBox.warning(self, "Posición inválida", "Las coordenadas deben ser finitas.")
            return
        cv_name = self.cv_edit.text().strip() if self.cv_edit else ""
        signal_type = self.signal_type.currentData() if self.signal_type is not None else None
        side_value = self.side_combo.currentData() if self.side_combo is not None else ""
        side = Lado[side_value] if side_value else None
        focos = [code for code, checkbox in self.signal_lamps.items() if checkbox.isChecked()]
        velocidades = [self.speed_list.item(i).text() for i in range(self.speed_list.count())] if self.speed_list else []
        cv_options = None
        if isinstance(item, CvLineal):
            try:
                offset = float(self.cv_offset.text().strip())
            except ValueError:
                QMessageBox.warning(self, "Offset inválido", "El offset debe ser numérico.")
                return
            if not 0 <= offset <= item.length():
                QMessageBox.warning(self, "Offset inválido", "El offset debe estar entre la entrada y la salida del CV.")
                return
            cv_options = (self.cv_type.currentData(), offset)
        self.editor.apply_item_configuration(
            item, short_id, cv_name, signal_type, side, focos, velocidades,
            cv_options, position,
        )


class GridView(QGraphicsView):
    def __init__(self, scene, editor):
        super().__init__(scene)
        self.editor = editor
        self.setMouseTracking(True)
        self.setMinimumSize(400, 300)
        scroll_policy = getattr(Qt, "ScrollBarPolicy", Qt)
        self.setHorizontalScrollBarPolicy(scroll_policy.ScrollBarAsNeeded)
        self.setVerticalScrollBarPolicy(scroll_policy.ScrollBarAsNeeded)
        self.gesture = None
        self.press_scene = None
        self.press_position = None
        self.drag_items = []
        self.drag_before = {}
        self.preview_delta = (0, 0)
        self.rubber_band = None
        self.zoom_factor = 1.0
        self.extend_item = None
        self.extend_port = None
        self.extend_before = None
        self.additive_selection = False
        self.setTransformationAnchor(
            getattr(getattr(QGraphicsView, "ViewportAnchor", QGraphicsView), "AnchorUnderMouse")
        )

    def zoom_by(self, factor):
        new_zoom = max(0.35, min(3.0, self.zoom_factor * factor))
        factor = new_zoom / self.zoom_factor
        if abs(factor - 1.0) > 1e-6:
            self.scale(factor, factor)
            self.zoom_factor = new_zoom

    def reset_zoom(self):
        self.resetTransform()
        self.zoom_factor = 1.0

    def wheelEvent(self, event):
        modifiers = event.modifiers()
        control = getattr(getattr(Qt, "KeyboardModifier", Qt), "ControlModifier")
        if modifiers & control:
            self.zoom_by(1.2 if event.angleDelta().y() > 0 else 1 / 1.2)
            event.accept()
            return
        super().wheelEvent(event)

    @staticmethod
    def event_point(event):
        return event.position().toPoint() if hasattr(event, "position") else event.pos()

    @staticmethod
    def item_at(point, candidates):
        def segment_distance(px, py, first, second):
            dx, dy = second[0] - first[0], second[1] - first[1]
            length_squared = dx * dx + dy * dy
            if length_squared == 0:
                return ((px - first[0]) ** 2 + (py - first[1]) ** 2) ** .5
            factor = max(0, min(1, ((px - first[0]) * dx + (py - first[1]) * dy) / length_squared))
            closest = (first[0] + factor * dx, first[1] + factor * dy)
            return ((px - closest[0]) ** 2 + (py - closest[1]) ** 2) ** .5

        def track_distance(item):
            if isinstance(item, CvLineal):
                local_paths = (item.points,)
            elif isinstance(item, Aguja):
                local_paths = (item.points_normal, item.points_invertida)
            elif isinstance(item, Cruzamiento):
                local_paths = (item.points_p1, item.points_p2, item.points_p3, item.points_p4)
            else:
                return None
            paths = [
                [item_screen_point(item.position + local.get_rotated(item.rotation))
                 for local in path]
                for path in local_paths
            ]
            return min(
                segment_distance(point.x(), point.y(), first, second)
                for path in paths
                for first, second in zip(path, path[1:])
            )

        hits = []
        for item in candidates:
            if isinstance(item, SECTION_TYPES):
                distance = track_distance(item)
                if distance <= PICK_TOLERANCE:
                    hits.append((distance, item))
                continue
            if not isinstance(item, Señal):
                if isinstance(item, Topera):
                    angle = item.rotation.angle
                    ux, uy = cos(angle), sin(angle)
                    center_x = item.position.x + ux * 2
                    center_y = item.position.y + uy * 2
                    along = ((point.x() - center_x) * ux
                             + (point.y() - center_y) * uy)
                    across = ((point.x() - center_x) * -uy
                              + (point.y() - center_y) * ux)
                    if abs(along) <= PICK_TOLERANCE and abs(across) <= 10 + PICK_TOLERANCE:
                        hits.append((0, item))
                continue
            angle = item.rotation.angle
            ux, uy = cos(angle), sin(angle)
            anchor_x, anchor_y = item_screen_point(signal_foot_position(item))
            dx, dy = point.x() - anchor_x, point.y() - anchor_y
            local_x = dx * ux + dy * uy
            local_y = -dx * uy + dy * ux
            text_half_width = len(item.id.id_corto) * 3.5
            if (0 <= local_x <= SIGNAL_ICON.width
                    and -SIGNAL_ICON.height <= local_y <= 0):
                hits.append((0, item))
                continue
            label_anchor = signal_label_anchor(item)
            label_dx, label_dy = point.x() - label_anchor[0], point.y() - label_anchor[1]
            if 0 <= label_dx <= 2 * text_half_width and abs(label_dy) <= SIGNAL_ICON.height / 2:
                hits.append((0, item))
        return min(hits, key=lambda hit: hit[0])[1] if hits else None

    def mousePressEvent(self, event):
        if event.button() == LEFT_BUTTON:
            point = self.mapToScene(self.event_point(event))
            if self.editor.mode[0] == "extend":
                extension = self.editor.begin_extend(point)
                if extension is None:
                    return
                self.extend_item, self.extend_port, self.extend_before = extension
                self.press_scene = point
                self.gesture = "extend"
                return
            if not self.scene().sceneRect().contains(point):
                return
            if self.editor.mode[0] != "select":
                self.editor.point_clicked(point)
                return
            self.press_scene = point
            self.press_position = None
            self.preview_delta = (0, 0)
            self.rubber_band = None
            control = getattr(getattr(Qt, "KeyboardModifier", Qt), "ControlModifier")
            self.additive_selection = bool(event.modifiers() & control)
            candidates = [item for items in self.editor.items_by_dependency.values() for item in items]
            clicked_item = self.item_at(point, candidates)
            if clicked_item is not None:
                if self.additive_selection:
                    selection = list(self.editor.selected_items)
                    if clicked_item in selection:
                        selection.remove(clicked_item)
                    else:
                        selection.append(clicked_item)
                    self.editor.set_selection(selection)
                elif clicked_item not in self.editor.selected_items:
                    self.editor.set_selection([clicked_item])
                self.drag_items = self.editor.movable_selection(self.editor.selected_items)
                self.drag_before = {item: self.editor.capture_move_state(item) for item in self.drag_items}
                self.gesture = "move"
                self.click_processed = True
            else:
                self.gesture = "rubber"
                self.click_processed = False
            return
        super().mousePressEvent(event)

    def mouseMoveEvent(self, event):
        if self.gesture is None or self.press_scene is None:
            super().mouseMoveEvent(event)
            return
        point = self.mapToScene(self.event_point(event))
        distance = ((point.x() - self.press_scene.x()) ** 2
                    + (point.y() - self.press_scene.y()) ** 2) ** .5
        if distance < 4:
            return
        if self.gesture == "extend":
            self.editor.preview_extend(
                self.extend_item, self.extend_port, self.extend_before, point
            )
            self.editor.draw_items()
            return
        if self.gesture == "rubber":
            if self.rubber_band is not None:
                self.scene().removeItem(self.rubber_band)
            rect = QRectF(self.press_scene, point).normalized()
            self.rubber_band = self.scene().addRect(
                rect, QPen(QColor("#1976d2"), 1), QBrush(QColor(25, 118, 210, 45))
            )
            self.rubber_band.setZValue(10)
            return

        start_col = int(self.press_scene.x() // GRID_WIDTH)
        start_row = int(self.press_scene.y() // GRID_HEIGHT)
        current_col = int(point.x() // GRID_WIDTH)
        current_row = int(point.y() // GRID_HEIGHT)
        delta = (current_col - start_col, current_row - start_row)
        group_drag = len(self.editor.selected_items) > 1
        signal_only_drag = self.drag_items and all(
            isinstance(item, Señal) for item in self.drag_items
        )
        if self.editor.can_move_items(self.drag_items, self.drag_before, delta):
            self.preview_delta = delta
            self.editor._moving_sections = {
                item for item in self.drag_items
                if isinstance(item, SECTION_TYPES)
            }
            signals_moving_with_track = {
                item for item in self.drag_items
                if isinstance(item, Señal) and any(
                    isinstance(other, SECTION_TYPES)
                    and self.editor.signal_matches_section(item, other)
                    for other in self.drag_items
                )
            }
            topes_moving_with_track = {
                item for item in self.drag_items
                if isinstance(item, Topera) and any(
                    isinstance(other, SECTION_TYPES)
                    and self.editor.topera_matches_section(item, other)
                    for other in self.drag_items
                )
            }
            signal_tracks = {
                signal: next(
                    (item for item in self.drag_items
                     if isinstance(item, SECTION_TYPES)
                     and self.editor.signal_matches_section(signal, item)),
                    None,
                )
                for signal in signals_moving_with_track
            }
            topera_tracks = {
                topera: next(
                    (item for item in self.drag_items
                     if isinstance(item, SECTION_TYPES)
                     and self.editor.topera_matches_section(topera, item)),
                    None,
                )
                for topera in topes_moving_with_track
            }
            for item, state in self.drag_before.items():
                move_port = group_drag or item in signals_moving_with_track or item in topes_moving_with_track
                fine_delta = (point.x() - self.press_scene.x(),
                              point.y() - self.press_scene.y())
                self.editor.preview_move(
                    item, state, delta, move_port, fine_delta,
                    snap=not group_drag,
                )
            if group_drag:
                self.editor.snap_group_to_grid(self.drag_items, point)
            for signal in () if group_drag else signals_moving_with_track:
                track = signal_tracks[signal]
                if track is None:
                    continue
                port, angle = min(
                    track.get_outs(),
                    key=lambda candidate: (
                        (candidate[0].x - signal.position.x) ** 2
                        + (candidate[0].y - signal.position.y) ** 2
                    ),
                )
                signal.position = port
                signal.rotation = angle - Angle(pi)
            for topera in () if group_drag else topes_moving_with_track:
                track = topera_tracks[topera]
                if track is None:
                    continue
                port, angle = min(
                    track.get_outs(),
                    key=lambda candidate: (
                        (candidate[0].x - topera.position.x) ** 2
                        + (candidate[0].y - topera.position.y) ** 2
                    ),
                )
                topera.position = port
                topera.rotation = angle
            if not group_drag and any(
                isinstance(item, Aguja)
                and not self.editor.aguja_has_connection(item, self.editor._moving_sections)
                for item in self.drag_items
            ):
                for item, state in self.drag_before.items():
                    self.editor.restore_move_state(item, state)
                self.preview_delta = (0, 0)
            self.editor._moving_sections = set()
            self.editor.draw_items()
        else:
            self.preview_delta = (0, 0)
            for item, state in self.drag_before.items():
                self.editor.restore_move_state(item, state)
            self.editor.draw_items()

    def mouseReleaseEvent(self, event):
        if event.button() != LEFT_BUTTON or self.gesture is None:
            super().mouseReleaseEvent(event)
            return
        else:
            point = self.mapToScene(self.event_point(event))
            if self.gesture == "extend":
                after = self.editor.capture_geometry_state(self.extend_item)
                self.editor.restore_geometry_state(self.extend_item, self.extend_before)
                if after != self.extend_before:
                    self.editor.run_command(
                        ResizeSectionCommand(
                            self.editor, self.extend_item, self.extend_before, after
                        )
                    )
                self.extend_item = None
                self.extend_port = None
                self.extend_before = None
                self.gesture = None
                self.press_scene = None
                return
            if self.gesture == "move":
                after = {item: self.editor.capture_move_state(item) for item in self.drag_items}
                if any(after[item] != self.drag_before[item] for item in self.drag_items):
                    for item, state in self.drag_before.items():
                        self.editor.restore_move_state(item, state)
                    self.editor.run_command(
                        MoveItemsCommand(self.editor, self.drag_items, self.drag_before, after)
                    )
                elif not self.click_processed and not self.additive_selection:
                    self.editor.point_clicked(point)
            else:
                distance = ((point.x() - self.press_scene.x()) ** 2
                            + (point.y() - self.press_scene.y()) ** 2) ** .5
                if distance >= 4:
                    rect = QRectF(self.press_scene, point).normalized()
                    selected = []
                    for items in self.editor.items_by_dependency.values():
                        for item in items:
                            if isinstance(item, Señal):
                                visible_points = [item.position]
                            elif isinstance(item, Topera):
                                angle = item.rotation.angle
                                ux, uy = cos(angle), sin(angle)
                                center = item.position + Point(ux * 2, uy * 2)
                                perpendicular = Point(-uy * 10, ux * 10)
                                visible_points = [center - perpendicular, center + perpendicular]
                            elif isinstance(item, CvLineal):
                                visible_points = [
                                    item.position + path_point.get_rotated(item.rotation)
                                    for path_point in item.points
                                ]
                            elif isinstance(item, Cruzamiento):
                                visible_points = [
                                    item.position + path_point.get_rotated(item.rotation)
                                    for path in (
                                        item.points_p1, item.points_p2,
                                        item.points_p3, item.points_p4,
                                    )
                                    for path_point in path
                                ]
                            else:
                                visible_points = [
                                    item.position + path_point.get_rotated(item.rotation)
                                    for path in (item.points_normal, item.points_invertida,
                                                 item.points_punta)
                                    for path_point in path
                                ]
                            bounds = QRectF(
                                min(point.x for point in visible_points) - PICK_TOLERANCE,
                                min(point.y for point in visible_points) - PICK_TOLERANCE,
                                max(point.x for point in visible_points)
                                - min(point.x for point in visible_points)
                                + 2 * PICK_TOLERANCE,
                                max(point.y for point in visible_points)
                                - min(point.y for point in visible_points)
                                + 2 * PICK_TOLERANCE,
                            )
                            if rect.intersects(bounds):
                                selected.append(item)
                    if self.additive_selection:
                        selected = list(self.editor.selected_items) + [
                            item for item in selected if item not in self.editor.selected_items
                        ]
                    self.editor.set_selection(selected)
                elif not self.click_processed and not self.additive_selection:
                    self.editor.point_clicked(point)
            if self.rubber_band is not None:
                self.scene().removeItem(self.rubber_band)
            self.rubber_band = None
            self.gesture = None
            self.press_scene = None
            self.press_position = None
            self.drag_items = []
            self.drag_before = {}
            self.preview_delta = (0, 0)
            self.additive_selection = False

    def mouseDoubleClickEvent(self, event):
        if event.button() == LEFT_BUTTON:
            point = self.mapToScene(
                event.position().toPoint() if hasattr(event, "position") else event.pos()
            )
            if self.scene().sceneRect().contains(point):
                candidates = [item for items in self.editor.items_by_dependency.values() for item in items]
                item = self.item_at(point, candidates)
                if item is not None:
                    self.editor.dependency.setCurrentText(item.id.dep)
                    self.editor.switch_dependency()
                    self.editor.set_selection([item])
                    return
        super().mouseDoubleClickEvent(event)


class TrackEditor(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("Editor de vías de estación")
        self.resize(1450, 920)
        self.items = []
        self.selected = None
        self.selected_items = []
        self.mode = ("select", None)
        self.undo_stack = []
        self.redo_stack = []
        self.current_dependency = "EST"
        self.items_by_dependency = {self.current_dependency: self.items}
        self.history_by_dependency = {
            self.current_dependency: (self.undo_stack, self.redo_stack)
        }
        self.layout_path = None
        self.is_dirty = False
        self.grid_left = 0
        self.grid_right = COLS
        self.scene = QGraphicsScene(self)
        self.scene.setSceneRect(
            self.grid_left * GRID_WIDTH, 0,
            (self.grid_right - self.grid_left) * GRID_WIDTH,
            ROWS * GRID_HEIGHT,
        )
        self.grid_graphics = []
        self.view = GridView(self.scene, self)
        self.config_panel = ItemConfigPanel(self, self)

        central = QWidget(self)
        root = QHBoxLayout(central)
        root.setContentsMargins(0, 0, 0, 0)

        left_panel = QWidget(self)
        left_layout = QVBoxLayout(left_panel)
        dep_row = QHBoxLayout()
        dep_row.addWidget(QLabel("Dependencia de trabajo:"))
        self.dependency = QComboBox(self)
        self.dependency.setEditable(True)
        self.dependency.addItem("EST")
        self.dependency.setPlaceholderText("Nombre de la estación")
        self.dependency.activated.connect(lambda *_: self.switch_dependency())
        self.dependency.lineEdit().editingFinished.connect(self.switch_dependency)
        dep_row.addWidget(self.dependency, 1)
        left_layout.addLayout(dep_row)

        tools = QHBoxLayout()
        self.tool_buttons = []
        self.add_tool(tools, "Seleccionar", "select", None)
        self.work_angle = 45.0
        tools.addWidget(QLabel("Ángulo:"))
        self.angle_edit = QLineEdit(self)
        self.angle_edit.setText("45")
        self.angle_edit.setFixedWidth(52)
        self.angle_edit.setToolTip("Ángulo de trabajo en grados")
        self.angle_edit.editingFinished.connect(self.update_work_angle)
        tools.addWidget(self.angle_edit)
        self.add_tool(tools, "Sección angular", "track", 3)
        self.add_tool(tools, "Recta 180°", "track", 4)
        self.add_tool(tools, "Aguja a izquierdas", "switch", 1)
        self.add_tool(tools, "Aguja a derechas", "switch", -1)
        self.add_tool(tools, "Alargar vía", "extend", None)
        self.add_tool(tools, "Señal", "signal", None)
        self.add_tool(tools, "Topera", "topera", None)
        self.merge_button = QToolButton(self)
        self.merge_button.setIcon(self.make_icon("merge"))
        self.merge_button.setIconSize(QSize(36, 36))
        self.merge_button.setFixedSize(46, 42)
        self.merge_button.setToolTip("Unir dos vías seleccionadas")
        self.merge_button.clicked.connect(self.merge_selected)
        self.merge_button.setEnabled(False)
        tools.addWidget(self.merge_button)
        self.junction_button = QToolButton(self)
        self.junction_button.setIcon(self.make_icon("switch"))
        self.junction_button.setIconSize(QSize(36, 36))
        self.junction_button.setFixedSize(46, 42)
        self.junction_button.setToolTip("Generar junction entre dos vías seleccionadas")
        self.junction_button.clicked.connect(self.generate_junction_selected)
        self.junction_button.setEnabled(False)
        tools.addWidget(self.junction_button)
        self.travesia_button = QToolButton(self)
        self.travesia_button.setIcon(self.make_icon("crossing"))
        self.travesia_button.setIconSize(QSize(36, 36))
        self.travesia_button.setFixedSize(46, 42)
        self.travesia_button.setToolTip("Convertir la aguja seleccionada en travesía")
        self.travesia_button.clicked.connect(self.convert_selected_to_travesia)
        self.travesia_button.setEnabled(False)
        tools.addWidget(self.travesia_button)
        self.crossing_button = QToolButton(self)
        self.crossing_button.setIcon(self.make_icon("crossing"))
        self.crossing_button.setIconSize(QSize(36, 36))
        self.crossing_button.setFixedSize(46, 42)
        self.crossing_button.setToolTip("Generar cruzamiento entre dos vías seleccionadas")
        self.crossing_button.clicked.connect(self.generate_crossing_selected)
        self.crossing_button.setEnabled(False)
        tools.addWidget(self.crossing_button)
        self.delete_button = QToolButton(self)
        self.delete_button.setIcon(self.make_icon("delete"))
        self.delete_button.setIconSize(QSize(36, 36))
        self.delete_button.setFixedSize(46, 42)
        self.delete_button.setToolTip("Eliminar elemento seleccionado (Supr)")
        self.delete_button.clicked.connect(self.delete_selected)
        tools.addWidget(self.delete_button)
        self.save_button = QToolButton(self)
        self.save_button.setIcon(self.make_icon("save"))
        self.save_button.setIconSize(QSize(36, 36))
        self.save_button.setFixedSize(46, 42)
        self.save_button.setToolTip("Guardar layout")
        self.save_button.clicked.connect(self.save_layout)
        self.save_as_button = QToolButton(self)
        self.save_as_button.setIcon(self.make_icon("save-as"))
        self.save_as_button.setIconSize(QSize(36, 36))
        self.save_as_button.setFixedSize(46, 42)
        self.save_as_button.setToolTip("Guardar layout como…")
        self.save_as_button.clicked.connect(self.save_layout_as)
        self.load_button = QToolButton(self)
        self.load_button.setIcon(self.make_icon("load"))
        self.load_button.setIconSize(QSize(36, 36))
        self.load_button.setFixedSize(46, 42)
        self.load_button.setToolTip("Cargar layout")
        self.load_button.clicked.connect(self.load_layout)
        tools.addWidget(self.save_button)
        tools.addWidget(self.save_as_button)
        tools.addWidget(self.load_button)
        self.export_button = QToolButton(self)
        self.export_button.setIcon(self.make_icon("export"))
        self.export_button.setIconSize(QSize(36, 36))
        self.export_button.setFixedSize(46, 42)
        self.export_button.setToolTip("Exportar JSON del enclavamiento")
        self.export_button.clicked.connect(self.exportar_config_ence)
        tools.addWidget(self.export_button)
        self.export_svg_button = QToolButton(self)
        self.export_svg_button.setIcon(self.make_icon("export"))
        self.export_svg_button.setIconSize(QSize(36, 36))
        self.export_svg_button.setFixedSize(46, 42)
        self.export_svg_button.setToolTip("Exportar layout a SVG")
        self.export_svg_button.clicked.connect(self.exportar_svg)
        tools.addWidget(self.export_svg_button)
        self.undo_button = QToolButton(self)
        self.undo_button.setIcon(self.make_icon("undo"))
        self.undo_button.setIconSize(QSize(36, 36))
        self.undo_button.setFixedSize(46, 42)
        self.undo_button.setToolTip("Deshacer (Ctrl+Z)")
        self.undo_button.clicked.connect(self.undo)
        self.redo_button = QToolButton(self)
        self.redo_button.setIcon(self.make_icon("redo"))
        self.redo_button.setIconSize(QSize(36, 36))
        self.redo_button.setFixedSize(46, 42)
        self.redo_button.setToolTip("Rehacer (Ctrl+Y)")
        self.redo_button.clicked.connect(self.redo)
        tools.addWidget(self.undo_button)
        tools.addWidget(self.redo_button)
        self.rotate_left = QToolButton(self)
        self.rotate_left.setIcon(self.make_icon("rotate-left"))
        self.rotate_left.setToolTip("Rotar antihorario")
        self.rotate_left.setIconSize(QSize(36, 36))
        self.rotate_left.setFixedSize(46, 42)
        self.rotate_right = QToolButton(self)
        self.rotate_right.setIcon(self.make_icon("rotate-right"))
        self.rotate_right.setToolTip("Rotar horario")
        self.rotate_right.setIconSize(QSize(36, 36))
        self.rotate_right.setFixedSize(46, 42)
        self.rotate_left.clicked.connect(lambda: self.rotate_selected(-1))
        self.rotate_right.clicked.connect(lambda: self.rotate_selected(1))
        tools.addWidget(self.rotate_left)
        tools.addWidget(self.rotate_right)
        zoom_out = QToolButton(self)
        zoom_out.setText("−")
        zoom_out.setToolTip("Alejar")
        zoom_out.setFixedSize(38, 42)
        zoom_out.clicked.connect(lambda: self.view.zoom_by(1 / 1.2))
        zoom_in = QToolButton(self)
        zoom_in.setText("+")
        zoom_in.setToolTip("Acercar")
        zoom_in.setFixedSize(38, 42)
        zoom_in.clicked.connect(lambda: self.view.zoom_by(1.2))
        zoom_reset = QToolButton(self)
        zoom_reset.setText("100%")
        zoom_reset.setToolTip("Restablecer zoom")
        zoom_reset.setFixedSize(54, 42)
        zoom_reset.clicked.connect(self.view.reset_zoom)
        tools.addWidget(zoom_out)
        tools.addWidget(zoom_in)
        tools.addWidget(zoom_reset)
        tools.addStretch(1)
        left_layout.addLayout(tools)
        left_layout.addWidget(self.view, 1)

        splitter = QSplitter(Qt.Orientation.Horizontal if hasattr(Qt, "Orientation") else Qt.Horizontal, self)
        splitter.addWidget(left_panel)
        splitter.addWidget(self.config_panel)
        splitter.setStretchFactor(0, 1)
        splitter.setStretchFactor(1, 0)
        splitter.setSizes([1100, 350])

        root.addWidget(splitter)
        self.setCentralWidget(central)
        QShortcut(QKeySequence("Ctrl+Z"), self).activated.connect(self.undo)
        QShortcut(QKeySequence("Ctrl+Y"), self).activated.connect(self.redo)
        QShortcut(QKeySequence("Ctrl+Shift+Z"), self).activated.connect(self.redo)
        QShortcut(QKeySequence("Delete"), self).activated.connect(self.delete_selected)
        self.statusBar().showMessage("Selecciona una herramienta y pulsa en el plano.")
        self.delete_button.setEnabled(False)
        self.update_history_buttons()
        self.draw_grid()

    def add_tool(self, layout, label, kind, value):
        button = QToolButton(self)
        button.setIcon(self.make_icon(kind, value))
        button.setIconSize(QSize(44, 44))
        button.setFixedSize(54, 50)
        button.setToolTip(label)
        button.setCheckable(True)
        button.clicked.connect(lambda _checked=False, k=kind, v=value: self.set_mode(k, v))
        layout.addWidget(button)
        self.tool_buttons.append((button, kind, value))
        if kind == "select":
            button.setChecked(True)

    @staticmethod
    def make_icon(kind, value=None):
        """Crea iconos vectoriales Qt para las herramientas del editor."""
        if kind == "signal":
            return QIcon(str(SIGNAL_ICON_PATH))
        size = QSize(48, 48)
        pixmap = QPixmap(size)
        pixmap.fill(Qt.GlobalColor.transparent if hasattr(Qt, "GlobalColor") else Qt.transparent)
        painter = QPainter(pixmap)
        if hasattr(QPainter, "RenderHint"):
            painter.setRenderHint(QPainter.RenderHint.Antialiasing, True)
        color = QColor("#26384a")
        pen = QPen(color, 3.5)
        if hasattr(Qt, "PenCapStyle"):
            pen.setCapStyle(Qt.PenCapStyle.FlatCap)
            pen.setJoinStyle(Qt.PenJoinStyle.MiterJoin)
        else:
            pen.setCapStyle(Qt.FlatCap)
            pen.setJoinStyle(Qt.MiterJoin)
        painter.setPen(pen)
        center = QPointF(24, 23)

        def endpoint(direction, radius=18):
            angle = direction * pi / 4
            return QPointF(center.x() + radius * cos(angle), center.y() + radius * sin(angle))

        def segment(a, b):
            painter.drawLine(a, b)

        if kind == "select":
            painter.setPen(QPen(color, 2))
            painter.setBrush(QColor("#ffffff"))
            painter.drawPolygon(QPolygonF([
                QPointF(12, 6), QPointF(13, 35), QPointF(20, 28),
                QPointF(25, 40), QPointF(30, 38), QPointF(25, 26),
                QPointF(36, 26),
            ]))
        elif kind in ("track", "switch"):
            dirs = (0, (value or 0) % 8)
            if kind == "track":
                path = QPainterPath(endpoint(dirs[0]))
                path.lineTo(center)
                path.lineTo(endpoint(dirs[1]))
                painter.drawPath(path)
            else:
                tip = (0 + 4) % 8
                segment(endpoint(dirs[0]), center)
                segment(endpoint(dirs[1]), center)
                segment(endpoint(tip), center)
        elif kind == "crossing":
            segment(QPointF(8, 8), QPointF(40, 40))
            segment(QPointF(40, 8), QPointF(8, 40))
        elif kind == "extend":
            segment(QPointF(8, 24), QPointF(40, 24))
            segment(QPointF(32, 16), QPointF(40, 24))
            segment(QPointF(32, 32), QPointF(40, 24))
            segment(QPointF(16, 16), QPointF(16, 32))
            segment(QPointF(10, 22), QPointF(16, 16))
            segment(QPointF(22, 22), QPointF(16, 16))
        elif kind == "topera":
            segment(QPointF(14, 10), QPointF(14, 38))
            segment(QPointF(14, 24), QPointF(38, 24))
        elif kind == "merge":
            segment(QPointF(8, 15), QPointF(24, 24))
            segment(QPointF(8, 33), QPointF(24, 24))
            segment(QPointF(24, 24), QPointF(40, 24))
            segment(QPointF(34, 17), QPointF(40, 24))
            segment(QPointF(34, 31), QPointF(40, 24))
        elif kind in ("rotate-left", "rotate-right"):
            path = QPainterPath()
            if kind == "rotate-right":
                path.arcMoveTo(8, 8, 32, 32, 55)
                path.arcTo(8, 8, 32, 32, 55, 270)
                tip, wing1, wing2 = QPointF(34, 8), QPointF(28, 8), QPointF(34, 14)
            else:
                path.arcMoveTo(8, 8, 32, 32, 125)
                path.arcTo(8, 8, 32, 32, 125, -270)
                tip, wing1, wing2 = QPointF(14, 8), QPointF(20, 8), QPointF(14, 14)
            painter.drawPath(path)
            segment(tip, wing1)
            segment(tip, wing2)
        elif kind in ("undo", "redo"):
            if kind == "undo":
                tip = QPointF(11, 23)
                segment(tip, QPointF(21, 14))
                segment(tip, QPointF(21, 32))
                painter.drawLine(QPointF(12, 23), QPointF(34, 23))
                painter.drawArc(20, 19, 24, 24, -90 * 16, 180 * 16)
            else:
                tip = QPointF(37, 23)
                segment(tip, QPointF(27, 14))
                segment(tip, QPointF(27, 32))
                painter.drawLine(QPointF(36, 23), QPointF(14, 23))
                painter.drawArc(4, 19, 24, 24, 90 * 16, 180 * 16)
        elif kind == "save":
            painter.drawRect(9, 8, 30, 32)
            painter.drawRect(15, 8, 18, 11)
            painter.drawRect(15, 27, 18, 13)
            painter.drawLine(QPointF(19, 10), QPointF(19, 17))
            painter.drawLine(QPointF(29, 10), QPointF(29, 17))
        elif kind == "save-as":
            painter.drawRect(6, 13, 28, 28)
            painter.drawRect(12, 13, 16, 9)
            painter.drawRect(12, 29, 16, 12)
            segment(QPointF(26, 30), QPointF(39, 17))
            segment(QPointF(36, 14), QPointF(41, 19))
        elif kind == "load":
            painter.drawLine(QPointF(8, 17), QPointF(8, 38))
            painter.drawLine(QPointF(8, 38), QPointF(40, 38))
            painter.drawLine(QPointF(40, 38), QPointF(40, 17))
            painter.drawLine(QPointF(8, 17), QPointF(17, 17))
            painter.drawLine(QPointF(31, 17), QPointF(40, 17))
            painter.drawLine(QPointF(24, 7), QPointF(24, 27))
            segment(QPointF(16, 19), QPointF(24, 27))
            segment(QPointF(32, 19), QPointF(24, 27))
        elif kind == "settings":
            painter.drawEllipse(14, 14, 20, 20)
            painter.drawEllipse(20, 20, 8, 8)
            for angle in range(0, 360, 45):
                radians = angle * pi / 180
                inner = QPointF(24 + 11 * cos(radians), 24 + 11 * sin(radians))
                outer = QPointF(24 + 18 * cos(radians), 24 + 18 * sin(radians))
                segment(inner, outer)
        elif kind == "delete":
            painter.drawLine(QPointF(12, 12), QPointF(36, 12))
            painter.drawLine(QPointF(18, 12), QPointF(20, 8))
            painter.drawLine(QPointF(20, 8), QPointF(28, 8))
            painter.drawLine(QPointF(28, 8), QPointF(30, 12))
            painter.drawLine(QPointF(15, 16), QPointF(17, 39))
            painter.drawLine(QPointF(17, 39), QPointF(31, 39))
            painter.drawLine(QPointF(31, 39), QPointF(33, 16))
            painter.drawLine(QPointF(21, 20), QPointF(21, 35))
            painter.drawLine(QPointF(27, 20), QPointF(27, 35))
        elif kind == "export":
            painter.drawRect(7, 18, 25, 23)
            painter.drawLine(QPointF(16, 18), QPointF(16, 12))
            painter.drawLine(QPointF(16, 12), QPointF(30, 12))
            painter.drawLine(QPointF(30, 12), QPointF(30, 20))
            segment(QPointF(23, 7), QPointF(37, 7))
            segment(QPointF(37, 7), QPointF(32, 2))
            segment(QPointF(37, 7), QPointF(32, 12))
        painter.end()
        return QIcon(pixmap)

    def set_mode(self, kind, value):
        self.mode = (kind, value)
        for button, button_kind, button_value in self.tool_buttons:
            button.setChecked((button_kind, button_value) == self.mode)
        self.statusBar().showMessage("Modo selección" if kind == "select" else "Pulsa en el plano para añadir el elemento.")

    def update_work_angle(self):
        try:
            angle = float(self.angle_edit.text().replace(",", "."))
        except ValueError:
            angle = self.work_angle
        if not 0 <= angle < 90:
            angle = self.work_angle
        self.work_angle = angle
        self.angle_edit.setText(f"{angle:g}")

    def set_selection(self, items):
        selection = []
        for item in items:
            if item not in selection:
                selection.append(item)
        if selection and all(item.id.dep == selection[0].id.dep for item in selection):
            dependency = selection[0].id.dep
            if dependency != self.current_dependency:
                self.dependency.setCurrentText(dependency)
                self.switch_dependency()
        self.selected_items = selection
        self.selected = selection[0] if len(selection) == 1 else None
        section_selection = bool(selection) and all(
            isinstance(item, SECTION_TYPES) for item in selection
        )
        if hasattr(self, "delete_button"):
            self.delete_button.setEnabled(bool(selection))
        if hasattr(self, "merge_button"):
            self.merge_button.setEnabled(section_selection)
        if hasattr(self, "junction_button"):
            self.junction_button.setEnabled(section_selection)
        if hasattr(self, "travesia_button"):
            self.travesia_button.setEnabled(
                len(selection) == 1 and isinstance(selection[0], Aguja)
                and not selection[0].is_travesia
            )
        if hasattr(self, "crossing_button"):
            self.crossing_button.setEnabled(section_selection)
        if hasattr(self, "config_panel"):
            self.config_panel.set_item(self.selected if len(selection) == 1 else None)
        self.draw_items()

    def movable_selection(self, items):
        result = [item for item in items if not isinstance(item, Topera)]
        index = 0
        while index < len(result):
            item = result[index]
            if isinstance(item, SECTION_TYPES):
                related_types = (Señal, Topera)
            else:
                related_types = ()
            for candidate in self.items_by_dependency.get(item.id.dep, []):
                matches = (
                    self.signal_matches_section(candidate, item)
                    if isinstance(candidate, Señal)
                    else self.topera_matches_section(candidate, item)
                )
                if (isinstance(candidate, related_types) and matches
                        and candidate not in result):
                    result.append(candidate)
            index += 1
        return result

    @staticmethod
    def signal_matches_section(signal, section):
        if not isinstance(signal, Señal) or not isinstance(section, SECTION_TYPES):
            return False
        return section.matches(signal.position, signal.rotation + Angle(pi))

    @staticmethod
    def topera_matches_section(topera, section):
        if not isinstance(topera, Topera) or not isinstance(section, SECTION_TYPES):
            return False
        return section.matches(topera.position, topera.rotation)

    def signal_for_section(self, section):
        return next((item for item in self.items_by_dependency.get(section.id.dep, [])
                     if self.signal_matches_section(item, section)), None)

    @staticmethod
    def capture_move_state(item):
        return (item.position, item.offset_pie) if isinstance(item, Señal) else item.position

    @staticmethod
    def restore_move_state(item, state):
        if isinstance(item, Señal):
            item.position, item.offset_pie = state
        else:
            item.position = state

    def preview_move(
        self, item, state, delta, move_port=False, fine_delta=None, snap=True
    ):
        dx, dy = delta[0] * GRID_WIDTH, delta[1] * GRID_HEIGHT
        if isinstance(item, Señal):
            item.position, old_offset = state
            if move_port:
                move_delta = fine_delta if fine_delta is not None else (dx, dy)
                item.position = Point(
                    item.position.x + move_delta[0],
                    item.position.y + move_delta[1],
                )
                item.offset_pie = old_offset
            else:
                if fine_delta is None:
                    fine_delta = (dx, dy)
                normal_x = sin(item.rotation.angle)
                normal_y = -cos(item.rotation.angle)
                offset_delta = fine_delta[0] * normal_x + fine_delta[1] * normal_y
                item.offset_pie = max(-GRID_HEIGHT, min(GRID_HEIGHT, old_offset + offset_delta))
        else:
            if fine_delta is not None:
                item.position = Point(
                    state.x + fine_delta[0], state.y + fine_delta[1]
                )
            else:
                item.position = Point(state.x + dx, state.y + dy)
            if snap:
                self.snap_section_to_grid_corners(item)
                self.snap_section_to_connection(
                    item,
                    exclude=getattr(self, "_moving_sections", set()) | {item},
                    rotate=False,
                )

    @staticmethod
    def capture_rotation_state(item, signal=None):
        if isinstance(item, CvLineal):
            geometry = (item.position, list(item.points), item.rotation)
        elif isinstance(item, Aguja):
            paths = tuple(
                list(getattr(item, name))
                for name in ("points_normal", "points_invertida", "points_punta")
            )
            geometry = (item.position, *paths, item.rotation)
        else:
            geometry = (item.position, item.rotation)
        signal_state = ((signal.position, signal.rotation) if signal is not None else None)
        return geometry, signal_state

    @staticmethod
    def restore_rotation_state(item, signal, state):
        geometry, signal_state = state
        if isinstance(item, CvLineal):
            item.position, item.points, item.rotation = geometry
        elif isinstance(item, Aguja):
            (item.position, item.points_normal, item.points_invertida,
             item.points_punta, item.rotation) = geometry
        elif isinstance(item, Cruzamiento):
            (item.position, item.points_p1, item.points_p2,
             item.points_p3, item.points_p4, item.rotation) = geometry
        else:
            item.position, item.rotation = geometry
        if signal is not None:
            signal.position, signal.rotation = signal_state

    @staticmethod
    def set_aguja_rotation(item, rotation):
        item.rotation = rotation

    def allowed_rotations(self):
        angle = self.work_angle
        degrees = (0, angle, 90 - angle, 90, 90 + angle, 180 - angle,
               180, 180 + angle, 270 - angle, 270, 270 + angle,
               360 - angle)
        rotations = []
        for value in degrees:
            rotation = Angle(radians(value))
            if not any(rotation.close_to(previous) for previous in rotations):
                rotations.append(rotation)
        return rotations

    def next_allowed_rotation(self, item, direction):
        rotations = self.allowed_rotations()
        current = min(
            range(len(rotations)),
            key=lambda index: min(
                (item.rotation.angle - rotations[index].angle) % (2 * pi),
                (rotations[index].angle - item.rotation.angle) % (2 * pi),
            ),
        )
        return rotations[(current + (1 if direction > 0 else -1)) % len(rotations)]

    def rotate_geometry(self, item, steps, signal=None):
        if isinstance(item, Topera):
            return False
        before = self.capture_rotation_state(item, signal) if isinstance(item, (Aguja, Cruzamiento)) else None
        if isinstance(item, Señal):
            track = next((candidate for candidate in self.items_by_dependency.get(item.id.dep, [])
                          if isinstance(candidate, SECTION_TYPES)
                          and self.signal_matches_section(item, candidate)), None)
            if track is None:
                return
            options = []
            for position, port_angle in track.get_outs():
                candidate_rotation = port_angle - Angle(pi)
                if not any(candidate_rotation.close_to(rotation)
                           for rotation in self.allowed_rotations()):
                    continue
                if candidate_rotation.close_to(item.rotation):
                    continue
                if steps > 0:
                    distance = (candidate_rotation.angle - item.rotation.angle) % (2 * pi)
                else:
                    distance = (item.rotation.angle - candidate_rotation.angle) % (2 * pi)
                options.append((distance, position, port_angle, candidate_rotation))
            if options:
                _, item.position, _, item.rotation = min(options, key=lambda entry: entry[0])
            return
        signal_port_index = None
        if signal is not None:
            old_ports = item.get_outs()
            signal_port_index = next(
                (index for index, (position, angle) in enumerate(old_ports)
                 if position.close_to(signal.position)
                 and angle.close_to(signal.rotation + Angle(pi))),
                None,
            )
        if isinstance(item, CvLineal):
            new_rotation = self.next_allowed_rotation(item, steps)
            delta = new_rotation - item.rotation
            old_points = [item.position + point.get_rotated(item.rotation)
                          for point in item.points]
            center = (old_points[0] + old_points[-1]) / 2
            new_points = [center + (point - center).get_rotated(delta)
                          for point in old_points]
            new_rotation = item.rotation + delta
            item.position = new_points[0]
            item.points = [
                (point - item.position).get_rotated(-new_rotation.angle)
                for point in new_points
            ]
            item.rotation = new_rotation
        elif isinstance(item, Aguja):
            rotations = self.allowed_rotations()
            connections = self.aguja_connections(item)
            connected_ports = {connection[0] for connection in connections}
            candidate_ports = set(range(len(item.get_outs()))) - connected_ports
            if not candidate_ports:
                candidate_ports = set(range(len(item.get_outs())))
            current = min(
                range(len(rotations)),
                key=lambda index: min(
                    (item.rotation.angle - rotations[index].angle) % (2 * pi),
                    (rotations[index].angle - item.rotation.angle) % (2 * pi),
                ),
            )
            for offset in range(1, len(rotations)):
                candidate = rotations[(current + (1 if steps > 0 else -1) * offset) % len(rotations)]
                item.rotation = candidate
                candidate_connections = [
                    (target, target_angle, port)
                    for _, target, target_angle in connections
                    for new_port_index, (port, port_angle) in enumerate(item.get_outs())
                    if new_port_index in candidate_ports
                    if port_angle.close_to(target_angle + Angle(pi))
                ]
                if candidate_connections:
                    target, _, port = candidate_connections[0]
                    item.position = target - (port - item.position)
                    break
            else:
                self.restore_rotation_state(item, signal, before)
                return False
        elif isinstance(item, Cruzamiento):
            item.rotation = self.next_allowed_rotation(item, steps)
        if isinstance(item, SECTION_TYPES):
            if not isinstance(item, Aguja):
                self.snap_section_to_connection(item, rotate=False)
        if signal is not None:
            if signal_port_index is not None:
                new_port = item.get_outs()[signal_port_index]
                signal.position = new_port[0]
                signal.rotation = new_port[1] - Angle(pi)
        if isinstance(item, Aguja) and not self.aguja_has_connection(item):
            self.restore_rotation_state(item, signal, before)
            return False
        return True

    def can_move_items(self, items, before, delta):
        return True

    def aguja_has_connection(self, item, exclude=()):
        """Return whether an aguja output reaches another track section."""
        return bool(self.aguja_connections(item, exclude))

    def aguja_connection_ports(self, item, exclude=()):
        """Return the output indexes currently connected to another section."""
        return tuple({connection[0] for connection in self.aguja_connections(item, exclude)})

    def aguja_connections(self, item, exclude=()):
        """Return connected output indexes and their target geometry."""
        if not isinstance(item, Aguja):
            return []
        excluded = set(exclude) | {item}
        return [
            (port_index, target, target_angle)
            for port_index, (port, port_angle) in enumerate(item.get_outs())
            for other in self._section_items(excluded)
            for target, target_angle in other.get_outs()
            if sqrt((port.x - target.x) ** 2 + (port.y - target.y) ** 2) <= SNAP_DISTANCE
            and port_angle.close_to(target_angle + Angle(pi))
        ]

    def convert_selected_to_travesia(self):
        if len(self.selected_items) != 1 or not isinstance(self.selected_items[0], Aguja):
            self.statusBar().showMessage("Selecciona una aguja para convertirla en travesía.")
            return
        item = self.selected_items[0]
        if item.is_travesia:
            return
        self.run_command(ConvertTravesiaCommand(self, item))

    def point_clicked(self, scene_point):
        kind, value = self.mode
        if kind == "select":
            candidates = [item for items in self.items_by_dependency.values() for item in items]
            existing = self.view.item_at(scene_point, candidates) if scene_point is not None else None
            if existing is not None:
                self.dependency.setCurrentText(existing.id.dep)
            self.set_selection([existing] if existing is not None else [])
            if existing:
                self.statusBar().showMessage(f"Seleccionado: {existing.id.id}")
            return
        if kind == "extend":
            sections = [
                item for item in self._section_items()
                if self.view.item_at(scene_point, [item]) is item
            ]
            if len(sections) != 1:
                QMessageBox.warning(
                    self, "Vía necesaria",
                    "Pulsa una única sección de vía.",
                )
                return
            self.run_command(ExtendSectionCommand(self, sections[0]))
            return
        if kind == "signal":
            tracks = [
                item for item in self._section_items()
                if self.view.item_at(scene_point, [item]) is item
            ]
            if len(tracks) != 1:
                QMessageBox.warning(
                    self, "Vía necesaria",
                    "Coloca la señal sobre una única vía sin señal.",
                )
                return
            if tracks[0].id.dep != self.current_dependency:
                self.dependency.setCurrentText(tracks[0].id.dep)
                self.switch_dependency()
        if kind == "topera":
            connection = min(
                (
                    (sqrt((port.x - scene_point.x()) ** 2
                          + (port.y - scene_point.y()) ** 2), track, port, angle)
                    for track in self._section_items()
                    for port, angle in track.get_outs()
                ),
                key=lambda candidate: candidate[0],
                default=None,
            )
            if connection is None or connection[0] > SNAP_DISTANCE:
                QMessageBox.warning(
                    self, "Vía necesaria",
                    "Coloca la topera sobre un puerto libre de una sección de vía.",
                )
                return
            _, track, port, angle = connection
            if any(
                isinstance(other, Topera)
                and self.topera_matches_section(other, track)
                for other in self.items_by_dependency.get(track.id.dep, [])
            ):
                QMessageBox.warning(
                    self, "Puerto ocupado",
                    "Ese puerto ya tiene una topera.",
                )
                return
            if track.id.dep != self.current_dependency:
                self.dependency.setCurrentText(track.id.dep)
                self.switch_dependency()
            suffix = 1
            while any(
                item.id.id == f"{track.id.dep}:T{suffix}"
                for item in self.items_by_dependency.get(track.id.dep, [])
            ):
                suffix += 1
            item = Topera(
                IdElemento(f"FV{suffix}", track.id.dep), port, angle
            )
            self.run_command(AddItemCommand(self, item))
            return
        dependency = self.dependency.currentText().strip()
        if not dependency:
            QMessageBox.warning(self, "Falta la dependencia", "Indica el nombre de la estación antes de añadir elementos.")
            self.dependency.setFocus()
            return
        if kind == "track":
            center = Point(scene_point.x(), scene_point.y())
            if value == 4:
                start = center + direction_vector(4, GRID_WIDTH / 2)
                end = center + direction_vector(0, GRID_WIDTH / 2)
                points = [end - start]
            else:
                length = GRID_HEIGHT / 2 / sin(radians(self.work_angle))
                start = center + direction_vector(4, length)
                end = center + Point(length * cos(radians(self.work_angle)), length * sin(radians(self.work_angle)))
                points = [center - start, end - start]
            item = CvLineal(IdElemento("", dependency), start, points, Angle(0))
            self.auto_rotate_for_connections(item, center=center)
            self.orient_section_for_connection(item, scene_point)
            self.snap_section_to_grid_corners(item, scene_point)
            self.snap_section_to_connection(item, anchor=scene_point)
            self.run_command(AddItemCommand(self, item))
            return
        prefix = {"switch": "A", "signal": ""}[kind]
        suffix = 1
        while any(
            item.id.id == f"{dependency}:{prefix}{suffix}"
            for item in self.items_by_dependency.get(dependency, [])
        ):
            suffix += 1
        short_id = f"{prefix}{suffix}"
        try:
            ident = IdElemento(short_id, dependency)
        except ValueError as exc:
            QMessageBox.warning(self, "Identificador inválido", str(exc))
            return
        if kind == "switch":
            center = Point(scene_point.x(), scene_point.y())
            branch_side = value if value in (-1, 1) else 1

            radan = radians(self.work_angle)
            cut1, bounds = get_junction_bounds(radan)
            cut1 = Point(*cut1)
            bounds = [Point(*p)-cut1 for p in bounds]
            punta = [bounds[2], bounds[2]+Point(-8, 0)]
            normal = [bounds[0], bounds[0]+Point(8, 0)]
            diff = GRID_HEIGHT / 2 - bounds[1][1]
            invertida = [Point(bounds[1][0], bounds[1][1]*branch_side), Point(bounds[1][0]+diff/tan(radan), GRID_HEIGHT / 2 * branch_side)]

            item = Aguja(ident, center, Angle(0), punta, normal, invertida, self.work_angle)
            self.orient_section_for_connection(item, scene_point)
            self.snap_section_to_grid_corners(item, scene_point)
            if not self.snap_section_to_connection(item, anchor=scene_point):
                QMessageBox.warning(
                    self, "Vía necesaria",
                    "Coloca el cambio de agujas cerca de una salida de vía existente.",
                )
                return
        else:
            track = tracks[0]
            click_model = Point(scene_point.x(), scene_point.y())
            port_index = min(
                range(len(track.get_outs())),
                key=lambda i: (track.get_outs()[i][0].x - click_model.x) ** 2
                + (track.get_outs()[i][0].y - click_model.y) ** 2,
            )
            port, direction = track.get_outs()[port_index]
            item = Señal(ident, port, TipoSeñal.Entrada,
                         6.0, direction - Angle(pi))
        if isinstance(item, Señal):
            self.auto_rotate_for_connections(item)
        self.run_command(AddItemCommand(self, item))

    def _section_items(self, exclude=()):
        excluded = set(exclude)
        return [
            item for values in self.items_by_dependency.values()
            for item in values
            if isinstance(item, SECTION_TYPES) and item not in excluded
        ]

    @staticmethod
    def capture_geometry_state(item):
        if isinstance(item, CvLineal):
            return item.position, list(item.points)
        if isinstance(item, Cruzamiento):
            return (
                item.position, list(item.points_p1), list(item.points_p2),
                list(item.points_p3), list(item.points_p4),
            )
        return (
            item.position,
            list(item.points_normal),
            list(item.points_invertida),
            list(item.points_punta),
        )

    @staticmethod
    def restore_geometry_state(item, state):
        if isinstance(item, CvLineal):
            item.position, item.points = state
        elif isinstance(item, Cruzamiento):
            (item.position, item.points_p1, item.points_p2,
             item.points_p3, item.points_p4) = state
        else:
            (item.position, item.points_normal, item.points_invertida,
             item.points_punta) = state

    @staticmethod
    def normalize_cv_points(item):
        if not isinstance(item, CvLineal) or len(item.points) <= 2:
            return
        first, last = item.points[0], item.points[-1]
        axis = last - first
        axis_length = sqrt(axis.x ** 2 + axis.y ** 2)
        if axis_length <= 1e-9:
            return
        if all(
            abs(axis.x * (point.y - first.y) - axis.y * (point.x - first.x)) <= 1e-6
            for point in item.points[1:-1]
        ):
            item.points = [first, last]

    @staticmethod
    def merge_order(first, second):
        if isinstance(first, CvLineal) and isinstance(second, (Aguja, Cruzamiento)):
            return second, first
        return first, second

    @staticmethod
    def _world_path(item, path):
        return [item.position + point.get_rotated(item.rotation) for point in path]

    @staticmethod
    def _path_for_output(item, port_index):
        if isinstance(item, CvLineal):
            path = TrackEditor._world_path(item, item.points)
            return path if port_index == 0 else list(reversed(path))
        if isinstance(item, Cruzamiento):
            return list(reversed(TrackEditor._world_path(
                item, (item.points_p1, item.points_p2, item.points_p3, item.points_p4)[port_index]
            )))
        if item.is_travesia and port_index == 2:
            return [item.position, item.position]
        paths = (item.points_normal, item.points_invertida, item.points_punta)
        return list(reversed(TrackEditor._world_path(item, paths[port_index])))

    @staticmethod
    def _line_intersection(first_position, first_angle, second_position, second_angle):
        first_vector = (cos(first_angle.angle), sin(first_angle.angle))
        second_vector = (cos(second_angle.angle), sin(second_angle.angle))
        cross = first_vector[0] * second_vector[1] - first_vector[1] * second_vector[0]
        if abs(cross) <= 1e-9:
            return None
        offset = second_position - first_position
        distance = (offset.x * second_vector[1] - offset.y * second_vector[0]) / cross
        return first_position + Point(first_vector[0], first_vector[1]) * distance

    @classmethod
    def _line_join_point(cls, first, second):
        intersection = cls._line_intersection(
            first[0], first[1], second[0], second[1]
        )
        if intersection is not None:
            return intersection
        direction = (cos(first[1].angle), sin(first[1].angle))
        offset = second[0] - first[0]
        if abs(offset.x * direction[1] - offset.y * direction[0]) <= 1e-6:
            return (first[0] + second[0]) / 2
        return None

    def _merge_geometry(self, destination, source):
        destination_outs = destination.get_outs()
        source_outs = source.get_outs()
        _, destination_port, source_port = min(
            (
                sqrt((first[0].x - second[0].x) ** 2 + (first[0].y - second[0].y) ** 2),
                destination_index,
                source_index,
            )
            for destination_index, first in enumerate(destination_outs)
            for source_index, second in enumerate(source_outs)
        )
        if ((isinstance(destination, Aguja) and destination.is_travesia and destination_port == 2)
                or (isinstance(source, Aguja) and source.is_travesia and source_port == 2)):
            return False
        intersection = self._line_join_point(
            destination_outs[destination_port], source_outs[source_port]
        )
        if intersection is None:
            return False
        source_path = self._path_for_output(source, source_port)
        source_path[0] = intersection
        if isinstance(destination, CvLineal):
            destination_path = list(reversed(self._path_for_output(destination, destination_port)))
            destination_path[-1] = intersection
            merged_path = destination_path + source_path[1:]
            destination.position = merged_path[0]
            destination.points = [
                (point - destination.position).get_rotated(-destination.rotation.angle)
                for point in merged_path
            ]
            self.normalize_cv_points(destination)
            return True
        if isinstance(destination, Cruzamiento):
            paths = [
                destination.points_p1, destination.points_p2,
                destination.points_p3, destination.points_p4,
            ]
        else:
            paths = [destination.points_normal, destination.points_invertida, destination.points_punta]
        destination_path = self._world_path(destination, paths[destination_port])
        destination_path[-1] = intersection
        merged_path = destination_path + source_path[1:]
        local_path = [
            (point - destination.position).get_rotated(-destination.rotation.angle)
            for point in merged_path
        ]
        paths[destination_port] = local_path
        if isinstance(destination, Cruzamiento):
            (destination.points_p1, destination.points_p2,
             destination.points_p3, destination.points_p4) = paths
        else:
            (destination.points_normal, destination.points_invertida,
             destination.points_punta) = paths
        return True

    def merge_sections(self, destination, source):
        bucket = self.items_by_dependency.get(source.id.dep, [])
        if source not in bucket or not self._merge_geometry(destination, source):
            return False
        bucket.remove(source)
        return True

    def merge_selected(self):
        if len(self.selected_items) != 2:
            self.statusBar().showMessage("Selecciona dos secciones de vía para unirlas.")
            return
        first, second = self.selected_items
        if not all(isinstance(item, (CvLineal, Aguja, Cruzamiento)) for item in self.selected_items):
            self.statusBar().showMessage("Solo se pueden unir CV, agujas y cruzamientos.")
            return
        if not any(isinstance(item, CvLineal) for item in self.selected_items):
            self.statusBar().showMessage("La unión debe incluir una CV lineal.")
            return
        destination, source = self.merge_order(first, second)
        if self._line_join_point(
            destination.get_outs()[0], source.get_outs()[0]
        ) is None and all(
            self._line_join_point(first_out, second_out) is None
            for first_out in destination.get_outs()
            for second_out in source.get_outs()
        ):
            self.statusBar().showMessage("Las salidas seleccionadas no tienen un punto de corte único.")
            return
        self.run_command(MergeSectionsCommand(self, first, second))

    def generate_junction_selected(self):
        if len(self.selected_items) != 2 or not all(
            isinstance(item, (CvLineal, Aguja)) for item in self.selected_items
        ):
            self.statusBar().showMessage("Selecciona dos secciones de vía.")
            return
        first, second = self.selected_items

        first_outs, second_outs = first.get_outs(), second.get_outs()
        _, first_port_index, second_port_index = min(
            (
                (first_output[0].x - second_output[0].x) ** 2
                + (first_output[0].y - second_output[0].y) ** 2,
                first_index,
                second_index,
            )
            for first_index, first_output in enumerate(first_outs)
            for second_index, second_output in enumerate(second_outs)
        )
        first_port = first_outs[first_port_index]
        second_port = second_outs[second_port_index]
        intersection = self._line_intersection(
            first_port[0], first_port[1], second_port[0], second_port[1]
        )
        if intersection is None:
            self.statusBar().showMessage("Las rectas seleccionadas son paralelas.")
            return
        rotation = Angle(atan2(
            first_port[0].y - intersection.y,
            first_port[0].x - intersection.x,
        ))
        local_second = (second_port[0] - intersection).get_rotated(-rotation.angle)
        junction_angle = abs((first_port[1].angle - second_port[1].angle) % pi)
        junction_angle = min(junction_angle, pi - junction_angle)
        if junction_angle <= 1e-6:
            self.statusBar().showMessage("Las rectas seleccionadas son paralelas.")
            return

        dependency = first.id.dep
        suffix = 1
        while any(
            item.id.id == f"{dependency}:A{suffix}"
            for item in self.items_by_dependency.get(dependency, [])
        ):
            suffix += 1
        ident = IdElemento(f"A{suffix}", dependency)

        cut1, bounds = get_junction_bounds(junction_angle)
        cut1 = Point(*cut1)
        bounds = [Point(*point) - cut1 for point in bounds]
        punta = [bounds[2], bounds[2] + Point(-8, 0)]
        normal_target = (first_port[0] - intersection).get_rotated(-rotation.angle)
        normal = [bounds[0], bounds[0] + Point(8, 0), normal_target]
        branch_start_distance = sqrt(bounds[1].x ** 2 + bounds[1].y ** 2)
        branch_target_distance = sqrt(local_second.x ** 2 + local_second.y ** 2)
        branch_start = local_second * (branch_start_distance / branch_target_distance)
        invertida = [branch_start, local_second]
        item = Aguja(
            ident, intersection, rotation, punta, normal, invertida,
            degrees(junction_angle),
        )
        self.run_command(AddItemCommand(self, item))

    def generate_crossing_selected(self):
        if len(self.selected_items) != 2 or not all(
            isinstance(item, SECTION_TYPES) for item in self.selected_items
        ):
            self.statusBar().showMessage("Selecciona dos secciones de vía.")
            return
        first, second = self.selected_items
        first_outs, second_outs = first.get_outs(), second.get_outs()
        _, first_port_index, second_port_index = min(
            (
                (first_output[0].x - second_output[0].x) ** 2
                + (first_output[0].y - second_output[0].y) ** 2,
                first_index,
                second_index,
            )
            for first_index, first_output in enumerate(first_outs)
            for second_index, second_output in enumerate(second_outs)
        )
        first_port = first_outs[first_port_index]
        second_port = second_outs[second_port_index]
        intersection = self._line_intersection(
            first_port[0], first_port[1], second_port[0], second_port[1]
        )
        if intersection is None:
            self.statusBar().showMessage("Las rectas seleccionadas son paralelas.")
            return
        crossing_angle = abs((first_port[1].angle - second_port[1].angle) % pi)
        crossing_angle = min(crossing_angle, pi - crossing_angle)
        if crossing_angle <= 1e-6:
            self.statusBar().showMessage("Las rectas seleccionadas son paralelas.")
            return
        base_angles = (0, crossing_angle, pi, crossing_angle + pi)
        rotation = None
        best_error = float("inf")
        for first_index, first_base_angle in enumerate(base_angles):
            candidate_rotation = first_port[1] - Angle(first_base_angle)
            for second_base_angle in base_angles:
                candidate_angle = (candidate_rotation + Angle(second_base_angle)).angle
                error = abs((candidate_angle - second_port[1].angle) % (2 * pi))
                error = min(error, 2 * pi - error)
                if error < best_error:
                    best_error = error
                    rotation = candidate_rotation
        bounds = [Point(*point) for point in get_crossing_bounds(crossing_angle)]
        paths = []
        for bound in bounds:
            length = sqrt(bound.x ** 2 + bound.y ** 2)
            direction = bound * (8 / length)
            paths.append([bound, bound + direction])
        dependency = first.id.dep
        suffix = 1
        while any(
            item.id.id == f"{dependency}:X{suffix}"
            for item in self.items_by_dependency.get(dependency, [])
        ):
            suffix += 1
        item = Cruzamiento(
            IdElemento(f"X{suffix}", dependency), intersection, rotation, 0,
            *paths, degrees(crossing_angle),
        )
        self.run_command(AddItemCommand(self, item))

    def begin_extend(self, point):
        point_x = point.x() if callable(point.x) else point.x
        point_y = point.y() if callable(point.y) else point.y
        candidates = []
        for item in self._section_items():
            for port, (position, _) in enumerate(item.get_outs()):
                if isinstance(item, Aguja) and item.is_travesia and port == 2:
                    continue
                distance = sqrt((position.x - point_x) ** 2 + (position.y - point_y) ** 2)
                candidates.append((distance, item, port))
        if not candidates:
            return None
        distance, item, port = min(candidates, key=lambda candidate: candidate[0])
        if distance > SNAP_DISTANCE:
            return None
        return item, port, self.capture_geometry_state(item)

    def preview_extend(self, item, port_index, before, point):
        if isinstance(item, Aguja) and item.is_travesia and port_index == 2:
            return
        self.restore_geometry_state(item, before)
        point_x = point.x() if callable(point.x) else point.x
        point_y = point.y() if callable(point.y) else point.y
        output_position, output_angle = item.get_outs()[port_index]
        projection = (
            (point_x - output_position.x) * cos(output_angle.angle)
            + (point_y - output_position.y) * sin(output_angle.angle)
        )
        direction_x, direction_y = cos(output_angle.angle), sin(output_angle.angle)
        if isinstance(item, CvLineal):
            world_points = [
                item.position + path_point.get_rotated(item.rotation)
                for path_point in item.points
            ]
            adjacent = world_points[1] if port_index == 0 else world_points[-2]
            minimum = (
                (adjacent.x - output_position.x) * direction_x
                + (adjacent.y - output_position.y) * direction_y
                + 1.0
            )
        else:
            attributes = (
                ("points_p1", "points_p2", "points_p3", "points_p4")
                if isinstance(item, Cruzamiento)
                else ("points_normal", "points_invertida", "points_punta")
            )
            path = getattr(item, attributes[port_index])
            previous = item.position + path[-2].get_rotated(item.rotation)
            minimum = (
                (previous.x - output_position.x) * direction_x
                + (previous.y - output_position.y) * direction_y
                + 1.0
            )
        distance = max(minimum, projection)
        target = Point(
            output_position.x + cos(output_angle.angle) * distance,
            output_position.y + sin(output_angle.angle) * distance,
        )
        target = self.snap_point_to_grid_corner(target)
        target = self.magnet_target(item, output_position, output_angle, target)
        if isinstance(item, CvLineal):
            world_points = [
                item.position + point.get_rotated(item.rotation)
                for point in item.points
            ]
            if port_index == 0:
                world_points[0] = target
                item.position = target
                item.points = [
                    (world_point - target).get_rotated(-item.rotation.angle)
                    for world_point in world_points
                ]
            else:
                world_points[-1] = target
                item.points = [
                    (world_point - item.position).get_rotated(-item.rotation.angle)
                    for world_point in world_points
                ]
            return
        attributes = (
            ("points_p1", "points_p2", "points_p3", "points_p4")
            if isinstance(item, Cruzamiento)
            else ("points_normal", "points_invertida", "points_punta")
        )
        path = list(getattr(item, attributes[port_index]))
        path[-1] = (target - item.position).get_rotated(-item.rotation.angle)
        setattr(item, attributes[port_index], path)

    def snap_section_to_grid_corners(self, item, anchor=None):
        if not isinstance(item, SECTION_TYPES):
            return False
        if anchor is not None:
            anchor = Point(
                anchor.x() if callable(anchor.x) else anchor.x,
                anchor.y() if callable(anchor.y) else anchor.y,
            )
        candidates = []
        for port, (position, _) in enumerate(item.get_outs()):
            if anchor is None:
                column = round(position.x / GRID_WIDTH)
                row = round(position.y / GRID_HEIGHT)
            else:
                column = round(anchor.x / GRID_WIDTH)
                row = round(anchor.y / GRID_HEIGHT)
            corner = Point(column * GRID_WIDTH, row * GRID_HEIGHT)
            distance = sqrt((position.x - corner.x) ** 2 + (position.y - corner.y) ** 2)
            if anchor is not None:
                distance = sqrt((anchor.x - corner.x) ** 2 + (anchor.y - corner.y) ** 2)
            if distance <= SNAP_DISTANCE:
                candidates.append((distance, port, corner))
        if not candidates:
            return False
        _, port, corner = min(candidates, key=lambda candidate: candidate[0])
        output = item.get_outs()[port][0]
        item.position = item.position + corner - output
        return True

    @staticmethod
    def snap_point_to_grid_corner(point):
        column = round(point.x / GRID_WIDTH)
        row = round(point.y / GRID_HEIGHT)
        corner = Point(column * GRID_WIDTH, row * GRID_HEIGHT)
        distance = sqrt((point.x - corner.x) ** 2 + (point.y - corner.y) ** 2)
        return corner if distance <= SNAP_DISTANCE else point

    def magnet_target(self, item, output_position, output_angle, target):
        normal_x, normal_y = -sin(output_angle.angle), cos(output_angle.angle)
        candidates = []
        for other in self._section_items({item}):
            for other_position, other_angle in other.get_outs():
                if not output_angle.close_to(other_angle + Angle(pi)):
                    continue
                offset_x = other_position.x - output_position.x
                offset_y = other_position.y - output_position.y
                offset_length = sqrt(offset_x ** 2 + offset_y ** 2)
                line_error = abs(offset_x * normal_x + offset_y * normal_y)
                if line_error > GEOMETRY_TOLERANCE * max(1.0, offset_length):
                    continue
                distance = sqrt(
                    (other_position.x - target.x) ** 2
                    + (other_position.y - target.y) ** 2
                )
                if distance <= SNAP_DISTANCE:
                    candidates.append((distance, other_position))
        return min(candidates, key=lambda candidate: candidate[0])[1] if candidates else target

    @staticmethod
    def _extend_path(path, length):
        if len(path) < 2:
            return path
        previous, endpoint = path[-2], path[-1]
        direction = endpoint - previous
        magnitude = sqrt(direction.x ** 2 + direction.y ** 2)
        if magnitude <= 1e-9:
            return path
        return path + [endpoint + direction * (length / magnitude)]

    def extend_section(self, item):
        extension_length = GRID_WIDTH
        if isinstance(item, CvLineal):
            first, last = item.points[0], item.points[-1]
            axis = last - first
            magnitude = sqrt(axis.x ** 2 + axis.y ** 2)
            if magnitude <= 1e-9:
                return
            factor = (magnitude + 2 * extension_length) / magnitude
            midpoint = (first + last) / 2
            extended = [midpoint + (point - midpoint) * factor for point in item.points]
            new_first = extended[0]
            item.position = item.position + new_first.get_rotated(item.rotation)
            item.points = [point - new_first for point in extended]
            return
        if isinstance(item, Aguja):
            item.points_normal = self._extend_path(item.points_normal, extension_length)
            item.points_invertida = self._extend_path(item.points_invertida, extension_length)
            if not item.is_travesia:
                item.points_punta = self._extend_path(item.points_punta, extension_length)
        elif isinstance(item, Cruzamiento):
            item.points_p1 = self._extend_path(item.points_p1, extension_length)
            item.points_p2 = self._extend_path(item.points_p2, extension_length)
            item.points_p3 = self._extend_path(item.points_p3, extension_length)
            item.points_p4 = self._extend_path(item.points_p4, extension_length)

    def snap_section_to_connection(
        self, item, anchor=None, exclude=(), rotate=True, port_indices=None
    ):
        if not isinstance(item, SECTION_TYPES):
            return False
        if anchor is not None:
            anchor = Point(
                anchor.x() if callable(anchor.x) else anchor.x,
                anchor.y() if callable(anchor.y) else anchor.y,
            )
        excluded = set(exclude) | {item}
        candidates = []
        for other in self._section_items(excluded):
            for target, target_angle in other.get_outs():
                for port_index, (port, port_angle) in enumerate(item.get_outs()):
                    if port_indices is not None and port_index not in port_indices:
                        continue
                    if not port_angle.close_to(target_angle + Angle(pi)):
                        continue
                    if anchor is None:
                        distance = sqrt((target.x - port.x) ** 2 + (target.y - port.y) ** 2)
                    else:
                        distance = sqrt((target.x - anchor.x) ** 2 + (target.y - anchor.y) ** 2)
                    if distance > SNAP_DISTANCE:
                        continue
                    rotation_delta = target_angle + Angle(pi) - port_angle if rotate else Angle(0)
                    rotated_offset = (port - item.position).get_rotated(rotation_delta)
                    position = target - rotated_offset
                    candidates.append((distance, port_index, target, rotation_delta, position))
        if not candidates:
            return False
        _, _, _, rotation_delta, position = min(candidates, key=lambda candidate: candidate[0])
        if rotate:
            item.rotation = item.rotation + rotation_delta
        item.position = position
        return True

    def snap_group_to_grid(self, items, mouse_position):
        moving_sections = [
            item for item in items if isinstance(item, SECTION_TYPES)
        ]
        if not moving_sections:
            return False
        mouse_x = mouse_position.x() if callable(mouse_position.x) else mouse_position.x
        mouse_y = mouse_position.y() if callable(mouse_position.y) else mouse_position.y
        ports = [
            port
            for item in moving_sections
            for port, _ in item.get_outs()
        ]
        port = min(
            ports,
            key=lambda candidate: (
                (candidate.x - mouse_x) ** 2
                + (candidate.y - mouse_y) ** 2
            ),
        )
        corner = self.snap_point_to_grid_corner(port)
        if corner is port:
            return False
        correction = corner - port
        for item in items:
            item.position = item.position + correction
        return True

    def orient_section_for_connection(self, item, anchor):
        if anchor is None:
            return False
        anchor = Point(
            anchor.x() if callable(anchor.x) else anchor.x,
            anchor.y() if callable(anchor.y) else anchor.y,
        )
        targets = [
            (position, angle)
            for other in self._section_items({item})
            for position, angle in other.get_outs()
        ]
        if not targets:
            return False
        target, target_angle = min(
            targets,
            key=lambda candidate: (
                (candidate[0].x - anchor.x) ** 2
                + (candidate[0].y - anchor.y) ** 2
            ),
        )
        distance = sqrt((target.x - anchor.x) ** 2 + (target.y - anchor.y) ** 2)
        if distance > SNAP_DISTANCE:
            return False
        item_outputs = item.get_outs()
        _, (_, port_angle) = min(
            enumerate(item_outputs),
            key=lambda candidate: abs(
                (target.x - item_outputs[candidate[0]][0].x) ** 2
                + (target.y - item_outputs[candidate[0]][0].y) ** 2
            ),
        )
        item.rotation = item.rotation + target_angle + Angle(pi) - port_angle
        return True

    def auto_rotate_for_connections(self, item, center=None):
        if isinstance(item, Señal):
            return
        if center is None:
            if isinstance(item, CvLineal):
                points = [item.position + point.get_rotated(item.rotation)
                          for point in item.points]
                center = (points[0] + points[-1]) / 2
            else:
                center = item.position
        others = [other for values in self.items_by_dependency.values() for other in values
                  if isinstance(other, SECTION_TYPES)]
        best_rotation, best_score = 0, -1
        if isinstance(item, CvLineal):
            local_midpoint = (item.points[0] + item.points[-1]) / 2
        rotations = self.allowed_rotations()
        for candidate_rotation in rotations:
            if isinstance(item, CvLineal):
                item.position = center - local_midpoint.get_rotated(candidate_rotation)
                item.rotation = candidate_rotation
            else:
                item.position = center
                self.set_aguja_rotation(item, candidate_rotation)
            score = sum(
                1 for point, angle in item.get_outs()
                for other in others if other is not item
                for other_point, other_angle in other.get_outs()
                if point.close_to(other_point)
                and angle.close_to(other_angle + Angle(pi))
            )
            if score > best_score:
                best_rotation, best_score = candidate_rotation, score
        if isinstance(item, CvLineal):
            item.position = center - local_midpoint.get_rotated(best_rotation)
            item.rotation = best_rotation
        else:
            item.position = center
            self.set_aguja_rotation(item, best_rotation)

    def apply_item_configuration(self, item, short_id, cv_name, signal_type, side, focos=None, velocidades=None, cv_options=None, position=None):
        if item is None:
            return False
        item_dependency = item.id.dep
        if not short_id:
            QMessageBox.warning(
                self,
                "Datos incompletos",
                "Indica el nombre del elemento y el circuito de vía cuando corresponda.",
            )
            return False
        try:
            new_id = IdElemento(short_id, item_dependency)
            new_cv = IdElemento(cv_name, item_dependency) if isinstance(item, Aguja) else None
        except ValueError as exc:
            QMessageBox.warning(self, "Identificador inválido", str(exc))
            return False
        if isinstance(item, (Aguja, Señal)) and any(
            other is not item and isinstance(other, type(item)) and other.id.id == new_id.id
            for other in self.items_by_dependency.get(item_dependency, [])
        ):
            QMessageBox.warning(
                self,
                "Identificador duplicado",
                f"Ya existe un elemento de este tipo con id {new_id.id}.",
            )
            return False
        new_state = {"id": new_id, "position": position or item.position}
        if isinstance(item, CvLineal):
            new_state["lado"] = side
            cv_type, offset = cv_options or (item.tipo_cv, item.offset)
            new_state.update(tipo_cv=cv_type, offset=offset)
        elif isinstance(item, Aguja):
            new_state["cv"] = new_cv
            new_state["lado"] = side
        elif isinstance(item, Señal):
            new_state["tipo_señal"] = signal_type
            new_state["focos"] = list(focos or [])
            new_state["aspectos_pantalla"] = list(velocidades or [])
        self.run_command(EditItemCommand(self, item, new_state, f"Configurar {item.id.id}"))
        return True

    def rotate_selected(self, amount):
        if self.selected is None or len(self.selected_items) != 1:
            self.statusBar().showMessage("Selecciona primero un elemento.")
            return
        if isinstance(self.selected, Topera):
            self.statusBar().showMessage("La topera está fijada al puerto de su sección de vía.")
            return
        if isinstance(self.selected, Señal):
            track = next((item for item in self.items_by_dependency.get(self.selected.id.dep, [])
                          if self.signal_matches_section(self.selected, item)), None)
            if track is None:
                self.statusBar().showMessage("La señal no está asociada a una salida de vía.")
                return
            current = self.selected.rotation
            options = []
            for position, port_angle in track.get_outs():
                candidate = port_angle - Angle(pi)
                if not any(candidate.close_to(rotation)
                           for rotation in self.allowed_rotations()):
                    continue
                if candidate.close_to(current):
                    continue
                diff = (candidate.angle - current.angle) % (2 * pi)
                if amount < 0:
                    diff = -((current.angle - candidate.angle) % (2 * pi))
                options.append((abs(diff), position, port_angle, candidate))
            if not options:
                self.statusBar().showMessage("La vía no tiene otro sentido disponible para la señal.")
                return
        self.run_command(RotateCommand(self, self.selected, amount))

    def delete_selected(self):
        items = list(self.selected_items)
        if not items and self.selected is not None:
            items = [self.selected]
        if not items:
            self.statusBar().showMessage("Selecciona primero un elemento.")
            return
        selected_set = set(items)
        for item in items:
            if isinstance(item, SECTION_TYPES) and any(
                isinstance(other, Señal) and self.signal_matches_section(other, item)
                and other not in selected_set
                for other in self.items_by_dependency.get(item.id.dep, [])
            ):
                QMessageBox.warning(
                    self, "La vía tiene una señal",
                    "Incluye o elimina primero la señal asociada a esta vía.",
                )
                return
            if isinstance(item, SECTION_TYPES) and any(
                isinstance(other, Topera) and self.topera_matches_section(other, item)
                and other not in selected_set
                for other in self.items_by_dependency.get(item.id.dep, [])
            ):
                QMessageBox.warning(
                    self, "La vía tiene una topera",
                    "Incluye o elimina primero la topera asociada a esta vía.",
                )
                return
        self.run_command(DeleteItemsCommand(self, items))

    def run_command(self, command):
        if command.execute() is False:
            return
        self.undo_stack.append(command)
        self.redo_stack.clear()
        self.is_dirty = True
        self.statusBar().showMessage(f"Comando: {command.text}")
        self.update_history_buttons()

    def undo(self):
        if not self.undo_stack:
            return
        command = self.undo_stack.pop()
        command.undo()
        self.redo_stack.append(command)
        self.is_dirty = True
        self.statusBar().showMessage(f"Deshecho: {command.text}")
        self.update_history_buttons()

    def redo(self):
        if not self.redo_stack:
            return
        command = self.redo_stack.pop()
        command.execute()
        self.undo_stack.append(command)
        self.is_dirty = True
        self.statusBar().showMessage(f"Rehecho: {command.text}")
        self.update_history_buttons()

    def update_history_buttons(self):
        self.undo_button.setEnabled(bool(self.undo_stack))
        self.redo_button.setEnabled(bool(self.redo_stack))

    def save_layout(self):
        self.switch_dependency()
        if not self.layout_path:
            return self.save_layout_as()
        return self.write_layout(self.layout_path)

    def save_layout_as(self):
        self.switch_dependency()
        dependency = self.dependency.currentText().strip()
        if not dependency:
            QMessageBox.warning(self, "Falta la dependencia", "Indica el nombre de la estación antes de guardar.")
            return False
        initial_path = self.layout_path or f"{dependency}.json"
        path, _ = QFileDialog.getSaveFileName(
            self, "Guardar layout como", initial_path, "Layout de vías (*.json)"
        )
        if not path:
            return False
        if not Path(path).suffix:
            path += ".json"
        return self.write_layout(path)

    def write_layout(self, path):
        dependency = self.current_dependency
        try:
            self.items_by_dependency[dependency] = self.items

            data = serialize_layout(self.items_by_dependency)
            with Path(path).open("w", encoding="utf-8") as output:
                json.dump(data, output, ensure_ascii=False, indent=2)
                output.write("\n")
        except (OSError, UnicodeError, ValueError, KeyError, TypeError, AttributeError) as exc:
            QMessageBox.critical(self, "Error al guardar", str(exc))
            return False
        self.layout_path = path
        self.is_dirty = False
        self.statusBar().showMessage(f"Layout guardado: {path}")
        return True

    def exportar_config_ence(self):
        self.switch_dependency()
        dependency = self.current_dependency
        initial_path = f"ENCE_{dependency}.json"
        path, _ = QFileDialog.getSaveFileName(
            self, "Exportar JSON del enclavamiento", initial_path,
            "Configuración JSON (*.json)"
        )
        if not path:
            return
        if not Path(path).suffix:
            path += ".json"
        items = [item for dependency_items in self.items_by_dependency.values()
                 for item in dependency_items]
        try:
            dependencies = generar_config_ence(deepcopy(items))
            output = {"Dependencias": dependencies}
            with open(path, "w", encoding="utf-8") as destination:
                json.dump(output, destination, ensure_ascii=False, indent=2)
                destination.write("\n")
        except Exception as exc:
            frames = traceback.extract_tb(exc.__traceback__)
            if frames:
                origin = frames[-1]
                location = f"{Path(origin.filename).name}, línea {origin.lineno}"
                source = f"\n{origin.line.strip()}" if origin.line else ""
                message = f"{location}{source}\n\n{exc}"
            else:
                message = str(exc)
            QMessageBox.critical(self, "Error al exportar", message)
            return
        self.statusBar().showMessage(f"Configuración exportada: {path}")

    def exportar_svg(self):
        self.switch_dependency()
        dependency = self.current_dependency
        initial_path = f"{dependency}.svg"
        path, _ = QFileDialog.getSaveFileName(
            self, "Exportar layout a SVG", initial_path, "Imágenes SVG (*.svg)"
        )
        if not path:
            return
        if not Path(path).suffix:
            path += ".svg"
        items = [item for dependency_items in self.items_by_dependency.values()
                 for item in dependency_items]
        try:
            with Path(path).open("w", encoding="utf-8") as destination:
                destination.write(export_svg(items))
        except (OSError, UnicodeError, ValueError, KeyError, TypeError, AttributeError) as exc:
            QMessageBox.critical(self, "Error al exportar", str(exc))
            return
        self.statusBar().showMessage(f"Layout SVG exportado: {path}")

    def load_layout(self):
        self.switch_dependency()
        path, _ = QFileDialog.getOpenFileName(
            self, "Cargar layout", self.layout_path or "",
            "Layout de vías (*.json);;Todos los archivos (*)"
        )
        if not path:
            return
        try:
            with Path(path).open("r", encoding="utf-8") as source:
                layouts = deserialize_layout(json.load(source))
        except (OSError, UnicodeError, ValueError, KeyError, TypeError, AttributeError) as exc:
            QMessageBox.critical(self, "No se pudo cargar el layout", str(exc))
            return
        command = ReplaceLayoutCommand(self, layouts, Path(path).name)
        self.run_command(command)
        self.layout_path = path
        self.is_dirty = False

    def closeEvent(self, event):
        if not self.is_dirty:
            event.accept()
            return

        standard_button = getattr(QMessageBox, "StandardButton", QMessageBox)
        save_button = standard_button.Save
        discard_button = standard_button.Discard
        cancel_button = standard_button.Cancel
        buttons = save_button | discard_button | cancel_button
        answer = QMessageBox.question(
            self,
            "Cambios sin guardar",
            "Hay cambios pendientes. ¿Quieres guardarlos antes de cerrar?",
            buttons,
            save_button,
        )
        if answer == save_button:
            if self.save_layout():
                event.accept()
            else:
                event.ignore()
        elif answer == discard_button:
            event.accept()
        else:
            event.ignore()

    def switch_dependency(self):
        dependency = self.dependency.currentText().strip()
        if not dependency:
            self.dependency.setCurrentText(self.current_dependency)
            return
        if dependency == self.current_dependency:
            return
        self.items_by_dependency[self.current_dependency] = self.items
        self.history_by_dependency[self.current_dependency] = (
            self.undo_stack, self.redo_stack
        )
        self.current_dependency = dependency
        self.items = self.items_by_dependency.setdefault(dependency, [])
        self.sync_dependency_choices()
        self.undo_stack, self.redo_stack = self.history_by_dependency.setdefault(
            dependency, ([], [])
        )
        self.selected = None
        self.selected_items = []
        if hasattr(self, "config_panel"):
            self.config_panel.set_item(None)
        self.draw_items()
        self.update_history_buttons()
        self.statusBar().showMessage(f"Dependencia activa: {dependency}")

    def sync_dependency_choices(self):
        current = self.current_dependency
        was_blocked = self.dependency.blockSignals(True)
        self.dependency.clear()
        self.dependency.addItems(sorted(self.items_by_dependency))
        self.dependency.setCurrentText(current)
        if self.dependency.currentText() != current:
            self.dependency.setEditText(current)
        self.dependency.blockSignals(was_blocked)

    def draw_grid(self):
        self._update_grid_bounds()
        self._draw_grid_lines()
        self.draw_items()

    def _update_grid_bounds(self):
        old_bounds = (self.grid_left, self.grid_right)
        item_positions = [
            position
            for items in self.items_by_dependency.values()
            for item in items
            for position in self._grid_item_positions(item)
        ]
        if item_positions:
            minimum_x = min(position.x for position in item_positions)
            maximum_x = max(position.x for position in item_positions)
            trigger_width = GRID_EXTENSION_TRIGGER * GRID_WIDTH
            if maximum_x >= self.grid_right * GRID_WIDTH - trigger_width:
                self.grid_right += GRID_EXTENSION_CELLS
            if minimum_x <= self.grid_left * GRID_WIDTH + trigger_width:
                self.grid_left -= GRID_EXTENSION_CELLS
        self.scene.setSceneRect(
            self.grid_left * GRID_WIDTH, 0,
            (self.grid_right - self.grid_left) * GRID_WIDTH,
            ROWS * GRID_HEIGHT,
        )
        return (self.grid_left, self.grid_right) != old_bounds

    def _draw_grid_lines(self):
        for graphic in getattr(self, "grid_graphics", []):
            self.scene.removeItem(graphic)
        self.grid_graphics = []
        pen = QPen(QColor("#d7dee7"), 1)
        for col in range(self.grid_left, self.grid_right + 1):
            self.grid_graphics.append(
                self.scene.addLine(
                    col * GRID_WIDTH, 0,
                    col * GRID_WIDTH, ROWS * GRID_HEIGHT, pen
                )
            )
        for row in range(ROWS + 1):
            self.grid_graphics.append(
                self.scene.addLine(
                    self.grid_left * GRID_WIDTH, row * GRID_HEIGHT,
                    self.grid_right * GRID_WIDTH, row * GRID_HEIGHT, pen
                )
            )

    @staticmethod
    def _grid_item_positions(item):
        if isinstance(item, SECTION_TYPES):
            return [position for position, _ in item.get_outs()]
        return [item.position]

    def draw_items(self):
        if self._update_grid_bounds():
            self._draw_grid_lines()
        # Preserve the grid graphics; replace only item graphics on each redraw.
        if hasattr(self, "delete_button"):
            self.delete_button.setEnabled(bool(self.selected_items))
        for graphic in getattr(self, "item_graphics", []):
            self.scene.removeItem(graphic)
        self.item_graphics = []
        drawable_items = [
            item for items in self.items_by_dependency.values() for item in items
        ]
        # Dibujar primero las dependencias inactivas para que la activa quede
        # por encima cuando dos dependencias ocupen la misma celda.
        drawable_items.sort(key=lambda item: item.id.dep == self.current_dependency)
        for item in drawable_items:
            if item in self.selected_items:
                color = QColor("#e53935")
            elif item.id.dep == self.current_dependency:
                color = QColor("#fdd835")
            else:
                color = QColor("#9e9e9e")
            pen = QPen(color, 5, Qt.PenStyle.SolidLine if hasattr(Qt, "PenStyle") else Qt.SolidLine)
            pen.setCapStyle(Qt.PenCapStyle.FlatCap if hasattr(Qt, "PenCapStyle") else Qt.FlatCap)
            pen.setJoinStyle(Qt.PenJoinStyle.MiterJoin if hasattr(Qt, "PenJoinStyle") else Qt.MiterJoin)
            if isinstance(item, CvLineal):
                local_points = [point.get_rotated(item.rotation.angle) for point in item.points]
                points = [item_screen_point(item.position + point) for point in local_points]
                path = QPainterPath(QPointF(*points[0]))
                for point in points[1:]:
                    path.lineTo(QPointF(*point))
                self.item_graphics.append(self.scene.addPath(path, pen))
                label_center = label_anchor(item)
                if item.lado is not None:
                    if item.lado == Lado.Impar:
                        start, end = points[-2], points[-1]
                    else:
                        start, end = points[1], points[0]
                    direction = (start[0] - end[0], start[1] - end[1])
                    anchor = end
                    self.item_graphics.extend(self.add_arrow_vector(anchor, direction, color))
            elif isinstance(item, Aguja):
                branch_ends = []
                for path_points in (item.points_normal, item.points_invertida, item.points_punta):
                    path_points = [Point(0,0)]+path_points[1:]
                    transformed = [item.position + p.get_rotated(item.rotation.angle) for p in path_points]
                    screen_points = [item_screen_point(p) for p in transformed]
                    branch_ends.append(screen_points[-1])
                    branch = QPainterPath(QPointF(*screen_points[0]))
                    for point in screen_points[1:]:
                        branch.lineTo(QPointF(*point))
                    self.item_graphics.append(self.scene.addPath(branch, pen))
                center = item_screen_point(item.position)
                label_center = center
                inverted_start = item.position + item.points_invertida[0].get_rotated(item.rotation)
                normal_start = item.position + item.points_normal[0].get_rotated(item.rotation)
                label_offset = 5 if inverted_start.y < normal_start.y else -19
                if item.lado is not None:
                    if item.is_travesia:
                        arrow_dir = (cos(item.rotation.angle), sin(item.rotation.angle))
                        arrow_anchor = center
                    else:
                        arrow_dir = ((branch_ends[0][0] - center[0], branch_ends[0][1] - center[1])
                                     if item.lado == Lado.Impar else
                                     (branch_ends[2][0] - center[0], branch_ends[2][1] - center[1]))
                        arrow_anchor = branch_ends[2]
                    self.item_graphics.extend(self.add_arrow_vector(arrow_anchor, arrow_dir, color))
            elif isinstance(item, Cruzamiento):
                label_center = item_screen_point(item.position)
                for path_points in (
                    item.points_p1, item.points_p2, item.points_p3, item.points_p4
                ):
                    transformed = [
                        item.position + point.get_rotated(item.rotation.angle)
                        for point in path_points
                    ]
                    screen_points = [item_screen_point(point) for point in transformed]
                    path = QPainterPath(QPointF(*screen_points[0]))
                    for point in screen_points[1:]:
                        path.lineTo(QPointF(*point))
                    self.item_graphics.append(self.scene.addPath(path, pen))
            elif isinstance(item, Señal):
                angle = item.rotation.angle
                ux, uy = cos(angle), sin(angle)
                bottom = item_screen_point(signal_foot_position(item))
                label_pos = label_anchor(item)
                pixmap = SIGNAL_ICON.pixmap()
                graphic = self.scene.addPixmap(pixmap)
                graphic.setOffset(0, -pixmap.height())
                graphic.setPos(*bottom)
                graphic.setRotation(item.rotation.angle * 180 / pi)
                graphic.setZValue(1)
                self.item_graphics.append(graphic)
            elif isinstance(item, Topera):
                angle = item.rotation.angle
                ux, uy = cos(angle), sin(angle)
                center = item.position + Point(ux * 2, uy * 2)
                perpendicular = Point(-uy * 10, ux * 10)
                first = item_screen_point(center - perpendicular)
                second = item_screen_point(center + perpendicular)
                self.item_graphics.append(
                    self.scene.addLine(first[0], first[1], second[0], second[1], pen)
                )
                label_center = label_anchor(item)
            if isinstance(item, Señal):
                font = QFont()
                font.setPointSizeF(9)
                width = QFontMetricsF(font).horizontalAdvance(item.id.id_corto) + 4
                label_group = QGraphicsItemGroup()
                label_group.setPos(*label_pos)
                label_group.setTransformOriginPoint(0, 0)
                label_group.setRotation(label_angle(item) * 180 / pi)
                label_group.setZValue(2)
                self.scene.addItem(label_group)
                text_anchor = label_text_anchor(item)
                label_left = -width if text_anchor == "end" else 0
                background = QGraphicsRectItem(label_group)
                background.setRect(
                    label_left, -SIGNAL_ICON_HEIGHT / 2, width, SIGNAL_ICON_HEIGHT,
                )
                background.setBrush(QBrush(QColor(255, 255, 255, 204)))
                no_pen = Qt.PenStyle.NoPen if hasattr(Qt, "PenStyle") else Qt.NoPen
                background.setPen(QPen(no_pen))
                text = QGraphicsTextItem(item.id.id_corto, label_group)
                text.document().setDocumentMargin(0)
                text.setDefaultTextColor(color)
                text.setFont(font)
                text.setPos(
                    label_left if text_anchor == "start" else -text.boundingRect().width(),
                    -text.boundingRect().height() / 2,
                )
                self.item_graphics.append(label_group)
                continue
            if isinstance(item, CvLineal):
                marker_x, marker_y = label_center
                marker_pen = QPen(color, 1.5)
                marker_brush = QBrush(QColor("white"))
                self.item_graphics.append(self.scene.addEllipse(
                    marker_x - 7, marker_y - 7, 14, 14, marker_pen, marker_brush
                ))
                self.item_graphics.append(self.scene.addLine(
                    marker_x - 3.5, marker_y - 3.5,
                    marker_x + 3.5, marker_y + 3.5, marker_pen
                ))
                self.item_graphics.append(self.scene.addLine(
                    marker_x - 3.5, marker_y + 3.5,
                    marker_x + 3.5, marker_y - 3.5, marker_pen
                ))
                self.add_canvas_label("*0", (marker_x, marker_y), color, label_angle(item), top_offset=-7)
                self.add_canvas_label("AVERÍA", (marker_x, marker_y), color, label_angle(item), top_offset=-7)
            self.add_canvas_label(
                item.id.id_corto, label_center, color, label_angle(item),
                top_offset=(
                    6 if isinstance(item, CvLineal)
                    else label_offset if isinstance(item, Aguja)
                    else 0
                ),
            )

    def add_canvas_label(self, text_value, center, color, angle=0, top_offset=0):
        x, y = center
        font = QFont()
        font.setPointSizeF(9)
        width = QFontMetricsF(font).horizontalAdvance(text_value) + 4
        label_group = QGraphicsItemGroup()
        label_group.setPos(x, y)
        label_group.setTransformOriginPoint(0, 0)
        label_group.setRotation(angle * 180 / pi)
        label_group.setZValue(2)
        self.scene.addItem(label_group)
        background = QGraphicsRectItem(label_group)
        background.setRect(-width / 2, top_offset, width, 14)
        background.setBrush(QBrush(QColor(255, 255, 255, 204)))
        no_pen = Qt.PenStyle.NoPen if hasattr(Qt, "PenStyle") else Qt.NoPen
        background.setPen(QPen(no_pen))
        label = QGraphicsTextItem(text_value, label_group)
        label.document().setDocumentMargin(0)
        label.setFont(font)
        label.setDefaultTextColor(color)
        label.setPos(-label.boundingRect().width() / 2,
                     top_offset + (14 - label.boundingRect().height()) / 2)
        self.item_graphics.append(label_group)

    def add_arrow_vector(self, anchor, direction, color):
        length = (direction[0] ** 2 + direction[1] ** 2) ** .5
        if length == 0:
            return []
        ux, uy = direction[0] / length, direction[1] / length
        px, py = -uy, ux
        tip = (anchor[0] + ux * 7, anchor[1] + uy * 7)
        base = (tip[0] - ux * 7, tip[1] - uy * 7)
        pen = QPen(color, 2.5)
        return [self.scene.addLine(tip[0], tip[1], base[0] + px * 4, base[1] + py * 4, pen),
                self.scene.addLine(tip[0], tip[1], base[0] - px * 4, base[1] - py * 4, pen)]


def main():
    app = QApplication(sys.argv)
    window = TrackEditor()
    window.show()
    return app.exec() if hasattr(app, "exec") else app.exec_()


if __name__ == "__main__":
    raise SystemExit(main())
