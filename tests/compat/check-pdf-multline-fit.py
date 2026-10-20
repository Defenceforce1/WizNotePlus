#!/usr/bin/env python3
"""Read-only/raster audit of the synthetic three-colour multline corpus.

Never accepts link resolution as layout evidence, or vector bounds as evidence
that clipped ink was painted. Temporary rasters are owned by this invocation.
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
from pypdf.generic import NameObject

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--displays", type=int, default=1)
parser.add_argument("--long-first-row", action="store_true")
parser.add_argument("--star", action="store_true")
parser.add_argument("--inline-reference", action="store_true")
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
if args.displays not in (1, 2):
    parser.error("one or two synthetic displays required")


def pdf_name(value):
    text = str(value)
    return text[1:] if isinstance(value, NameObject) and text.startswith("/") else text


failures = []
records = []
probe = json.loads((args.directory / "native-pdf-results.json").read_text())
for key in ("sourceBefore", "sourceAfter", "htmlSame", "modifiedSame"):
    if probe.get(key) is not True:
        failures.append("probe preservation: " + key)
if probe.get("failures") != 0 or probe.get("blockedRequests") != 0:
    failures.append("native probe failure or remote request")
if probe.get("multlinePrototype", False):
    failures.append("diagnostic prototype is not product acceptance")
status = probe.get("eventLinks", {})
paper_copy = probe.get("pageMode") == "a4"
expected_source_prints = 0 if paper_copy else 3
if any(status.get(k) != v for k, v in dict(prepares=expected_source_prints, restores=expected_source_prints, pending=0, errors=0,
                                           active=False, lastFitted=0 if paper_copy else args.displays).items()):
    failures.append("print fit/restore contract")

with tempfile.TemporaryDirectory(prefix="wiz-multline-raster-") as temporary:
    for filename in ("references-5mm.pdf", "references-20mm.pdf", "references-visible-heading.pdf"):
        path = args.directory / filename
        reader = PdfReader(path)
        named = {pdf_name(key): value for key, value in reader.named_destinations.items()}
        gotos = []
        uri_fragments = []
        for page in reader.pages:
            for ref in page.get("/Annots", []):
                ann = ref.get_object()
                action = ann.get("/A", {})
                action = action.get_object() if hasattr(action, "get_object") else action
                destination = ann.get("/Dest")
                if action.get("/S") == "/GoTo":
                    destination = action.get("/D")
                if destination is not None:
                    if isinstance(destination, (str, NameObject)):
                        key = pdf_name(destination)
                        gotos.append(key)
                        if key not in named:
                            failures.append(filename + ": unresolved exact GoTo " + key)
                if action.get("/S") == "/URI":
                    uri = str(action.get("/URI", ""))
                    if uri.startswith("file:") and "#" in uri:
                        uri_fragments.append(uri)
        if uri_fragments:
            failures.append(filename + ": local URI instead of GoTo")
        math_links = [key for key in gotos if key.startswith("mjx-eqn-")]
        row_names = (["mjx-eqn-eq%3Aquote", "mjx-eqn-eq%3Alist"] if args.displays == 2 else
                     ["mjx-eqn-eq%3Amultline"])
        expected_names = set() if args.star else set(row_names)
        if args.inline_reference:
            expected_names.add("mjx-eqn-eq%3Asource")
        if set(math_links) != expected_names:
            failures.append(filename + ": missing or unexpected exact equation link names")
        minimum = 0 if args.star else args.displays + int(args.inline_reference)
        if len(math_links) < minimum:
            failures.append(filename + ": missing mathematical link annotations")
        if args.star and any(key.startswith("mjx-eqn-") for key in named):
            failures.append(filename + ": starred display acquired a number target")

        with pdfplumber.open(path) as document:
            # Deliberately short controlled documents must fit on one page.
            if len(document.pages) != 1:
                failures.append(filename + ": unexpected pagination")
            page = document.pages[0]
            labels = page.search("Last row:", regex=False)
            if len(labels) != 1:
                raise ValueError("synthetic end-of-display marker missing or duplicated")
            end_math = labels[0]["top"]
            ordinary = [c["size"] for c in labels[0]["chars"]]
            body_size = statistics.median(ordinary)
            prefix = Path(temporary) / path.stem
            subprocess.run(["pdftoppm", "-f", "1", "-singlefile", "-scale-to", "3200",
                            "-png", str(path), str(prefix)], check=True, capture_output=True)
            raster = Image.open(prefix.with_suffix(".png")).convert("RGB")
            scale = raster.height / page.height
            colours = {key: [] for key in ("red", "green", "blue")}
            raster_pixels = raster.load()
            # Exclude the reference after the display: MathJax may colour its
            # equation-number glyphs blue too. They cannot prove row visibility.
            for y in range(min(raster.height, int(end_math * scale) - 2)):
                for x in range(raster.width):
                    r, g, b = raster_pixels[x, y]
                    colour = ("red" if r > 130 and g < 90 and b < 90 else
                              "green" if g > 65 and r < 170 and b < 170 and g > r + 35 and g > b + 35 else
                              "blue" if b > 130 and r < 90 and g < 90 else None)
                    if colour:
                        colours[colour].append((x, y))
            ink = {}
            for colour, pixels in colours.items():
                rows = sorted({y for _, y in pixels})
                groups = []
                for y in rows:
                    if not groups or y - groups[-1][-1] > 14:
                        groups.append([])
                    groups[-1].append(y)
                valid_count = len(groups) == args.displays
                if colour == "red" and args.long_first_row:
                    # The fixed 40-term fixture naturally wraps to three rows
                    # at A4/20mm after true paper-width typesetting; the old
                    # reader-width path had at most two. Keep a bounded count,
                    # all glyph-size/edge/number tests and exact links intact.
                    valid_count = args.displays <= len(groups) <= args.displays * (3 if paper_copy else 2)
                if not valid_count:
                    failures.append(filename + ": missing/extra " + colour + " rows")
                metrics = []
                for group in groups:
                    points = [(x, y) for x, y in pixels if group[0] <= y <= group[-1]]
                    left, right = min(x for x, _ in points), max(x for x, _ in points)
                    height = (group[-1] - group[0] + 1) / scale
                    ratio = height / body_size
                    minimum_ratio = .08 if colour == "red" and args.long_first_row else .5
                    if ratio < minimum_ratio or ratio > 1.5:
                        failures.append(filename + ": abnormal " + colour + " glyph/body size")
                    if left <= 2 or right >= raster.width - 3:
                        failures.append(filename + ": " + colour + " ink touches paper edge")
                    metrics.append(dict(boundsPx=[left, group[0], right, group[-1]],
                                        paintedPixels=len(points), heightPt=height, glyphBodyRatio=ratio))
                ink[colour] = metrics
            positions = []
            badges = []
            if not args.star:
                for key in row_names:
                    destination = named.get(key)
                    if destination is None or destination.get("/Top") is None:
                        failures.append(filename + ": missing last-row destination " + key)
                        continue
                    if reader.get_destination_page_number(destination) != 0:
                        failures.append(filename + ": last-row target points to wrong page")
                        continue
                    positions.append(page.height - float(destination["/Top"]))
                blue_tops = sorted(row["boundsPx"][1] / scale for row in ink["blue"])
                if len(positions) != len(blue_tops) or any(abs(a-b) > max(2., body_size * .25)
                        for a, b in zip(sorted(positions), blue_tops)):
                    failures.append(filename + ": last-row destination is not aligned with painted ink")
                for row in ink["blue"]:
                    points = []
                    low = max(0, int(row["boundsPx"][1] - 3 * scale))
                    high = min(raster.height, int(row["boundsPx"][3] + 3 * scale) + 1)
                    for y in range(low, high):
                        for x in range(raster.width):
                            rgb = raster_pixels[x, y]
                            if max(rgb) - min(rgb) < 12 and max(rgb) < 210:
                                points.append((x, y))
                    if not points:
                        failures.append(filename + ": equation number is not visibly painted")
                        continue
                    left = min(x for x, _ in points)
                    top = min(y for _, y in points) / scale
                    if left <= row["boundsPx"][2] + 1:
                        failures.append(filename + ": number overlaps the last-row ink")
                    badges.append(dict(topPt=top, leftPx=left, paintedPixels=len(points)))
                if len(badges) != len(positions) or any(abs(a-b["topPt"]) > 1.
                        for a, b in zip(sorted(positions), sorted(badges, key=lambda b: b["topPt"]))):
                    failures.append(filename + ": number/destination vertical error exceeds one point")
            records.append(dict(file=filename, pages=len(reader.pages), bodySizePt=body_size,
                                mathematicalGoTos=math_links, lastRowDestinationTopPt=positions,
                                numberBadges=badges, ink=ink))

result = dict(readOnly=True, acceptance=not failures, failures=failures, records=records,
              scope="synthetic coloured multline visibility/size and exact links, not all TeX layouts")
with args.output.open("x", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
    stream.write("\n")
print(json.dumps(dict(acceptance=not failures, failures=failures), ensure_ascii=False))
raise SystemExit(1 if failures else 0)
