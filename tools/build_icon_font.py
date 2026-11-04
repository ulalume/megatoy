#!/usr/bin/env python3
"""Builds assets/fonts/megatoy-icons.otf from the SVGs in tools/icons.

Each SVG is one filled glyph on Font Awesome's grid: a 512-high viewBox with
the baseline at y = 448. Requires fontTools.
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path

# Code points below Font Awesome's range (ICON_MIN_FA = 0xe005).
GLYPHS = {
    'piano': 0xE000,
}

UNITS_PER_EM = 512
ASCENT = 448
DESCENT = -64

root = Path(__file__).resolve().parent.parent
icons = root / 'tools' / 'icons'
output = root / 'assets' / 'fonts' / 'megatoy-icons.otf'


def translation(element):
    match = re.search(r'matrix\(1,0,0,1,([-\d.]+),([-\d.]+)\)',
                      element.get('transform', ''))
    return (float(match[1]), float(match[2])) if match else (0.0, 0.0)


def advance(svg_path):
    view_box = ET.parse(svg_path).getroot().get('viewBox').split()
    return round(float(view_box[2]))


def draw(svg_path, pen):
    tree = ET.parse(svg_path)

    def walk(element, dx, dy):
        tx, ty = translation(element)
        dx, dy = dx + tx, dy + ty
        if element.tag.endswith('path'):
            flipped = TransformPen(pen, (1, 0, 0, -1, dx, ASCENT - dy))
            parse_path(element.get('d'), flipped)
        for child in element:
            walk(child, dx, dy)

    walk(tree.getroot(), 0.0, 0.0)


def main():
    names = ['.notdef'] + list(GLYPHS)
    builder = FontBuilder(UNITS_PER_EM, isTTF=False)
    builder.setupGlyphOrder(names)
    builder.setupCharacterMap({code: name for name, code in GLYPHS.items()})
    charstrings = {}
    metrics = {}
    charstrings['.notdef'] = T2CharStringPen(UNITS_PER_EM, None).getCharString()
    metrics['.notdef'] = (UNITS_PER_EM, 0)
    for name in GLYPHS:
        svg = icons / f'{name}.svg'
        width = advance(svg)
        pen = T2CharStringPen(width, None)
        draw(svg, pen)
        charstrings[name] = pen.getCharString()
        metrics[name] = (width, 0)
    builder.setupCFF('megatoyIcons', {'FullName': 'megatoy Icons'},
                     charstrings, {})
    builder.setupHorizontalMetrics(metrics)
    builder.setupHorizontalHeader(ascent=ASCENT, descent=DESCENT)
    builder.setupNameTable({'familyName': 'megatoy Icons',
                            'styleName': 'Regular'})
    builder.setupOS2(sTypoAscender=ASCENT, sTypoDescender=DESCENT,
                     usWinAscent=ASCENT, usWinDescent=-DESCENT)
    builder.setupPost()
    builder.save(output)
    print(output)


if __name__ == '__main__':
    sys.exit(main())
