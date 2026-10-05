#!/usr/bin/env python3
"""Generates the bundled file-type icons in data/icons/files/ (§4.3).

Usage: scripts/make-file-icons.py [output-dir]

The SVGs are committed; this is how they are made, so that a change to the
shared geometry -- the page, its fold, the alphas -- lands on every icon at once
instead of drifting across twenty hand-edited files.

The set is drawn for one job: being tinted to a single colour by IconProvider
through CompositionMode_SourceIn, at 16px in a row and 32px on a 2x display.
That decides every rule here:

  * Depth comes from alpha, never from colour. SourceIn keeps each pixel's
    alpha and replaces its colour, so a body at BODY alpha, a secondary panel
    at MID and a glyph at full alpha survive any tint as three tones of it.
  * Everything sits on a 16px grid, outlines are 1px on half-pixel centres and
    fills on whole pixels, so 16px is pixel-exact and 32px is exactly double.
    A 1.5px stroke would look finer on paper and blur into two grey pixels in
    the row.
  * Documents share one page; media and containers get their own silhouette
    (frame, disc, box, cylinder), as Adwaita does. A row of identical pages
    told apart only by an 8px glyph is a row of identical pages at a glance.
  * The folder is solid, because it is the entry a listing is navigated by
    and should read first; its front panel is a lighter alpha than the tab.
"""
import os
import sys

OUT = sys.argv[1] if len(sys.argv) > 1 else os.path.join(
    os.path.dirname(os.path.abspath(__file__)), "..", "data", "icons", "files")
# Alphas for the page or frame body and for secondary panels (fold, lid, tab).
BODY = ".2"
MID = ".5"

def svg(*parts):
    return ('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16" width="16" height="16">\n'
            + "\n".join("  " + p for p in parts) + "\n</svg>\n")

def fill(d, a="1"):
    o = "" if a == "1" else f' fill-opacity="{a}"'
    return f'<path d="{d}" fill="currentColor"{o}/>'

def stroke(d, w="1", cap="butt", join="round", a="1"):
    o = "" if a == "1" else f' stroke-opacity="{a}"'
    return (f'<path d="{d}" fill="none" stroke="currentColor" stroke-width="{w}" '
            f'stroke-linecap="{cap}" stroke-linejoin="{join}"{o}/>')

def rect(x, y, w, h, a="1", rx=None):
    r = f' rx="{rx}"' if rx else ""
    o = "" if a == "1" else f' fill-opacity="{a}"'
    return f'<rect x="{x}" y="{y}" width="{w}" height="{h}"{r} fill="currentColor"{o}/>'

# The page: 12x14 outer (x 2..14, y 1..15), fold 4px at the top right.
PAGE = "M3.5 1.5H9.5L13.5 5.5V13.5A1 1 0 0 1 12.5 14.5H3.5A1 1 0 0 1 2.5 13.5V2.5A1 1 0 0 1 3.5 1.5Z"
FOLD_FILL = "M9.5 1.5V4.5A1 1 0 0 0 10.5 5.5H13.5Z"
FOLD_LINE = "M9.5 1.5V4.5A1 1 0 0 0 10.5 5.5H13.5"

def page(*glyph):
    return svg(fill(PAGE, BODY), fill(FOLD_FILL, MID), stroke(PAGE), stroke(FOLD_LINE), *glyph)

# A landscape frame: x 1..15, y 2.5..13.5 -> outline on 1.5..14.5, 2.5..13.5
FRAME = "M2.5 2.5H13.5A1 1 0 0 1 14.5 3.5V12.5A1 1 0 0 1 13.5 13.5H2.5A1 1 0 0 1 1.5 12.5V3.5A1 1 0 0 1 2.5 2.5Z"

icons = {}

# --- Documents: the page, with the type said by the glyph inside it. ---

icons["file"] = page()

# Ragged lines. "document" adds a heading block, which is the difference
# between a .txt and something with formatting.
icons["text"] = page(rect(5, 7, 6, 1), rect(5, 9, 6, 1), rect(5, 11, 4, 1))

