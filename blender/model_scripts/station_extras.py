"""Interior touches tied to what each room is for: the bar's jukebox and neon, the chapel's lectern, candles and
stained glass, bedside monitors in the wards, turbines and control walls in the engine hall, forklifts in cargo.

Runs after station_fill's room lists: build_station calls add_extras(builder, layout) once per station.
"""

import station_fill as sf
import station_layout as sl

fill = sf.fill
accents = sf.accents
wall_decor = sf.wall_decor
spaced = sf.spaced
far_point = sf.far_point
across = sf.across

CLOCK_STEP = 36
CCTV_STEP = 24
POSTER_STEP = 18
POSTER_VARIANTS = ("blue", "red", "green")
NEON_COLOURS = ("pink", "cyan", "amber")
COUNTER_Z = 0.95
TABLE_Z = 0.77
CORNER_INSET = 1.0
WARNING_STEP = 5.0

GENERIC_SKIP_CLOCK = ("airlock_prep", "cabin", "head_cabin", "airlock", "corridor", "dock")


def posters(s, layout, room, edge, step, offset=0.0):
    low, high = sf.edge_range(room, edge)
    along, index = low + step / 2 + offset, 0
    while along < high - 1:
        wall_decor(s, layout, room, edge, f"decor_poster_{POSTER_VARIANTS[index % len(POSTER_VARIANTS)]}", along, 0.0)
        along += step
        index += 1


def corner_plants(s, room, count=2):
    spots = ((room.x0 + CORNER_INSET, room.y0 + CORNER_INSET), (room.x1 - CORNER_INSET, room.y0 + CORNER_INSET),
             (room.x0 + CORNER_INSET, room.y1 - CORNER_INSET), (room.x1 - CORNER_INSET, room.y1 - CORNER_INSET))
    for x, y in spots[:count]:
        s.stand("decor_plant_pot", x, y)


def corner_lamps(s, room):
    for x in (room.x0 + CORNER_INSET, room.x1 - CORNER_INSET):
        s.stand("decor_floor_lamp", x, room.y0 + CORNER_INSET)


def warnings(s, layout, room, edges="NSEW", template="decor_warning_sign", step=WARNING_STEP, offset=0.0):
    accents(s, layout, room, edges, template, step, 0.0, offset=offset)


def counter_tops(s, layout, room, edge, template, step, inset=0.3, offset=0.0):
    """Items sitting on the counters that run along a wall (counters of `step` metres, set in by `inset`)."""
    low, high = sf.edge_range(room, edge)
    along = low + step / 2 + offset
    while along < high - 1:
        wall_decor(s, layout, room, edge, template, along, COUNTER_Z, inset=inset)
        along += step


# ----- Generic touches -----


def common(s, layout, room):
    far = sl.far_edge(layout, room)
    if room.corridor:
        long = sl.long_edges(room)
        accents(s, layout, room, long[:1], "decor_clock", CLOCK_STEP, 0.0, offset=9)
        accents(s, layout, room, long[1:], "decor_cctv", CCTV_STEP, 0.0, offset=12)
        posters(s, layout, room, long[1:], POSTER_STEP, offset=3.0)
        return
    if room.kind not in GENERIC_SKIP_CLOCK and not room.kind.startswith("old_"):
        low, high = sf.edge_range(room, far)
        wall_decor(s, layout, room, far, "decor_clock", (low + high) / 2 + 1.5, 0.0)


# ----- Command -----


def bridge(s, layout, room):
    for x in (room.cx - 7, room.cx + 7):
        wall_decor(s, layout, room, "S", "decor_flag", x, 0.0)


def captain(s, layout, room):
    far = sl.far_edge(layout, room)
    for offset in (-2.2, 2.2):
        low, high = sf.edge_range(room, far)
        wall_decor(s, layout, room, far, "decor_flag", (low + high) / 2 + offset, 0.0)
    corner_plants(s, room)
    corner_lamps(s, room)
    s.stand("decor_rug", room.cx, room.cy)


def hop(s, layout, room):
    posters(s, layout, room, "W", 5.0, offset=3.0)
    corner_plants(s, room)


def meeting(s, layout, room):
    far = sl.far_edge(layout, room)
    low, high = sf.edge_range(room, far)
    wall_decor(s, layout, room, far, "decor_viewscreen", (low + high) / 2, 0.0)
    corner_plants(s, room, 2)


