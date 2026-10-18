"""What each department of the station holds: its characteristic machines and furniture, and the
signs that name every room. It overrides station_layout's generic decor with room-specific lists.

Imported by station_layout.build_station(); the lists per room are in ROOM_FILL below, each
room function says what the room is for in terms of the items placed.
"""

import math

import bpy

import station_layout as sl
import station_models as sm
import station_scenes as ss

DECOR = sl.DECOR
fill = sl.fill
accents = sl.accents
lamps = sl.lamps
spaced = sl.spaced
wall_decor = sl.wall_decor
desk_line = sl.desk_line
dining_set = sl.dining_set
corner_planters = sl.corner_planters
long_edges = sl.long_edges
edge_range = sl.edge_range

EXTINGUISHER_STEP = 20
WARDEN_DESK_FROM_WALL = 5.0
CAPTAIN_DESK_FROM_WALL = 5.0
DOOR_DESK_SHIFT = 3.2
DOOR_DESK_DEPTH = 1.6
COURT_CLEAR = 6.0
COURT_TABLE_DEPTH = 6.5
CHAPEL_CLEAR = 6.0
THEATRE_CLEAR = 6.5
PASTOR_BEHIND = 1.1
HOP_DESK_FROM_WALL = 7.0
VISITOR_SPREAD = 0.7
VISITOR_GAP = 1.2
SIGN_Z_DOOR = 2.72
SIGN_Z_LOW = 2.45
SIGN_Z_HIGH = 3.7
SIGN_OUT = 0.02
SIGN_DOOR_SIZE = 0.28
SIGN_DOOR_WIDTH = 1.8
SIGN_BANNER_SIZE = 0.8
SIGN_CHAR_WIDTH = 0.62
SIGN_MARGIN = 1.5
SIGN_COLOR = (0.03, 0.03, 0.05, 1.0)
SIGN_EXTRUDE = 0.01

TITLES = {
    "bridge": "МОСТИК", "captain": "КАЮТА КАПИТАНА", "hop": "ГЛАВА ПЕРСОНАЛА", "heads_hall": "КАЮТЫ ГЛАВ",
    "meeting": "КОНФЕРЕНЦ-ЗАЛ", "ai_core": "ЯДРО ИИ", "ai_upload": "ЗАГРУЗКА ИИ", "vault": "ХРАНИЛИЩЕ",
    "teleporter": "ТЕЛЕПОРТ", "eva": "СКАФАНДРЫ", "gateway": "ВРАТА", "theatre": "ТЕАТР",
    "checkpoint": "КПП", "brig": "БРИГ", "prison": "ТЮРЬМА", "interrogation": "ДОПРОСНАЯ",
    "armory": "ОРУЖЕЙНАЯ", "warden": "СМОТРИТЕЛЬ", "detective": "ДЕТЕКТИВ", "courtroom": "СУД",
    "library": "БИБЛИОТЕКА", "chapel": "ЧАСОВНЯ", "bar": "БАР", "kitchen": "КУХНЯ", "dining": "СТОЛОВАЯ",
    "hydroponics": "ОРАНЖЕРЕЯ", "surgery": "ОПЕРАЦИОННАЯ", "chemistry": "ХИМЛАБ", "virology": "ВИРУСОЛОГИЯ",
    "morgue": "МОРГ", "wards": "МЕДБЛОК", "cryo": "КРИОСОН", "genetics": "ГЕНЕТИКА", "garage": "СКОРАЯ",
    "lockers": "РАЗДЕВАЛКИ", "rd_office": "ГЛАВА НАУКИ", "robotics": "РОБОТОТЕХНИКА", "mech_bay": "МЕХ-БЭЙ",
    "server_room": "СЕРВЕРНАЯ", "xenobio": "КСЕНОБИОЛОГИЯ", "toxins": "ТОКСИНЫ", "toxins_test": "ИСПЫТАНИЯ",
    "labs": "ЛАБОРАТОРИИ", "janitor": "УБОРЩИК", "restrooms": "САНУЗЛЫ", "arrivals": "ПРИБЫТИЕ",
    "escape": "ЭВАКУАЦИЯ", "lounge": "ГОСТИНАЯ", "cargo_bay": "КАРГО", "cargo_receiving": "ПРИЁМКА ГРУЗА", "ore_processing": "ПЕРЕРАБОТКА РУДЫ",
    "cargo_dock": "ГРУЗОВОЙ ДОК", "quartermaster": "ИНТЕНДАНТ",
    "mail": "ПОЧТА", "mining": "ШАХТЁРЫ", "gym": "СПОРТЗАЛ", "atmos": "АТМОСФЕРА", "power": "ЭНЕРГОБЛОК",
    "engine": "ДВИГАТЕЛЬ", "telecom": "ТЕЛЕКОМ", "tools": "ИНСТРУМЕНТЫ", "ce_office": "ГЛАВНЫЙ ИНЖЕНЕР",
    "solars": "СОЛНЕЧНЫЕ ПАНЕЛИ", "disposal": "УТИЛИЗАЦИЯ", "atrium": "АТРИУМ",
    "hc_sec": "КАЮТА ГЛАВЫ ОХРАНЫ", "hc_med": "КАЮТА ГЛАВВРАЧА", "hc_eng": "КАЮТА ГЛ. ИНЖЕНЕРА",
    "hc_sci": "КАЮТА ГЛАВЫ НАУКИ", "hos_office": "ГЛАВА ОХРАНЫ", "cmo_office": "ГЛАВНЫЙ ВРАЧ",
    "hc_qm": "КАЮТА ИНТЕНДАНТА", "hc_hop": "КАЮТА ГЛ. ПЕРСОНАЛА", "hc_guest1": "ГОСТЕВАЯ 1", "hc_guest2": "ГОСТЕВАЯ 2",
    "old_store1": "СКЛАД 1", "old_store2": "СКЛАД 2", "old_workshop": "МАСТЕРСКАЯ", "old_canteen": "СТАРАЯ СТОЛОВАЯ",
    "old_kitchen": "СТАРАЯ КУХНЯ", "old_brig": "СТАРЫЙ БРИГ", "old_guard": "ПОСТ ОХРАНЫ", "old_medbay": "СТАРЫЙ МЕДБЛОК",
    "old_office": "КОНТОРА", "old_lab": "СТАРАЯ ЛАБОРАТОРИЯ", "old_server": "СЕРВЕРНАЯ 2", "old_lounge": "СТАРАЯ ГОСТИНАЯ",
    "temp_brig": "БРИГ (ВРЕМЕННЫЙ)", "barracks": "КАЗАРМА",
    "c1": "СЕВЕРНЫЙ КОРИДОР", "c2": "ЦЕНТРАЛЬНЫЙ КОРИДОР", "c3": "ЮЖНЫЙ КОРИДОР",
    "m_w": "ЗАПАДНЫЙ КОРИДОР", "m_e": "ВОСТОЧНЫЙ КОРИДОР", "dorm_hall": "ЖИЛОЙ БЛОК",
}


def lock_title(name):
    if name.endswith("_prep"):
        return "СКАФАНДРЫ"
    return "ТЕХ. ШЛЮЗ" if name.startswith(("lock_t", "lock_o")) else "ШЛЮЗ"


def cabin_title(name):
    side, number = name.removeprefix("cabin_")[0], int(name.removeprefix("cabin_")[1:]) + 1
    return f"КАЮТА {number}" if side == "n" else f"КАЮТА {number + 12}"


