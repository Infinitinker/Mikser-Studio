"""Draws Mikser Studio's icon: docs/images/icon.png, the README's copy at 256 px.

A close-up of a mixing console's fader bank: a dark panel in a slight three-quarter view, with
upright sides, four long fader slots under a row of short round knobs, and the faders' caps at
different heights, in the app's coral, amber and sky, the fourth a dark master cap. The panel is
lit from behind, so only its upper edges catch a pale rim of light, on a round violet badge that
glows behind its top-left corner, transparent outside it.

Every size is drawn at 1024 px and scaled down. At 48 px and less the slots and caps of the
detailed drawing are thinner than a pixel and blur away, so those sizes get a simple drawing
instead: three bold faders with light slots and big caps on a panel with a lighter top.

Run it from the repository root:

    .venv\\Scripts\\python tools\\draw_icon.py [--preview FILE]

--preview also writes a sheet of the sizes on a light and a dark background to FILE, to check a
change to the drawing; the sheet is never committed.
"""

import argparse
import math
from collections.abc import Callable, Sequence
from dataclasses import dataclass
from pathlib import Path

from PIL import Image, ImageChops, ImageDraw, ImageFilter

Point = tuple[float, float]
Color = tuple[int, int, int, int]
PointAt = Callable[[float, float], Point]

README_ICON = Path(__file__).resolve().parents[1] / "docs" / "images" / "icon.png"
README_SIZE = 256
PREVIEW_SIZES = (16, 32, 64, 128, 256)
SMALL = 48
"""Sizes up to this one get the simple drawing."""

S = 1024
"""Drawn at this size, then scaled down."""
CENTER = (512, 512)
RADIUS = 500

HOT = (196, 178, 252)
LIGHT = (167, 139, 250)
"""#a78bfa"""
MID = (124, 58, 237)
"""#7c3aed"""
DEEP = (76, 29, 149)
"""#4c1d95"""
RING = (52, 18, 108, 255)

RIM = (241, 234, 255, 255)
FRONT = (22, 21, 28, 255)
SIDE = (11, 10, 15, 255)
TOP = (40, 37, 50, 255)
SMALL_TOP = (184, 176, 206, 255)
SLOT = (4, 4, 7, 255)
SLOT_EDGE = (98, 94, 114, 255)
SMALL_SLOT = (124, 118, 146, 255)
"""At small sizes the slots are lighter than the panel, so they still read as lines."""
TICK = (54, 52, 64, 255)
KNOB = (34, 32, 42, 255)
KNOB_MARK = (120, 116, 136, 255)
MASTER_CAP = (58, 56, 68, 255)
TICKS = 5
"""Ticks on each fader's scale."""
KNOB_HEIGHT = 0.22
"""How far a knob stands out of the face, as a share of the slab's depth."""

CORAL = (255, 94, 91)
AMBER = (255, 180, 0)
SKY = (46, 164, 240)
ACCENTS = (CORAL, AMBER, SKY)
"""The colors of the current icon, and the only bright ones besides the rim light."""

LIGHT_BG = (236, 236, 238, 255)
DARK_BG = (30, 30, 32, 255)


@dataclass(frozen=True)
class Panel:
    """The proportions of the fader panel, a slab seen in a slight three-quarter view."""

    width: float
    """The width of the front face, along its top edge."""
    height: float
    """The height of the front face."""
    rise: float
    """How far the top edge drops toward the right, per unit of width; the sides stay upright."""
    depth: tuple[float, float]
    """The shift from the front face to the back of the slab."""
    inset: tuple[float, float]
    """The margins of the fader channels from the face's left and right edges, in pixels."""
    caps: tuple[float, ...]
    """Where each fader's cap sits along its slot, from 0 (top) to 1 (bottom)."""
    colors: tuple[tuple[int, int, int], ...]
    """The cap colors, one per fader."""
    cap_size: tuple[float, float]
    """The width and height of a cap, as shares of the channel width and the face height."""
    cap_depth: float
    """How far a cap stands out of the face, as a share of the slab's depth."""
    slot_width: float
    """The width of a slot, in pixels."""
    slot_span: tuple[float, float]
    """Where the slots start and end, as shares of the face height."""
    simple: bool


