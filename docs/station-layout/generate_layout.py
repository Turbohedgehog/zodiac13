#!/usr/bin/env python3
"""Draws the test station's decks as text and estimates its load.

One character is 1 m (4 grid cells). Run from this directory: writes deck-*.txt,
section.txt and stats.txt next to itself.
"""

from pathlib import Path

WIDTH, DEPTH = 97, 49  # hull 96 x 48 m
CELLS_PER_M = 4
DECK_PITCH_M = 4  # floor slab + 3.5 m room + ceiling slab
ROOM_HEIGHT_CELLS = 14
WEDGES_PER_STAIRWELL = 8  # two 2 m wide flights of 1.75 m, 1 m wedges
CABIN_WIDTH_M = 3  # single cabins, 3 x 4 m
PANEL_M = 4  # realistic estimate: walls and windows in panels up to 4 m, slabs up to 4 x 4 m
OUT = Path(__file__).parent

WALL, WINDOW, DOOR, SEALED, RAMP, SHAFT, DANCE = "#", "=", "D", "X", "/", "L", ":"
HIDDEN, TUNNEL, UNDER_LEDGE, LADDER = "S", ".", "~", "H"
DOORS = DOOR + SEALED + HIDDEN
# Rooms staff never need but maintenance does: the hidden tunnels open into them.
SERVICE_ROOMS = ("Щ", "ТЕХ", "АТМОСФЕРА", "РЕАКТОР", "ЭНЕРГОСИСТЕМЫ", "ГРАВИГЕН", "ВОДООЧИСТКА", "СВЯЗЬ", "ОТСЕК")
HEADER_RULER = ("    " + "".join(f"{x:<10}" for x in range(0, WIDTH, 10))).rstrip()