def title_of(room):
    if room.kind.startswith("old_") or room.name.startswith("old_"):
        return None
    if room.name in TITLES:
        return TITLES[room.name]
    if room.name.startswith("cabin_"):
        return cabin_title(room.name)
    if room.name.startswith("lock_"):
        return lock_title(room.name)
    return None


def extinguishers(s, layout, room):
    accents(s, layout, room, long_edges(room)[:1], "decor_extinguisher", EXTINGUISHER_STEP, 0.0, offset=4)


# ----- Command -----


def d_bridge(s, layout, room):
    sl_bridge(s, layout, room)
    s.stand("decor_map_table", room.cx, room.y0 + 11.5, "S")
    extinguishers(s, layout, room)


def d_captain(s, layout, room):
    sl_captain(s, layout, room)
    sl.desk(s, room.cx - 3.0, room.cy + 3.0, "N", chair="decor_captain_chair")
    wall_decor(s, layout, room, "N", "decor_safe", room.x1 - 3, 0.0, inset=0.5)


def d_hop(s, layout, room):
    sl_hop(s, layout, room)
    accents(s, layout, room, "E", "decor_pigeonholes", 2.2, 0.0, offset=14.0)
    desk_x, desk_y = room.cx, room.y1 - HOP_DESK_FROM_WALL
    sl.desk(s, desk_x, desk_y, "N", chair="decor_captain_chair")
    for dx in (-VISITOR_SPREAD, VISITOR_SPREAD):
        s.stand("decor_crew_chair", desk_x + dx, desk_y - VISITOR_GAP, "N")


def d_head_cabin(s, layout, room):
    far = sl.far_edge(layout, room)
    lamps(s, layout, room, "NS")
    bed_x = room.x0 + 1.4 if far == "W" else room.x1 - 1.4
    s.stand("decor_bed", bed_x, room.y1 - 1.6, "E")
    wall_decor(s, layout, room, far, "decor_locker", room.cy - 0.7, 0.0)
    wall_decor(s, layout, room, far, "decor_locker", room.cy + 0.7, 0.0)
    desk_line(s, layout, room, "N", 5.0, margin=2.5)
    wall_decor(s, layout, room, "S", "decor_shelf", room.cx, 0.0)
    wall_decor(s, layout, room, "S", "decor_planter", room.cx + 2.6, 0.0, inset=0.35)


def d_ai_core(s, layout, room):
    lamps(s, layout, room, "NS")
    s.stand("decor_ai_core", room.cx, room.cy)
    fill(s, layout, room, "decor_mainframe", 3.2, 3.2, margin=2.5, skip=lambda i, j: i == 1)
    desk_line(s, layout, room, "S", 4.0, margin=2.0)


def d_vault(s, layout, room):
    lamps(s, layout, room, "NS")
    fill(s, layout, room, "decor_safe", 2.6, 2.6, margin=2.0)
    fill(s, layout, room, "decor_crate", 2.6, 2.6, margin=3.3, skip=lambda i, j: (i + j) % 2)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)


def d_pad_room(s, layout, room):
    lamps(s, layout, room)
    if room.name == "gateway":
        s.stand("decor_gate_ring", room.cx, room.cy, "S")
    else:
        s.stand("decor_pad", room.cx, room.cy)
        s.stand("decor_pad", room.cx - 3, room.cy + 5)
        s.stand("decor_pad", room.cx + 3, room.cy + 5)
    desk_line(s, layout, room, "N", 4.0, margin=3.0)
    accents(s, layout, room, "EW", "decor_locker", 3.0, 0.0)
    accents(s, layout, room, "E", "decor_switch_panel", 6, 1.3, offset=1.5)


def d_eva(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "EW", "decor_locker", 1.3, 0.0)
    fill(s, layout, room, "decor_suit", 2.0, 5.0, margin=3.5)
    fill(s, layout, room, "decor_bench", 6.0, 5.0, margin=5.0, skip=lambda i, j: j == 0)


# ----- Security -----


def d_checkpoint(s, layout, room):
    lamps(s, layout, room, "NS")
    fill(s, layout, room, "decor_gate", 4.0, 20.0, margin=2.0, skip=lambda i, j: False)
    accents(s, layout, room, "S", "decor_counter", 2.3, 0.0, inset=0.3)
    desk_line(s, layout, room, "N", 3.3, margin=2.0)
    accents(s, layout, room, "EW", "decor_weapon_rack", 2.3, 0.0, offset=3.0)
    accents(s, layout, room, "EW", "decor_locker", 1.8, 0.0, offset=8.0)


def d_armory(s, layout, room):
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "EW", "decor_weapon_rack", 2.2, 0.0)
    accents(s, layout, room, "N", "decor_locker", 1.3, 0.0)
    fill(s, layout, room, "decor_table", 3.0, 4.0, margin=3.0)
    fill(s, layout, room, "decor_crate", 3.0, 4.0, margin=3.0, skip=lambda i, j: j % 2 == 0)


def d_warden(s, layout, room):
    lamps(s, layout, room, "NS")
    for i, x in enumerate(spaced(room.x0, room.x1, 1.6, 0.8)):
        for z in (1.2, 1.9):
            wall_decor(s, layout, room, "S", "decor_monitor_crt_wall", x, z)
    desk_line(s, layout, room, "N", 3.0, margin=1.5)
    accents(s, layout, room, "E", "decor_weapon_rack", 2.2, 0.0, offset=6.0)
    s.stand("decor_sofa", room.cx, room.cy - 3, "S")


# ----- Service -----


def d_kitchen(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "W", "decor_stove", 2.4, 0.0)
    accents(s, layout, room, "N", "decor_stove", 2.4, 0.0)
    accents(s, layout, room, "S", "decor_fridge", 1.6, 0.0)
    accents(s, layout, room, "E", "decor_sink", 2.6, 0.0, offset=3.0)
    accents(s, layout, room, "E", "decor_counter", 2.1, 0.0, offset=7.0, inset=0.3)
    fill(s, layout, room, "decor_prep_table", 3.6, 3.6, margin=4.5)
    extinguishers(s, layout, room)


def d_bar(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "N", "decor_bottle_shelf", 2.1, 0.0)
    for x in spaced(room.x0, room.x1, 2.2, 3.0):
        s.stand("decor_counter", x, room.y1 - 5.5, "S")
        s.stand("decor_bar_stool", x, room.y1 - 6.7)
    for y in spaced(room.y1 - 5.0, room.y1 - 1.0, 0.8, 0.5):
        s.stand("decor_keg", room.x0 + 1.0, y)
    accents(s, layout, room, "E", "decor_arcade", 3.0, 0.0, offset=6.0)
    for x in spaced(room.x0, room.x1, 4.5, 3.0):
        for y in spaced(room.y0, room.y1 - 8.0, 3.8, 3.0):
            if layout.point_free(room, x, y):
                s.stand("decor_table", x, y)
                s.stand("decor_bar_stool", x - 0.5, y - 0.8)
                s.stand("decor_bar_stool", x + 0.5, y - 0.8)
                s.stand("decor_bar_stool", x - 0.5, y + 0.8)
                s.stand("decor_bar_stool", x + 0.5, y + 0.8)
    accents(s, layout, room, "E", "decor_vending", 4.0, 0.0, offset=2.0)


def d_dining(s, layout, room):
    lamps(s, layout, room)
    dining_set(s, layout, room, 4.2, 3.4, margin=3.0)
    accents(s, layout, room, "N", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "N", "decor_fridge", 4.2, 0.0, offset=2.1)
    accents(s, layout, room, "S", "decor_vending", 4.0, 0.0)
    corner_planters(s, layout, room)
    extinguishers(s, layout, room)


