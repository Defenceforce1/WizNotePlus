#!/usr/bin/env python3
"""Read-only acceptance for the fixed --paper-assets-base A4 fixture.

Not an arbitrary attachment, account, TeX or library acceptance test. Retains
exact PDF link names and measures painted ink, not merely SVG/DOM bounds.
"""
import argparse
import json
import statistics
import subprocess
import tempfile
from pathlib import Path

import pdfplumber
from PIL import Image
from pypdf import PdfReader

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--poppler", required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
failures, records = [], []
for viewport in (850, 1200):
    folder = args.directory / f"assets-a4-{viewport}"
    probe = json.loads((folder / "native-pdf-results.json").read_text())
    if probe.get("pageMode") != "a4" or probe.get("cssViewportWidth") != viewport:
        failures.append(folder.name + ": incorrect mode/viewport")
    if probe.get("failures") or probe.get("blockedRequests") or not all(probe.get(k) for k in ("sourceBefore", "sourceAfter", "htmlSame", "modifiedSame")):
        failures.append(folder.name + ": source/resource regression")
    if probe["referenceDom"]["jax"] != 6:
        failures.append(folder.name + ": unexpected source math count")
    required = sorted([link["raw"] for link in probe["referenceDom"]["links"]] + ["control-target"])
    for filename in ("references-5mm.pdf", "references-20mm.pdf", "references-visible-heading.pdf"):
        path = folder / filename
        reader = PdfReader(path)
        named = {str(k).removeprefix("/"): v for k, v in reader.named_destinations.items()}
        gotos, uris = [], []
        for page in reader.pages:
            for ref in page.get("/Annots", []):
                annotation = ref.get_object()
                action = annotation.get("/A", {})
                action = action.get_object() if hasattr(action, "get_object") else action
                target = action.get("/D") if action.get("/S") == "/GoTo" else annotation.get("/Dest")
                if target is not None:
                    gotos.append(str(target).removeprefix("/"))
                if action.get("/S") == "/URI" and str(action.get("/URI", "")).startswith("file:"):
                    uris.append(str(action.get("/URI")))
        if sorted(gotos) != required or any(k not in named for k in gotos) or uris:
            failures.append(str(path) + ": exact GoTo/resource link mismatch")
        width, height = float(reader.pages[0].mediabox.width), float(reader.pages[0].mediabox.height)
        expected_pages = 2 if filename == "references-20mm.pdf" else 1
        if len(reader.pages) != expected_pages or any(abs(float(p.mediabox.width) - 210 * 72 / 25.4) > 1 or abs(float(p.mediabox.height) - 297 * 72 / 25.4) > 1 for p in reader.pages):
            failures.append(str(path) + ": unexpected paper geometry/pagination")
        with pdfplumber.open(path) as pdf:
            text = "\n".join(p.extract_text() or "" for p in pdf.pages)
            compact = "".join(text.split())
            for value in ("FINAL PAPER RESOURCE MARKER", "$literal$", "\\begin{equation}code only\\end{equation}", "LOCAL IMAGE"):
                if "".join(value.split()) not in compact:
                    failures.append(str(path) + ": ordinary/code/table/image text incomplete")
            tables = [t for p in pdf.pages for t in p.extract_tables() if t and t[0][0] == "Long cell"]
            if len(tables) != 1 or len(tables[0]) != 2 or "".join(tables[0][1][0].split()) != "paper" * 40 or "".join(tables[0][1][1].split()) != "End":
                failures.append(str(path) + ": long table cell text incomplete")
            marker = pdf.pages[0].search("FINAL PAPER RESOURCE MARKER", regex=False)
            font = statistics.median(c["size"] for c in marker[0]["chars"]) if len(marker) == 1 else 0
            if abs(font - 12) > .02:
                failures.append(str(path) + ": page or ordinary text was scaled")
        with tempfile.TemporaryDirectory(prefix="wiz-paper-assets-") as temporary:
            prefix = Path(temporary) / "page"
            subprocess.run([args.poppler, "-f", "1", "-singlefile", "-scale-to", "3200", "-png", str(path), str(prefix)], check=True, capture_output=True)
            image = Image.open(prefix.with_suffix(".png")).convert("RGB")
            pixels = image.load()
            scale = image.height / height
            magenta, purple, red, wide_tag = [], [], [], []
            number_rows = []
            # Fixed fixture order: code, three displays/four tags, theorem,
            # image and table. The independently painted blue block bounds
            # the number scan, excluding later right-aligned ordinary text.
            theorem_top = height - float(named["wiz-math-thm:paper"]["/Top"])
            first_top = height - float(named["mjx-eqn-eq%3Apaper"]["/Top"])
            # This reader fixture has 48 CSS px (=36pt) body margins. PDF
            # destinations intentionally use the block's left edge, not the
            # tag's x; derive a separate painted-number region from paper CSS.
            margin = 20 if filename == "references-20mm.pdf" else 5
            content_right = width - margin * 72 / 25.4 - 36
            number_left = content_right - 24
            wide_top = height - float(named["mjx-eqn-eq%3Awide"]["/Top"])
            for y in range(image.height):
                number = False
                for x in range(image.width):
                    r, g, b = pixels[x, y]
                    if r > 200 and b > 140 and g < 80:
                        magenta.append((x, y))
                    elif 60 < r < 190 and b > 60 and g < 60 and abs(r - b) < 50 and first_top * scale <= y < theorem_top * scale - 4:
                        purple.append((x, y))
                    if r > 160 and g < 80 and b < 80 and first_top * scale - 6 <= y < theorem_top * scale:
                        red.append((x, y))
                    if first_top * scale - 6 <= y < theorem_top * scale - 4 and number_left * scale < x < (content_right + 2) * scale and max(r, g, b) < 180:
                        number = True
                        if (wide_top - 2) * scale < y < (wide_top + 18) * scale:
                            wide_tag.append((x, y))
                if number:
                    number_rows.append(y)
            groups = []
            for y in number_rows:
                if not groups or y > groups[-1][-1] + 3:
                    groups.append([])
                groups[-1].append(y)
            bands = [(g[0] / scale, (g[-1] - g[0] + 1) / scale) for g in groups]
            if len(bands) != 4 or any(not 7 < size < 16 for _, size in bands):
                failures.append(str(path) + ": four normal-sized number bands missing")
            number_errors = []
            for key, index in (("mjx-eqn-eq%3Apaper", 0), ("mjx-eqn-eq%3Aalign", 1), ("mjx-eqn-eq%3Awide", 3)):
                top = height - float(named[key]["/Top"])
                error = abs(top - bands[index][0]) if len(bands) > index else 999
                if reader.get_destination_page_number(named[key]) != 0 or error > 1:
                    failures.append(str(path) + ": number/destination exceeds 1pt: " + key)
                number_errors.append(error)
            ink = {}
            for colour, points in (("localImage", magenta), ("unbreakableFormula", purple), ("wrappedFormula", red)):
                if not points:
                    failures.append(str(path) + ": missing painted " + colour)
                    continue
                left, right = min(x for x, _ in points), max(x for x, _ in points)
                top, bottom = min(y for _, y in points), max(y for _, y in points)
                if left <= 2 or right >= image.width - 3:
                    failures.append(str(path) + ": ink touches paper edge: " + colour)
                if colour == "localImage" and abs((right - left + 1) / (bottom - top + 1) / (1200 / 180) - 1) > .02:
                    failures.append(str(path) + ": local image aspect ratio changed")
                ink[colour] = dict(boundsPx=[left, top, right, bottom], paintedPixels=len(points))
            if "unbreakableFormula" in ink:
                # The separate normal-size tag is at the far right; formula
                # width must reserve space for it rather than overlap it.
                if not wide_tag or ink["unbreakableFormula"]["boundsPx"][2] > min(x for x, _ in wide_tag) - 2 * scale:
                    failures.append(str(path) + ": unbreakable formula overlaps number region")
        records.append(dict(file=str(path), pages=len(reader.pages), bodyFontPt=font,
                            gotos=gotos, numberBandsPt=bands, numberInkErrorPt=number_errors, ink=ink))
result = dict(accepted=not failures, failures=failures, records=records,
              scope="six fixed synthetic paper-resource PDFs, no real attachments/account/iPhone acceptance")
with args.output.open("x", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
    stream.write("\n")
print(json.dumps(dict(accepted=not failures, pdfs=len(records), failures=failures), ensure_ascii=False))
raise SystemExit(bool(failures))