def vault(s, layout, room):
    far = sl.far_edge(layout, room)
    low, high = sf.edge_range(room, far)
    wall_decor(s, layout, room, far, "decor_vault_door", (low + high) / 2, 0.0)
    warnings(s, layout, room, "EW", step=6.0, offset=2.0)


def ai_core(s, layout, room):
    warnings(s, layout, room, "EW", step=6.0)


def head_cabin(s, layout, room):
    far = sl.far_edge(layout, room)
    s.stand("decor_rug", room.cx, room.cy)
    wall_decor(s, layout, room, far, "decor_poster_blue", room.cy + 2.2, 0.0)
    corner_lamps(s, room)


# ----- Service and public rooms -----


def kitchen(s, layout, room):
    accents(s, layout, room, "E", "decor_pot_rack", 2.6, 0.0, offset=3.0)
    for x in spaced(room.x0, room.x1, 3.6, 4.5):
        for y in spaced(room.y0, room.y1, 3.6, 4.5):
            if layout.point_free(room, x, y):
                s.stand("decor_microwave", x, y, z=TABLE_Z + 0.17)


def dining(s, layout, room):
    far = sl.far_edge(layout, room)
    low, high = sf.edge_range(room, far)
    for offset in (-4.0, 4.0):
        wall_decor(s, layout, room, far, "decor_menu_board", (low + high) / 2 + offset, 0.0)
    counter_tops(s, layout, room, "N", "decor_microwave", 4.2, offset=2.1)
    posters(s, layout, room, "E", 6.0, offset=2.0)


def bar(s, layout, room):
    accents(s, layout, room, "E", "decor_jukebox", 6.0, 0.0, offset=4.0)
    for i, colour in enumerate(NEON_COLOURS):
        wall_decor(s, layout, room, "S", f"decor_neon_{colour}", room.x0 + 2.5 + i * 3.4, 0.0)
    accents(s, layout, room, "W", "decor_dartboard", 6.0, 0.0, offset=6.0)
    corner_lamps(s, room)


def library(s, layout, room):
    accents(s, layout, room, "S", "decor_card_catalog", 2.4, 0.0, offset=1.0)
    for dx in (-2.0, 2.0):
        s.stand("decor_globe", room.cx + dx, room.cy)
    corner_lamps(s, room)


def chapel(s, layout, room):
    far = sl.far_edge(layout, room)
    x, y = far_point(room, far, 3.8)
    s.stand("decor_lectern", x, y, sl.FACING_OF_EDGE[far])
    for offset in (-2.4, -4.0, 2.4, 4.0):
        dx, dy = across(room, far, offset)
        px, py = far_point(room, far, 2.0)
        s.stand("decor_candle_stand", px + dx, py + dy)
    low, high = sf.edge_range(room, far)
    for offset in (-3.0, 0.0, 3.0):
        wall_decor(s, layout, room, far, "decor_stained_glass", (low + high) / 2 + offset, 0.0)


def theatre(s, layout, room):
    far = sl.far_edge(layout, room)
    x, y = far_point(room, far, 4.6)
    dx, dy = across(room, far, 3.2)
    s.stand("decor_synth", x + dx, y + dy, sl.FACING_OF_EDGE[far])
    corner_lamps(s, room)
    door_edge = layout.door_edges(room)[0]
    posters(s, layout, room, door_edge, 4.0, offset=1.0)


def gym(s, layout, room):
    accents(s, layout, room, "S", "decor_dumbbell_rack", 2.0, 0.0, offset=0.5)
    far = sl.far_edge(layout, room)
    low, high = sf.edge_range(room, far)
    for offset in (-3.5, -1.2, 1.2, 3.5):
        wall_decor(s, layout, room, far, "decor_mirror", (low + high) / 2 + offset, 0.0)
    fill(s, layout, room, "decor_gym_mat", 4.5, 4.5, margin=3.5, skip=lambda i, j: (i + j) % 2)