def d_hydroponics(s, layout, room):
    lamps(s, layout, room, "NS")
    fill(s, layout, room, "decor_planter", 3.2, 3.2, margin=3.0)
    accents(s, layout, room, "N", "decor_sink", 2.6, 0.0)
    accents(s, layout, room, "EW", "decor_water_tank", 8.0, 0.0, offset=0.5, inset=0.6)
    accents(s, layout, room, "S", "decor_vending", 3.0, 0.0)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0, offset=3.0)


def d_library(s, layout, room):
    sl_library(s, layout, room)
    desk_line(s, layout, room, "S", 4.0, margin=3.0)


def d_chapel(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_sofa", 3.2, 2.2, margin=2.2, facing="N", margin_y=3.0, skip=lambda i, j: j >= 6)
    s.stand("decor_altar", room.cx, room.y1 - 2.5, "S")
    corner_planters(s, layout, room)
    accents(s, layout, room, "EW", "decor_shelf", 2.2, 0.0, offset=11.0)


def d_theatre(s, layout, room):
    lamps(s, layout, room)
    s.stand("decor_stage", room.cx, room.y1 - 2.0, "S")
    fill(s, layout, room, "decor_sofa", 2.6, 2.4, margin=2.5, facing="N", margin_y=2.5, skip=lambda i, j: j >= 6)
    corner_planters(s, layout, room)


def d_courtroom(s, layout, room):
    lamps(s, layout, room, "EW")
    s.stand("decor_judge_bench", room.cx, room.y1 - 3.0, "S")
    fill(s, layout, room, "decor_sofa", 6.0, 2.4, margin=3.0, facing="N", margin_y=3.0, skip=lambda i, j: j > 4)
    for x in (room.cx - 3, room.cx + 3):
        s.stand("decor_table", x, room.y1 - 8.0, "S")
        s.stand("decor_crew_chair", x, room.y1 - 9.0, "N")
    desk_line(s, layout, room, "S", 4.0, margin=2.0)


# ----- Medical -----


def d_wards(s, layout, room):
    lamps(s, layout, room)
    for i, x in enumerate(spaced(room.x0, room.x1, 3.2, 2.5)):
        for y in spaced(room.y0, room.y1, 4.0, 2.5):
            if layout.point_free(room, x, y):
                s.stand("decor_bed", x, y)
                s.stand("decor_iv_stand", x + 0.65, y + 0.5)
    desk_line(s, layout, room, "N", 4.0, margin=3.0)
    accents(s, layout, room, "W", "decor_med_cabinet", 2.2, 1.0)
    accents(s, layout, room, "E", "decor_locker", 2.0, 0.0)
    wall_decor(s, layout, room, "S", "decor_med_cabinet", room.cx, 1.0)


def d_cryo(s, layout, room):
    sl_cryo(s, layout, room)
    accents(s, layout, room, "EW", "decor_med_cabinet", 4.0, 1.0, offset=1.0)
    s.stand("decor_scanner", room.cx, room.y0 + 3.0, "S")


def d_genetics(s, layout, room):
    lamps(s, layout, room)
    desk_line(s, layout, room, "N", 3.2, margin=2.0)
    fill(s, layout, room, "decor_dna_scanner", 4.0, 5.0, margin=3.5, skip=lambda i, j: j == 0)
    accents(s, layout, room, "E", "decor_counter", 2.1, 0.0, inset=0.3)
    for y in spaced(room.y0, room.y1, 2.1, 3.0):
        wall_decor(s, layout, room, "E", "decor_analyzer", y, 0.95, inset=0.3)
    accents(s, layout, room, "W", "decor_locker", 2.0, 0.0)


def d_garage(s, layout, room):
    lamps(s, layout, room, "EW")
    fill(s, layout, room, "decor_bed", 4.0, 5.0, margin=3.0)
    fill(s, layout, room, "decor_cart", 4.0, 5.0, margin=4.0, skip=lambda i, j: (i + j) % 2 == 0)
    accents(s, layout, room, "EW", "decor_med_cabinet", 4.0, 1.0)
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0, offset=2.0)


def d_surgery(s, layout, room):
    lamps(s, layout, room)
    for x in (room.cx - 3, room.cx + 3):
        s.stand("decor_bed", x, room.cy)
        s.stand("decor_surgery_lamp", x, room.cy)
        s.stand("decor_instrument_tray", x + 0.9, room.cy + 0.5)
    accents(s, layout, room, "N", "decor_sink", 2.6, 0.0)
    accents(s, layout, room, "S", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "EW", "decor_med_cabinet", 4.0, 1.0)
    desk_line(s, layout, room, "S", 6.0, margin=3.0, depth=1.3)


def d_chemistry(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "N", "decor_chem_dispenser", 2.1, 0.0)
    accents(s, layout, room, "S", "decor_bottle_shelf", 2.1, 0.0)
    accents(s, layout, room, "E", "decor_sink", 2.6, 0.0, offset=4.0)
    accents(s, layout, room, "W", "decor_counter", 2.1, 0.0, offset=4.0, inset=0.3)
    for x in spaced(room.x0, room.x1, 3.6, 3.5):
        for y in spaced(room.y0, room.y1, 3.6, 4.5):
            if layout.point_free(room, x, y):
                s.stand("decor_table", x, y)
                s.stand("decor_analyzer", x, y, z=0.77)


def d_virology(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_bed", 4.0, 5.0, margin=3.0)
    accents(s, layout, room, "N", "decor_incubator", 1.6, 0.0)
    accents(s, layout, room, "S", "decor_counter", 2.1, 0.0, inset=0.3)
    accents(s, layout, room, "EW", "decor_med_cabinet", 4.0, 1.0)
    desk_line(s, layout, room, "E", 5.0, margin=3.0)


def d_morgue(s, layout, room):
    sl_morgue(s, layout, room)
    accents(s, layout, room, "S", "decor_med_cabinet", 4.0, 1.0)
    s.stand("decor_instrument_tray", room.cx, room.cy)


# ----- Science -----


def d_labs(s, layout, room):
    lamps(s, layout, room)
    for y in spaced(room.y0, room.y1, 4.5, 3.0):
        for x in spaced(room.x0, room.x1, 4.0, 3.0):
            s.stand("decor_counter", x, y)
            s.stand("decor_monitor_crt", x - 0.4, y, z=0.95)
            s.stand("decor_analyzer", x + 0.4, y, z=0.95)
    accents(s, layout, room, "N", "decor_chem_dispenser", 3.0, 0.0, offset=1.0)
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)


def d_robotics(s, layout, room):
    lamps(s, layout, room)
    for y in spaced(room.y0, room.y1, 5.0, 4.0):
        for x in spaced(room.x0, room.x1, 4.0, 3.0):
            s.stand("decor_counter", x, y)
            s.stand("decor_robot_arm", x + 0.4, y, z=0.95)
            s.stand("decor_monitor_crt", x - 0.5, y, z=0.95)
    accents(s, layout, room, "N", "decor_tool_wall", 2.1, 0.0)
    s.stand("decor_mech", room.cx - 4, room.y0 + 2.5, "N")
    s.stand("decor_mech", room.cx + 4, room.y0 + 2.5, "N")


