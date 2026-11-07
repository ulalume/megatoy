#!/usr/bin/env python3
"""Builds assets/fonts/megatoy-icons.otf from the SVGs in tools/icons.

Each SVG is one filled glyph on Font Awesome's grid: a 512-high viewBox with
the baseline at y = 448. Also writes src/gui/fonts/megatoy_icons.hpp and the
instrument icon table, src/gui/fonts/instrument_icon_table.inc. Requires
fontTools.
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from fontTools.fontBuilder import FontBuilder
from fontTools.pens.t2CharStringPen import T2CharStringPen
from fontTools.pens.transformPen import TransformPen
from fontTools.svgLib.path import parse_path

# Instrument icons by group, in the order the picker shows them.
INSTRUMENT_GROUPS = [
    ('Piano', ['grand-piano', 'upright-piano', 'keyboard', 'harpsichord']),
    ('Chromatic Percussion', ['xylophone', 'bell', 'church-bell', 'music-box',
                              'glockenspiel']),
    ('Organ', ['pipe-organ', 'organ', 'accordion', 'harmonica']),
    ('Guitar', ['acoustic-guitar', 'electric-guitar']),
    ('Bass', ['bass', 'upright-bass']),
    ('Strings', ['violin', 'cello', 'harp']),
    ('Ensemble', ['choir', 'orchestra', 'strings-ensemble']),
    ('Brass', ['trumpet', 'trombone', 'tuba', 'french-horn', 'horn']),
    ('Reed', ['saxophone', 'clarinet', 'oboe', 'bassoon']),
    ('Pipe', ['flute', 'recorder', 'pan-flute', 'ocarina', 'whistle']),
    ('Synth', ['synthesizer', 'synth-bass', 'sine', 'square', 'sawtooth',
               'triangle-wave', 'pad', 'fx']),
    ('Ethnic', ['sitar', 'banjo', 'mandolin', 'ukulele', 'lute', 'shamisen',
                'koto', 'kalimba', 'bagpipe', 'didgeridoo']),
    ('Percussive', ['timpani', 'gong', 'cowbell', 'triangle', 'steel-drum',
                    'taiko']),
    ('Drums', ['drum-kit', 'drum-machine', 'kick', 'snare', 'cymbal', 'conga',
               'bongos', 'tambourine', 'maracas', 'castanets', 'clap']),
    ('Voice', ['voice', 'mic']),
    ('Sound Effects', ['bird', 'phone', 'helicopter', 'siren', 'explosion',
                       'thunder', 'wind', 'rain', 'seashore', 'laser', 'ufo',
                       'robot']),
]
# Shown for a patch without an icon.
INSTRUMENT_NONE = 'music-note'

# Code points below Font Awesome's range (ICON_MIN_FA = 0xe005).
GLYPHS = {
    'piano': 0xE000,
}
# The instrument icons, in a block Font Awesome's font leaves empty.
INSTRUMENT_FIRST = 0xE900
INSTRUMENTS = [INSTRUMENT_NONE] + [
    name for _, names in INSTRUMENT_GROUPS for name in names]
GLYPHS.update(
    {name: INSTRUMENT_FIRST + i for i, name in enumerate(INSTRUMENTS)})
INSTRUMENT_LAST = INSTRUMENT_FIRST + len(INSTRUMENTS) - 1

UNITS_PER_EM = 512
ASCENT = 448
DESCENT = -64

root = Path(__file__).resolve().parent.parent
icons = root / 'tools' / 'icons'
output = root / 'assets' / 'fonts' / 'megatoy-icons.otf'
header = root / 'src' / 'gui' / 'fonts' / 'megatoy_icons.hpp'
table = root / 'src' / 'gui' / 'fonts' / 'instrument_icon_table.inc'


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


def utf8(code):
    return ''.join(f'\\x{b:02x}' for b in chr(code).encode())


def write_header():
    header.write_text(f'''#pragma once

// Icons drawn for megatoy: assets/fonts/megatoy-icons.otf, built from
// tools/icons by tools/build_icon_font.py, which writes this file.
#define ICON_MIN_MEGATOY 0xe000
#define ICON_MAX_MEGATOY 0xe000
#define ICON_MEGATOY_PIANO "{utf8(GLYPHS['piano'])}" // U+e000
#define ICON_MIN_MEGATOY_INSTRUMENT {INSTRUMENT_FIRST:#x}
#define ICON_MAX_MEGATOY_INSTRUMENT {INSTRUMENT_LAST:#x}
''')


def array_name(group):
    return 'k' + group.replace(' ', '') + 'Icons'


def write_table():
    none = utf8(GLYPHS[INSTRUMENT_NONE])
    lines = ['// Written by tools/build_icon_font.py.',
             '// clang-format off',
             f'constexpr char kNoneGlyph[] = "{none}";']
    for group, names in INSTRUMENT_GROUPS:
        var = array_name(group)
        lines.append(f'constexpr Icon {var}[] = {{')
        lines += [f'    {{"{name}", "{utf8(GLYPHS[name])}"}},'
                  for name in names]
        lines.append('};')
    lines.append('constexpr Group kGroups[] = {')
    lines += [f'    {{"{group}", {array_name(group)}}},'
              for group, _ in INSTRUMENT_GROUPS]
    lines.append('};')
    table.write_text('\n'.join(lines) + '\n')


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
    write_header()
    write_table()
    print(output)


if __name__ == '__main__':
    sys.exit(main())
