#!/usr/bin/env python3
"""Outlines 24x24 stroke SVGs into filled glyph SVGs for build_icon_font.py.

usage: outline_icons.py SOURCE_DIR

Each SOURCE_DIR/<name>.svg is converted to tools/icons/<name>.svg: strokes
and transforms are flattened to fills (picosvg), the shapes are merged into
one outline (skia-pathops), and the 2..22 drawing area is scaled to Font
Awesome's 512 grid. Requires picosvg.
"""
import sys
from pathlib import Path

import pathops
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path
from picosvg.svg import SVG

SCALE = 512 / 20
OFFSET = -2 * SCALE

out_dir = Path(__file__).resolve().parent / 'icons'


def outline(svg_path):
    pico = SVG.parse(svg_path).topicosvg()
    paths = []
    for shape in pico.shapes():
        path = pathops.Path()
        pen = TransformPen(path.getPen(),
                           (SCALE, 0, 0, SCALE, OFFSET, OFFSET))
        parse_path(shape.as_path().d, pen)
        paths.append(path)
    merged = pathops.Path()
    pathops.union(paths, merged.getPen())
    pen = SVGPathPen(None, ntos=lambda v: f'{round(v, 1):g}')
    merged.draw(pen)
    return pen.getCommands()


def main():
    for svg_path in sorted(Path(sys.argv[1]).glob('*.svg')):
        d = outline(svg_path)
        (out_dir / svg_path.name).write_text(
            '<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512">'
            f'<path d="{d}"/></svg>\n')


if __name__ == '__main__':
    sys.exit(main())