icons["document"] = page(rect(5, 4, 3, 2), rect(5, 7, 6, 1), rect(5, 9, 6, 1), rect(5, 11, 6, 1))

icons["code"] = page(stroke("M6.5 7.5L4.75 9.75L6.5 12", w="1.25", cap="round"),
                     stroke("M9.5 7.5L11.25 9.75L9.5 12", w="1.25", cap="round"))

icons["markdown"] = page(stroke("M4.5 12.5V7.5L6 9.5L7.5 7.5V12.5", w="1", cap="butt", join="miter"),
                         rect(10, 7, 1, 3),
                         fill("M8.5 10H12.5L10.5 12.5Z"))

# A solid label hanging off the page: the convention for "a fixed, printed
# format", and the one shape here that no other type uses.
icons["pdf"] = page(rect(1, 8, 10, 5, rx="1"))

icons["spreadsheet"] = page(rect(4, 6, 8, 2), stroke("M4.5 6.5H11.5V12.5H4.5Z"),
                            rect(7, 8, 1, 4), rect(4, 10, 8, 1))

icons["presentation"] = page(fill("M4.5 6.5H11.5V10.5H4.5Z", MID), stroke("M4.5 6.5H11.5V10.5H4.5Z"),
                             rect(7.5, 10.5, 1, 2), rect(6, 12, 4, 1))

# A zip pull down the page.
icons["archive"] = page(rect(7, 2, 1, 1), rect(8, 3, 1, 1), rect(7, 4, 1, 1), rect(8, 5, 1, 1),
                        rect(7, 6, 1, 1), rect(8, 7, 1, 1),
                        stroke("M6.5 8.5H9.5V11.5H6.5Z"), rect(7, 9, 2, 2, a=MID))

icons["font"] = page(stroke("M4.75 12.5L7.5 5.5H8.5L11.25 12.5", w="1.25", cap="butt", join="miter"),
                     rect(6, 10, 4, 1))

# Two sliders.
icons["config"] = page(rect(4, 7, 8, 1, a=MID), rect(8, 6, 2, 3, rx=".5"),
                       rect(4, 11, 8, 1, a=MID), rect(5, 10, 2, 3, rx=".5"))

icons["audio"] = page(stroke("M8.5 11.5V5.5L11 6.5", w="1"),
                      fill("M8.5 11.5A1.75 1.5 0 1 1 5 11.5A1.75 1.5 0 1 1 8.5 11.5Z"))

# --- Media and containers: their own silhouettes. ---

icons["image"] = svg(fill(FRAME, BODY), stroke(FRAME),
                     fill("M5.5 4.75A1.25 1.25 0 1 1 5.5 7.25A1.25 1.25 0 1 1 5.5 4.75Z"),
                     fill("M2 13V12L6 8L9 11L10.5 9.5L14 13Z"))

icons["video"] = svg(fill(FRAME, BODY), stroke(FRAME),
                     fill("M6.5 5.25V10.75L11 8Z"))

# A terminal window with a prompt: executables and scripts alike.
icons["executable"] = svg(fill(FRAME, BODY), fill("M2.5 2.5H13.5A1 1 0 0 1 14.5 3.5V5H1.5V3.5A1 1 0 0 1 2.5 2.5Z", MID),
                          stroke(FRAME), stroke("M1.5 5.5H14.5"),
                          stroke("M4.5 7.75L6.5 9.25L4.5 10.75", w="1.25", cap="round"),
                          rect(8, 10, 4, 1))

# A globe. A 1px line through the exact centre of a 16px grid straddles two
# pixel rows and renders as two half-grey ones, so the equator sits half a
# pixel low and the meridian's widest points land on pixel centres (5.5, 10.5).
icons["web"] = svg(fill("M8 1.5A6.5 6.5 0 1 1 8 14.5A6.5 6.5 0 1 1 8 1.5Z", BODY),
                   stroke("M8 1.5A6.5 6.5 0 1 1 8 14.5A6.5 6.5 0 1 1 8 1.5Z"),
                   stroke("M8 1.5C5.5 3.5 5.5 12.5 8 14.5C10.5 12.5 10.5 3.5 8 1.5Z"),
                   stroke("M1.5 8.5H14.5"))

