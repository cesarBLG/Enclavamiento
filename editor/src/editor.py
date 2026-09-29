#!/usr/bin/env python3
"""Editor gráfico sencillo de topologías ferroviarias sobre una rejilla.

Requiere PyQt6 o PyQt5. Ejecutar con ``python3 editor.py`` desde este directorio.
"""

from __future__ import annotations

import sys
import json
import traceback
from copy import deepcopy
from math import cos, pi, sin
from pathlib import Path

try:
    from PyQt6.QtCore import QPointF, QRectF, QSize, Qt
    from PyQt6.QtGui import QBrush, QColor, QIcon, QKeySequence, QPainter, QPainterPath, QPen, QPixmap, QPolygonF, QShortcut
    from PyQt6.QtWidgets import (
        QApplication, QComboBox, QDialog, QDialogButtonBox, QFormLayout,
        QFileDialog, QGraphicsScene, QGraphicsView, QHBoxLayout,
        QLabel, QLineEdit, QMainWindow, QMessageBox,
        QToolButton, QVBoxLayout, QWidget,
    )
    LEFT_BUTTON = Qt.MouseButton.LeftButton
except ImportError:
    from PyQt5.QtCore import QPointF, QRectF, QSize, Qt
    from PyQt5.QtGui import QBrush, QColor, QIcon, QKeySequence, QPainter, QPainterPath, QPen, QPixmap, QPolygonF
    from PyQt5.QtWidgets import (
        QApplication, QComboBox, QDialog, QDialogButtonBox, QFormLayout,
        QFileDialog, QGraphicsScene, QGraphicsView, QHBoxLayout,
        QLabel, QLineEdit, QMainWindow, QMessageBox,
        QShortcut, QToolButton, QVBoxLayout, QWidget,
    )
    LEFT_BUTTON = Qt.LeftButton

from items import (
    Aguja, Angle, CvLineal, Direccion, IdElemento, Lado, Point, Señal, TipoSeñal,
    deserialize_layout, get_next_position, serialize_layout,
)
from export import generar_config_ence


CELL = 52
COLS = 24
ROWS = 16


def direction_vector(direction, length=CELL / 2):
    """Vector local de una dirección antigua en coordenadas cartesianas."""
    vectors = ((1, 0), (1, -1), (0, -1), (-1, -1),
               (-1, 0), (-1, 1), (0, 1), (1, 1))
    index = direction.value if isinstance(direction, Direccion) else int(direction)
    dx, dy = vectors[index % 8]
    return Point(dx * length, dy * length)


def direction_for_offset(offset):
    """Recupera la dirección discreta de un extremo en el borde de celda."""
    return min(range(8), key=lambda direction: (
        (offset.x - direction_vector(direction).x) ** 2
        + (offset.y - direction_vector(direction).y) ** 2
    ))

def cell_origin(cell):
    return Point(cell[0] * CELL, cell[1] * CELL)


