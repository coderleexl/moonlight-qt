#!/usr/bin/env python3
"""Export the Desk SVG to native icons. Requires Qt (qmake/make) and Pillow."""
import os
from pathlib import Path
import subprocess
import tempfile

from PIL import Image

root = Path(__file__).resolve().parent.parent
source = root / 'app/res/desk.svg'
# Use the same SVG renderer as the application.
renderer = r'''
#include <QGuiApplication>
#include <QSvgRenderer>
#include <QImage>
#include <QPainter>
int main(int argc, char** argv) {
    QGuiApplication app(argc, argv);
    if (argc != 3) return 1;
    QSvgRenderer svg(QString::fromLocal8Bit(argv[1]));
    if (!svg.isValid()) return 2;
    QImage image(1024, 1024, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    svg.render(&painter);
    painter.end();
    return image.save(QString::fromLocal8Bit(argv[2])) ? 0 : 3;
}
'''
with tempfile.TemporaryDirectory(prefix='desk-icons-') as temporary:
    work = Path(temporary)
    (work / 'render.cpp').write_text(renderer)
    (work / 'render.pro').write_text('QT = core gui svg\nCONFIG += console c++17 release\n'
                                   'CONFIG -= app_bundle debug_and_release\nDESTDIR = .\n'
                                   'SOURCES = render.cpp\nTARGET = render\n')
    subprocess.run(['qmake', 'render.pro'], cwd=work, check=True)
    subprocess.run(['nmake' if os.name == 'nt' else 'make'], cwd=work, check=True)
    subprocess.run([str(work / ('render.exe' if os.name == 'nt' else 'render')), str(source), str(work / 'icon.png')],
                   env=dict(os.environ, QT_QPA_PLATFORM='offscreen'), check=True)
    with Image.open(work / 'icon.png') as image:
        image.save(root / 'app/desk.ico', sizes=[(s, s) for s in (16, 20, 24, 32, 40, 48, 64, 128, 256)])
        image.save(root / 'app/desk.icns')
        image.resize((512, 512), Image.Resampling.LANCZOS).save(root / 'example/desk-icon.png')
print('Updated app/desk.ico, app/desk.icns and example/desk-icon.png from app/res/desk.svg')