def d_mech_bay(s, layout, room):
    lamps(s, layout, room)
    for x in spaced(room.x0, room.x1, 4.0, 3.5):
        for y in spaced(room.y0, room.y1, 5.0, 4.0):
            if layout.point_free(room, x, y):
                s.stand("decor_pad", x, y)
                s.stand("decor_mech", x, y, z=0.1)
    accents(s, layout, room, "N", "decor_tool_wall", 2.1, 0.0)
    accents(s, layout, room, "S", "decor_counter", 2.1, 0.0, inset=0.3)


def d_xenobio(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_pen", 3.0, 3.0, margin=2.5)
    accents(s, layout, room, "N", "decor_sink", 2.6, 0.0)
    accents(s, layout, room, "S", "decor_counter", 2.1, 0.0, inset=0.3)
    desk_line(s, layout, room, "E", 5.0, margin=3.0)


def d_toxins(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_gas_tank_yellow", 1.8, 2.0, margin=2.5, skip=lambda i, j: j > 0)
    fill(s, layout, room, "decor_gas_tank_blue", 1.8, 2.0, margin=2.5, skip=lambda i, j: j != 1)
    fill(s, layout, room, "decor_gas_tank_red", 1.8, 2.0, margin=2.5, skip=lambda i, j: j != 2)
    accents(s, layout, room, "NS", "decor_counter", 2.1, 0.0, inset=0.3)
    for x in spaced(room.x0, room.x1, 4.2, 2.5):
        wall_decor(s, layout, room, "N", "decor_analyzer", x, 0.95, inset=0.3)
    desk_line(s, layout, room, "E", 4.0, margin=3.0)


def d_toxins_test(s, layout, room):
    lamps(s, layout, room, "NS")
    for x in (room.x0 + 3, room.x1 - 3):
        s.pillar(x, room.cy, room.height)
    fill(s, layout, room, "decor_crate", 3.0, 3.0, margin=4.5)
    fill(s, layout, room, "decor_gas_tank_red", 3.0, 3.0, margin=3.0, skip=lambda i, j: (i + j) % 2 == 0)


# ----- Engineering -----


def d_engine(s, layout, room):
    lamps(s, layout, room)
    s.stand("decor_reactor", room.cx, room.cy)
    for x in (room.x0 + 3, room.x1 - 3):
        for y in (room.y0 + 3, room.y1 - 3):
            s.pillar(x, y, room.height)
    for x in spaced(room.x0, room.x1, 5.0, 8.0):
        s.stand("decor_generator", x, room.y0 + 2.0, "S")
    for x in spaced(room.x0, room.x1, 2.0, 8.0):
        s.stand("decor_smes", x, room.y1 - 2.0, "S")
    sl.station_row(s, layout, room, 6, "S", margin=8.0)
    accents(s, layout, room, "N", "decor_pipe_panel", 6, 1.8)
    accents(s, layout, room, "EW", "decor_pipe_panel", 5, 1.8)
    accents(s, layout, room, "EW", "decor_tool_wall", 6.0, 0.0, offset=3.0)
    for x in spaced(room.x0, room.x1, 3.0, 6.0):
        if abs(x - room.cx) > 6:
            s.stand("decor_gas_tank_yellow", x, room.y1 - 5.0)
    extinguishers(s, layout, room)


def d_power(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_smes", 1.8, 3.0, margin=2.5, skip=lambda i, j: j == 0)
    for x in spaced(room.x0, room.x1, 4.0, 3.0):
        s.stand("decor_generator", x, room.y0 + 2.0, "S")
    desk_line(s, layout, room, "N", 5.0, margin=3.0)
    accents(s, layout, room, "EW", "decor_pipe_panel", 5, 1.6)
    extinguishers(s, layout, room)


def d_atmos(s, layout, room):
    lamps(s, layout, room)
    for i, color in enumerate(("blue", "red", "yellow")):
        fill(s, layout, room, f"decor_gas_tank_{color}", 1.4, 6.0, margin=2.0, skip=lambda a, j, i=i: j != i)
    accents(s, layout, room, "NS", "decor_pump", 2.5, 0.0, offset=1.0)
    accents(s, layout, room, "NS", "decor_pipe_panel", 2.5, 1.9, offset=1.0)
    accents(s, layout, room, "EW", "decor_wall_vent_grille", 5, 1.8)
    desk_line(s, layout, room, "S", 6.0, margin=3.0)


def d_telecom(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_mainframe", 2.8, 3.0, margin=2.5)
    s.stand("decor_antenna", room.cx, room.cy)
    desk_line(s, layout, room, "N", 4.0, margin=2.0)


def d_tools(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "NS", "decor_tool_wall", 2.1, 0.0)
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)
    fill(s, layout, room, "decor_crate", 3.5, 3.5, margin=3.5)
    s.stand("decor_generator", room.cx, room.cy, "S")
    s.stand("decor_cart", room.cx + 3, room.cy + 3, "S")


def d_solars(s, layout, room):
    lamps(s, layout, room, "NS")
    desk_line(s, layout, room, "N", 3.5, margin=2.0)
    fill(s, layout, room, "decor_smes", 2.0, 4.0, margin=2.0, skip=lambda i, j: j != 1)
    accents(s, layout, room, "EW", "decor_pipe_panel", 4, 1.6)


def d_disposal(s, layout, room):
    lamps(s, layout, room, "NS")
    for y in spaced(room.y0, room.y1, 4.5, 3.0):
        s.stand("decor_conveyor", room.cx, y, "S")
    accents(s, layout, room, "EW", "decor_bin", 3.0, 0.0, inset=0.45)
    accents(s, layout, room, "EW", "decor_pump", 4.0, 0.0, offset=1.5)


# ----- Cargo -----


def d_cargo_bay(s, layout, room):
    sl_cargo_bay(s, layout, room)
    for y in spaced(room.y0, room.y1, 5.0, 4.0):
        s.stand("decor_conveyor", room.x0 + 3.5, y, "S")
    fill(s, layout, room, "decor_cart", 8.0, 7.0, margin=5.0, skip=lambda i, j: (i + j) % 2)


def d_office(s, layout, room):
    """A head's office: the head's own desk and command chair, visitor chairs and the usual filing."""
    sl_office(s, layout, room)
    desk_y = room.cy
    sl.desk(s, room.cx, desk_y, "N", chair="decor_captain_chair")
    for dx in (-VISITOR_SPREAD, VISITOR_SPREAD):
        s.stand("decor_crew_chair", room.cx + dx, desk_y - VISITOR_GAP, "N")
    if room.name == "quartermaster":
        accents(s, layout, room, "S", "decor_pigeonholes", 2.2, 0.0, offset=1.0)


def d_mail(s, layout, room):
    lamps(s, layout, room)
    accents(s, layout, room, "NS", "decor_pigeonholes", 2.1, 0.0)
    for y in spaced(room.y0, room.y1, 5.0, 4.0):
        s.stand("decor_conveyor", room.cx, y, "S")
    fill(s, layout, room, "decor_cart", 4.0, 6.0, margin=3.0, skip=lambda i, j: (i + j) % 2)
    accents(s, layout, room, "EW", "decor_bin", 3.0, 0.0, inset=0.45)


def d_mining(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_ore_cart", 3.0, 3.5, margin=3.0, skip=lambda i, j: (i + j) % 2)
    fill(s, layout, room, "decor_crate", 3.0, 3.5, margin=3.0, skip=lambda i, j: (i + j) % 2 == 0)
    accents(s, layout, room, "N", "decor_tool_wall", 2.1, 0.0)
    for x in spaced(room.x0, room.x1, 2.0, 2.0):
        s.stand("decor_suit", x, room.y0 + 1.5, "N")
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)


def d_dock(s, layout, room):
    lamps(s, layout, room, "NS")
    s.stand("decor_shuttle", room.cx, room.cy + 1.0, "S")
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)
    accents(s, layout, room, "NS", "decor_lamp_beacon", 4, 2.3)
    for x in (room.x0 + 1.5, room.x1 - 1.5):
        for y in spaced(room.y0, room.y1, 2.0, 2.5):
            s.stand("decor_bench", x, y, "E" if x < room.cx else "W")


# ----- Crew life -----


def d_gym(s, layout, room):
    lamps(s, layout, room)
    fill(s, layout, room, "decor_treadmill", 2.2, 10.0, margin=2.5, facing="S", skip=lambda i, j: j != 0)
    fill(s, layout, room, "decor_weight_rack", 3.0, 10.0, margin=3.0, facing="S", skip=lambda i, j: j != 1)
    for x in spaced(room.x0, room.x1, 4.0, 2.5):
        s.stand("decor_punching_bag", x, room.y0 + 4.0, "S")
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)
    accents(s, layout, room, "N", "decor_vending", 4.0, 0.0)
    corner_planters(s, layout, room)


def d_lounge(s, layout, room):
    sl_lounge(s, layout, room)
    accents(s, layout, room, "N", "decor_arcade", 2.2, 0.0, offset=1.0)


def d_janitor(s, layout, room):
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "EW", "decor_locker", 1.5, 0.0)
    accents(s, layout, room, "N", "decor_sink", 2.6, 0.0)
    fill(s, layout, room, "decor_mop_bucket", 2.0, 2.5, margin=3.0)
    fill(s, layout, room, "decor_cart", 3.0, 5.0, margin=3.5, skip=lambda i, j: (i + j) % 2)


