#!/usr/bin/env python3
"""make_skin.py - give F4 Phantom its hand-tuned MPC skin (pizzazz).

The generated screen (tools/gen_ui.py from ui.json) is plain. This script copies it to skins/ and adds two things:
  * bg.png       the decal (shield + pixel ghost) as a faint background on every page (1280 x 628, the tab size).
  * ghost_dial   the CUTOFF control becomes the Phantom itself: a 90 x 90 film strip (127 frames) where the pixel ghost's
                 glow brightens, its eyes slide from left to right and it rises a little as the filter opens, inside the
                 usual cyan value arc. The ghost's pixels come from the decal SVG, so the two always match.
A TOUCHSCREEN skin: nothing here is animated by the plugin (the MPC UI format has no meter / animation channel, and a
plugin pushing parameter values from the audio thread would flood project automation). The ghost reacts because YOU turn
the dial.

TUI.json is patched as TEXT: it contains duplicate "version" keys (docs/ui-reference.md 1.8) that Python's json module
would destroy.

Pipeline (run from the SDK root; <P> = this project folder):
    1. delete <P>/skins                       (so build.py runs the generator)
    2. python tools/build.py "<P>"
    3. python "<P>/tools/make_skin.py" "<P>"  (writes <P>/skins)
    4. python tools/build.py "<P>"            (now copies skins/ verbatim)
If ui.json changes, repeat from step 1.
"""
import os
import re
import shutil
import sys

from PIL import Image, ImageDraw, ImageFilter

FRAMES, FS = 127, 90
CYAN = (0, 242, 255)
SS = 4


def ghost_rects(project):
    """Body pixels of the ghost, from the decal SVG (class 'pix' rects that are not the trail)."""
    svg = open(os.path.join(project, "assets", "F4 Phantom decal.svg"), encoding="utf-8").read()
    rects = []
    for m in re.finditer(r'<rect class="pix(?: \w+)?"\s+x="(\d+)"\s+y="(\d+)"\s+width="26"', svg):
        rects.append((int(m.group(1)), int(m.group(2))))
    return rects


def ghost_frame(norm, rects):
    w = FS * SS
    c = w / 2
    img = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    glow = Image.new("RGBA", (w, w), (0, 0, 0, 0))
    ImageDraw.Draw(glow).ellipse((c - 32 * SS, c - 32 * SS, c + 32 * SS, c + 32 * SS), fill=CYAN + (int(25 + 140 * norm),))
    img.alpha_composite(glow.filter(ImageFilter.GaussianBlur(7 * SS)))
    d = ImageDraw.Draw(img)
    bb = (c - 42 * SS, c - 42 * SS, c + 42 * SS, c + 42 * SS)
    d.arc(bb, 135, 405, fill=(60, 60, 64, 255), width=int(5 * SS))
    if norm > 0.003:
        d.arc(bb, 135, 135 + 270 * norm, fill=CYAN + (255,), width=int(5 * SS))
    # the ghost: 282 x 282 source units -> 50 px; it rises up to 3 px as the filter opens
    k = 50.0 * SS / 282.0
    ox = c - (40 + 282 / 2.0) * k + 0.0          # body spans x 40..322 -> centre it
    oy = c - 141 * k - 3 * SS * norm
    bright = int(150 + 105 * norm)
    for (x, y) in rects:
        d.rectangle((ox + x * k, oy + y * k, ox + (x + 26) * k - 0.6 * SS, oy + (y + 26) * k - 0.6 * SS), fill=(CYAN[0], CYAN[1], bright, 255))
    # eyes: bright pixels in the head, sliding left -> right with the cutoff
    ex = (norm - 0.5) * 28.0
    for exx in (128, 200):
        x0 = ox + (exx + ex) * k
        y0 = oy + 96 * k
        d.rectangle((x0, y0, x0 + 20 * k, y0 + 20 * k), fill=(255, 255, 255, 255))
    return img.resize((FS, FS), Image.LANCZOS)


def make_background(project):
    bg = Image.new("RGBA", (1280, 628), (10, 10, 10, 255))
    decal = Image.open(os.path.join(project, "tools", "decal.png")).convert("RGBA")
    size = 560
    decal = decal.resize((size, size), Image.LANCZOS)
    decal.putalpha(decal.split()[3].point(lambda v: int(v * 0.07)))
    bg.alpha_composite(decal, (640 - size // 2, 314 - size // 2))
    return bg


def match_brace(t, start):
    depth = 0
    for i in range(start, len(t)):
        if t[i] == '{': depth += 1
        elif t[i] == '}':
            depth -= 1
            if depth == 0: return i + 1
    raise ValueError("unbalanced braces")


def main():
    project = os.path.abspath(sys.argv[1])
    build = os.path.join(project, "build")
    src = None
    for d in os.listdir(build):
        cand = os.path.join(build, d, "Plugin Skins")
        if os.path.isdir(cand): src = cand
    if not src: sys.exit("no generated Plugin Skins under build/ - run tools/build.py first (with skins/ removed)")
    out = os.path.join(project, "skins")
    if os.path.isdir(out): shutil.rmtree(out)
    shutil.copytree(src, out)
    for f in os.listdir(out):
        if f.lower().endswith(".svg"): os.remove(os.path.join(out, f))

    rects = ghost_rects(project)
    if len(rects) < 40: sys.exit("could not read the ghost pixels from the decal SVG (%d found)" % len(rects))
    strip = Image.new("RGBA", (FS, FS * FRAMES), (0, 0, 0, 0))
    for f in range(FRAMES):
        strip.paste(ghost_frame(f / (FRAMES - 1), rects), (0, f * FS))
    strip.save(os.path.join(out, "ghost_dial.png"), optimize=True)
    make_background(project).convert("RGB").save(os.path.join(out, "bg.png"), optimize=True)

    p = os.path.join(out, "TUI.json")
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import skin_fix as sf
    root = sf.load(p)
    n_bg = sf.add_background(root, "bg.png")
    n_dial = sf.clone_knob_def(root, "knobGhost", "ghost_dial.png", "Cutoff")
    n_menu = sf.fix_menus(root)                                   # empty native pick-lists -> ValueSlider value fields
    made = sf.make_translucent_segments(out, (0, 242, 255))            # captions must not sit on top of buttons
    n_img, n_move = sf.fix_segments(root)
    sf.save(root, p)
    print("skin written to", out, "| backgrounds:", n_bg, "| Cutoff dial:", n_dial, "| menus -> value fields:", n_menu,
          "| segment images:", len(made), "| captions moved under buttons:", n_move)


if __name__ == "__main__":
    main()