DETAILED = Panel(
    width=440,
    height=520,
    rise=0.3,
    depth=(92, -42),
    inset=(60, 2),
    caps=(0.64, 0.30, 0.50, 0.14),
    colors=(CORAL, AMBER, SKY, MASTER_CAP[:3]),
    cap_size=(0.88, 0.14),
    cap_depth=0.42,
    slot_width=18,
    slot_span=(0.25, 0.93),
    simple=False,
)
SIMPLE = Panel(
    width=580,
    height=600,
    rise=0.26,
    depth=(72, -34),
    inset=(40, 0),
    caps=(0.66, 0.22, 0.48),
    colors=(CORAL, AMBER, SKY),
    cap_size=(0.86, 0.13),
    cap_depth=0.4,
    slot_width=40,
    slot_span=(0.08, 0.93),
    simple=True,
)


def mix(a: Sequence[int], b: Sequence[int], t: float) -> tuple[int, int, int]:
    """The color t of the way from a to b."""
    return (
        int(a[0] + (b[0] - a[0]) * t),
        int(a[1] + (b[1] - a[1]) * t),
        int(a[2] + (b[2] - a[2]) * t),
    )


def badge() -> Image.Image:
    """The disc: lighter at the hot spot behind the panel, violet, deeper toward the edge."""
    image = Image.new("RGBA", (S, S), (*DEEP, 255))
    draw = ImageDraw.Draw(image)
    # Up and to the left of the panel's middle, so the glow shows around its top-left corner.
    hot = (CENTER[0] - 70, CENTER[1] - 130)
    steps = 160
    for step in range(steps, 0, -1):
        t = step / steps
        r = int(RADIUS * 1.3 * t)
        if t < 0.3:
            color = mix(HOT, LIGHT, t / 0.3)
        elif t < 0.6:
            color = mix(LIGHT, MID, (t - 0.3) / 0.3)
        else:
            color = mix(MID, DEEP, ((t - 0.6) / 0.4) ** 2)
        draw.ellipse((hot[0] - r, hot[1] - r, hot[0] + r, hot[1] + r), fill=(*color, 255))
    return image.filter(ImageFilter.GaussianBlur(6))


def mask_of(points: Sequence[Point]) -> Image.Image:
    """A polygon as a mask."""
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).polygon([(round(x), round(y)) for x, y in points], fill=255)
    return mask


def fade(top: float, bottom: float) -> Image.Image:
    """A mask that is full at top and fades out by bottom."""
    span = max(1.0, bottom - top)
    column = Image.new("L", (1, S))
    column.putdata([max(0, min(255, int(255 * (1 - (y - top) / span)))) for y in range(S)])
    return column.resize((S, S), Image.Resampling.NEAREST)


def rim(points: Sequence[Point], width: int, reach: float = 0.55, strength: float = 1.0) -> Image.Image:
    """The part of a shape's outline that the light from behind catches: its top, fading downward."""
    shape = mask_of(points)
    edge = ImageChops.subtract(shape, shape.filter(ImageFilter.MinFilter(width)))
    ys = [y for _, y in points]
    top, bottom = min(ys), max(ys)
    light = ImageChops.multiply(edge, fade(top, top + (bottom - top) * reach))
    if strength < 1:
        light = light.point(lambda v: int(v * strength))
    image = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    image.paste(RIM, (0, 0), light)
    return image


def fill(points: Sequence[Point], color: Color) -> Image.Image:
    """A polygon filled with color, on a transparent layer."""
    image = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    image.paste(color, (0, 0), mask_of(points))
    return image


def shifted(points: Sequence[Point], by: Point) -> list[Point]:
    """The points moved by the given offset."""
    return [(x + by[0], y + by[1]) for x, y in points]


def bbox(points: Sequence[Point]) -> tuple[float, float, float, float]:
    """The bounding box of points as (left, top, right, bottom)."""
    xs = [x for x, _ in points]
    ys = [y for _, y in points]
    return min(xs), min(ys), max(xs), max(ys)


def face_mapping(panel: Panel, origin: Point) -> PointAt:
    """Maps coordinates on the front face (x along the top edge, y down the face) to the canvas."""

    def at(x: float, y: float) -> Point:
        return (origin[0] + x, origin[1] + x * panel.rise + y)

    return at


