#!/usr/bin/env python3
"""Read-only SVG equation-number position audit, using a synthetic paint marker.

The marker is the existing group's exact getBBox rectangle; generator requires
unchanged layout, source, HTML and modified state after removing it. Not a
production highlight, coordinate compensation or PDF rewrite.
"""
import argparse
import json
import logging
import math
from pathlib import Path
from urllib.parse import unquote

import pdfplumber
from pypdf import PdfReader
from pypdf.generic import NameObject

logging.getLogger("pdfminer").setLevel(logging.ERROR)
parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--output", required=True, type=Path)
parser.add_argument("--require-accurate", action="store_true")
args = parser.parse_args()
if args.output.exists():
    raise ValueError("Never replace an audit")
manifest = json.loads((args.directory / "native-pdf-results.json").read_text())
if manifest.get("failures") != 0 or manifest.get("blockedRequests") != 0 or any(
        manifest.get(key) is not True for key in ("sourceBefore", "sourceAfter", "htmlSame", "modifiedSame")):
    raise ValueError("Generator/source preservation failed")
marker = manifest.get("equationGeometryMarker", {})
if marker.get("valid") is not True or marker.get("rgb") != [250, 13, 204]:
    raise ValueError("Explicit geometry-preserving equation marker required")
if not isinstance(marker.get("id"), str) or not marker["id"].startswith("mjx-eqn-"):
    raise ValueError("Explicit equation target required")
for field in ("bbox", "rectBefore", "rectAfter"):
    values = marker.get(field, {})
    if any(not isinstance(values.get(key), (int, float)) or isinstance(values[key], bool)
           or not math.isfinite(values[key]) for key in ("x", "y", "width", "height")):
        raise ValueError("Finite equation geometry required")
    if values["width"] <= 0 or values["height"] <= 0:
        raise ValueError("Positive equation dimensions required")
if any(abs(marker["rectBefore"][key] - marker["rectAfter"][key]) >= .00001 for key in ("x", "y", "width", "height")):
    raise ValueError("SVG target geometry changed")
if any(manifest.get(key) for key in ("anchorPrototype", "transformVariant", "cssPageMarginsProbe")):
    raise ValueError("Do not accept coordinate compensation prototypes")
event = manifest.get("eventLinks", {})
if (event.get("prepares"), event.get("restores"), event.get("pending"), event.get("errors"), event.get("active")) != (3, 3, 0, 0, False):
    raise ValueError("Actual controlled print/restore path required")


def name(value):
    text = str(value)
    return text[1:] if isinstance(value, NameObject) and text.startswith("/") else text


records = []
for filename in ("references-5mm.pdf", "references-20mm.pdf", "references-visible-heading.pdf"):
    reader = PdfReader(args.directory / filename)
    named = {name(key): value for key, value in reader.named_destinations.items()}
    candidates = [(key, value) for key, value in named.items() if unquote(key) == marker["id"]]
    if len(candidates) != 1:
        raise ValueError("Exactly one actual equation destination required")
    key, destination = candidates[0]
    exact_links = 0
    for page in reader.pages:
        for reference in page.get("/Annots", []):
            annotation = reference.get_object()
            if annotation.get("/Subtype") != "/Link":
                continue
            action = annotation.get("/A")
            action = action.get_object() if action else {}
            target = action.get("/D") if action.get("/S") == "/GoTo" else annotation.get("/Dest")
            if target is not None and name(target) == key:
                exact_links += 1
    if exact_links != 1:
        raise ValueError("The equation's GoTo link must resolve by EXACT name, not decoding fallback")
    boxes = []
    with pdfplumber.open(args.directory / filename) as pdf:
        for index, page in enumerate(pdf.pages):
            for item in page.rects + page.curves:
                color = item.get("non_stroking_color")
                if item.get("fill") and isinstance(color, (tuple, list)) and len(color) == 3 and all(
                        abs(a - b / 255) < .0001 for a, b in zip(color, marker["rgb"])):
                    boxes.append((index, item))
    if len(boxes) != 1:
        raise ValueError("Exactly one painted equation bbox required")
    painted_page, box = boxes[0]
    destination_page = reader.get_destination_page_number(destination)
    top = float(reader.pages[destination_page].mediabox.height) - float(destination["/Top"])
    left = float(destination["/Left"])
    accurate = destination_page == painted_page and abs(top - box["top"]) <= 1 and abs(left - box["x0"]) <= 1
    records.append({"file": filename, "destinationName": key, "exactGoToLinks": exact_links,
                    "destinationPage": destination_page + 1, "paintedPage": painted_page + 1,
                    "deltaTopPt": top - box["top"], "deltaLeftPt": left - box["x0"], "accurate": accurate})
passed = all(item["accurate"] for item in records)
result = {"readOnly": True, "pdfRewritten": False, "syntheticMarkerOnly": True,
          "records": records, "positionChecksPassed": passed, "acceptanceRequested": args.require_accurate}
with args.output.open("x") as stream:
    json.dump(result, stream, indent=2)
    stream.write("\n")
print(json.dumps({"positionChecksPassed": passed, "pdfs": len(records),
                  "samePages": all(x["destinationPage"] == x["paintedPage"] for x in records),
                  "maxVerticalErrorPt": max(abs(x["deltaTopPt"]) for x in records)}))
raise SystemExit(1 if args.require_accurate and not passed else 0)