icons["database"] = svg(fill("M2.5 4C2.5 2.5 13.5 2.5 13.5 4V12C13.5 13.5 2.5 13.5 2.5 12Z", BODY),
                        fill("M2.5 4C2.5 2.5 13.5 2.5 13.5 4C13.5 5.5 2.5 5.5 2.5 4Z", MID),
                        stroke("M2.5 4C2.5 2.5 13.5 2.5 13.5 4C13.5 5.5 2.5 5.5 2.5 4Z"),
                        stroke("M2.5 4V12C2.5 13.5 13.5 13.5 13.5 12V4"),
                        stroke("M2.5 8C2.5 9.5 13.5 9.5 13.5 8"))

# A disc. ISO, DMG and IMG files are images of one, and the shape says so
# more plainly than a drive would.
icons["disk-image"] = svg(fill("M8 1.5A6.5 6.5 0 1 1 8 14.5A6.5 6.5 0 1 1 8 1.5Z", BODY),
                          stroke("M8 1.5A6.5 6.5 0 1 1 8 14.5A6.5 6.5 0 1 1 8 1.5Z"),
                          stroke("M8 5.5A2.5 2.5 0 1 1 8 10.5A2.5 2.5 0 1 1 8 5.5Z", w="2"),
                          stroke("M4.2 6.3A4 4 0 0 1 6.3 4.2", w="1", cap="round", a=MID))

BOX_BODY = "M2.5 5.5H13.5V13.5A1 1 0 0 1 12.5 14.5H3.5A1 1 0 0 1 2.5 13.5Z"
BOX_LID = "M1.5 2.5A1 1 0 0 1 2.5 1.5H13.5A1 1 0 0 1 14.5 2.5V5.5H1.5Z"
# A taped parcel: deb, rpm, pkg, AppImage, Flatpak bundles. The tape is what
# keeps it from reading as the archive's filing box.
icons["package"] = svg(fill(BOX_BODY, BODY), fill(BOX_LID, MID), stroke(BOX_BODY), stroke(BOX_LID),
                       rect(7, 2, 2, 7))

# Folder: back (tab + rear strip) and front panel, no overlap.
FOLDER_BACK = "M1 3A1 1 0 0 1 2 2H5.6A1 1 0 0 1 6.3 2.3L7.4 3.4A.4 .4 0 0 0 7.7 3.5H14A1 1 0 0 1 15 4.5V6H1Z"
FOLDER_FRONT = "M1 6H15V13A1 1 0 0 1 14 14H2A1 1 0 0 1 1 13Z"
icons["folder"] = svg(fill(FOLDER_BACK, "1"), fill(FOLDER_FRONT, ".65"))

# The symlink badge, composited by IconProvider over whatever the link points
# at: link-cutout is erased from the base (a 1px halo around the arrow, so the
# arrow stays legible over any glyph), then link-badge is drawn.
LINK_SHAFT = "M1.5 14.5V13A2.5 2.5 0 0 1 4 10.5H6"
LINK_HEAD = "M5.5 8V13L8 10.5Z"
icons["link-cutout"] = svg(stroke("M1.5 16V13A2.5 2.5 0 0 1 4 10.5H6", w="3.5", cap="butt"),
                           stroke(LINK_HEAD, w="2", join="round"), fill(LINK_HEAD),
                           rect(0, 12, 4.5, 4))
icons["link-badge"] = svg(stroke(LINK_SHAFT, w="1.5", cap="butt"), fill(LINK_HEAD))

os.makedirs(OUT, exist_ok=True)
for name, content in icons.items():
    with open(os.path.join(OUT, name + ".svg"), "w", encoding="utf-8") as f:
        f.write(content)
print(f"{len(icons)} icons written to {os.path.normpath(OUT)}")