def quad(at: PointAt, x0: float, y0: float, x1: float, y1: float) -> list[Point]:
    """The rectangle (x0, y0)-(x1, y1) on a face, as canvas points."""
    return [at(x0, y0), at(x1, y0), at(x1, y1), at(x0, y1)]


def channels(panel: Panel) -> tuple[float, list[float]]:
    """The width of a fader channel and the x of each channel's center on the face."""
    left, right = panel.inset
    width = (panel.width - left - right) / len(panel.caps)
    return width, [left + width * (index + 0.5) for index in range(len(panel.caps))]


def cap(at: PointAt, panel: Panel, cx: float, cy: float, color: tuple[int, int, int]) -> list[Image.Image]:
    """A fader cap standing out of the face at (cx, cy): a block with a front, a top and a right side."""
    w = channels(panel)[0] * panel.cap_size[0]
    h = panel.height * panel.cap_size[1]
    out = (-panel.depth[0] * panel.cap_depth, -panel.depth[1] * panel.cap_depth)
    base = quad(at, cx - w / 2, cy - h / 2, cx + w / 2, cy + h / 2)
    front = shifted(base, out)
    top = [front[0], base[0], base[1], front[1]]
    side = [front[1], base[1], base[2], front[2]]
    dark = (*mix(color, (0, 0, 0), 0.55), 255)
    # The top catches the light from behind; the grey master cap only a little, to stay dark.
    lit = (*mix(color, (255, 255, 255), 0.55 if color in ACCENTS else 0.2), 255)
    layers = [fill(side, dark), fill(top, lit), fill(front, (*color, 255))]
    if not panel.simple:
        # The grip: a dark line across the cap, as on a real fader.
        gw = 0.1 * h
        grip = shifted(quad(at, cx - w / 2, cy - gw / 2, cx + w / 2, cy + gw / 2), out)
        layers.append(fill(grip, (*mix(color, (0, 0, 0), 0.7), 255)))
    layers.append(rim(front, 9 if panel.simple else 5, reach=0.5, strength=0.8))
    return layers


def knob(at: PointAt, panel: Panel, cx: float, cy: float, r: float, angle: float) -> list[Image.Image]:
    """A short round knob standing out of the face at (cx, cy), turned to angle (radians from straight up)."""
    out = (-panel.depth[0] * KNOB_HEIGHT, -panel.depth[1] * KNOB_HEIGHT)
    base = [at(cx + r * c, cy + r * s) for c, s in circle(48)]
    top = shifted(base, out)
    # The cylinder's side: the base disc swept out to the top disc.
    steps = 12
    side = [fill(shifted(base, (out[0] * k / steps, out[1] * k / steps)), SIDE) for k in range(steps)]
    tip = at(cx + r * 0.8 * math.sin(angle), cy - r * 0.8 * math.cos(angle))
    pointer = shifted([at(cx, cy), tip], out)
    return [*side, fill(top, KNOB), line(pointer, 6, KNOB_MARK), rim(top, 5, reach=0.5, strength=0.75)]


