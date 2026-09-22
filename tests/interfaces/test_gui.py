import time

from PySide6.QtWidgets import QApplication, QMessageBox
import pytest
from PySide6.QtCore import QPoint, Qt

from srcds64_ui.gui import ManagerWindow
from srcds64_ui.model import Settings


@pytest.fixture(scope="module")
def qt_app():
    app = QApplication.instance() or QApplication([])
    yield app


def finish(qt_app, window):
    deadline = time.monotonic() + 10
    while window.job is not None and time.monotonic() < deadline:
        qt_app.processEvents()
        time.sleep(0.01)
    assert window.job is None


def test_gui_streams_success_and_failure(qt_app, fake_backend):
    window = ManagerWindow(Settings())
    window.show()
    window.start_action("plan")
    assert not window.form_widget.isEnabled()
    finish(qt_app, window)
    assert "BACKEND plan" in window.output.toPlainText()
    assert "Completed successfully" == window.status.text()
    window.start_action("verify")
    finish(qt_app, window)
    assert "exit 3" in window.status.text()
    assert window.form_widget.isEnabled()
    window.close()


def test_gui_install_confirmation(qt_app, fake_backend, monkeypatch):
    window = ManagerWindow(Settings())
    monkeypatch.setattr(QMessageBox, "question", lambda *args: QMessageBox.StandardButton.No)
    window.start_action("install")
    assert window.job is None
    monkeypatch.setattr(QMessageBox, "question", lambda *args: QMessageBox.StandardButton.Yes)
    window.start_action("install")
    finish(qt_app, window)
    assert "BACKEND install" in window.output.toPlainText()
    window.close()


def test_gui_actions_stay_visible_when_resized(qt_app):
    window = ManagerWindow(Settings())
    window.show()
    for width, height in ((1024, 600), (480, 600), (640, 480), (1120, 760)):
        window.resize(width, height)
        qt_app.processEvents()
        assert window.width() == width
        assert window.height() == height
        expected = Qt.Orientation.Vertical if width < 720 else Qt.Orientation.Horizontal
        assert window.splitter.orientation() == expected
        for button in window.buttons:
            top_left = button.mapTo(window, QPoint(0, 0))
            bottom_right = button.mapTo(window, button.rect().bottomRight())
            assert window.rect().contains(top_left)
            assert window.rect().contains(bottom_right)
            assert button.width() >= button.minimumSizeHint().width()
        assert window.output.height() >= 80
    window.close()