def d_restrooms(s, layout, room):
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "N", "decor_toilet", 1.8, 0.0)
    accents(s, layout, room, "S", "decor_sink", 2.0, 0.0, offset=0.5)
    accents(s, layout, room, "E", "decor_shower", 2.2, 0.0, offset=0.4)
    accents(s, layout, room, "W", "decor_vending", 4.0, 0.0)


def d_airlock(s, layout, room):
    """The airlock chamber itself: bare walls, a warning beacon on each wall, a pressure panel and a pipe panel."""
    long = long_edges(room)
    lamps(s, layout, room, long)
    for edge in "NSEW":
        wall_decor(s, layout, room, edge, "decor_lamp_beacon", sum(edge_range(room, edge)) / 2, 2.3)
    accents(s, layout, room, long[:1], "decor_switch_panel", 3.0, 1.3, offset=0.5)
    accents(s, layout, room, long[1:], "decor_pipe_panel", 5.0, 1.6)


def d_airlock_prep(s, layout, room):
    """The equipment room on the station side of an airlock: lockers, spacesuits on stands, benches."""
    long = long_edges(room)
    lamps(s, layout, room, long)
    accents(s, layout, room, long[:1], "decor_locker", 1.3, 0.0, offset=0.3)
    accents(s, layout, room, long[1:], "decor_suit", 1.4, 0.0, offset=0.3, inset=0.4)
    accents(s, layout, room, long[:1], "decor_switch_panel", 3.0, 1.3, offset=0.6)


def d_corridor(s, layout, room):
    sl_corridor(s, layout, room)
    extinguishers(s, layout, room)


BRIG_CELL_WIDTH = 3
BRIG_CELL_DEPTH = 6
BRIG_CELL_MARGIN = 1


def d_checkpoint(s, layout, room):
    """A pass-through post between the north corridor and the brig: gates on the way, desks off to the side."""
    lamps(s, layout, room, "NS")
    s.stand("decor_gate", room.cx, room.y1 - 3.5, "S")
    s.stand("decor_gate", room.cx, room.y1 - 6.5, "S")
    desk_line(s, layout, room, "W", 3.3, margin=2.0)
    accents(s, layout, room, "S", "decor_weapon_rack", 2.3, 0.0)
    accents(s, layout, room, "S", "decor_locker", 1.8, 0.0, offset=4.6)
    wall_decor(s, layout, room, "N", "decor_lamp_beacon", room.x0 + 1.5, 2.3)
    accents(s, layout, room, "E", "decor_weapon_rack", 2.3, 0.0, offset=1.0)