def lounge(s, layout, room):
    accents(s, layout, room, "E", "decor_tv", 6.0, 0.0, offset=3.0, inset=0.3)
    s.stand("decor_pool_table", room.cx, room.y0 + 4.0)
    for x in spaced(room.x0, room.x1, 4.5, 3.5):
        for y in spaced(room.y0 + 8.0, room.y1, 4.5, 3.5):
            if layout.point_free(room, x, y):
                s.stand("decor_coffee_table", x, y - 1.3)
    corner_lamps(s, room)
    posters(s, layout, room, "N", 3.0, offset=1.0)


def cabin(s, layout, room):
    far = sl.far_edge(layout, room)
    wall_decor(s, layout, room, far, "decor_poster_" + POSTER_VARIANTS[room.index % 3], room.cx + 0.2, 0.0)


def restrooms(s, layout, room):
    accents(s, layout, room, "S", "decor_mirror", 2.0, 0.0, offset=0.5)


def janitor(s, layout, room):
    for x in spaced(room.x0, room.x1, 2.0, 2.0):
        s.stand("decor_wet_floor_sign", x, room.cy)


def hydroponics(s, layout, room):
    for x in spaced(room.x0, room.x1, 3.2, 3.0):
        for y in spaced(room.y0, room.y1, 3.2, 3.0):
            if layout.point_free(room, x, y):
                s.stand("decor_grow_lamp", x, y)
    accents(s, layout, room, "W", "decor_seed_rack", 2.6, 0.0, offset=4.0)


# ----- Security -----


def security(s, layout, room):
    far = sl.far_edge(layout, room)
    low, high = sf.edge_range(room, far)
    wall_decor(s, layout, room, far, "decor_cctv", (low + high) / 2, 0.0)


def brig(s, layout, room):
    for edge in "EW":
        wall_decor(s, layout, room, edge, "decor_cctv", room.cy, 0.0)


# ----- Medical and science -----


def wards(s, layout, room):
    for x in spaced(room.x0, room.x1, 3.2, 2.5):
        for y in spaced(room.y0, room.y1, 4.0, 2.5):
            if layout.point_free(room, x, y):
                s.stand("decor_heart_monitor", x - 0.65, y - 0.5)
    for x in spaced(room.x0, room.x1, 4.0, 4.0):
        s.stand("decor_wheelchair", x, room.y0 + 1.5)
    for x in (room.cx - 3.0, room.cx + 3.0):
        s.stand("decor_defib_cart", x, room.cy)


def surgery(s, layout, room):
    for x in (room.cx - 3, room.cx + 3):
        s.stand("decor_heart_monitor", x - 0.8, room.cy + 0.9)
    s.stand("decor_defib_cart", room.cx, room.y0 + 2.0)
    s.stand("decor_xray", room.cx, room.y1 - 3.0, "S")


def chemistry(s, layout, room):
    accents(s, layout, room, "E", "decor_fume_hood", 3.2, 0.0, offset=8.0)
    for x in spaced(room.x0, room.x1, 3.6, 3.5):
        for y in spaced(room.y0, room.y1, 3.6, 4.5):
            if layout.point_free(room, x, y):
                s.stand("decor_centrifuge", x + 0.5, y, z=TABLE_Z)


def virology(s, layout, room):
    counter_tops(s, layout, room, "S", "decor_microscope", 2.1)
    warnings(s, layout, room, "EW", "decor_biohazard_sign", 4.0, offset=1.0)


def morgue(s, layout, room):
    warnings(s, layout, room, "EW", "decor_biohazard_sign", 5.0, offset=1.0)
    for x in (room.cx - 3.0, room.cx + 3.0):
        s.stand("decor_autopsy_table", x, room.cy - 2.5, "E")


def genetics(s, layout, room):
    counter_tops(s, layout, room, "E", "decor_microscope", 2.1)


def garage(s, layout, room):
    fill(s, layout, room, "decor_wheelchair", 4.0, 5.0, margin=3.5, skip=lambda i, j: (i + j) % 2 == 0)


def labs(s, layout, room):
    for y in spaced(room.y0, room.y1, 4.5, 3.0):
        for x in spaced(room.x0, room.x1, 4.0, 3.0):
            s.stand("decor_microscope", x, y, z=COUNTER_Z)
    warnings(s, layout, room, "EW", "decor_warning_sign", 6.0, offset=1.0)


def robotics(s, layout, room):
    far = sl.far_edge(layout, room)
    for offset in (-3.0, 3.0):
        x, y = far_point(room, far, 2.0)
        dx, dy = across(room, far, offset)
        s.stand("decor_workbench", x + dx, y + dy, "S")
    warnings(s, layout, room, "EW", "decor_warning_sign", 6.0, offset=1.0)