class Deck:
    def __init__(self, number, name, sealed=False):
        self.number = number
        self.name = name
        self.sealed = sealed
        self.grid = [[" "] * WIDTH for _ in range(DEPTH)]
        self.rooms = []
        self.rects = []
        self.counters = {}
        self.berths = 0

    @property
    def entry(self):
        """The doors leading onto the deck, welded shut on a sealed one."""
        return SEALED if self.sealed else DOOR

    def put(self, x, y, ch):
        self.grid[y][x] = ch

    def rect(self, x0, y0, x1, y1):
        for x in range(x0, x1 + 1):
            self.put(x, y0, WALL)
            self.put(x, y1, WALL)
        for y in range(y0, y1 + 1):
            self.put(x0, y, WALL)
            self.put(x1, y, WALL)

    def numbered(self, prefix):
        self.counters[prefix] = self.counters.get(prefix, 0) + 1
        return f"{prefix}{self.counters[prefix]}"

    def room(self, x0, y0, x1, y1, label, door=None):
        """`door`: (side, offset[, width]) with side in N/S/W/E; offset from the room's start, None centres it."""
        self.rect(x0, y0, x1, y1)
        self.rooms.append(label)
        self.rects.append((x0, y0, x1, y1, label))
        self.label(x0, y0, x1, y1, label)
        if door:
            self.door_on(x0, y0, x1, y1, *door)

    def label(self, x0, y0, x1, y1, text):
        y = (y0 + y1) // 2
        x = max(x0 + 1, (x0 + x1 - len(text)) // 2 + 1)
        for i, ch in enumerate(text[: x1 - x0 - 1]):
            self.put(x + i, y, ch)

    def door_on(self, x0, y0, x1, y1, side, offset=None, width=2, ch=None):
        ch = ch or DOOR
        if side in "NS":
            y = y0 if side == "N" else y1
            start = x0 + (offset if offset is not None else (x1 - x0 - width) // 2 + 1)
            for x in range(start, start + width):
                self.put(x, y, ch)
        else:
            x = x0 if side == "W" else x1
            start = y0 + (offset if offset is not None else (y1 - y0 - width) // 2 + 1)
            for y in range(start, start + width):
                self.put(x, y, ch)

    def window(self, x0, y0, x1, y1):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                if self.grid[y][x] == WALL:
                    self.put(x, y, WINDOW if not self.sealed else WALL)

    def fill(self, x0, y0, x1, y1, ch):
        for y in range(y0, y1 + 1):
            for x in range(x0, x1 + 1):
                self.put(x, y, ch)


def common_core(deck):
    """Hull, side service rooms, spine corridor and its end airlocks, shared by every deck."""
    deck.rect(0, 0, WIDTH - 1, DEPTH - 1)
    for x0, x1 in ((0, 6), (90, 96)):
        deck.room(x0, 0, x1, 20, "ТЕХ")
        deck.room(x0, 28, x1, 48, "ТЕХ")
    deck.rect(6, 22, 90, 26)
    deck.rooms.append("СПИНА")
    for x0, x1, outer, inner in ((0, 6, "W", "E"), (90, 96, "E", "W")):
        deck.room(x0, 20, x1, 28, "ШЛ", door=(outer, None, 3, deck.entry))
        deck.door_on(x0, 20, x1, 28, inner, None, 3)


def stairs_and_shaft(deck):
    """Drawn last, over the rooms: they cut through every deck."""
    deck.rect(6, 12, 16, 22)
    deck.fill(7, 13, 8, 21, RAMP)
    deck.fill(14, 13, 15, 21, RAMP)
    deck.door_on(6, 12, 16, 22, "S", 4, ch=deck.entry)
    deck.rect(80, 26, 90, 36)
    deck.fill(81, 27, 82, 35, RAMP)
    deck.fill(88, 27, 89, 35, RAMP)
    deck.door_on(80, 26, 90, 36, "N", 4, ch=deck.entry)
    deck.rect(46, 17, 50, 22)
    deck.fill(47, 18, 49, 21, SHAFT)
    deck.door_on(46, 17, 50, 22, "S", 1, 3, deck.entry)
    deck.room(46, 0, 50, 17, "Щ", door=("W", None))
    deck.room(6, 0, 16, 12, "ШЛЮЗ-С", door=("N", 4, 2, deck.entry))
    deck.door_on(6, 0, 16, 12, "S", 4)
    deck.room(80, 36, 90, 48, "ШЛЮЗ-Ю", door=("S", 4, 2, deck.entry))
    deck.door_on(80, 36, 90, 48, "N", 4)
    deck.stairwells = 2
    deck.rooms += ["ЛЕСТНИЦА", "ЛЕСТНИЦА", "ЛИФТ", "ТЕХКОРИДОР", "ТЕХКОРИДОР"]
    service_tunnels(deck)


# Cabin rows and the corridors between them, per band: (y0, y1, door side) and corridor (y0, y1).
CABIN_ROWS = {
    "N": ([(2, 6, "S"), (8, 12, "N"), (12, 16, "S"), (18, 22, "N")], [(6, 8), (16, 18)]),
    "S": ([(26, 30, "S"), (32, 36, "N"), (36, 40, "S"), (42, 46, "N")], [(30, 32), (40, 42)]),
}


# Officers' cabins, 5 x 8 and 5 x 9 m, on one corridor south of the spine.
OFFICER_ROWS = ([(26, 34, "S"), (37, 46, "N")], [(34, 37)])


def cabin_wing(deck, x0, x1, band, passages, shared=(), layout=None, width=CABIN_WIDTH_M):
    """Single cabins along corridors, joined to the spine by 2 m passages at `passages` (x of the west wall).

    `shared`: (x0, x1, row index, label) rooms that replace cabins in one row.
    """
    rows, corridors = layout or CABIN_ROWS[band]
    door_width = 1 if width <= CABIN_WIDTH_M else 2
    for cy0, cy1 in corridors:
        deck.rect(x0, cy0, x1, cy1)
    blocked = [(px, px + 3) for px in passages]
    for index, (y0, y1, side) in enumerate(rows):
        taken = blocked + [(sx0, sx1) for sx0, sx1, row, _ in shared if row == index]
        for sx0, sx1, row, label in shared:
            if row == index:
                deck.room(sx0, y0, sx1, y1, label, door=(side, None))
        cx = x0
        while cx + width <= x1:
            clash = next((end for start, end in taken if cx < end and cx + width > start), None)
            if clash is not None:
                cx = clash
                continue
            deck.room(cx, y0, cx + width, y1, "", door=(side, 1, door_width))
            deck.berths += 1
            cx += width
    spine_wall = 22 if band == "N" else 26
    far = min(cy0 for cy0, _ in corridors) + 1 if band == "N" else max(cy1 for _, cy1 in corridors) - 1
    for px in passages:
        lo, hi = sorted((far, spine_wall))
        deck.fill(px + 1, lo, px + 2, hi, " ")
        for y in range(lo, hi + 1):
            inside_corridor = any(cy0 < y < cy1 for cy0, cy1 in corridors)
            for x in (px, px + 3):
                deck.put(x, y, " " if inside_corridor else WALL)
        deck.door_on(px, spine_wall, px + 3, spine_wall, "N", 1, 2)
    deck.rooms.append("КАЮТЫ")


def service_tunnels(deck):
    """1 m tunnels along the north and south hull, behind a wall, with hidden doors into service rooms.

    Under a hull window or gate the tunnel runs below a 1 m ledge instead, so the view stays open.
    """
    for hull, inner, x0, x1 in ((0, 2, 16, 90), (DEPTH - 1, DEPTH - 3, 6, 80)):
        tunnel = (hull + inner) // 2
        for x in range(x0 + 1, x1):
            if deck.grid[hull][x] in WINDOW + DOORS:
                deck.put(x, tunnel, UNDER_LEDGE)
                deck.put(x, inner, " ")
            else:
                deck.put(x, tunnel, TUNNEL)
                deck.put(x, inner, WALL)
        deck.put(x0, tunnel, HIDDEN)
        deck.put(x1, tunnel, HIDDEN)
        for rx0, ry0, rx1, ry1, label in deck.rects:
            touches = ry0 == 0 if hull == 0 else ry1 == DEPTH - 1
            if touches and label.startswith(SERVICE_ROOMS) and x0 <= rx0 < x1:
                deck.put((rx0 + rx1) // 2, inner, HIDDEN)
    for x0, y_wall in ((0, 20), (0, 28), (90, 20), (90, 28)):  # tech rooms into the end airlocks
        deck.put(x0 + 3, y_wall, HIDDEN)
    for x, y in ((2, 3), (93, 3), (2, 44), (93, 44)):  # ladders between decks
        deck.fill(x, y, x + 1, y + 1, SEALED if deck.sealed else LADDER)


def north_rooms(deck, rooms):
    for x0, x1, label in rooms:
        deck.room(x0, 0, x1, 22, label, door=("S", None))


def south_rooms(deck, rooms):
    for x0, x1, label in rooms:
        deck.room(x0, 26, x1, 48, label, door=("N", None))


def sealed(number, letter):
    """Known only from the original blueprints; every way in is welded shut."""
    deck = Deck(number, f"Запечатанная палуба {letter}", sealed=True)
    common_core(deck)
    north_rooms(deck, [(16, 32, "ОТСЕК ?"), (32, 46, "ОТСЕК ?"), (50, 70, "ОТСЕК ?"), (70, 90, "ОТСЕК ?")])
    south_rooms(deck, [(6, 30, "ОТСЕК ?"), (30, 56, "ОТСЕК ?"), (56, 80, "ОТСЕК ?")])
    deck.room(80, 26, 90, 36, "")
    deck.room(56, 3, 64, 8, "?")
    stairs_and_shaft(deck)
    return deck


def engineering(number):
    deck = Deck(number, "Инженерная палуба")
    common_core(deck)
    north_rooms(deck, [(16, 46, "РЕАКТОР"), (50, 70, "ЭНЕРГОСИСТЕМЫ"), (70, 90, "АТМОСФЕРА-1")])
    south_rooms(deck, [(6, 30, "ГРАВИГЕН"), (30, 50, "ВОДООЧИСТКА"), (50, 66, "МАСТЕРСКАЯ"), (66, 80, "ЗИП")])
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def security_and_storage(number):
    deck = Deck(number, "Палуба охраны и складов")
    common_core(deck)
    north_rooms(deck, [(16, 30, "ПОСТ ОХРАНЫ"), (30, 46, "АРСЕНАЛ")])
    cabin_wing(deck, 70, 90, "N", [79], shared=[(70, 78, 3, "КАЗАРМА")])
    deck.room(50, 0, 70, 22, "", door=("S", None))
    for x0 in range(50, 70, 4):
        deck.room(x0, 0, x0 + 4, 7, deck.numbered("Г"), door=("S", 1))
    deck.label(50, 7, 70, 22, "ГАУПТВАХТА")
    deck.rooms.append("ГАУПТВАХТА")
    south_rooms(deck, [(6, 30, "ГРУЗОВОЙ ШЛЮЗ"), (30, 46, "СКЛАД-1"), (46, 62, "СКЛАД-2"), (62, 80, "СКЛАД-3")])
    deck.door_on(6, 26, 30, 48, "S", None, 8)
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def habitat(number, letter, lounge, extra):
    deck = Deck(number, f"Жилая палуба {letter}")
    common_core(deck)
    # A washroom on every corridor: the cabins of one corridor never walk past another's.
    cabin_wing(deck, 16, 46, "N", [28], shared=[(16, 22, 1, "САН"), (16, 28, 3, "ДУШ-1")])
    cabin_wing(deck, 50, 90, "N", [68], shared=[(50, 56, 1, "САН"), (50, 56, 2, "САН"), (76, 90, 3, "ПРАЧЕЧНАЯ")])
    cabin_wing(deck, 6, 50, "S", [24], shared=[(44, 50, 1, "САН"), (6, 18, 3, "ДУШ-2")])
    south_rooms(deck, [(50, 66, lounge), (66, 80, extra)])
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def campus(number):
    deck = Deck(number, "Палуба кампуса")
    common_core(deck)
    north_rooms(deck, [(16, 28, "КУХНЯ"), (28, 46, "СТОЛОВАЯ")])
    deck.door_on(16, 0, 28, 22, "E", 6)
    deck.window(29, 0, 45, 0)
    deck.room(50, 0, 90, 22, "", door=("S", None, 4))
    deck.fill(60, 4, 80, 14, DANCE)
    deck.label(60, 4, 80, 14, " ТАНЦПОЛ ")
    deck.fill(52, 18, 62, 18, WALL)  # the bar counter: a low wall
    deck.label(50, 18, 70, 22, "БАР")
    deck.window(51, 0, 89, 0)
    deck.rooms.append("БАР")
    south_rooms(deck, [(6, 24, "МЕД-ПРИЁМНЫЙ"), (24, 40, "МЕД-ОПЕРАЦ"), (40, 54, "МЕД-ИЗОЛЯТОР"), (54, 66, "СПОРТЗАЛ"),
                       (66, 80, "АТМОСФЕРА-3")])
    deck.door_on(24, 26, 40, 48, "W", 10)
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def science(number):
    deck = Deck(number, "Научная палуба")
    common_core(deck)
    north_rooms(deck, [(16, 30, "ЛАБ-БИО"), (30, 46, "ЛАБ-ХИМ"), (50, 66, "ЛАБ-ФИЗ"), (66, 90, "ОБСЕРВАТОРИЯ")])
    deck.window(67, 0, 89, 0)
    south_rooms(deck, [(6, 30, "ГИДРОПОНИКА-1"), (30, 54, "ГИДРОПОНИКА-2"), (54, 66, "ЛАБ-МЕД"), (66, 80, "АТМОСФЕРА-4")])
    deck.door_on(6, 26, 30, 48, "E", 10)
    deck.window(8, 48, 52, 48)
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def command(number):
    deck = Deck(number, "Командная палуба")
    common_core(deck)
    north_rooms(deck, [(16, 46, "МОСТИК"), (50, 62, "НАВИГАЦИЯ"), (62, 74, "СВЯЗЬ"), (74, 90, "СОВЕЩАНИЯ")])
    deck.window(17, 0, 45, 0)
    south_rooms(deck, [(6, 26, "КАЮТА КЭПА"), (64, 80, "АТМОСФЕРА-5")])
    deck.berths += 1
    cabin_wing(deck, 26, 64, "S", [46], shared=[(26, 36, 0, "ОХР.МОСТ")], layout=OFFICER_ROWS, width=5)
    deck.room(80, 26, 90, 36, "")
    stairs_and_shaft(deck)
    return deck


def run_lengths(grid, chars):
    """Maximal straight runs of `chars` at least 2 long, horizontal and vertical."""
    lengths = []
    lines = ["".join(row) for row in grid]
    lines += ["".join(grid[y][x] for y in range(DEPTH)) for x in range(WIDTH)]
    for line in lines:
        lengths += [len(run) for run in "".join(c if c in chars else " " for c in line).split() if len(run) >= 2]
    return lengths


def door_count(grid):
    """Doors are connected groups of door cells, whatever their width."""
    seen = set()
    count = 0
    for y in range(DEPTH):
        for x in range(WIDTH):
            if grid[y][x] in DOORS and (x, y) not in seen:
                count += 1
                stack = [(x, y)]
                while stack:
                    cx, cy = stack.pop()
                    if (cx, cy) in seen or not (0 <= cx < WIDTH and 0 <= cy < DEPTH) or grid[cy][cx] not in DOORS:
                        continue
                    seen.add((cx, cy))
                    stack += [(cx + 1, cy), (cx - 1, cy), (cx, cy + 1), (cx, cy - 1)]
    return count


def blocks(lengths, panel=None):
    """One block per run, or per `panel` metres of it."""
    return sum(-(-length // panel) if panel else 1 for length in lengths)


def interior_m2(deck):
    return sum(1 for row in deck.grid[1:-1] for c in row[1:-1] if c not in (WALL, WINDOW, *DOORS))


def render(deck):
    header = [
        f"Палуба {deck.number}: {deck.name}. Отметка пола {(deck.number - 1) * DECK_PITCH_M} м.",
        "1 символ = 1 м (4 ячейки). # стена, = окно, D дверь, X заварено, / лестница, L лифт, : танцпол.",
        ". скрытый техкоридор, ~ техкоридор под подоконной ступенью, S потайная дверь, H трап между палубами.",
        "ШЛ — шлюзы на концах спинного коридора, ШЛЮЗ-С/Ю — выходы в космос у лестниц, Щ — щитовая, ТЕХ — техотсеки.",
    ]
    if deck.sealed:
        header.append("Палуба запечатана: входы заварены (X), план — по исходным чертежам, назначение отсеков персоналу неизвестно.")
    header += ["", HEADER_RULER]
    body = [f"{y:3} " + "".join(row).rstrip() for y, row in enumerate(deck.grid)]
    return "\n".join(header + body) + "\n"


def render_section(decks):
    """Cut along the spine (y = 24 m), seen from the south; one row is 1 m of height."""
    top = len(decks) * DECK_PITCH_M
    dome = 3
    rows = [[" "] * WIDTH for _ in range(top + dome + 1)]
    for deck in decks:
        base = (deck.number - 1) * DECK_PITCH_M
        rows[base] = [WALL] * WIDTH
        for z in range(base + 1, base + DECK_PITCH_M):
            rows[z][0] = rows[z][WIDTH - 1] = WALL
            for x0 in (7, 81):  # stairwell flights, as on the decks
                step = z - base
                rows[z][x0 + step] = RAMP
                rows[z][x0 + 8 - step] = "\\"
        label = f"{deck.number}: {deck.name.upper()}"
        for i, ch in enumerate(label):
            rows[base + 2][20 + i] = ch
        # Stair openings, closed over a sealed deck.
        if deck.number > 1:
            below_sealed = decks[deck.number - 2].sealed
            for x in (*range(7, 16), *range(81, 90)):
                rows[base][x] = SEALED if below_sealed and not deck.sealed else (" " if not below_sealed else " ")
    rows[top] = [WALL] * WIDTH
    for z in range(top, top + dome + 1):
        rows[z][16] = rows[z][46] = WALL
    rows[top + dome][16:47] = [WINDOW] * 31
    for z in range(0, top):
        for x in range(47, 50):
            rows[z][x] = SHAFT
        for x in (2, 3, 93, 94):
            rows[z][x] = LADDER
    for z in range(DECK_PITCH_M, top, DECK_PITCH_M):  # ladder hatches over a sealed deck
        if decks[z // DECK_PITCH_M - 1].sealed and not decks[z // DECK_PITCH_M].sealed:
            for x in (2, 3, 93, 94):
                rows[z][x] = SEALED
        if decks[z // DECK_PITCH_M].sealed and z % DECK_PITCH_M == 2:
            rows[z][46] = SEALED
    header = [
        "Разрез по спинному коридору (y = 24 м), вид с юга. 1 символ = 1 м по горизонтали, строка = 1 м высоты.",
        "/ \\ — марши лестниц (x 6–16 и 80–90), L — шахта лифта (x 46–50), H — трапы техкоридоров (x 2–3 и 93–94),",
        "= — стеклянный купол над мостиком, X — заваренные люки над запечатанными палубами и двери лифта на них.",
        "",
    ]
    body = [f"{z:3} " + "".join(row).rstrip() for z, row in reversed(list(enumerate(rows)))]
    return "\n".join(header + body + [HEADER_RULER]) + "\n"


def main():
    builders = [
        lambda n: sealed(n, "Σ"), lambda n: sealed(n, "Ω"), engineering, security_and_storage,
        lambda n: habitat(n, "А", "ЗАЛ ОТДЫХА", "АТМОСФЕРА-2"), lambda n: habitat(n, "Б", "ИГРОВАЯ", "АТМОСФЕРА-6"),
        campus, science, command,
    ]
    decks = [build(number) for number, build in enumerate(builders, start=1)]
    keys = ("berths", "rooms", "walls", "windows", "doors", "slabs", "wedges", "air_cells", "walls_p", "windows_p", "slabs_p")
    total = dict.fromkeys(keys, 0)
    lines = ["Оценка нагрузки тестовой станции (генерируется generate_layout.py).", ""]
    for old in OUT.glob("deck-*.txt"):
        old.unlink()
    for deck in decks:
        (OUT / f"deck-{deck.number}.txt").write_text(render(deck), encoding="utf-8")
        walls = run_lengths(deck.grid, WALL)
        windows = run_lengths(deck.grid, WINDOW)
        area = interior_m2(deck)
        deck_stats = {
            "berths": deck.berths,
            "rooms": len(deck.rooms),
            "walls": blocks(walls),
            "windows": blocks(windows),
            "doors": door_count(deck.grid),
            "slabs": len(deck.rooms) * 2,
            "wedges": deck.stairwells * WEDGES_PER_STAIRWELL,
            "air_cells": area * CELLS_PER_M * CELLS_PER_M * ROOM_HEIGHT_CELLS,
            "walls_p": blocks(walls, PANEL_M),
            "windows_p": blocks(windows, PANEL_M),
            "slabs_p": 2 * -(-area // (PANEL_M * PANEL_M)),
        }
        for key in keys:
            total[key] += deck_stats[key]
        lines.append(
            f"Палуба {deck.number} ({deck.name}): мест {deck_stats['berths']}, помещений {deck_stats['rooms']}, "
            f"дверей {deck_stats['doors']}, "
            f"площадь {area} м², ячеек воздуха {deck_stats['air_cells']:_}".replace("_", " "))

    fixed = total["doors"] + total["wedges"]
    minimal = total["walls"] + total["windows"] + total["slabs"] + fixed
    panels = total["walls_p"] + total["windows_p"] + total["slabs_p"] + fixed
    lines += [
        "",
        f"Одноместных кают: {total['berths']}. Помещений {total['rooms']}, дверей {total['doors']}, "
        f"клиньев {total['wedges']}.",
        f"Блоков, если каждый прямой отрезок — один блок: {minimal} "
        f"(стен {total['walls']}, окон {total['windows']}, плит {total['slabs']}).",
        f"Блоков панелями до {PANEL_M} м и плитами до {PANEL_M}×{PANEL_M} м: {panels} "
        f"(стен {total['walls_p']}, окон {total['windows_p']}, плит {total['slabs_p']}).",
        f"Ячеек воздуха (шаг 0,25 м): {total['air_cells']:_}.".replace("_", " "),
    ]
    (OUT / "stats.txt").write_text("\n".join(lines) + "\n", encoding="utf-8")
    (OUT / "section.txt").write_text(render_section(decks), encoding="utf-8")
    print("\n".join(lines))


if __name__ == "__main__":
    main()