def d_brig(s, layout, room):
    """Single cells along both long walls of a central aisle, bars towards the aisle, a toilet on each back wall."""
    lamps(s, layout, room, "NS")
    south_bars = room.y0 + BRIG_CELL_MARGIN + BRIG_CELL_DEPTH
    north_bars = room.y1 - BRIG_CELL_MARGIN - BRIG_CELL_DEPTH
    cells = list(range(room.x0 + BRIG_CELL_MARGIN, room.x1 - BRIG_CELL_MARGIN - BRIG_CELL_WIDTH + 1, BRIG_CELL_WIDTH))
    for x in cells:
        for offset in range(BRIG_CELL_WIDTH):
            if offset == BRIG_CELL_WIDTH // 2:
                continue
            for row in range(sl.DOOR_ROWS):
                s.wall_piece("wall_bars", "N", south_bars, x + offset, row)
                s.wall_piece("wall_bars", "S", north_bars, x + offset, row)
        centre = x + BRIG_CELL_WIDTH / 2
        s.stand("decor_bed", centre, room.y0 + BRIG_CELL_MARGIN + 2.5)
        s.stand("decor_bed", centre, room.y1 - BRIG_CELL_MARGIN - 2.5)
        wall_decor(s, layout, room, "S", "decor_toilet", centre, 0.0)
        wall_decor(s, layout, room, "N", "decor_toilet", centre, 0.0)
        for bars_y, facing in ((south_bars, "N"), (north_bars, "S")):
            s.mount("decor_lamp_beacon", facing, bars_y, x + BRIG_CELL_WIDTH // 2, 2.3)
    for x in cells + [cells[-1] + BRIG_CELL_WIDTH]:
        for y in range(room.y0 + BRIG_CELL_MARGIN, south_bars):
            for row in range(sl.DOOR_ROWS):
                s.wall_piece("wall_plate_riveted", "E", x, y, row)
        for y in range(north_bars, room.y1 - BRIG_CELL_MARGIN):
            for row in range(sl.DOOR_ROWS):
                s.wall_piece("wall_plate_riveted", "E", x, y, row)
    s.stand("decor_table", room.cx, room.cy, "S")
    s.stand("decor_crew_chair", room.cx, room.cy + 0.95, "S")


PRISON_CELL_WIDTH = 3
PRISON_CELL_DEPTH = 3
PRISON_CELLS_PER_SIDE = 5
INTERROGATION_CHAIR_END = 1.15
OBSERVER_CHAIR_STEP = 1.4
WARDEN_VISITOR_GAP = 1.2


def d_prison(s, layout, room):
    """Long-term cells along both walls with bars towards a narrow aisle; the doors are at the north end."""
    lamps(s, layout, room, "NS")
    ys = [room.y0 + 1 + PRISON_CELL_WIDTH * i for i in range(PRISON_CELLS_PER_SIDE)]
    sides = (
        ("W", room.x0 + 1 + PRISON_CELL_DEPTH, "E", room.x0 + 1, room.x0 + 1 + PRISON_CELL_DEPTH / 2),
        ("E", room.x1 - 1 - PRISON_CELL_DEPTH, "W", room.x1 - 1 - PRISON_CELL_DEPTH, room.x1 - 1 - PRISON_CELL_DEPTH / 2 + 0.25),
    )
    for wall, bars_x, facing, divider_from, bed_x in sides:
        for y in ys:
            for offset in range(PRISON_CELL_WIDTH):
                if offset == PRISON_CELL_WIDTH // 2:
                    continue
                for row in range(sl.DOOR_ROWS):
                    s.wall_piece("wall_bars", facing, bars_x, y + offset, row)
            centre = y + PRISON_CELL_WIDTH / 2
            s.stand("decor_bed", bed_x, centre, "E")
            wall_decor(s, layout, room, wall, "decor_toilet", centre, 0.0)
            s.mount("decor_lamp_beacon", facing, bars_x, centre, 2.3)
        for y in ys + [ys[-1] + PRISON_CELL_WIDTH]:
            first = room.x0 + 1 if wall == "W" else bars_x + 1
            last = bars_x if wall == "W" else room.x1 - 1
            for x in range(first, last):
                for row in range(sl.DOOR_ROWS):
                    s.wall_piece("wall_plate_riveted", "S", y, x, row)
    wall_decor(s, layout, room, "S", "decor_monitor_crt_wall", room.cx, 1.8)


def d_interrogation(s, layout, room):
    """Two interrogation tables with four chairs each, a row of observer chairs and an evidence locker."""
    lamps(s, layout, room, "NS")
    for y in (room.y1 - 4.5, room.y0 + 5.0):
        s.stand("decor_table", room.cx - 0.4, y)
        for dx in (-0.45, 0.45):
            s.stand("decor_crew_chair", room.cx - 0.4 + dx, y - DESK_GAP, "N")
            s.stand("decor_crew_chair", room.cx - 0.4 + dx, y + DESK_GAP, "S")
        s.stand("decor_crew_chair", room.cx - 0.4 - INTERROGATION_CHAIR_END, y, "E")
        s.stand("decor_crew_chair", room.cx - 0.4 + INTERROGATION_CHAIR_END, y, "W")
    for y in spaced(room.y0 + 8.0, room.y1 - 8.0, OBSERVER_CHAIR_STEP, 0.0):
        s.stand("decor_crew_chair", room.x1 - 1.3, y, "W")
    accents(s, layout, room, "W", "decor_locker", 1.5, 0.0, offset=6.0)
    wall_decor(s, layout, room, "W", "decor_monitor_crt_wall", room.cy, 1.8)


DESK_GAP = 0.95


def d_warden(s, layout, room):
    """The warden's own desk with a command chair and two visitor chairs, clerks' desks, a monitor wall."""
    lamps(s, layout, room, "NS")
    for x in spaced(room.x0, room.x1, 1.6, 0.8):
        for z in (1.2, 1.9):
            wall_decor(s, layout, room, "S", "decor_monitor_crt_wall", x, z)
    desk_line(s, layout, room, "N", 3.0, margin=1.5)
    desk_y = room.cy + 1.0
    sl.desk(s, room.cx, desk_y, "N", chair="decor_captain_chair")
    for dx in (-0.7, 0.7):
        s.stand("decor_crew_chair", room.cx + dx, desk_y - WARDEN_VISITOR_GAP, "N")
    accents(s, layout, room, "E", "decor_weapon_rack", 2.2, 0.0, offset=6.0)
    accents(s, layout, room, "W", "decor_locker", 2.0, 0.0, offset=2.0)


def far_point(room, far, depth):
    """A point `depth` metres off the middle of the wall on the `far` side."""
    wall = {"N": (room.cx, room.y1), "S": (room.cx, room.y0), "E": (room.x1, room.cy), "W": (room.x0, room.cy)}[far]
    ux, uy = sl.INWARD[far]
    return wall[0] - ux * depth, wall[1] - uy * depth


def distance_to_wall(room, far, x, y):
    return {"N": room.y1 - y, "S": y - room.y0, "E": room.x1 - x, "W": x - room.x0}[far]


def seats_facing_far(s, layout, room, template, step_x, step_y, far, margin=2.5, clear=6.0, margin_y=None):
    """Rows of seats looking at the focus on the far wall, leaving `clear` metres in front of it."""
    margin_y = margin if margin_y is None else margin_y
    for x in spaced(room.x0, room.x1, step_x, margin):
        for y in spaced(room.y0, room.y1, step_y, margin_y):
            if distance_to_wall(room, far, x, y) >= clear and layout.point_free(room, x, y):
                s.stand(template, x, y, far)


def across(room, far, offset):
    """A displacement along the far wall, for placing things side by side."""
    return (offset, 0.0) if far in "NS" else (0.0, offset)


def exec_desk(s, layout, room, depth=5.0):
    """The head's own desk and command chair at the far side of the room, the head looking at the door."""
    far = sl.far_edge(layout, room)
    x, y = far_point(room, far, depth)
    sl.desk(s, x, y, far, chair="decor_captain_chair")
    ux, uy = sl.INWARD[far]
    for offset in (-VISITOR_SPREAD, VISITOR_SPREAD):
        dx, dy = across(room, far, offset)
        s.stand("decor_crew_chair", x - ux * VISITOR_GAP + dx, y - uy * VISITOR_GAP + dy, far)


def door_desk(s, layout, room, template_chair="decor_crew_chair"):
    """A reception desk just inside the door, its user between the desk and the door wall looking into the room."""
    door_edge = layout.door_edges(room)[0]
    start = layout.door_spans[(room.index, door_edge)][0]
    low, high = edge_range(room, door_edge)
    along = start + sm.DOOR_WIDTH / 2 + DOOR_DESK_SHIFT
    if along > high - SIGN_MARGIN:
        along = start + sm.DOOR_WIDTH / 2 - DOOR_DESK_SHIFT
    face = {"N": room.y1 - sl.SLAB, "S": room.y0 + sl.SLAB, "E": room.x1 - sl.SLAB, "W": room.x0 + sl.SLAB}[door_edge]
    ux, uy = sl.INWARD[door_edge]
    x, y = (along, face) if door_edge in "NS" else (face, along)
    sl.desk(s, x - ux * DOOR_DESK_DEPTH, y - uy * DOOR_DESK_DEPTH, door_edge, chair=template_chair)
    return door_edge, along


def d_hop(s, layout, room):
    sl_hop(s, layout, room)
    accents(s, layout, room, "E", "decor_pigeonholes", 2.2, 0.0, offset=14.0)
    exec_desk(s, layout, room, depth=HOP_DESK_FROM_WALL)


def d_warden(s, layout, room):
    lamps(s, layout, room, "NS")
    for x in spaced(room.x0, room.x1, 1.6, 0.8):
        for z in (1.2, 1.9):
            wall_decor(s, layout, room, "S", "decor_monitor_crt_wall", x, z)
    desk_line(s, layout, room, "N", 3.0, margin=1.5)
    exec_desk(s, layout, room, depth=WARDEN_DESK_FROM_WALL)
    accents(s, layout, room, "E", "decor_weapon_rack", 2.2, 0.0, offset=6.0)
    accents(s, layout, room, "W", "decor_locker", 2.0, 0.0, offset=2.0)


def d_office(s, layout, room):
    sl_office(s, layout, room)
    exec_desk(s, layout, room, depth=min(5.0, (room.y1 - room.y0) / 3))


def d_captain(s, layout, room):
    sl_captain(s, layout, room)
    exec_desk(s, layout, room, depth=CAPTAIN_DESK_FROM_WALL)
    wall_decor(s, layout, room, "N", "decor_safe", room.x1 - 3, 0.0, inset=0.5)


def d_courtroom(s, layout, room):
    """The judge at the far wall looking at the door, the gallery facing the judge."""
    far = sl.far_edge(layout, room)
    lamps(s, layout, room, "EW")
    x, y = far_point(room, far, 2.8)
    s.stand("decor_judge_bench", x, y, sl.FACING_OF_EDGE[far])
    seats_facing_far(s, layout, room, "decor_sofa", 6.0, 2.4, far, margin=3.0, clear=COURT_CLEAR)
    for offset in (-3.0, 3.0):
        dx, dy = across(room, far, offset)
        tx, ty = far_point(room, far, COURT_TABLE_DEPTH)
        ux, uy = sl.INWARD[far]
        s.stand("decor_table", tx + dx, ty + dy, far)
        s.stand("decor_crew_chair", tx + dx - ux * DESK_GAP, ty + dy - uy * DESK_GAP, far)
    accents(s, layout, room, "EW", "decor_shelf", 2.2, 0.0, offset=11.0)


def d_chapel(s, layout, room):
    """The altar and the pastor's seat at the far wall facing the door, the pews facing the altar."""
    far = sl.far_edge(layout, room)
    lamps(s, layout, room)
    ax, ay = far_point(room, far, 2.2)
    s.stand("decor_altar", ax, ay, sl.FACING_OF_EDGE[far])
    ux, uy = sl.INWARD[far]
    s.stand("decor_captain_chair", ax + ux * PASTOR_BEHIND, ay + uy * PASTOR_BEHIND, sl.FACING_OF_EDGE[far])
    seats_facing_far(s, layout, room, "decor_sofa", 3.2, 2.2, far, margin=2.2, clear=CHAPEL_CLEAR, margin_y=3.0)
    corner_planters(s, layout, room)
    accents(s, layout, room, "EW", "decor_shelf", 2.2, 0.0, offset=11.0)


def d_theatre(s, layout, room):
    """The stage at the far wall facing the door, the seats facing the stage."""
    far = sl.far_edge(layout, room)
    lamps(s, layout, room)
    x, y = far_point(room, far, 2.6)
    s.stand("decor_stage", x, y, sl.FACING_OF_EDGE[far])
    seats_facing_far(s, layout, room, "decor_sofa", 2.6, 2.4, far, margin=2.5, clear=THEATRE_CLEAR)
    corner_planters(s, layout, room)


def d_library(s, layout, room):
    sl_library(s, layout, room)
    door_desk(s, layout, room, template_chair="decor_captain_chair")
    door_edge = layout.door_edges(room)[0]
    wall_decor(s, layout, room, door_edge, "decor_pigeonholes", room.cx, 0.0)


def d_cabin(s, layout, room):
    """A two-person cabin: two beds along the far wall, lockers, a table and a chair."""
    far = sl.far_edge(layout, room)
    lamps(s, layout, room, "EW")
    wall_decor(s, layout, room, far, "decor_lamp_tube", room.cx, 2.4)
    y = room.y1 - 1.4 if far == "N" else room.y0 + 1.4
    head_facing = "S" if far == "N" else "N"
    for dx in (1.2, 2.6):
        s.stand("decor_bed", room.x0 + dx, y, head_facing)
    wall_decor(s, layout, room, far, "decor_locker", room.x1 - 0.9, 0.0)
    wall_decor(s, layout, room, far, "decor_locker", room.x1 - 1.6, 0.0)
    s.stand("decor_table", room.x1 - 1.0, room.cy, "E")
    s.stand("decor_crew_chair", room.x1 - 1.95, room.cy, "E")


def d_ore_processing(s, layout, room):
    """The ore line: conveyors from the intake, furnaces (generators), bins and the stacked ingots."""
    lamps(s, layout, room)
    for y in spaced(room.y0, room.y1, 4.0, 3.0):
        s.stand("decor_conveyor", room.cx, y, "S")
    for x in (room.x0 + 3, room.x1 - 3):
        for y in spaced(room.y0, room.y1, 5.0, 3.0):
            s.stand("decor_generator", x, y, "S")
    accents(s, layout, room, "NS", "decor_bin", 3.0, 0.0, inset=0.45, offset=1.0)
    accents(s, layout, room, "EW", "decor_pipe_panel", 5.0, 1.8)
    fill(s, layout, room, "decor_crate", 4.0, 5.0, margin=6.0, skip=lambda i, j: (i + j) % 2)
    extinguishers(s, layout, room)


def d_cargo_receiving(s, layout, room):
    """Where shipments are received from the dock: conveyors, stacked crates, carts and the clerks' desks."""
    lamps(s, layout, room)
    for y in spaced(room.y0, room.y1, 5.0, 3.0):
        s.stand("decor_conveyor", room.cx, y, "S")
    fill(s, layout, room, "decor_crate", 3.5, 3.5, margin=2.5, skip=lambda i, j: i == 1)
    fill(s, layout, room, "decor_cart", 7.0, 6.0, margin=4.0, skip=lambda i, j: (i + j) % 2)
    desk_line(s, layout, room, "N", 5.0, margin=3.0)
    extinguishers(s, layout, room)


def d_dock(s, layout, room):
    lamps(s, layout, room, "NS")
    if room.name == "cargo_dock":
        s.stand("decor_shuttle", room.cx, room.cy, "S")
        for y in spaced(room.y0, room.y1, 3.0, 3.0):
            s.stand("decor_conveyor", room.x0 + 3.5, y, "W")
        fill(s, layout, room, "decor_crate", 3.0, 3.0, margin=2.5, skip=lambda i, j: i != 0 and j % 2)
        accents(s, layout, room, "NS", "decor_lamp_beacon", 4, 2.3)
        return
    s.stand("decor_shuttle", room.cx, room.cy + 1.0, "S")
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)
    accents(s, layout, room, "NS", "decor_lamp_beacon", 4, 2.3)
    for x in (room.x0 + 1.5, room.x1 - 1.5):
        for y in spaced(room.y0, room.y1, 2.0, 2.5):
            s.stand("decor_bench", x, y, "E" if x < room.cx else "W")


