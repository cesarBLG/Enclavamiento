#!/usr/bin/env python3
"""Editor gráfico sencillo de topologías ferroviarias sobre una rejilla.

Requiere PyQt6 o PyQt5. Ejecutar con ``python3 editor.py`` desde este directorio.
"""

from __future__ import annotations

import sys
import json
from copy import deepcopy
from math import cos, pi, sin
from pathlib import Path

try:
    from PyQt6.QtCore import QPointF, QSize, Qt
    from PyQt6.QtGui import QBrush, QColor, QIcon, QKeySequence, QPainter, QPainterPath, QPen, QPixmap, QPolygonF, QShortcut
    from PyQt6.QtWidgets import (
        QApplication, QComboBox, QDialog, QDialogButtonBox, QFormLayout,
        QFileDialog, QGraphicsScene, QGraphicsView, QHBoxLayout,
        QLabel, QLineEdit, QMainWindow, QMessageBox,
        QToolButton, QVBoxLayout, QWidget,
    )
    LEFT_BUTTON = Qt.MouseButton.LeftButton
except ImportError:
    from PyQt5.QtCore import QPointF, QSize, Qt
    from PyQt5.QtGui import QBrush, QColor, QIcon, QKeySequence, QPainter, QPainterPath, QPen, QPixmap, QPolygonF
    from PyQt5.QtWidgets import (
        QApplication, QComboBox, QDialog, QDialogButtonBox, QFormLayout,
        QFileDialog, QGraphicsScene, QGraphicsView, QHBoxLayout,
        QLabel, QLineEdit, QMainWindow, QMessageBox,
        QShortcut, QToolButton, QVBoxLayout, QWidget,
    )
    LEFT_BUTTON = Qt.LeftButton

from items import (
    Aguja, CvLineal, Direccion, IdElemento, Lado, Señal, TipoSeñal,
    deserialize_layout, serialize_layout,
)
from export import generar_config_ence


CELL = 52
COLS = 24
ROWS = 16


class AddItemCommand:
    def __init__(self, editor, item):
        self.editor = editor
        self.item = item
        self.previous_selection = editor.selected
        x, y = item.position
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
        self.editor.selected = self.item
        self.editor.draw_items()

    def undo(self):
        if self.item in self.editor.items:
            self.editor.items.remove(self.item)
        self.editor.selected = (
            self.previous_selection
            if self.previous_selection in self.editor.items else None
        )
        self.editor.draw_items()


class RotateCommand:
    def __init__(self, editor, item, amount):
        self.editor = editor
        self.item = item
        self.amount = amount
        self.previous_selection = editor.selected
        x, y = item.position
        self.text = f"{'RotCW' if amount > 0 else 'RotCCW'} {x} {y}"

    def execute(self):
        self.item.rotate(self.amount)
        self.editor.selected = self.item
        self.editor.draw_items()

    def undo(self):
        self.item.rotate(-self.amount)
        self.editor.selected = (
            self.previous_selection
            if self.previous_selection in self.editor.items else None
        )
        self.editor.draw_items()


