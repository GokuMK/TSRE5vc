#!/usr/bin/env python3
"""Writes the dark colours of the OSM map styles into osm-map-classes.json.

The dark set (the "dark" block: background, default, then one entry a style in the
styles' order) is derived from the light colours by role, in OKLab with hues kept:
- areas dark and toned down, paler (less important) ones darkest, near the
  background, their colour strength capped by their lightness; water a fixed
  deep blue;
- lines (roads) light and dimmed, their class colour kept; grey lines (railways)
  turned light;
- casings dark, black ones (bridge edges) light.
Run it after editing the light colours (or regenerating the file); it changes only
the "dark" block. FeatureClasses::dark() merges these colours over the styles.

Usage: scripts/osm_dark_palette.py [src/tsre/geo/osm/osm-map-classes.json]
"""
import json
import math
import re
import sys
from pathlib import Path

DEFAULT = Path(__file__).resolve().parent.parent / 'src' / 'tsre' / 'geo' / 'osm' / 'osm-map-classes.json'
BACKGROUND = '#1e2227'
WATER_FILL, WATER_LINE = '#16324b', '#1f4a6e'


def to_linear(c):
    return c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4


def from_linear(c):
    return 12.92 * c if c <= 0.0031308 else 1.055 * c ** (1 / 2.4) - 0.055


def lch(colour):
    r, g, b = [to_linear(int(colour[i:i + 2], 16) / 255) for i in (1, 3, 5)]
    l = 0.4122214708 * r + 0.5363325363 * g + 0.0514459929 * b
    m = 0.2119034982 * r + 0.6806995451 * g + 0.1073969566 * b
    s = 0.0883024619 * r + 0.2817188376 * g + 0.6299787005 * b
    l, m, s = [math.copysign(abs(x) ** (1 / 3), x) for x in (l, m, s)]
    L = 0.2104542553 * l + 0.7936177850 * m - 0.0040720468 * s
    a = 1.9779984951 * l - 2.4285922050 * m + 0.4505937099 * s
    bb = 0.0259040371 * l + 0.7827717662 * m - 0.8086757660 * s
    return L, math.hypot(a, bb), math.atan2(bb, a)


def colour(L, C, H):
    a, b = C * math.cos(H), C * math.sin(H)
    l = (L + 0.3963377774 * a + 0.2158037573 * b) ** 3
    m = (L - 0.1055613458 * a - 0.0638541728 * b) ** 3
    s = (L - 0.0894841775 * a - 1.2914855480 * b) ** 3
    rgb = (4.0767416621 * l - 3.3077115913 * m + 0.2309699292 * s,
           -1.2684380046 * l + 2.6097574011 * m - 0.3413193965 * s,
           -0.0041960863 * l - 0.7034186147 * m + 1.7076147010 * s)
    return '#' + ''.join('%02x' % round(255 * min(1, max(0, from_linear(max(0, c))))) for c in rgb)


def fill(c):
    L, C, H = lch(c)
    dark = min(0.42, max(0.24, 0.24 + 0.62 * (0.97 - L)))
    # Pale fills (grass, fields, built-up) less coloured, so they do not turn olive;
    # and no dark fill more coloured than its lightness carries (strong colour at low
    # lightness looks garish: allotments, pitches).
    cap = 0.2 * dark
    # Yellow-greens (allotments, heath, grass) go muddy olive when dark: a little
    # greener, and less coloured.
    if math.radians(95) <= H <= math.radians(130):
        H += math.radians(12)
        cap = 0.12 * dark
    return colour(dark, min(C * (0.4 if L > 0.86 else 0.55), cap), H)


def outline(c):
    L, C, H = lch(fill(c))
    return colour(L + 0.07, C, H)


def line(c):
    L, C, H = lch(c)
    if C < 0.03 and L < 0.6:  # dark grey lines (railways, the default): light on dark
        return colour(min(0.86, 1.2 - L), 0, H)
    return colour(0.36 + 0.5 * L, C * 0.75, H)


def casing(c):
    L, C, H = lch(c)
    return colour(0.17 if L > 0.5 else 0.82, 0, H)


def is_water(tags):
    return any(t.startswith(('natural=water', 'waterway=', 'natural=coastline')) for t in tags)


def dark_style(style, tags):
    """The colours of a style, dark, in the style's own shape (colours only)."""
    water = is_water(tags)
    out = {}
    if 'fill' in style:
        out['fill'] = WATER_FILL if water else fill(style['fill'])
    if 'outline' in style:
        out['outline'] = {'color': outline(style['outline']['color'])}
    if 'casing' in style:
        out['casing'] = {'color': casing(style['casing']['color'])}
        if 'bridgeColor' in style['casing']:
            out['casing']['bridgeColor'] = casing(style['casing']['bridgeColor'])
    if 'casings' in style:
        out['casings'] = [{'color': casing(c['color'])} for c in style['casings']]
    if 'line' in style:
        out['line'] = {'color': WATER_LINE if water else line(style['line']['color'])}
    if 'bridge' in style:
        out['bridge'] = dark_style(style['bridge'], tags)
    return out


def main():
    path = Path(sys.argv[1]) if len(sys.argv) > 1 else DEFAULT
    text = path.read_text(encoding='utf-8')
    classes = json.loads(text)
    compact = lambda v: json.dumps(v, ensure_ascii=False, separators=(', ', ': '))
    styles = [dark_style(s, s['tags']) for s in classes['styles']]
    head = {'background': BACKGROUND, 'default': dark_style(classes['default'], [])}
    # Lines of no class stay quiet: mid grey, not as light as railways.
    if 'line' in head['default']:
        head['default']['line']['color'] = colour(0.55, 0, 0)
    block = ('  "dark": {"background": %s, "default": %s, "styles": [\n' % (compact(head['background']), compact(head['default']))
             + ',\n'.join('    ' + compact(s) for s in styles) + '\n  ]},\n')
    # Replace the block, or add it before the overview rules.
    text, count = re.subn(r'  "dark": \{.*?\n  \]\},\n', lambda m: block, text, flags=re.S)
    if count == 0:
        text = text.replace('  "overview": {', block + '  "overview": {', 1)
    path.write_text(text, encoding='utf-8')
    json.loads(text)
    print('Wrote %d dark styles to %s' % (len(styles), path))


if __name__ == '__main__':
    main()