BRIG_CELL_UNIT = 3
BRIG_CELL_DEPTH = 7
PRISON_CELL_DEPTH = 3


def cell_widths(total, unit=BRIG_CELL_UNIT):
    """Cell widths in whole metres that exactly fill `total`, the remainder going to the end cells."""
    count = total // unit
    widths = [unit] * count
    extra = total - unit * count
    widths[0] += extra - extra // 2
    widths[-1] += extra // 2
    return widths


def cell_front(s, facing, fixed, start, width):
    """A cell's glass front with an armoured door in the middle; it spans the whole width and the whole height."""
    door = start + width // 2
    for x in range(start, start + width):
        if x == door:
            continue
        for row in range(sl.DOOR_ROWS):
            s.wall_piece("wall_window", facing, fixed, x, row)
    s.wall_piece("wall_cell_door", facing, fixed, door, 0)
    s.mount("decor_lamp_beacon", facing, fixed, door + 0.5, 2.3)


def d_brig(s, layout, room):
    """Single cells along both long walls of a central aisle: glass fronts with armoured doors, solid dividers
    that run from the outer wall right up to the front, and a bed and a toilet in each."""
    lamps(s, layout, room, "NS")
    south_front = room.y0 + BRIG_CELL_DEPTH
    north_front = room.y1 - BRIG_CELL_DEPTH
    x, cells = room.x0, []
    for width in cell_widths(room.x1 - room.x0):
        cells.append((x, width))
        x += width
    for start, width in cells:
        cell_front(s, "N", south_front, start, width)
        cell_front(s, "S", north_front, start, width)
        centre = start + width / 2
        s.stand("decor_bed", centre, room.y0 + 3.0)
        s.stand("decor_bed", centre, room.y1 - 3.0)
        wall_decor(s, layout, room, "S", "decor_toilet", centre, 0.0)
        wall_decor(s, layout, room, "N", "decor_toilet", centre, 0.0)
    for start, _ in cells[1:]:
        for y in list(range(room.y0, south_front)) + list(range(north_front, room.y1)):
            for row in range(sl.DOOR_ROWS):
                s.wall_piece("wall_plate_riveted", "E", start, y, row)
    s.stand("decor_table", room.cx, room.cy, "S")
    s.stand("decor_crew_chair", room.cx, room.cy + 0.95, "S")


