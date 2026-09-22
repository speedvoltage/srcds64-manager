"""Render the original vector icon into PNG and a multi-resolution Windows ICO."""
import os
from pathlib import Path
import struct
os.environ.setdefault('QT_QPA_PLATFORM', 'offscreen')
from PySide6.QtCore import QByteArray, QBuffer, QIODevice, Qt
from PySide6.QtGui import QGuiApplication, QImage, QPainter
from PySide6.QtSvg import QSvgRenderer

app = QGuiApplication([])
assets = Path(__file__).resolve().parents[1] / 'interfaces/src/srcds64_ui/assets'
renderer = QSvgRenderer(str(assets / 'srcds64-manager.svg'))
images = []
for size in (16, 24, 32, 48, 64, 128, 256):
    image = QImage(size, size, QImage.Format.Format_ARGB32)
    image.fill(Qt.GlobalColor.transparent)
    painter = QPainter(image)
    renderer.render(painter)
    painter.end()
    data = QByteArray()
    buffer = QBuffer(data)
    buffer.open(QIODevice.OpenModeFlag.WriteOnly)
    assert image.save(buffer, 'PNG')
    images.append((size, bytes(data)))
    if size == 256:
        (assets / 'srcds64-manager.png').write_bytes(bytes(data))
offset = 6 + 16 * len(images)
header = struct.pack('<HHH', 0, 1, len(images))
for size, data in images:
    header += struct.pack('<BBBBHHII', size % 256, size % 256, 0, 0, 1, 32, len(data), offset)
    offset += len(data)
(assets / 'srcds64-manager.ico').write_bytes(header + b''.join(data for _, data in images))
