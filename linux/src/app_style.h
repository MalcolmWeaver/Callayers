#pragma once
#include <QApplication>

inline void ApplyAppStyle(QApplication& app) {
    app.setStyleSheet("QWidget { background:#1d1d21; color:#f4f3fa; } QLineEdit, QPlainTextEdit, QListWidget, QComboBox { background:#151519; border:1px solid #3c3c44; border-radius:6px; padding:6px; } QPushButton { background:#2e2e34; border:1px solid #3c3c44; border-radius:6px; padding:8px 12px; } QPushButton:hover { background:#3a3a42; } QMenuBar, QMenu { background:#1d1d21; }");
}
