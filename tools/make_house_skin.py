#!/usr/bin/env python3
"""make_house_skin.py - give F4 Phantom the Lumina Aerospace house skin.

The SDK's own control kit is a set of red plastic knobs. The house look is different: vantablack surfaces,
hairline rules and bright neon. So this script draws its own controls in that language and writes them into
the plugin's skins/ folder, where tools/build.py picks them up verbatim.

WHAT IT MAKES
  knob_uni.png / knob_bi.png      90 x 90, 127 frames   a neon arc and a pointer on a dark disc
  fader_h_uni.png / fader_h_bi.png 248 x 36, 127 frames   a hairline track with a glowing cap
  toggle_on.png / toggle_off.png  64 x 32                a pill that lights up
  seg4_on/off.png, segt4_on/off   58 x 40                a segment button that lights up
  bg.png                          1280 x 628             the panel behind everything

THE DESIGN TOKENS (the house rules)
  surface   #05070A   vantablack with a blue cast: no glare in a dark studio
  panel     #0A0E13   the card a control sits on
  rule      #16222B   hairlines, never brighter than they have to be
  cyan      #00F2FF   the primary: values and pointers
  magenta   #FF2D95   the secondary: the far corner of the panel
  amber     #FFB020   the warm one, used for the Vulcan's page
  Text is not drawn here: the MPC draws the captions itself, in its own font.

FRAME ORDER: frame 0 is the minimum and it is at the TOP of the strip (the SDK's rule). The bipolar strip has
its centre at frame 63 of 127.

PIPELINE (run from the SDK root; <P> = this project folder):
    1. delete <P>/skins                       (so build.py runs the generator)
    2. python tools/build.py "<P>"
    3. python "<P>/tools/make_skin.py" "<P>" (writes <P>/skins: the decal background and the ghost dial)
    4. python "<P>/tools/make_house_skin.py" "<P>"   (this file: the house controls)
    5. python tools/build.py "<P>"            (now copies skins/ verbatim)
If ui.json changes, repeat from step 1.
"""
import math
import os
import sys

from PIL import Image, ImageDraw, ImageFilter

FRAMES = 127
KNOB = 90
FADER_W, FADER_H = 248, 36
TOGGLE_W, TOGGLE_H = 64, 32
SEG_W, SEG_H = 58, 40

SURFACE = (5, 7, 10, 255)
PANEL = (10, 14, 19, 255)
RULE = (22, 34, 43, 255)
CYAN = (0, 242, 255, 255)
MAGENTA = (255, 45, 149, 255)
AMBER = (255, 176, 32, 255)
TEXT = (200, 212, 220, 255)

ARC_START = 135.0      # degrees, the SDK's knob sweep: lower left, over the top, to the lower right
ARC_SWEEP = 270.0


def polar(cx, cy, r, deg):
    """angle in degrees, 0 = east, growing clockwise (screen coordinates)"""
    a = math.radians(deg)
    return cx + r * math.cos(a), cy + r * math.sin(a)


def glow_layer(draw_fn, blur, size):
    """draw into a transparent layer, blur it, and return it (the caller composites it under the crisp art)"""
    lay = Image.new("RGBA", size, (0, 0, 0, 0))
    draw_fn(ImageDraw.Draw(lay))
    return lay.filter(ImageFilter.GaussianBlur(blur))


SS = 4               # the controls are drawn four times oversize and scaled down: smooth arcs, no jaggies