def d_prison(s, layout, room):
    """Long-term cells along both walls, built like the brig's: dividers from the outer wall to the glass front,
    an armoured door in each front, and the cell blocks closed off at the aisle's north end."""
    lamps(s, layout, room, "NS")
    ys = [room.y0 + PRISON_CELL_WIDTH * i for i in range(PRISON_CELLS_PER_SIDE)]
    sides = (
        ("W", room.x0 + PRISON_CELL_DEPTH, "E", room.x0 + 1.6, room.x0, room.x0 + PRISON_CELL_DEPTH),
        ("E", room.x1 - PRISON_CELL_DEPTH, "W", room.x1 - 1.6, room.x1 - PRISON_CELL_DEPTH + 1, room.x1),
    )
    for wall, front_x, facing, bed_x, first_x, last_x in sides:
        for y in ys:
            cell_front(s, facing, front_x, y, PRISON_CELL_WIDTH)
            centre = y + PRISON_CELL_WIDTH / 2
            s.stand("decor_bed", bed_x, centre, "E")
            wall_decor(s, layout, room, wall, "decor_toilet", centre, 0.0)
        for y in ys + [ys[-1] + PRISON_CELL_WIDTH]:
            for x in range(first_x, last_x):
                for row in range(sl.DOOR_ROWS):
                    s.wall_piece("wall_plate_riveted", "S", y, x, row)
    wall_decor(s, layout, room, "S", "decor_monitor_crt_wall", room.cx, 1.8)


TUNNEL_LAMP_STEP = 12
TUNNEL_LAMP_Z = 2.4
TUNNEL_PIPE_STEP = 4.0
TUNNEL_LOCKER_STEP = 14.0


def d_tunnel(s, layout, room):
    """Maintenance tunnel: sparse lamps, pipe and cable runs along the walls, a locker here and there, warning plates."""
    long = long_edges(room)
    accents(s, layout, room, long, "decor_lamp_tube", TUNNEL_LAMP_STEP, TUNNEL_LAMP_Z, offset=3)
    accents(s, layout, room, long[:1], "decor_pipe_run", TUNNEL_PIPE_STEP, 0.0)
    accents(s, layout, room, long[1:], "decor_cable_tray", TUNNEL_PIPE_STEP, 0.0, offset=2.0)
    accents(s, layout, room, long[1:], "decor_locker", TUNNEL_LOCKER_STEP, 0.0, offset=5.0)
    accents(s, layout, room, long[:1], "decor_warning_sign", TUNNEL_LOCKER_STEP, 0.0, offset=9.0)


def d_maint_room(s, layout, room):
    """A maintenance storeroom: tools, crates, a workbench and the pipe and cable runs."""
    lamps(s, layout, room, "NS")
    accents(s, layout, room, "EW", "decor_locker", 2.0, 0.0)
    accents(s, layout, room, "NS", "decor_pipe_run", 4.0, 0.0)
    accents(s, layout, room, "NS", "decor_cable_tray", 4.0, 0.0, offset=2.0)
    fill(s, layout, room, "decor_crate", 3.2, 3.2, margin=2.5, skip=lambda i, j: (i + j) % 2)
    s.stand("decor_workbench", room.cx, room.cy, "S")


# Keep the generic versions as the base for rooms that only get extras.
sl_bridge, sl_captain, sl_hop = DECOR["bridge"], DECOR["captain"], DECOR["hop"]
sl_library, sl_cryo, sl_morgue = DECOR["library"], DECOR["cryo"], DECOR["morgue"]
sl_cargo_bay, sl_office, sl_lounge = DECOR["cargo_bay"], DECOR["office"], DECOR["lounge"]
sl_airlock, sl_corridor = DECOR["airlock"], DECOR["corridor"]

DECOR.update({name[2:]: fn for name, fn in list(globals().items()) if name.startswith("d_") and callable(fn)})


# ----- Signs -----


def sign_material():
    return sm.material("sign_text", SIGN_COLOR, roughness=0.6)


def add_text(s, text, location, facing, size):
    curve = bpy.data.curves.new("sign", "FONT")
    curve.body = text
    curve.size = size
    curve.align_x = "CENTER"
    curve.align_y = "CENTER"
    curve.extrude = SIGN_EXTRUDE
    curve.materials.append(sign_material())
    obj = bpy.data.objects.new(f"sign_{text}", curve)
    obj.location = location
    obj.rotation_euler = (math.pi / 2, 0.0, ss.FACING_ANGLE[facing])
    s.collection.objects.link(obj)
    s.count += 1
    return obj


def face_point(layout, room, edge, along, z):
    """A point on a room wall's inner face, nudged off it so the lettering does not sink in."""
    fixed = layout.inner_face(room, edge, along)
    facing = sl.FACING_OF_EDGE[edge]
    x, y = s_origin(facing, fixed, along)
    dx, dy = sl.INWARD[facing]
    return (x + dx * SIGN_OUT, y + dy * SIGN_OUT, z), facing


def s_origin(facing, fixed, along):
    if facing == "S":
        return (along, fixed)
    if facing == "N":
        return (along, fixed + sl.SLAB)
    if facing == "E":
        return (fixed + sl.SLAB, along)
    return (fixed, along)


def fit_size(text, width, size):
    return min(size, width / (SIGN_CHAR_WIDTH * max(len(text), 1)))


def add_signs(s, layout):
    for room in layout.rooms:
        title = title_of(room)
        if title is None:
            continue
        if not room.corridor:
            for edge in layout.door_edges(room):
                for start in layout.door_spans[(room.index, edge)]:
                    position, facing = face_point(layout, room, edge, start + sm.DOOR_WIDTH / 2, SIGN_Z_DOOR)
                    add_text(s, title, position, facing, fit_size(title, SIGN_DOOR_WIDTH, SIGN_DOOR_SIZE))
        if room.name.startswith(("cabin_", "lock_")):
            continue
        edge = sl.far_edge(layout, room)
        low, high = edge_range(room, edge)
        z = SIGN_Z_HIGH if room.height >= 4 else SIGN_Z_LOW
        along = (low + high) / 2
        if layout.near_door(room, edge, along):
            along += sm.DOOR_WIDTH + SIGN_MARGIN
        span = high - low - 2 * SIGN_MARGIN
        if room.corridor:
            edges = sl.long_edges(room)[1:]
            low, high = edge_range(room, edges)
            step = 24
            for pos in spaced(low, high, step, SIGN_MARGIN + 3):
                if not layout.near_door(room, edges, pos):
                    position, facing = face_point(layout, room, edges, pos, SIGN_Z_LOW)
                    add_text(s, title, position, facing, fit_size(title, 8.0, 0.3))
            continue
        position, facing = face_point(layout, room, edge, along, z)
        add_text(s, title, position, facing, fit_size(title, span, SIGN_BANNER_SIZE))