class DeleteItemCommand:
    def __init__(self, editor, item):
        self.editor = editor
        self.item = item
        self.dependency = item.id.dep
        self.items = editor.items_by_dependency[self.dependency]
        self.index = self.items.index(item)
        x, y = item.position
        self.text = f"Delete {x} {y} {item.id.id}"

    def execute(self):
        items = self.editor.items_by_dependency.get(self.dependency, [])
        if self.item in items:
            items.remove(self.item)
        if self.editor.selected is self.item:
            self.editor.selected = None
        self.editor.draw_items()

    def undo(self):
        items = self.editor.items_by_dependency.setdefault(self.dependency, [])
        if self.item not in items:
            items.insert(min(self.index, len(items)), self.item)
        if self.editor.current_dependency == self.dependency:
            self.editor.selected = self.item
        self.editor.draw_items()


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
        self.previous_selection = editor.selected
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
        self.editor.selected = None
        self.editor.draw_items()

    def execute(self):
        self.apply_layout(self.layouts, self.loaded_histories)

    def undo(self):
        self.apply_layout(self.previous_layouts, self.previous_histories)
        self.editor.selected = (
            self.previous_selection if self.previous_selection in self.editor.items else None
        )
        self.editor.draw_items()


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
        self.editor.selected = self.item
        self.editor.draw_items()

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
        self.setMinimumSize(COLS * CELL + 4, ROWS * CELL + 4)

    def mousePressEvent(self, event):
        if event.button() == LEFT_BUTTON:
            point = self.mapToScene(event.position().toPoint() if hasattr(event, "position") else event.pos())
            col, row = int(point.x() // CELL), int(point.y() // CELL)
            if 0 <= col < COLS and 0 <= row < ROWS:
                self.editor.cell_clicked((col, ROWS - row - 1))
                return
        super().mousePressEvent(event)

    def mouseDoubleClickEvent(self, event):
        if event.button() == LEFT_BUTTON:
            point = self.mapToScene(
                event.position().toPoint() if hasattr(event, "position") else event.pos()
            )
            col, row = int(point.x() // CELL), int(point.y() // CELL)
            if 0 <= col < COLS and 0 <= row < ROWS:
                position = (col, ROWS - row - 1)
                item = next(
                    (item for items in self.editor.items_by_dependency.values()
                     for item in items if item.position == position),
                    None,
                )
                if item is not None:
                    self.editor.dependency.setCurrentText(item.id.dep)
                    self.editor.switch_dependency()
                    self.editor.selected = item
                    self.editor.draw_items()
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

    def cell_clicked(self, position):
        existing = next((item for item in self.items if item.position == position), None)
        kind, value = self.mode
        if kind == "select":
            existing = next(
                (item for items in self.items_by_dependency.values() for item in items
                 if item.position == position),
                None,
            )
            if existing is not None:
                self.dependency.setCurrentText(existing.id.dep)
                self.switch_dependency()
            self.selected = existing
            self.draw_items()
            self.configure_button.setEnabled(existing is not None)
            if existing:
                self.statusBar().showMessage(f"Seleccionado: {existing.id.id}")
            return
        occupant = next(
            (item for items in self.items_by_dependency.values() for item in items
             if item.position == position),
            None,
        )
        if occupant:
            QMessageBox.warning(
                self, "Celda ocupada",
                f"La celda ya está ocupada por {occupant.id.id}.",
            )
            return
        dependency = self.dependency.currentText().strip()
        if not dependency:
            QMessageBox.warning(self, "Falta la dependencia", "Indica el nombre de la estación antes de añadir elementos.")
            self.dependency.setFocus()
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
        if kind == "track":
            item = CvLineal(ident, position, angle=value)
            item.lado = side
        elif kind == "switch":
            item = Aguja(ident, position, angle=value)
            item.cv = IdElemento(cv_name, dependency)
            item.lado = side
        else:
            item = Señal(ident, position, signal_type)
        self.run_command(AddItemCommand(self, item))

    def configure_selected(self):
        item = self.selected
        if item is None:
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
        if self.selected is None:
            self.statusBar().showMessage("Selecciona primero un elemento de la rejilla.")
            return
        self.run_command(RotateCommand(self, self.selected, amount))

    def delete_selected(self):
        if self.selected is None:
            self.statusBar().showMessage("Selecciona primero un elemento de la rejilla.")
            return
        self.run_command(DeleteItemCommand(self, self.selected))

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
        except (OSError, ValueError, KeyError, IndexError, TypeError, AttributeError) as exc:
            QMessageBox.critical(self, "Error al exportar", str(exc))
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
        pen = QPen(QColor("#d7dee7"), 1)
        for col in range(COLS + 1):
            self.scene.addLine(col * CELL, 0, col * CELL, ROWS * CELL, pen)
        for row in range(ROWS + 1):
            self.scene.addLine(0, row * CELL, COLS * CELL, row * CELL, pen)
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
            self.delete_button.setEnabled(self.selected is not None)
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
            x, model_y = item.position
            row = ROWS - model_y - 1
            center = ((x + .5) * CELL, (row + .5) * CELL)
            if item is self.selected:
                color = QColor("#e53935")
            elif item.id.dep == self.current_dependency:
                color = QColor("#fdd835")
            else:
                color = QColor("#9e9e9e")
            pen = QPen(color, 4, Qt.PenStyle.SolidLine if hasattr(Qt, "PenStyle") else Qt.SolidLine)
            pen.setCapStyle(Qt.PenCapStyle.FlatCap if hasattr(Qt, "PenCapStyle") else Qt.FlatCap)
            pen.setJoinStyle(Qt.PenJoinStyle.MiterJoin if hasattr(Qt, "PenJoinStyle") else Qt.MiterJoin)
            directions = item.get_directions()
            if isinstance(item, CvLineal):
                pairs = [(directions[0], directions[1])]
            elif isinstance(item, Aguja):
                pairs = [(directions[0], directions[2]), (directions[1], directions[2])]
            elif isinstance(item, Señal):
                pairs = []
            else:
                pairs = []
            for a, b in pairs:
                p1 = self.direction_point(center, a)
                p2 = self.direction_point(center, b)
                if isinstance(item, CvLineal):
                    path = QPainterPath(QPointF(*p1))
                    path.lineTo(QPointF(*center))
                    path.lineTo(QPointF(*p2))
                    graphic = self.scene.addPath(path, pen)
                    self.item_graphics.append(graphic)
                elif isinstance(item, Señal):
                    line = self.scene.addLine(p1[0], p1[1], p2[0], p2[1], pen)
                    self.item_graphics.append(line)
                else:
                    first = self.scene.addLine(p1[0], p1[1], center[0], center[1], pen)
                    second = self.scene.addLine(center[0], center[1], p2[0], p2[1], pen)
                    self.item_graphics.extend((first, second))
            if isinstance(item, Señal):
                angle = item.rotation * pi / 4
                ux, uy = cos(angle), sin(angle)
                for offset, light_color in (
                    (-11, QColor("#fdd835")),
                    (0, QColor("#e53935")),
                    (11, QColor("#2ecc71")),
                ):
                    light_center = (center[0] + ux * offset, center[1] + uy * offset)
                    radius = 4.5
                    light_pen = QPen(QColor("#9e9e9e"), 1.5)
                    lamp = self.scene.addEllipse(
                        light_center[0] - radius,
                        light_center[1] - radius,
                        radius * 2,
                        radius * 2,
                        light_pen,
                        QBrush(light_color),
                    )
                    self.item_graphics.append(lamp)
            if isinstance(item, CvLineal) and item.lado is not None:
                arrow_direction = directions[0] if item.lado == Lado.Impar else directions[1]
                self.item_graphics.extend(
                    self.add_direction_arrow(
                        center, arrow_direction, color, position_direction=directions[1]
                    )
                )
            elif isinstance(item, Aguja) and item.lado is not None:
                arrow_direction = directions[0] if item.lado == Lado.Impar else directions[2]
                self.item_graphics.extend(
                    self.add_direction_arrow(
                        center, arrow_direction, color, position_direction=directions[2]
                    )
                )
            label = self.scene.addText(item.id.id_corto)
            label.setDefaultTextColor(color)
            label_offset = CELL * (.32 if isinstance(item, Señal) else .17)
            label.setPos(center[0] - label.boundingRect().width() / 2, center[1] + label_offset)
            label.setZValue(2)
            self.item_graphics.append(label)


def main():
    app = QApplication(sys.argv)
    window = TrackEditor()
    window.show()
    return app.exec() if hasattr(app, "exec") else app.exec_()


if __name__ == "__main__":
    raise SystemExit(main())