def fader_bank(panel: Panel) -> list[Image.Image]:
    """The panel with its slots, caps, scale ticks and (in the detailed drawing) a row of knobs."""
    at0 = face_mapping(panel, (0, 0))
    corners = quad(at0, 0, 0, panel.width, panel.height)
    back = shifted(corners, panel.depth)
    outline = [corners[0], back[0], back[1], back[2], corners[2], corners[3]]
    left, top, right, bottom = bbox(outline)
    # Centered on the slab, a little low so the caps' tops don't crowd the badge's rim.
    origin = (CENTER[0] - (left + right) / 2, CENTER[1] - (top + bottom) / 2 + 6)
    at = face_mapping(panel, origin)
    corners = quad(at, 0, 0, panel.width, panel.height)
    back = shifted(corners, panel.depth)
    front = corners
    side = [corners[1], back[1], back[2], corners[2]]
    top_face = [corners[0], back[0], back[1], corners[1]]
    outline = [corners[0], back[0], back[1], back[2], corners[2], corners[3]]

    layers = [
        fill(front, FRONT),
        fill(side, SIDE),
        fill(top_face, SMALL_TOP if panel.simple else TOP),
    ]
    count = len(panel.caps)
    channel, centers = channels(panel)
    y0, y1 = (panel.height * share for share in panel.slot_span)
    sw = panel.slot_width
    for index, cx in enumerate(centers):
        if not panel.simple:
            # The scale: a few short ticks to the right of the slot.
            for step in range(TICKS):
                ty = y0 + 18 + (y1 - y0 - 36) * step / (TICKS - 1)
                length = 24 if step in (0, TICKS - 1) else 14
                tick = quad(at, cx + sw / 2 + 10, ty - 3, cx + sw / 2 + 10 + length, ty + 3)
                layers.append(fill(tick, TICK))
            layers.extend(knob(at, panel, cx, panel.height * 0.12, channel * 0.25, -0.7 + 0.45 * index))
        if panel.simple:
            layers.append(fill(quad(at, cx - sw / 2, y0, cx + sw / 2, y1), SMALL_SLOT))
        else:
            layers.append(fill(quad(at, cx - sw / 2 - 6, y0 - 6, cx + sw / 2 + 6, y1 + 6), SLOT_EDGE))
            layers.append(fill(quad(at, cx - sw / 2, y0, cx + sw / 2, y1), SLOT))

    layers.append(rim(outline, 17 if panel.simple else 11, reach=0.5))
    layers.append(rim(top_face, 9 if panel.simple else 5, reach=1.0, strength=0.6))
    # Caps last, from the back row forward, so a lower cap overlaps a higher one.
    order = sorted(range(count), key=lambda i: panel.caps[i])
    for index in order:
        cx = centers[index]
        cy = y0 + (y1 - y0) * panel.caps[index]
        layers.extend(cap(at, panel, cx, cy, panel.colors[index]))
    return layers


def circle(steps: int) -> list[tuple[float, float]]:
    """Points around the unit circle as (cos, sin)."""
    return [(math.cos(math.tau * k / steps), math.sin(math.tau * k / steps)) for k in range(steps)]


def line(points: Sequence[Point], width: int, color: Color) -> Image.Image:
    """A line through points on a transparent layer."""
    image = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(image).line([(round(x), round(y)) for x, y in points], fill=color, width=width)
    return image


def icon(simple: bool) -> Image.Image:
    """The whole round icon at 1024 px."""
    image = badge()
    for part in fader_bank(SIMPLE if simple else DETAILED):
        image = Image.alpha_composite(image, part)
    # A slightly darker edge, so the circle reads on light backgrounds too.
    ring = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    ImageDraw.Draw(ring).ellipse(
        (CENTER[0] - RADIUS, CENTER[1] - RADIUS, CENTER[0] + RADIUS, CENTER[1] + RADIUS),
        outline=RING,
        width=14,
    )
    image = Image.alpha_composite(image, ring)
    mask = Image.new("L", (S, S), 0)
    ImageDraw.Draw(mask).ellipse(
        (CENTER[0] - RADIUS, CENTER[1] - RADIUS, CENTER[0] + RADIUS, CENTER[1] + RADIUS), fill=255
    )
    image.putalpha(ImageChops.multiply(image.getchannel("A"), mask))
    return image


def preview(detailed: Image.Image, simple: Image.Image) -> Image.Image:
    """A sheet of the preview sizes on a light row and a dark row."""
    gap = 30
    width = gap + sum(size + gap for size in PREVIEW_SIZES)
    row = README_SIZE + 2 * gap
    sheet = Image.new("RGBA", (width, 2 * row), LIGHT_BG)
    sheet.paste(DARK_BG, (0, row, width, 2 * row))
    for top in (0, row):
        x = gap
        for size in PREVIEW_SIZES:
            image = (simple if size <= SMALL else detailed).resize((size, size), Image.Resampling.LANCZOS)
            sheet.alpha_composite(image, (x, top + gap + README_SIZE - size))
            x += size + gap
    return sheet


def main() -> None:
    parser = argparse.ArgumentParser(description="Draws Mikser Studio's icon into docs/images/icon.png.")
    parser.add_argument("--preview", type=Path, metavar="FILE", help="also write a sheet of the sizes to FILE")
    args = parser.parse_args()
    detailed = icon(simple=False)
    detailed.resize((README_SIZE, README_SIZE), Image.Resampling.LANCZOS).save(README_ICON, optimize=True)
    print(f"Wrote {README_ICON}")
    if args.preview:
        preview(detailed, icon(simple=True)).save(args.preview, optimize=True)
        print(f"Wrote {args.preview}")


if __name__ == "__main__":
    main()