def knob_frame(norm, bipolar, accent):
    """one frame of the knob: a dark disc, a hairline track, a neon arc and a pointer"""
    S = KNOB * SS
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    emis = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    di = ImageDraw.Draw(img)
    de = ImageDraw.Draw(emis)
    cx = cy = S / 2.0
    r = S / 2.0 - 4.0 * SS

    # the disc: a very dark card with a hairline edge, so it reads as a physical surface
    di.ellipse((cx - r, cy - r, cx + r, cy + r), fill=PANEL, outline=RULE, width=SS)

    track_r = r - 4.0 * SS
    bbox = (cx - track_r, cy - track_r, cx + track_r, cy + track_r)
    # the track: the whole sweep in a hairline
    di.arc(bbox, ARC_START, ARC_START + ARC_SWEEP, fill=RULE, width=SS)

    if bipolar:
        # the arc grows from the centre: up for a positive value, down for a negative one
        mid = ARC_START + ARC_SWEEP * 0.5
        a = mid + (norm - 0.5) * ARC_SWEEP * 0.5
        a0, a1 = (mid, a) if norm >= 0.5 else (a, mid)
    else:
        a0 = ARC_START
        a1 = ARC_START + ARC_SWEEP * norm

    if abs(a1 - a0) > 0.4:
        di.arc(bbox, a0, a1, fill=accent, width=2 * SS)
        de.arc(bbox, a0, a1, fill=accent, width=2 * SS)

    # the pointer: a bright line from the hub out to the arc
    x0, y0 = polar(cx, cy, r * 0.22, a1)
    x1, y1 = polar(cx, cy, track_r, a1)
    for d in (di, de):
        d.line((x0, y0, x1, y1), fill=accent, width=2 * SS)
    di.ellipse((cx - 3 * SS, cy - 3 * SS, cx + 3 * SS, cy + 3 * SS), fill=accent)
    de.ellipse((cx - 3 * SS, cy - 3 * SS, cx + 3 * SS, cy + 3 * SS), fill=accent)

    # the glow sits under the crisp art, so it bleeds outwards without blurring anything
    glow = emis.filter(ImageFilter.GaussianBlur(2.5 * SS))
    out = Image.alpha_composite(glow, img)
    return out.resize((KNOB, KNOB), Image.LANCZOS)


def make_knobs(path_uni, path_bi):
    for bipolar, path in ((False, path_uni), (True, path_bi)):
        strip = Image.new("RGBA", (KNOB, KNOB * FRAMES), (0, 0, 0, 0))
        for f in range(FRAMES):
            norm = f / (FRAMES - 1.0)
            strip.paste(knob_frame(norm, bipolar, CYAN), (0, f * KNOB))
        strip.save(path, optimize=True)


def fader_frame(norm, bipolar, accent):
    w, h = FADER_W * SS, FADER_H * SS
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    emis = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    di = ImageDraw.Draw(img)
    de = ImageDraw.Draw(emis)
    track_y = h // 2
    di.rectangle((6 * SS, track_y - SS, w - 6 * SS, track_y + SS), fill=RULE)
    if bipolar:
        mid = w // 2
        x = int(mid + (norm - 0.5) * (w - 24 * SS))
        a, b = (mid, x) if norm >= 0.5 else (x, mid)
    else:
        a, b = 6 * SS, int(6 * SS + norm * (w - 12 * SS))
    if b > a:
        di.rectangle((a, track_y - SS, b, track_y + SS), fill=accent)

    # the cap: a bright bar with a dark core
    cap_x = b if not bipolar else x
    for d in (di, de):
        d.rectangle((cap_x - 2 * SS, 3 * SS, cap_x + 2 * SS, h - 3 * SS), fill=accent)
    di.rectangle((cap_x - SS, 5 * SS, cap_x + SS, h - 5 * SS), fill=SURFACE)
    glow = emis.filter(ImageFilter.GaussianBlur(3.0 * SS))
    out = Image.alpha_composite(glow, img)
    return out.resize((FADER_W, FADER_H), Image.LANCZOS)


def make_faders(path_uni, path_bi):
    for bipolar, path in ((False, path_uni), (True, path_bi)):
        strip = Image.new("RGBA", (FADER_W, FADER_H * FRAMES), (0, 0, 0, 0))
        for f in range(FRAMES):
            strip.paste(fader_frame(f / (FRAMES - 1.0), bipolar, CYAN), (0, f * FADER_H))
        strip.save(path, optimize=True)


def make_toggle(on_path, off_path):
    for on, path in ((True, on_path), (False, off_path)):
        w, h = TOGGLE_W * SS, TOGGLE_H * SS
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        box = (2 * SS, 4 * SS, w - 3 * SS, h - 5 * SS)
        if on:
            def draw(dd, colour):
                dd.rounded_rectangle(box, radius=10 * SS, fill=colour)
            img.alpha_composite(glow_layer(lambda dd: draw(dd, CYAN), 3 * SS, (w, h)))
            draw(d, (0, 120, 132, 255))
            d.ellipse((w - 20 * SS, 9 * SS, w - 8 * SS, h - 10 * SS), fill=CYAN)
        else:
            d.rounded_rectangle(box, radius=10 * SS, outline=RULE, width=SS, fill=(10, 14, 19, 255))
            d.ellipse((w - 20 * SS, 9 * SS, w - 8 * SS, h - 10 * SS), fill=(44, 58, 68, 255))
        img.resize((TOGGLE_W, TOGGLE_H), Image.LANCZOS).save(path, optimize=True)


