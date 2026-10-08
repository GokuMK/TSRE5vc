# Generates osm-map-classes.json from the legacy OSMFeatures table and the MapDataOSM::draw() styles.
import re, json, sys
src = open(sys.argv[1], encoding='utf-8', errors='replace').read()
names = {int(i): n for n, i in re.findall(r'\{"([A-Z0-9_:]+)",\s*(\d+)\}', src)}
layers = {int(a): int(b) for a, b in re.findall(r'/\*(\d+)\*/ v\.push_back\((\d+)\);', src)}
def tag(name):
    for key in ('MAN_MADE', 'PUBLIC_TRANSPORT'):
        if name.startswith(key + '_'): return key.lower() + '=' + name[len(key) + 1:].lower()
    k, v = name.split('_', 1)
    return k.lower() + '=' + v.lower()
classes = [{"tag": tag(names[i]), "layer": layers.get(i, 0)} for i in sorted(names)]

def c(r, g, b): return '#%02x%02x%02x' % (r, g, min(b, 255))
# Old pen widths are pixels at 2 px per metre (4096 px over a 2048 m tile).
GREY_CASING, BRIDGE_CASING = c(180, 180, 180), c(0, 0, 0)
def road(tags, casing_px, line_rgb, line_px):
    return {"tags": tags, "casing": {"color": GREY_CASING, "bridgeColor": BRIDGE_CASING, "width": casing_px / 2},
            "line": {"color": c(*line_rgb), "width": line_px / 2, "cap": "round"}}
def area(tags, fill, outline=None):
    s = {"tags": tags, "fill": c(*fill)}
    if outline: s["outline"] = {"color": c(*outline), "width": 0}
    return s
def line(tags, rgb, px, cap="round"):
    return {"tags": tags, "line": {"color": c(*rgb), "width": px / 2, "cap": cap}}
styles = [
    area(["building=*"], (190, 173, 173), (169, 148, 165)),
    area(["shop=*"], (200, 170, 170), (169, 148, 165)),
    area(["natural=wood"], (141, 196, 108)),
    area(["landuse=forest"], (133, 193, 133)),
    area(["railway=station"], (212, 170, 170)),
    area(["landuse=grass", "landuse=village_green", "landuse=recreation_ground"], (207, 236, 168)),
    area(["leisure=sports_centre", "leisure=stadium", "leisure=park"], (206, 246, 202)),
    area(["leisure=pitch"], (137, 210, 174), (180, 180, 180)),
    area(["leisure=track"], (116, 219, 185), (180, 180, 180)),
    area(["natural=scrub"], (181, 226, 181)),
    area(["natural=wetland"], (95, 180, 160)),
    area(["natural=water", "waterway=riverbank", "landuse=reservoir", "landuse=basin"], (181, 208, 208)),
    area(["amenity=parking"], (246, 238, 182)),
    area(["landuse=farm"], (234, 216, 184)),
    area(["landuse=quarry"], (195, 195, 195)),
    area(["landuse=garages"], (224, 224, 206)),
    area(["landuse=commercial"], (238, 200, 200)),
    area(["landuse=cemetery"], (151, 191, 164)),
    area(["landuse=railway"], (222, 208, 213)),
    area(["landuse=industrial"], (222, 208, 213)),
    area(["landuse=retail"], (234, 214, 214)),
    area(["landuse=greenhouse_horticulture"], (231, 241, 222)),
    area(["landuse=plant_nursery", "landuse=allotments"], (204, 220, 112)),
    area(["highway=passing_place"], (157, 255, 108)),
    area(["tourism=zoo"], (164, 242, 161)),
    area(["natural=heath"], (213, 216, 159)),
    area(["natural=beach"], (254, 240, 186)),
    area(["landuse=landfill", "landuse=construction", "landuse=greenfield", "landuse=brownfield"], (176, 176, 142)),
    area(["leisure=playground"], (204, 254, 254), (180, 180, 180)),
    area(["leisure=common", "leisure=garden", "leisure=golf_course"], (199, 241, 163), (148, 214, 151)),
    line(["waterway=*"], (181, 208, 208), 10),
    road(["highway=residential", "highway=construction"], 12, (254, 254, 254), 10),
    road(["highway=unclassified"], 8, (254, 254, 254), 6),
    road(["highway=service"], 6, (254, 254, 254), 4),
    road(["highway=tertiary"], 12, (252, 250, 116), 10),
    road(["highway=tertiary_link"], 10, (252, 255, 136), 8),
    road(["highway=path", "highway=byway"], 4, (230, 230, 230), 2),
    road(["highway=pedestrian", "highway=living_street"], 4, (230, 230, 230), 2),
    area(["landuse=residential"], (220, 220, 220)),
    road(["highway=secondary"], 12, (253, 191, 111), 10),
    road(["highway=secondary_link"], 10, (253, 211, 121), 8),
    road(["highway=footway"], 4, (254, 200, 200), 2),
    road(["highway=bridleway"], 4, (200, 254, 200), 2),
    road(["highway=steps"], 4, (254, 100, 100), 2),
    road(["highway=cycleway"], 4, (200, 200, 254), 2),
    road(["highway=primary"], 12, (228, 109, 113), 10),
    road(["highway=primary_link"], 10, (228, 129, 133), 8),
    road(["highway=motorway", "highway=trunk"], 14, (255, 69, 0), 12),
    road(["highway=motorway_junction", "highway=motorway_link", "highway=trunk_link"], 10, (255, 89, 20), 8),
    road(["highway=track"], 4, (220, 220, 220), 2),
    {"tags": ["railway=tram"], "line": {"color": c(90, 90, 90), "width": 1, "cap": "round"},
     "bridge": {"casings": [{"color": c(0, 0, 0), "width": 3}, {"color": c(255, 255, 255), "width": 2}],
                "line": {"color": c(110, 110, 110), "width": 1, "cap": "round"}}},
    {"tags": ["railway=rail"], "line": {"color": c(70, 70, 70), "width": 2, "cap": "round"},
     "bridge": {"casings": [{"color": c(0, 0, 0), "width": 4}, {"color": c(255, 255, 255), "width": 3}],
                "line": {"color": c(90, 90, 90), "width": 2, "cap": "round"}}},
    area(["aeroway=terminal"], (204, 153, 254), (154, 117, 182)),
    area(["amenity=school"], (240, 240, 216), (210, 180, 160)),
    area(["amenity=place_of_worship"], (220, 130, 110), (150, 150, 150)),
    line(["aeroway=runway", "aeroway=taxiway"], (187, 187, 204), 10),
    area(["amenity=public_building"], (190, 173, 173), (169, 148, 165)),
    area(["historic=castle"], (173, 173, 173), (169, 148, 165)),
    area(["landuse=farmland"], (233, 216, 189)),
    area(["landuse=farmyard"], (220, 190, 146)),
    area(["landuse=meadow"], (207, 236, 168)),
    area(["landuse=orchard"], (207, 255, 168)),
    area(["natural=grassland"], (198, 228, 180)),
    area(["place=island"], (241, 238, 232)),
    line(["man_made=pier"], (241, 238, 232), 10),
    {"tags": ["natural=coastline"], "line": {"color": c(150, 150, 150), "width": 0, "cap": "flat"}},
]
known = {cl["tag"] for cl in classes}
for s in styles:
    for t in s["tags"]:
        assert t.endswith("=*") or t in known, t