def xenobio(s, layout, room):
    warnings(s, layout, room, "EW", "decor_biohazard_sign", 5.0, offset=1.0)


def toxins(s, layout, room):
    accents(s, layout, room, "S", "decor_fume_hood", 3.2, 0.0, offset=1.0)
    warnings(s, layout, room, "EW", "decor_warning_sign", 5.0, offset=1.0)


def toxins_test(s, layout, room):
    warnings(s, layout, room, "NS", "decor_warning_sign", 4.0, offset=1.0)


def server(s, layout, room):
    warnings(s, layout, room, "EW", "decor_warning_sign", 6.0, offset=1.0)


# ----- Engineering and cargo -----


def engine(s, layout, room):
    for dx in (-9.0, 9.0):
        s.stand("decor_turbine", room.cx + dx, room.cy, "S")
    accents(s, layout, room, "EW", "decor_control_wall", 10.0, 0.0)
    warnings(s, layout, room, "N", "decor_warning_sign", 7.0, offset=2.0)


def power(s, layout, room):
    warnings(s, layout, room, "NS", "decor_warning_sign", 4.0, offset=1.0)
    accents(s, layout, room, "EW", "decor_control_wall", 10.0, 0.0)


def atmos(s, layout, room):
    accents(s, layout, room, "NS", "decor_valve_wheel", 4.0, 0.0, offset=2.0)
    warnings(s, layout, room, "EW", "decor_warning_sign", 6.0, offset=1.0)


def tools(s, layout, room):
    for y in spaced(room.y0, room.y1, 5.0, 3.0):
        s.stand("decor_workbench", room.cx, y, "S")


def ore_processing(s, layout, room):
    warnings(s, layout, room, "EW", "decor_warning_sign", 5.0, offset=1.0)


def mining(s, layout, room):
    warnings(s, layout, room, "NS", "decor_warning_sign", 6.0, offset=1.0)


def cargo_bay(s, layout, room):
    for y in (room.y0 + 4.0, room.y1 - 4.0):
        s.stand("decor_forklift", room.x1 - 2.5, y, "W")
    for y in (room.cy - 2.0, room.cy + 2.0):
        s.stand("decor_scale", room.x1 - 5.0, y)


def cargo_receiving(s, layout, room):
    s.stand("decor_forklift", room.x0 + 2.5, room.cy, "E")
    for y in (room.y0 + 4.0, room.y1 - 4.0):
        s.stand("decor_scale", room.cx, y)


def cargo_dock(s, layout, room):
    for y in (room.y0 + 4.0, room.y1 - 4.0):
        s.stand("decor_forklift", room.x0 + 2.5, y, "E")


def mail(s, layout, room):
    s.stand("decor_scale", room.cx, room.cy)


def dock(s, layout, room):
    if room.name == "cargo_dock":
        cargo_dock(s, layout, room)


EXTRAS = {
    "bridge": bridge, "captain": captain, "hop": hop, "meeting": meeting, "vault": vault, "ai_core": ai_core,
    "head_cabin": head_cabin, "kitchen": kitchen, "dining": dining, "bar": bar, "library": library,
    "chapel": chapel, "theatre": theatre, "gym": gym, "lounge": lounge, "cabin": cabin,
    "restrooms": restrooms, "janitor": janitor, "hydroponics": hydroponics, "checkpoint": security,
    "armory": security, "warden": security, "brig": brig, "wards": wards, "surgery": surgery,
    "chemistry": chemistry, "virology": virology, "morgue": morgue, "genetics": genetics, "garage": garage,
    "labs": labs, "robotics": robotics, "xenobio": xenobio, "toxins": toxins, "toxins_test": toxins_test,
    "server": server, "engine": engine, "power": power, "atmos": atmos, "tools": tools,
    "ore_processing": ore_processing, "mining": mining, "cargo_bay": cargo_bay,
    "cargo_receiving": cargo_receiving, "mail": mail, "dock": dock,
}


def add_extras(s, layout):
    for room in layout.rooms:
        common(s, layout, room)
        extra = EXTRAS.get(room.kind)
        if extra is not None:
            extra(s, layout, room)