def make_segment(on_path, off_path):
    for on, path in ((True, on_path), (False, off_path)):
        w, h = SEG_W * SS, SEG_H * SS
        img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        d = ImageDraw.Draw(img)
        box = (SS, SS, w - 2 * SS, h - 2 * SS)
        if on:
            def draw(dd, colour):
                dd.rectangle(box, fill=colour)
            img.alpha_composite(glow_layer(lambda dd: draw(dd, CYAN), 3 * SS, (w, h)))
            d.rectangle(box, fill=(0, 90, 100, 255))
            d.rectangle((SS, SS, w - 2 * SS, 2 * SS), fill=CYAN)
        else:
            d.rectangle(box, fill=(10, 14, 19, 255), outline=RULE, width=SS)
            d.rectangle((SS, SS, w - 2 * SS, 2 * SS), fill=(30, 46, 56, 255))
        img.resize((SEG_W, SEG_H), Image.LANCZOS).save(path, optimize=True)


def make_background(project):
    w, h = 1280, 628
    img = Image.new("RGBA", (w, h), SURFACE)

    # a fine grid: 40 px, one pixel, barely there
    grid = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    gd = ImageDraw.Draw(grid)
    for x in range(0, w, 40):
        gd.line((x, 0, x, h), fill=(14, 24, 31, 255))
    for y in range(0, h, 40):
        gd.line((0, y, w, y), fill=(14, 24, 31, 255))
    img.alpha_composite(grid)

    # two soft corner glows: cyan on the right, magenta on the left
    def corner(centre, colour, radius):
        lay = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        ImageDraw.Draw(lay).ellipse(
            (centre[0] - radius, centre[1] - radius, centre[0] + radius, centre[1] + radius),
            fill=(colour[0], colour[1], colour[2], 46))
        return lay.filter(ImageFilter.GaussianBlur(90))

    img.alpha_composite(corner((w - 120, h - 60), CYAN, 320))
    img.alpha_composite(corner((120, 60), MAGENTA, 280))

    # the decal, as a ghost in the middle
    decal_path = os.path.join(project, "tools", "decal.png")
    if os.path.exists(decal_path):
        decal = Image.open(decal_path).convert("RGBA")
        size = 520
        decal = decal.resize((size, size), Image.LANCZOS)
        alpha = decal.split()[3].point(lambda v: int(v * 0.10))
        decal.putalpha(alpha)
        img.alpha_composite(decal, (w // 2 - size // 2, h // 2 - size // 2))

    # a horizon line with a glow, below the last row of controls so it never crosses a caption
    def horizon(dd, colour, width):
        dd.line((0, h - 16, w, h - 16), fill=colour, width=width)
    img.alpha_composite(glow_layer(lambda dd: horizon(dd, (0, 242, 255, 70), 2), 6, (w, h)))
    horizon(ImageDraw.Draw(img), (0, 120, 132, 255), 1)

    img.convert("RGB").save(os.path.join(project, "skins", "bg.png"), optimize=True)


def main():
    project = os.path.abspath(sys.argv[1])
    skins = os.path.join(project, "skins")
    if not os.path.isdir(skins):
        sys.exit("no skins/ folder - run tools/make_skin.py first")
    make_knobs(os.path.join(skins, "knob_uni.png"), os.path.join(skins, "knob_bi.png"))
    make_faders(os.path.join(skins, "fader_h_uni.png"), os.path.join(skins, "fader_h_bi.png"))
    make_toggle(os.path.join(skins, "toggle_on.png"), os.path.join(skins, "toggle_off.png"))
    make_segment(os.path.join(skins, "seg4_on.png"), os.path.join(skins, "seg4_off.png"))
    make_segment(os.path.join(skins, "segt4_on.png"), os.path.join(skins, "segt4_off.png"))
    make_background(project)
    print("house skin written to", skins)


if __name__ == "__main__":
    main()
