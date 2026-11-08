#!/usr/bin/env python3
"""Brings icons into tools/icons as filled glyph SVGs for build_icon_font.py.

usage: import_icons.py [--force] [--fontawesome OTF]
       import_icons.py --outline SOURCE_DIR

The first form reads tools/icons/manifest.json and writes
tools/icons/<name>.svg for each icon whose "from" is

  iconoven:<id>     IconOven's 24x24 stroke icon, fetched from Iconify and
                    outlined to Font Awesome's 512 grid;
  fontawesome:<id>  Font Awesome Free Solid's glyph, as it is in the font,
                    read from OTF (fetched from Font Awesome's repository
                    when not given);
  custom            drawn for megatoy and kept as it is.

Icons already there are left alone unless --force. The second form outlines
every 24x24 stroke SVG in SOURCE_DIR into tools/icons/<name>.svg, as the
instrument icons drawn for megatoy were. Requires the packages in
tools/requirements.txt.
"""
import argparse
import json
import sys
import tempfile
import urllib.request
from pathlib import Path

import pathops
from fontTools.pens.svgPathPen import SVGPathPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path
from fontTools.ttLib import TTFont
from picosvg.svg import SVG

# A 24x24 icon's 2..22 drawing area on Font Awesome's 512 grid.
SCALE = 512 / 20
OFFSET = -2 * SCALE
# Font Awesome's baseline on that grid, where its glyphs' y = 0 falls.
ASCENT = 448

ICONIFY = 'https://api.iconify.design/iconoven/{}.svg'
FONT_AWESOME = ('https://raw.githubusercontent.com/FortAwesome/Font-Awesome/'
                '7.x/otfs/Font%20Awesome%207%20Free-Solid-900.otf')

icons = Path(__file__).resolve().parent / 'icons'


def svg(d, width=512):
    return ('<svg xmlns="http://www.w3.org/2000/svg" '
            f'viewBox="0 0 {width} 512"><path d="{d}"/></svg>\n')


def outline(source):
    """A stroke SVG's shapes, flattened and merged into one filled path."""
    pico = SVG.fromstring(source).topicosvg()
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
    return svg(pen.getCommands())


def fetch(url):
    request = urllib.request.Request(url, headers={'User-Agent': 'megatoy'})
    with urllib.request.urlopen(request) as response:
        return response.read()


def iconoven(icon_id):
    return outline(fetch(ICONIFY.format(icon_id)).decode())


class FontAwesome:
    """Font Awesome Free Solid's glyphs by name, read from its font."""

    def __init__(self, path):
        self.path = path
        self.font = None

    def glyph(self, icon_id):
        if self.font is None:
            if self.path is None:
                download = Path(tempfile.gettempdir()) / 'fa7-solid.otf'
                download.write_bytes(fetch(FONT_AWESOME))
                self.path = download
            self.font = TTFont(self.path)
        glyphs = self.font.getGlyphSet()
        if icon_id not in glyphs:
            sys.exit(f'Font Awesome has no glyph named {icon_id}')
        pen = SVGPathPen(glyphs, ntos=lambda v: f'{round(v, 1):g}')
        glyphs[icon_id].draw(TransformPen(pen, (1, 0, 0, -1, 0, ASCENT)))
        return svg(pen.getCommands(), glyphs[icon_id].width)


def import_manifest(force, fontawesome_path):
    manifest = json.loads((icons / 'manifest.json').read_text())
    fontawesome = FontAwesome(fontawesome_path)
    for icon in manifest['ui']:
        target = icons / f'{icon["name"]}.svg'
        kind, _, icon_id = icon['from'].partition(':')
        if kind == 'custom':
            if not target.exists():
                sys.exit(f'{target} is missing')
            continue
        if target.exists() and not force:
            continue
        if kind == 'iconoven':
            target.write_text(iconoven(icon_id))
        elif kind == 'fontawesome':
            target.write_text(fontawesome.glyph(icon_id))
        else:
            sys.exit(f'{icon["name"]}: unknown source {icon["from"]}')
        print(target)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--force', action='store_true')
    parser.add_argument('--fontawesome', type=Path)
    parser.add_argument('--outline', type=Path)
    args = parser.parse_args()
    if args.outline:
        for source in sorted(args.outline.glob('*.svg')):
            (icons / source.name).write_text(outline(source.read_text()))
        return
    import_manifest(args.force, args.fontawesome)


if __name__ == '__main__':
    sys.exit(main())
