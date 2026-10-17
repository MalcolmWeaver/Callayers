#pragma once
#include <QApplication>
#include <QFont>
inline void ApplyAppStyle(QApplication& app) {
    app.setFont(QFont("sans-serif", 10));
    app.setStyleSheet(R"(
QWidget { background:#1d1d21; color:#f4f3fa; }
QWidget#libraryPanel { background:#242428; border-radius:10px; }
QLabel { background:transparent; }
QLabel#sectionHeading { font-size:20px; font-weight:600; padding:4px 0 8px; }
QLineEdit, QPlainTextEdit, QListWidget, QComboBox { background:#19191d; border:1px solid #42414b; border-radius:6px; padding:7px; selection-background-color:#51467e; }
QLineEdit:focus, QPlainTextEdit:focus, QComboBox:focus { border-color:#a39bfa; }
QListWidget { border:0; padding:0; background:transparent; }
QListWidget::item { padding:9px 5px; border-radius:5px; }
QListWidget::item:selected { background:#403855; }
QListWidget::item:hover { background:#34323e; }
QPushButton { background:#303034; border:1px solid #44434d; border-radius:6px; padding:8px 10px; }
QPushButton:hover { background:#3d3b46; border-color:#777183; }
QPushButton:pressed, QPushButton:checked { background:#51467e; border-color:#a39bfa; }
QPushButton:disabled { color:#78757f; background:#29282e; }
QPushButton#primaryAction { background:#a39bfa; color:#211c35; border:0; font-weight:600; }
QPushButton#primaryAction:hover { background:#b7b0ff; }
QPushButton#primaryAction:disabled { background:#35333f; color:#78757f; }
QLineEdit:disabled, QPlainTextEdit:disabled, QListWidget:disabled { color:#78757f; background:#222126; }
QMenuBar, QMenu { background:#242428; }
QMenu::item { padding:7px 20px; }
QMenu::item:selected { background:#51467e; }
QToolTip { background:#34313f; color:#fff; border:1px solid #625976; padding:5px; }
)");
}