def item_cell(item):
    """Celda de referencia para las herramientas de selección de la rejilla."""
    if isinstance(item, (CvLineal, Aguja)):
        if isinstance(item, CvLineal):
            pts = [item.position + p.get_rotated(item.rotation.angle) for p in item.points]
            anchor = Point(sum(p.x for p in pts) / len(pts), sum(p.y for p in pts) / len(pts))
        else:
            anchor = item.position
        return int(anchor.x // CELL), int(anchor.y // CELL)
    if isinstance(item, Señal):
        toward_track = item.rotation.angle
        anchor = Point(item.position.x + cos(toward_track),
                       item.position.y + sin(toward_track))
        return int(anchor.x // CELL), int(anchor.y // CELL)
    return (0, 0)


def item_cell_model(item):
    x, y = item_cell(item)
    return x, y


def item_screen_point(point):
    return point.x, ROWS * CELL - point.y


def signal_foot_position(item):
    normal_x = -sin(item.rotation.angle)
    normal_y = cos(item.rotation.angle)
    return Point(item.position.x + normal_x * item.offset_pie,
                 item.position.y + normal_y * item.offset_pie)


class AddItemCommand:
    def __init__(self, editor, item):
        self.editor = editor
        self.item = item
        self.previous_selection = list(editor.selected_items)
        x, y = item_cell(item)
        if isinstance(item, CvLineal):
            side = item.lado.name if item.lado else "None"
            self.text = f"CvLineal {item.angle} {x} {y} {item.id.id} {side}"
        elif isinstance(item, Aguja):
            cv = item.get_cv().id if item.get_cv() else ""
            side = item.lado.name if item.lado else "None"
            self.text = f"Aguja {item.angle} {x} {y} {item.id.id} {cv} {side}"
        else:
            self.text = f"Señal {item.tipo_señal.name} {x} {y} {item.id.id}"

    def execute(self):
        if self.item not in self.editor.items:
            self.editor.items.append(self.item)
        self.editor.ensure_grid_for_position(item_cell(self.item))
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
        self.editor.rotate_geometry(self.item, self.amount, self.linked_signal)
        self.after = self.editor.capture_rotation_state(self.item, self.linked_signal)
        self.editor.set_selection([self.item])

    def undo(self):
        self.editor.restore_rotation_state(self.item, self.linked_signal, self.before)
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
            self.editor.ensure_grid_for_position(item_cell(item))
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
        self.editor.ensure_grid_for_items()
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
        state = {"id": item.id}
        if isinstance(item, CvLineal):
            state["lado"] = item.lado
        elif isinstance(item, Aguja):
            state["cv"] = item.cv
            state["lado"] = item.lado
        elif isinstance(item, Señal):
            state["tipo_señal"] = item.tipo_señal
        return state

    def apply(self, state):
        self.item.id = state["id"]
        if isinstance(self.item, CvLineal):
            self.item.lado = state["lado"]
        elif isinstance(self.item, Aguja):
            self.item.cv = state["cv"]
            self.item.lado = state["lado"]
        elif isinstance(self.item, Señal):
            self.item.tipo_señal = state["tipo_señal"]
        self.editor.set_selection([self.item])

    def execute(self):
        self.apply(self.after)

    def undo(self):
        self.apply(self.before)


class ElementDialog(QDialog):
    """Formulario modal para completar los datos de un elemento."""

    def __init__(self, kind: str, dependency: str, parent=None, edit=False):
        super().__init__(parent)
        self.setWindowTitle(("Editar " if edit else "Añadir ") + kind)
        self.name_edit = QLineEdit(self)
        self.cv_edit = QLineEdit(self) if kind == "Cambio de agujas" else None
        self.signal_type = QComboBox(self) if kind == "Señal" else None
        self.side_combo = QComboBox(self) if kind in ("Tramo de vía", "Cambio de agujas") else None
        form = QFormLayout()
        if kind == "Tramo de vía":
            form.addRow("Nombre del CV:", self.name_edit)
        else:
            form.addRow("Nombre:", self.name_edit)
        if kind == "Cambio de agujas":
            form.addRow("Circuito de vía:", self.cv_edit)
        if self.side_combo is not None:
            # Use simple string data: Qt/PyQt bindings can conflate a None
            # userData with an omitted userData argument in addItem().
            self.side_combo.addItem("", "")
            self.side_combo.addItem("Par", "Par")
            self.side_combo.addItem("Impar", "Impar")
            form.addRow(QLabel("Lado:", self), self.side_combo)
        if kind == "Señal":
            for signal_type in TipoSeñal:
                self.signal_type.addItem(signal_type.name, signal_type)
            form.addRow("Tipo de señal:", self.signal_type)
        self.dependency = dependency
        buttons = QDialogButtonBox(QDialogButtonBox.StandardButton.Ok | QDialogButtonBox.StandardButton.Cancel) if hasattr(QDialogButtonBox, "StandardButton") else QDialogButtonBox(QDialogButtonBox.Ok | QDialogButtonBox.Cancel)
        buttons.accepted.connect(self.accept)
        buttons.rejected.connect(self.reject)
        layout = QVBoxLayout(self)
        layout.addLayout(form)
        layout.addWidget(buttons)

    def values(self):
        signal_type = self.signal_type.currentData() if self.signal_type else None
        side_value = self.side_combo.currentData() if self.side_combo is not None else ""
        side = Lado[side_value] if side_value else None
        cv_name = self.cv_edit.text().strip() if self.cv_edit else ""
        return self.name_edit.text().strip(), cv_name, signal_type, side


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
    def cell_at(point):
        col, row = int(point.x() // CELL), int(point.y() // CELL)
        if 0 <= col < COLS and 0 <= row < ROWS:
            return (col, ROWS - row - 1)
        return None

    @staticmethod
    def item_at(point, candidates):
        for item in candidates:
            if not isinstance(item, Señal):
                continue
            signal_pos = signal_foot_position(item)
            angle = -item.rotation.angle
            ux, uy = cos(angle), sin(angle)
            center_x = signal_pos.x + ux * (4.5 + 11)
            center_y = ROWS * CELL - signal_pos.y + uy * (4.5 + 11)
            left_x, left_y = -uy, ux
            dx, dy = point.x() - center_x, point.y() - center_y
            along = dx * ux + dy * uy
            left = dx * left_x + dy * left_y
            text_half_width = len(item.id.id_corto) * 3.5
            if (abs(along) <= 18 and abs(left) <= 9) or (
                abs(along) <= text_half_width and 8 <= left <= 32
            ):
                return item
        return next((item for item in candidates if not isinstance(item, Señal)), None)

    def mousePressEvent(self, event):
        if event.button() == LEFT_BUTTON:
            point = self.mapToScene(self.event_point(event))
            position = self.cell_at(point)
            if position is None:
                return
            if self.editor.mode[0] != "select":
                self.editor.cell_clicked(position, point)
                return
            self.press_scene = point
            self.press_position = position
            self.preview_delta = (0, 0)
            self.rubber_band = None
            candidates = [
                item for items in self.editor.items_by_dependency.values()
                for item in items if item_cell(item) == position or isinstance(item, Señal)
            ]
            clicked_item = self.item_at(point, candidates)
            if clicked_item is not None:
                if clicked_item not in self.editor.selected_items:
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
        if self.gesture == "rubber":
            if self.rubber_band is not None:
                self.scene().removeItem(self.rubber_band)
            rect = QRectF(self.press_scene, point).normalized()
            self.rubber_band = self.scene().addRect(
                rect, QPen(QColor("#1976d2"), 1), QBrush(QColor(25, 118, 210, 45))
            )
            self.rubber_band.setZValue(10)
            return

        start_col = int(self.press_scene.x() // CELL)
        start_row = int(self.press_scene.y() // CELL)
        current_col = int(point.x() // CELL)
        current_row = int(point.y() // CELL)
        delta = (current_col - start_col, start_row - current_row)
        signal_only_drag = self.drag_items and all(
            isinstance(item, Señal) for item in self.drag_items
        )
        if delta == self.preview_delta and not signal_only_drag:
            return
        if self.editor.can_move_items(self.drag_items, self.drag_before, delta):
            self.preview_delta = delta
            signals_moving_with_track = {
                item for item in self.drag_items
                if isinstance(item, Señal) and any(
                    isinstance(other, (CvLineal, Aguja))
                    and self.editor.signal_matches_section(item, other)
                    for other in self.drag_items
                )
            }
            for item, state in self.drag_before.items():
                move_port = item in signals_moving_with_track
                fine_delta = (point.x() - self.press_scene.x(),
                              self.press_scene.y() - point.y())
                self.editor.preview_move(item, state, delta, move_port, fine_delta)
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
        point = self.mapToScene(self.event_point(event))
        if self.gesture == "move":
            after = {item: self.editor.capture_move_state(item) for item in self.drag_items}
            if any(after[item] != self.drag_before[item] for item in self.drag_items):
                for item, state in self.drag_before.items():
                    self.editor.restore_move_state(item, state)
                self.editor.run_command(
                    MoveItemsCommand(self.editor, self.drag_items, self.drag_before, after)
                )
            elif not self.click_processed:
                self.editor.cell_clicked(self.press_position)
        else:
            distance = ((point.x() - self.press_scene.x()) ** 2
                        + (point.y() - self.press_scene.y()) ** 2) ** .5
            if distance >= 4:
                rect = QRectF(self.press_scene, point).normalized()
                selected = []
                for items in self.editor.items_by_dependency.values():
                    for item in items:
                        x, y = item_cell(item)
                        row = ROWS - y - 1
                        cell_rect = QRectF(x * CELL, row * CELL, CELL, CELL)
                        if rect.intersects(cell_rect):
                            selected.append(item)
                self.editor.set_selection(selected)
            elif not self.click_processed:
                self.editor.cell_clicked(self.press_position)
        if self.rubber_band is not None:
            self.scene().removeItem(self.rubber_band)
        self.rubber_band = None
        self.gesture = None
        self.press_scene = None
        self.press_position = None
        self.drag_items = []
        self.drag_before = {}
        self.preview_delta = (0, 0)

    def mouseDoubleClickEvent(self, event):
        if event.button() == LEFT_BUTTON:
            point = self.mapToScene(
                event.position().toPoint() if hasattr(event, "position") else event.pos()
            )
            col, row = int(point.x() // CELL), int(point.y() // CELL)
            if 0 <= col < COLS and 0 <= row < ROWS:
                position = (col, ROWS - row - 1)
                candidates = [
                    item for items in self.editor.items_by_dependency.values()
                    for item in items if item_cell(item) == position or isinstance(item, Señal)
                ]
                item = self.item_at(point, candidates)
                if item is not None:
                    self.editor.dependency.setCurrentText(item.id.dep)
                    self.editor.switch_dependency()
                    self.editor.set_selection([item])
                    self.editor.configure_button.setEnabled(True)
                    self.editor.configure_selected()
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
        self.scene = QGraphicsScene(self)
        self.scene.setSceneRect(0, 0, COLS * CELL, ROWS * CELL)
        self.grid_graphics = []
        self.view = GridView(self.scene, self)

        central = QWidget(self)
        root = QVBoxLayout(central)
        dep_row = QHBoxLayout()
        dep_row.addWidget(QLabel("Dependencia de trabajo:"))
        self.dependency = QComboBox(self)
        self.dependency.setEditable(True)
        self.dependency.addItem("EST")
        self.dependency.setPlaceholderText("Nombre de la estación")
        self.dependency.activated.connect(lambda *_: self.switch_dependency())
        self.dependency.lineEdit().editingFinished.connect(self.switch_dependency)
        dep_row.addWidget(self.dependency, 1)
        root.addLayout(dep_row)

        tools = QHBoxLayout()
        self.tool_buttons = []
        self.add_tool(tools, "Seleccionar", "select", None)
        self.add_tool(tools, "Recta 90°", "track", 2)
        self.add_tool(tools, "Recta 135°", "track", 3)
        self.add_tool(tools, "Recta 180°", "track", 4)
        self.add_tool(tools, "Aguja +1", "switch", 1)
        self.add_tool(tools, "Aguja −1", "switch", -1)
        self.add_tool(tools, "Señal", "signal", None)
        self.configure_button = QToolButton(self)
        self.configure_button.setIcon(self.make_icon("settings"))
        self.configure_button.setIconSize(QSize(36, 36))
        self.configure_button.setFixedSize(46, 42)
        self.configure_button.setToolTip("Configurar elemento seleccionado")
        self.configure_button.clicked.connect(self.configure_selected)
        tools.addWidget(self.configure_button)
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
        root.addLayout(tools)
        root.addWidget(self.view, 1)
        self.setCentralWidget(central)
        QShortcut(QKeySequence("Ctrl+Z"), self).activated.connect(self.undo)
        QShortcut(QKeySequence("Ctrl+Y"), self).activated.connect(self.redo)
        QShortcut(QKeySequence("Ctrl+Shift+Z"), self).activated.connect(self.redo)
        QShortcut(QKeySequence("Delete"), self).activated.connect(self.delete_selected)
        self.statusBar().showMessage("Selecciona una herramienta y pulsa una celda de la rejilla.")
        self.configure_button.setEnabled(False)
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
        elif kind == "signal":
            painter.setBrush(QColor("#26384a"))
            painter.drawRoundedRect(13, 3, 22, 39, 5, 5)
            for y, light_color in (
                (11, "#2ecc71"),
                (23, "#e53935"),
                (35, "#fdd835"),
            ):
                painter.setBrush(QColor(light_color))
                painter.drawEllipse(18, y - 5, 12, 10)
            painter.drawLine(QPointF(24, 42), QPointF(24, 47))
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
        self.statusBar().showMessage("Modo selección" if kind == "select" else "Pulsa una celda para añadir el elemento.")

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
        if hasattr(self, "configure_button"):
            self.configure_button.setEnabled(len(selection) == 1)
        if hasattr(self, "delete_button"):
            self.delete_button.setEnabled(bool(selection))
        self.draw_items()

    def movable_selection(self, items):
        result = list(items)
        index = 0
        while index < len(result):
            item = result[index]
            if isinstance(item, (CvLineal, Aguja)):
                related_types = (Señal,)
            else:
                related_types = ()
            for candidate in self.items_by_dependency.get(item.id.dep, []):
                if (isinstance(candidate, related_types)
                        and self.signal_matches_section(candidate, item)
                        and candidate not in result):
                    result.append(candidate)
            index += 1
        return result

    @staticmethod
    def signal_matches_section(signal, section):
        if not isinstance(signal, Señal) or not isinstance(section, (CvLineal, Aguja)):
            return False
        return section.matches(signal.position, signal.rotation + Angle(pi))

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

    @staticmethod
    def preview_move(item, state, delta, move_port=False, fine_delta=None):
        dx, dy = delta[0] * CELL, delta[1] * CELL
        if isinstance(item, Señal):
            item.position, old_offset = state
            if move_port:
                item.position = Point(item.position.x + dx, item.position.y + dy)
                item.offset_pie = old_offset
            else:
                if fine_delta is None:
                    fine_delta = (dx, dy)
                normal_x = -sin(item.rotation.angle)
                normal_y = cos(item.rotation.angle)
                offset_delta = fine_delta[0] * normal_x + fine_delta[1] * normal_y
                item.offset_pie = max(-CELL, min(CELL, old_offset + offset_delta))
        else:
            item.position = Point(state.x + dx, state.y + dy)

    @staticmethod
    def capture_rotation_state(item, signal=None):
        if isinstance(item, CvLineal):
            geometry = (item.position, list(item.points), item.rotation)
        elif isinstance(item, Aguja):
            geometry = (item.position, list(item.points_normal),
                        list(item.points_invertida), list(item.points_punta), item.rotation)
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
        else:
            item.position, item.rotation = geometry
        if signal is not None:
            signal.position, signal.rotation = signal_state

    def rotate_geometry(self, item, steps, signal=None):
        if isinstance(item, Señal):
            track = next((candidate for candidate in self.items_by_dependency.get(item.id.dep, [])
                          if isinstance(candidate, (CvLineal, Aguja))
                          and self.signal_matches_section(item, candidate)), None)
            if track is None:
                return
            options = []
            for position, port_angle in track.get_outs():
                candidate_rotation = port_angle - Angle(pi)
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
            cell = item_cell(item)
            center = Point((cell[0] + .5) * CELL, (cell[1] + .5) * CELL)
            old_points = [item.position + p.get_rotated(item.rotation.angle)
                          for p in item.points]
            start_direction = direction_for_offset(old_points[0] - center)
            end_direction = direction_for_offset(old_points[-1] - center)
            start = center + direction_vector(start_direction + steps)
            middle = center
            end = center + direction_vector(end_direction + steps)
            item.position = start
            item.points = [Point(0, 0), middle - start, end - start]
            item.rotation = Angle(0)
        elif isinstance(item, Aguja):
            for attr in ("points_normal", "points_invertida", "points_punta"):
                path = getattr(item, attr)
                direction = direction_for_offset(path[-1])
                setattr(item, attr, [Point(0, 0), direction_vector(direction + steps)])
            item.rotation = Angle(0)
        if signal is not None:
            if signal_port_index is not None:
                new_port = item.get_outs()[signal_port_index]
                signal.position = new_port[0]
                signal.rotation = new_port[1] - Angle(pi)

    def can_move_items(self, items, before, delta):
        if items and all(isinstance(item, Señal) for item in items):
            return True
        target_cells = {}
        for item in items:
            cell = item_cell(item)
            target_cells[item] = (cell[0] + delta[0], cell[1] + delta[1])
        if any(not (0 <= x < COLS and 0 <= y < ROWS)
               for x, y in target_cells.values()):
            return False
        cells = {}
        for dependency_items in self.items_by_dependency.values():
            for item in dependency_items:
                cell = target_cells.get(item, item_cell(item))
                cells.setdefault(cell, []).append(item)
        for cell_items in cells.values():
            if len(cell_items) <= 1:
                continue
            if len(cell_items) != 2:
                return False
            first, second = cell_items
            if first.id.dep != second.id.dep:
                return False
            if isinstance(first, Señal) and isinstance(second, (CvLineal, Aguja)):
                signal, track = first, second
            elif isinstance(second, Señal) and isinstance(first, (CvLineal, Aguja)):
                signal, track = second, first
            else:
                return False
            if not self.signal_matches_section(signal, track):
                return False
        return True

    def ensure_grid_for_position(self, position):
        global COLS, ROWS
        x, y = position
        needed_cols = max(COLS, x + 5)
        needed_rows = max(ROWS, y + 5)
        while COLS < needed_cols:
            COLS += 4
        while ROWS < needed_rows:
            ROWS += 4
        if self.scene.sceneRect().width() != COLS * CELL or self.scene.sceneRect().height() != ROWS * CELL:
            self.scene.setSceneRect(0, 0, COLS * CELL, ROWS * CELL)
            self.draw_grid()

    def ensure_grid_for_items(self):
        positions = [item_cell(item) for items in self.items_by_dependency.values()
                     for item in items]
        if positions:
            self.ensure_grid_for_position((max(p[0] for p in positions),
                                           max(p[1] for p in positions)))

    def cell_clicked(self, position, scene_point=None):
        kind, value = self.mode
        if kind == "select":
            candidates = [
                item for items in self.items_by_dependency.values() for item in items
                if item_cell(item) == position
            ]
            existing = None
            if candidates:
                selected_index = next(
                    (index for index, item in enumerate(candidates)
                     if item in self.selected_items),
                    -1,
                )
                existing = candidates[(selected_index + 1) % len(candidates)]
            if existing is not None:
                self.dependency.setCurrentText(existing.id.dep)
            self.set_selection([existing] if existing is not None else [])
            if existing:
                self.statusBar().showMessage(f"Seleccionado: {existing.id.id}")
            return
        cell_items = [
            item for items in self.items_by_dependency.values() for item in items
            if item_cell(item) == position
        ]
        if kind == "signal":
            tracks = [item for item in cell_items if isinstance(item, (CvLineal, Aguja))]
            signals = [item for item in cell_items if isinstance(item, Señal)]
            if len(tracks) != 1 or signals or len(cell_items) != 1:
                QMessageBox.warning(
                    self, "Vía necesaria",
                    "Coloca la señal en la misma celda que una vía sin señal.",
                )
                return
            if tracks[0].id.dep != self.current_dependency:
                self.dependency.setCurrentText(tracks[0].id.dep)
                self.switch_dependency()
        elif cell_items:
            QMessageBox.warning(
                self, "Celda ocupada",
                f"La celda ya está ocupada por {cell_items[0].id.id}.",
            )
            return
        dependency = self.dependency.currentText().strip()
        if not dependency:
            QMessageBox.warning(self, "Falta la dependencia", "Indica el nombre de la estación antes de añadir elementos.")
            self.dependency.setFocus()
            return
        if kind == "track":
            center = cell_origin(position) + Point(CELL / 2, CELL / 2)
            item = CvLineal(IdElemento("", dependency), center, [Point(CELL / 2, 0)])
            item.angle = value
            self.auto_rotate_for_connections(item)
            self.run_command(AddItemCommand(self, item))
            return
        kind_name = {"track": "Tramo de vía", "switch": "Cambio de agujas", "signal": "Señal"}[kind]
        dialog = ElementDialog(kind_name, dependency, self)
        if hasattr(dialog, "exec"):
            accepted = dialog.exec() == QDialog.DialogCode.Accepted
        else:
            accepted = dialog.exec_() == QDialog.Accepted
        if not accepted:
            return
        short_id, cv_name, signal_type, side = dialog.values()
        if not short_id or (kind == "switch" and not cv_name):
            QMessageBox.warning(self, "Datos incompletos", "Indica el nombre del elemento y el circuito de vía cuando corresponda.")
            return
        try:
            ident = IdElemento(short_id, dependency)
        except ValueError as exc:
            QMessageBox.warning(self, "Identificador inválido", str(exc))
            return
        item_class = {"track": CvLineal, "switch": Aguja, "signal": Señal}[kind]
        if kind != "track" and any(
            isinstance(item, item_class) and item.id.id == ident.id
            for item in self.items
        ):
            QMessageBox.warning(
                self,
                "Identificador duplicado",
                f"Ya existe un elemento de este tipo con id {ident.id}.",
            )
            return
        if kind == "switch":
            center = cell_origin(position) + Point(CELL / 2, CELL / 2)
            item = Aguja(ident, center, Angle(0),
                         [Point(-CELL / 2, 0)], [Point(CELL / 2, 0)],
                         [Point(CELL / 2, CELL / 2)])
            item.angle = value
            item.cv = IdElemento(cv_name, dependency)
            item.lado = side
        else:
            track = tracks[0]
            if scene_point is None:
                click_model = cell_origin(position) + Point(CELL / 2, CELL / 2)
            else:
                click_model = Point(scene_point.x(), ROWS * CELL - scene_point.y())
            port_index = min(
                range(len(track.get_outs())),
                key=lambda i: (track.get_outs()[i][0].x - click_model.x) ** 2
                + (track.get_outs()[i][0].y - click_model.y) ** 2,
            )
            port, direction = track.get_outs()[port_index]
            item = Señal(ident, port, signal_type,
                         10.0, direction - Angle(pi))
        self.auto_rotate_for_connections(item)
        self.run_command(AddItemCommand(self, item))

    def auto_rotate_for_connections(self, item):
        if isinstance(item, Señal):
            return
        cell = item_cell(item)
        center = cell_origin(cell) + Point(CELL / 2, CELL / 2)
        others = [other for values in self.items_by_dependency.values() for other in values
                  if isinstance(other, (CvLineal, Aguja))]
        best_rotation, best_score = 0, -1
        for rotation in range(8):
            if isinstance(item, CvLineal):
                d0, d1 = rotation, (rotation + item.angle) % 8
                start = center + direction_vector(d0)
                end = center + direction_vector(d1)
                item.position = start
                item.points = [Point(0, 0), center - start, end - start]
                item.rotation = Angle(0)
            else:
                d0, d1, d2 = rotation, (rotation + item.angle) % 8, (rotation + 4) % 8
                item.position = center
                item.points_normal = [Point(0, 0), direction_vector(d0)]
                item.points_invertida = [Point(0, 0), direction_vector(d1)]
                item.points_punta = [Point(0, 0), direction_vector(d2)]
                item.rotation = Angle(0)
            score = sum(
                1 for point, angle in item.get_outs()
                for other in others if other is not item
                for other_point, other_angle in other.get_outs()
                if point.close_to(other_point)
                and angle.close_to(other_angle + Angle(pi))
            )
            if score > best_score:
                best_rotation, best_score = rotation, score
        if isinstance(item, CvLineal):
            d0, d1 = best_rotation, (best_rotation + item.angle) % 8
            start, end = center + direction_vector(d0), center + direction_vector(d1)
            item.position = start
            item.points = [Point(0, 0), center - start, end - start]
        else:
            item.position = center
            item.points_normal = [Point(0, 0), direction_vector(best_rotation)]
            item.points_invertida = [Point(0, 0), direction_vector((best_rotation + item.angle) % 8)]
            item.points_punta = [Point(0, 0), direction_vector((best_rotation + 4) % 8)]
        item.rotation = Angle(0)

    def configure_selected(self):
        item = self.selected
        if item is None or len(self.selected_items) != 1:
            self.statusBar().showMessage("Selecciona primero un elemento de la rejilla.")
            return
        if isinstance(item, CvLineal):
            kind = "Tramo de vía"
        elif isinstance(item, Aguja):
            kind = "Cambio de agujas"
        else:
            kind = "Señal"
        item_dependency = item.id.dep
        dialog = ElementDialog(kind, item_dependency, self, edit=True)
        dialog.name_edit.setText(item.id.id_corto)
        if isinstance(item, (CvLineal, Aguja)):
            index = dialog.side_combo.findData(item.lado.name if item.lado else "")
            if index >= 0:
                dialog.side_combo.setCurrentIndex(index)
        if isinstance(item, Aguja):
            dialog.cv_edit.setText(item.get_cv().id_corto if item.get_cv() else "")
        elif isinstance(item, Señal):
            index = dialog.signal_type.findData(item.tipo_señal)
            if index >= 0:
                dialog.signal_type.setCurrentIndex(index)
        if hasattr(dialog, "exec"):
            accepted = dialog.exec() == QDialog.DialogCode.Accepted
        else:
            accepted = dialog.exec_() == QDialog.Accepted
        if not accepted:
            return
        short_id, cv_name, signal_type, side = dialog.values()
        if not short_id or (isinstance(item, Aguja) and not cv_name):
            QMessageBox.warning(self, "Datos incompletos", "Indica el nombre del elemento y el circuito de vía cuando corresponda.")
            return
        try:
            new_id = IdElemento(short_id, item_dependency)
            new_cv = IdElemento(cv_name, item_dependency) if isinstance(item, Aguja) else None
        except ValueError as exc:
            QMessageBox.warning(self, "Identificador inválido", str(exc))
            return
        if isinstance(item, (Aguja, Señal)) and any(
            other is not item and isinstance(other, type(item)) and other.id.id == new_id.id
            for other in self.items_by_dependency.get(item_dependency, [])
        ):
            QMessageBox.warning(self, "Identificador duplicado", f"Ya existe un elemento de este tipo con id {new_id.id}.")
            return
        new_state = {"id": new_id}
        if isinstance(item, CvLineal):
            new_state["lado"] = side
        elif isinstance(item, Aguja):
            new_state["cv"] = new_cv
            new_state["lado"] = side
        elif isinstance(item, Señal):
            new_state["tipo_señal"] = signal_type
        self.run_command(EditItemCommand(self, item, new_state, f"Configurar {item.id.id}"))

    def rotate_selected(self, amount):
        if self.selected is None or len(self.selected_items) != 1:
            self.statusBar().showMessage("Selecciona primero un elemento de la rejilla.")
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
            self.statusBar().showMessage("Selecciona primero un elemento de la rejilla.")
            return
        selected_set = set(items)
        for item in items:
            if isinstance(item, (CvLineal, Aguja)) and any(
                isinstance(other, Señal) and self.signal_matches_section(other, item)
                and other not in selected_set
                for other in self.items_by_dependency.get(item.id.dep, [])
            ):
                QMessageBox.warning(
                    self, "La vía tiene una señal",
                    "Incluye o elimina primero la señal asociada a esta vía.",
                )
                return
        self.run_command(DeleteItemsCommand(self, items))

    def run_command(self, command):
        command.execute()
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
        self.configure_button.setEnabled(False)
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
        for graphic in getattr(self, "grid_graphics", []):
            self.scene.removeItem(graphic)
        self.grid_graphics = []
        pen = QPen(QColor("#d7dee7"), 1)
        for col in range(COLS + 1):
            self.grid_graphics.append(
                self.scene.addLine(col * CELL, 0, col * CELL, ROWS * CELL, pen)
            )
        for row in range(ROWS + 1):
            self.grid_graphics.append(
                self.scene.addLine(0, row * CELL, COLS * CELL, row * CELL, pen)
            )
        self.draw_items()

    @staticmethod
    def direction_point(center, direction):
        angle = direction.value * pi / 4
        dx, dy = cos(angle), sin(angle)
        half_cell = CELL / 2
        if direction.value % 2 == 0:
            # Los puntos cardinales terminan exactamente en el borde.
            return center[0] + dx * half_cell, center[1] + dy * half_cell
        # El extremo diagonal llega a la esquina de la celda; el propio
        # recorrido diagonal ocupa visualmente las celdas cercanas.
        extent = half_cell
        return (center[0] + (1 if dx > 0 else -1) * extent,
                center[1] + (1 if dy > 0 else -1) * extent)

    def add_direction_arrow(self, center, direction, color, position_direction=None):
        angle = direction.value * pi / 4
        ux, uy = cos(angle), sin(angle)
        px, py = -uy, ux
        if position_direction is None:
            position_direction = direction
        position_angle = position_direction.value * pi / 4
        anchor = (
            center[0] + cos(position_angle) * 12,
            center[1] + sin(position_angle) * 12,
        )
        tip = (anchor[0] + ux * 6, anchor[1] + uy * 6)
        base = (tip[0] - ux * 7, tip[1] - uy * 7)
        wing_a = (base[0] + px * 4, base[1] + py * 4)
        wing_b = (base[0] - px * 4, base[1] - py * 4)
        arrow_pen = QPen(color, 2.5)
        arrow_pen.setCapStyle(Qt.PenCapStyle.FlatCap if hasattr(Qt, "PenCapStyle") else Qt.FlatCap)
        return [
            self.scene.addLine(tip[0], tip[1], wing_a[0], wing_a[1], arrow_pen),
            self.scene.addLine(tip[0], tip[1], wing_b[0], wing_b[1], arrow_pen),
        ]

    def draw_items(self):
        # Preserve the grid graphics; replace only item graphics on each redraw.
        if hasattr(self, "configure_button"):
            self.configure_button.setEnabled(self.selected is not None)
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
            pen = QPen(color, 4, Qt.PenStyle.SolidLine if hasattr(Qt, "PenStyle") else Qt.SolidLine)
            pen.setCapStyle(Qt.PenCapStyle.FlatCap if hasattr(Qt, "PenCapStyle") else Qt.FlatCap)
            pen.setJoinStyle(Qt.PenJoinStyle.MiterJoin if hasattr(Qt, "PenJoinStyle") else Qt.MiterJoin)
            if isinstance(item, CvLineal):
                local_points = [point.get_rotated(item.rotation.angle) for point in item.points]
                points = [item_screen_point(item.position + point) for point in local_points]
                path = QPainterPath(QPointF(*points[0]))
                for point in points[1:]:
                    path.lineTo(QPointF(*point))
                self.item_graphics.append(self.scene.addPath(path, pen))
                outs = item.get_outs()
                label_center = item_screen_point((outs[0][0] + outs[1][0]) / 2)
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
                    transformed = [item.position + p.get_rotated(item.rotation.angle) for p in path_points]
                    screen_points = [item_screen_point(p) for p in transformed]
                    branch_ends.append(screen_points[-1])
                    branch = QPainterPath(QPointF(*screen_points[0]))
                    for point in screen_points[1:]:
                        branch.lineTo(QPointF(*point))
                    self.item_graphics.append(self.scene.addPath(branch, pen))
                center = item_screen_point(item.position)
                label_center = center
                if item.lado is not None:
                    arrow_dir = (branch_ends[0][0] - center[0], branch_ends[0][1] - center[1]) if item.lado == Lado.Impar else (branch_ends[2][0] - center[0], branch_ends[2][1] - center[1])
                    self.item_graphics.extend(self.add_arrow_vector(branch_ends[2], arrow_dir, color))
            elif isinstance(item, Señal):
                angle = -item.rotation.angle
                ux, uy = cos(angle), sin(angle)
                left_x, left_y = -uy, ux
                bottom = item_screen_point(signal_foot_position(item))
                lamps = [
                    (bottom[0] + ux * offset, bottom[1] + uy * offset)
                    for offset in (4.5, 15.5, 26.5)
                ]
                signal_center = lamps[1]
                housing_start = bottom
                housing_end = (bottom[0] + ux * 31, bottom[1] + uy * 31)
                housing = self.scene.addLine(
                    housing_start[0], housing_start[1],
                    housing_end[0], housing_end[1], pen,
                )
                self.item_graphics.append(housing)
                for light_center, light_color in zip(
                    lamps,
                    (QColor("#fdd835"), QColor("#e53935"), QColor("#2ecc71")),
                ):
                    radius = 4.5
                    light_pen = QPen(color, 1.5)
                    lamp = self.scene.addEllipse(
                        light_center[0] - radius,
                        light_center[1] - radius,
                        radius * 2,
                        radius * 2,
                        light_pen,
                        QBrush(light_color),
                    )
                    self.item_graphics.append(lamp)
                center = signal_center
                label_center = (center[0] + left_x * 22, center[1] + left_y * 22)
            label = self.scene.addText(item.id.id_corto)
            label.setDefaultTextColor(color)
            if isinstance(item, Señal):
                label.setPos(
                    label_center[0] - label.boundingRect().width() / 2,
                    label_center[1] - label.boundingRect().height() / 2,
                )
            else:
                label.setPos(
                    label_center[0] - label.boundingRect().width() / 2,
                    label_center[1] + CELL * .17,
                )
            label.setZValue(2)
            self.item_graphics.append(label)

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