# Overview levels for large-scale views (see docs/tasks/geo/osm-data-design.md). The detail
# file serves views finer than the first level; each level keeps only these features.
area_tags = ["natural=water", "waterway=riverbank", "landuse=reservoir", "landuse=basin", "landuse=forest", "natural=wood"]
overview = {"levels": [
    {"name": "regional", "fromMetersPerPixel": 20, "toleranceMeters": 5, "rules": [
        {"tags": ["railway=rail", "railway=light_rail", "railway=narrow_gauge", "railway=subway", "railway=preserved"], "types": ["way"]},
        {"tags": ["railway=station", "railway=halt"], "types": ["node"]},
        {"tags": ["highway=motorway", "highway=motorway_link", "highway=trunk", "highway=trunk_link", "highway=primary", "highway=primary_link",
                  "highway=secondary", "highway=secondary_link", "highway=tertiary"], "types": ["way"]},
        {"tags": ["waterway=river", "waterway=canal", "natural=coastline"], "types": ["way"]},
        {"tags": area_tags + ["landuse=residential", "landuse=industrial", "landuse=commercial", "landuse=retail", "landuse=railway", "aeroway=aerodrome"],
         "types": ["way", "relation"], "minAreaKm2": 0.05},
        {"tags": ["place=city", "place=town", "place=village"], "types": ["node"]}]},
    {"name": "national", "fromMetersPerPixel": 150, "toleranceMeters": 40, "rules": [
        {"tags": ["railway=rail", "railway=narrow_gauge"], "unless": ["service=*"], "types": ["way"]},
        {"tags": ["highway=motorway", "highway=trunk", "highway=primary"], "types": ["way"]},
        {"tags": ["waterway=river", "waterway=canal", "natural=coastline"], "types": ["way"]},
        {"tags": area_tags, "types": ["way", "relation"], "minAreaKm2": 1.0},
        {"tags": ["place=city", "place=town"], "types": ["node"]}]}]}
out = {
    "version": 1,
    "description": "OSM feature classes and tile-map styles. Generated from the legacy OSMFeatures table and MapDataOSM::draw(); widths in metres (the legacy pixel widths at 2 px per metre, width 0 = one pixel). The first style listing a class wins; an exact tag beats key=*.",
    "classification": {
        "skipKeyPrefixes": ["addr", "name", "oneway", "maxspeed", "surface", "amenity", "barrier", "wood", "sport"],
        "bridgeKeyPrefix": "bridge", "tunnelKeyPrefix": "tunnel",
        "buildingKeyPrefix": "building", "buildingClass": "building=yes"
    },
    "background": c(241, 238, 232),
    "default": {"line": {"color": c(50, 50, 50), "width": 0, "cap": "flat"}},
    "classes": classes,
    "styles": styles,
    "overview": overview,
}
def compact(v): return json.dumps(v, ensure_ascii=False, separators=(', ', ': '))
lines = ['{']
for k in ("version", "description", "classification", "background", "default"):
    lines.append('  %s: %s,' % (json.dumps(k), compact(out[k])))
lines.append('  "classes": [')
lines.append(',\n'.join('    ' + compact(cl) for cl in classes))
lines.append('  ],')
lines.append('  "styles": [')
lines.append(',\n'.join('    ' + compact(st) for st in styles))
lines.append('  ],')
lines.append('  "overview": {"levels": [')
lines.append(',\n'.join('    ' + compact(lv) for lv in overview["levels"]))
lines.append('  ]}')
lines.append('}')
open(sys.argv[2], 'w').write('\n'.join(lines) + '\n')
json.load(open(sys.argv[2]))
print(len(classes), "classes,", len(styles), "styles")
