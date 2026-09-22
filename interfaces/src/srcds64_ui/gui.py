"""Native Qt desktop interface for Windows and Linux."""
from __future__ import annotations

import sys
from pathlib import Path
from PySide6.QtCore import Qt, QTimer
from PySide6.QtGui import QCloseEvent, QIcon, QTextCursor
from PySide6.QtWidgets import (QApplication, QCheckBox, QComboBox, QFileDialog,
                              QFormLayout, QGridLayout, QLabel, QLineEdit,
                              QMainWindow, QMessageBox, QPlainTextEdit, QPushButton,
                              QScrollArea, QSplitter, QVBoxLayout, QWidget)

from .jobs import Job
from .model import GAMES, NOTICE, Settings, load_settings, save_settings
from .presentation import FIELDS, confirmation_text, plain_output


class ManagerWindow(QMainWindow):
    def __init__(self, settings: Settings | None = None):
        super().__init__()
        self.setWindowTitle("SRCDS 64 Manager")
        self.setWindowIcon(QIcon(str(Path(__file__).parent / "assets/srcds64-manager.ico")))
        available = self.screen().availableGeometry()
        self.resize(min(1120, max(320, available.width() - 40)),
                    min(760, max(320, available.height() - 80)))
        self._compact = None
        self.job: Job | None = None
        startup_error = ""
        try:
            settings = settings or load_settings()
        except ValueError as error:
            settings = Settings()
            startup_error = str(error)
        root = QWidget()
        self.setCentralWidget(root)
        layout = QVBoxLayout(root)
        title = QLabel("SRCDS 64 Manager")
        title.setStyleSheet("font-size: 26px; font-weight: 600; padding: 8px 0;")
        layout.addWidget(title)
        layout.addWidget(QLabel(NOTICE))
        splitter = self.splitter = QSplitter()
        splitter.setChildrenCollapsible(False)
        layout.addWidget(splitter, 1)
        self.form_widget = QWidget()
        form = QFormLayout(self.form_widget)
        form.setRowWrapPolicy(QFormLayout.RowWrapPolicy.WrapAllRows)
        self.game = QComboBox()
        for game in GAMES:
            self.game.addItem(game["name"], game["id"])
        self.game.setCurrentIndex(max(0, self.game.findData(settings.game)))
        form.addRow("Game", self.game)
        self.inputs: dict[str, QLineEdit] = {}
        for name, label, placeholder in FIELDS:
            edit = QLineEdit(getattr(settings, name))
            edit.setPlaceholderText(placeholder)
            edit.setAccessibleName(label)
            self.inputs[name] = edit
            form.addRow(label, edit)
        browse = QPushButton("Choose server directory…")
        browse.clicked.connect(self.choose_directory)
        form.addRow(browse)
        self.validate = QCheckBox("Validate game downloads")
        self.validate.setChecked(settings.validate)
        form.addRow(self.validate)
        save = QPushButton("Save settings")
        save.clicked.connect(self.save)
        form.addRow(save)
        scroll = QScrollArea()
        scroll.setWidgetResizable(True)
        scroll.setMinimumSize(0, 100)
        scroll.setWidget(self.form_widget)
        splitter.addWidget(scroll)
        output_pane = QWidget()
        output_layout = QVBoxLayout(output_pane)
        self.status = QLabel("Ready • Preview an installation to begin")
        self.status.setWordWrap(True)
        output_layout.addWidget(self.status)
        self.output = QPlainTextEdit()
        self.output.setReadOnly(True)
        self.output.setMinimumSize(0, 80)
        self.output.setMaximumBlockCount(10000)
        self.output.setAccessibleName("Installer output")
        self.output.setStyleSheet("font-family: monospace;")
        output_layout.addWidget(self.output)
        export = QPushButton("Export visible log…")
        export.clicked.connect(self.export_log)
        output_layout.addWidget(export)
        splitter.addWidget(output_pane)
        splitter.setSizes([360, 720])
        actions = self.actions = QGridLayout()
        self.buttons = []
        for label, action in (("Preview installation", "plan"), ("Set up tools", "doctor"),
                              ("Install server…", "install"), ("Verify server", "verify")):
            button = QPushButton(label)
            button.clicked.connect(lambda checked=False, name=action: self.start_action(name))
            actions.addWidget(button, 0, len(self.buttons))
            self.buttons.append(button)
        layout.addLayout(actions)
        self.timer = QTimer(self)
        self.timer.timeout.connect(self.poll_job)
        self.timer.start(50)
        if startup_error:
            self.append_output(startup_error + "\nUsing defaults; Save replaces invalid settings.\n")

    def resizeEvent(self, event):
        super().resizeEvent(event)
        if not hasattr(self, "actions"):
            return
        compact = self.width() < 720
        if compact == self._compact:
            return
        self._compact = compact
        self.splitter.setOrientation(Qt.Orientation.Vertical if compact else Qt.Orientation.Horizontal)
        for button in self.buttons:
            self.actions.removeWidget(button)
        for column in range(4):
            self.actions.setColumnStretch(column, 0)
        columns = 2 if compact else 4
        for index, button in enumerate(self.buttons):
            self.actions.addWidget(button, index // columns, index % columns)
            self.actions.setColumnStretch(index % columns, 1)
        self.splitter.setSizes([220, 280] if compact else [360, 720])

    def read_settings(self) -> Settings:
        return Settings(game=self.game.currentData(), validate=self.validate.isChecked(),
                        **{name: edit.text().strip() for name, edit in self.inputs.items()})

    def choose_directory(self) -> None:
        directory = QFileDialog.getExistingDirectory(self, "Choose a fresh server directory")
        if directory:
            self.inputs["install_dir"].setText(directory)

    def save(self) -> None:
        try:
            settings = self.read_settings()
            settings.command("plan")
            save_settings(settings)
            self.status.setText("Settings saved")
        except (ValueError, OSError) as error:
            QMessageBox.warning(self, "Cannot save settings", str(error))

    def export_log(self) -> None:
        from pathlib import Path
        path, _ = QFileDialog.getSaveFileName(self, "Export log", "srcds64.log", "Log files (*.log)")
        if path:
            try:
                Path(path).write_text(self.output.toPlainText(), encoding="utf-8")
            except OSError as error:
                QMessageBox.warning(self, "Cannot export log", str(error))

    def append_output(self, text: str) -> None:
        self.output.moveCursor(QTextCursor.MoveOperation.End)
        self.output.insertPlainText(plain_output(text))
        self.output.ensureCursorVisible()

    def start_action(self, action: str) -> None:
        if self.job:
            return
        try:
            settings = self.read_settings()
            command = settings.command(action)
        except ValueError as error:
            QMessageBox.warning(self, "Check settings", str(error))
            return
        if action == "install":
            answer = QMessageBox.question(self, "Install server", confirmation_text(settings),
                                          QMessageBox.StandardButton.Yes | QMessageBox.StandardButton.No,
                                          QMessageBox.StandardButton.No)
            if answer != QMessageBox.StandardButton.Yes:
                return
        self.append_output(f"\n── {action.upper()} ──\n")
        self.status.setText(f"Running: {action} • Keep this window open")
        self.form_widget.setEnabled(False)
        for button in self.buttons:
            button.setEnabled(False)
        self.job = Job(command)
        self.job.start()

    def poll_job(self) -> None:
        if not self.job:
            return
        for event in self.job.drain():
            if event.kind == "output":
                self.append_output(event.text)
            elif event.kind == "finished":
                self.status.setText("Completed successfully" if event.code == 0
                                    else f"Failed (exit {event.code}) • See output")
                self.job = None
                self.form_widget.setEnabled(True)
                for button in self.buttons:
                    button.setEnabled(True)

    def closeEvent(self, event: QCloseEvent) -> None:
        if self.job:
            QMessageBox.information(self, "Operation running",
                                    "Wait for the current operation to finish before closing.")
            event.ignore()
        else:
            event.accept()


def main() -> None:
    if sys.platform == "win32":
        import ctypes
        ctypes.windll.shell32.SetCurrentProcessExplicitAppUserModelID("Speedvoltage.SRCDS64.Manager")
    app = QApplication(sys.argv)
    app.setApplicationName("SRCDS 64 Manager")
    app.setDesktopFileName("srcds64-manager")
    window = ManagerWindow()
    window.show()
    sys.exit(app.exec())


if __name__ == "__main__":
    main()
