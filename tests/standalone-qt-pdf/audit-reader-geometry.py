#!/usr/bin/env python3
"""Read-only scientific-reader acceptance; never rewrite or compensate a PDF.

Use all three retained synthetic runs: ordinary, split-block edge, plain eq1.
Only the unmodified block-ID PDFs are coordinate checks. The visible-heading
variant deliberately moves IDs inside a block and is NOT compared to its top.
"""
import argparse
import json
import logging
from pathlib import Path

import pdfplumber
from pypdf import PdfReader
from pypdf.generic import NameObject

logging.getLogger("pdfminer").setLevel(logging.ERROR)
parser = argparse.ArgumentParser()
parser.add_argument("directories", nargs=3, type=Path)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
if args.output.exists():
    raise ValueError("Refusing to replace an existing report")


def name(value):
    text = str(value)
    return text[1:] if isinstance(value, NameObject) and text.startswith("/") else text


records, failures, variants = [], [], set()
colors = {"wiz-math-thm:pdf-a": (.251, .3569, .6902),
          "wiz-math-def:pdf-b": (.1569, .4627, .349)}
for directory in args.directories:
    manifest = json.loads((directory / "native-pdf-results.json").read_text())
    for key in ("sourceBefore", "sourceAfter", "htmlSame", "modifiedSame"):
        if manifest.get(key) is not True:
            raise ValueError("Reader preservation failed: " + key)
    if manifest.get("failures") != 0 or manifest.get("blockedRequests") != 0:
        raise ValueError("Generator or offline isolation failed")
    if manifest.get("renderState") != {"blocks": 2, "errors": 0, "jax": 3}:
        raise ValueError("Unexpected scientific/math render state")
    event = manifest.get("eventLinks", {})
    if (event.get("prepares"), event.get("restores"), event.get("errors"),
        event.get("pending"), event.get("active")) != (3, 3, 0, 0, False):
        raise ValueError("Print/save preparation was not fully restored")
    if any(manifest.get(key) for key in ("anchorPrototype", "transformVariant", "cssPageMarginsProbe")):
        raise ValueError("Do not accept coordinate-compensating prototypes")
    equation_ids = [item["id"] for item in manifest["referenceDom"]["equationTargets"]]
    if equation_ids not in (["mjx-eqn-eq1"], ["mjx-eqn-eq:pdf-eq"]):
        raise ValueError("Unexpected equation-label fixture")
    if manifest["referenceDom"].get("mathJaxVersion") != "2.7.6":
        raise ValueError("Keep the original MathJax fixture")
    variant = "edge" if manifest.get("edgeFixture") else "plain" if equation_ids == ["mjx-eqn-eq1"] else "normal"
    if variant in variants:
        raise ValueError("Repeated fixture variant")
    variants.add(variant)
    for filename in ("references-5mm.pdf", "references-20mm.pdf", "references-visible-heading.pdf"):
        reader = PdfReader(directory / filename)
        destinations = {name(key): value for key, value in reader.named_destinations.items()}
        unresolved, link_count = [], 0
        for page in reader.pages:
            for reference in page.get("/Annots", []):
                annotation = reference.get_object()
                if annotation.get("/Subtype") != "/Link":
                    continue
                action = annotation.get("/A")
                action = action.get_object() if action else {}
                target = action.get("/D") if action.get("/S") == "/GoTo" else annotation.get("/Dest")
                if target is None or name(target) not in destinations:
                    unresolved.append(name(target) if target is not None else "non-GoTo link")
                link_count += 1
        if unresolved or link_count != 4 or len(destinations) != 4:
            failures.append({"variant": variant, "file": filename, "unresolved": unresolved,
                             "links": link_count, "destinations": len(destinations)})
        positions = []
        with pdfplumber.open(directory / filename) as pdf:
            if filename != "references-visible-heading.pdf":
                for target, color in colors.items():
                    boxes = [(index, item) for index, page in enumerate(pdf.pages)
                             for item in page.curves
                             if item.get("fill") and item.get("width", 0) > 300 and item.get("height", 0) > 20
                             and isinstance(item.get("non_stroking_color"), (tuple, list))
                             and len(item["non_stroking_color"]) == 3
                             and all(abs(a - b) < .0001 for a, b in zip(item["non_stroking_color"], color))]
                    if not boxes or target not in destinations:
                        raise ValueError("Missing painted scientific block or destination")
                    painted_page, box = min(boxes, key=lambda entry: (entry[0], entry[1]["top"]))
                    destination = destinations[target]
                    destination_page = reader.get_destination_page_number(destination)
                    top = float(reader.pages[destination_page].mediabox.height) - float(destination["/Top"])
                    delta = top - box["top"]
                    accurate = destination_page == painted_page and abs(delta) <= 1
                    item = {"target": target, "destinationPage": destination_page + 1,
                            "paintedPage": painted_page + 1, "destinationTopPt": top,
                            "paintedTopPt": box["top"], "deltaTopPt": delta, "accurate": accurate}
                    positions.append(item)
                    if not accurate:
                        failures.append({"variant": variant, "file": filename, **item})
        records.append({"variant": variant, "file": filename, "pages": len(reader.pages),
                        "allFourLinksExactlyResolved": not unresolved and link_count == 4 and len(destinations) == 4,
                        "positions": positions, "headingVariantNotComparedToBlockTop": not positions})
if variants != {"normal", "edge", "plain"}:
    raise ValueError("All three distinct fixtures required")
result = {"readOnly": True, "pdfRewritten": False, "records": records, "failures": failures,
          "checks": "exact internal destinations, original scientific block tops, source/HTML/modified state",
          "equationPositionClaimed": False, "readerAcceptancePassed": not failures}
with args.output.open("x", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
    stream.write("\n")
print(json.dumps({"readerAcceptancePassed": not failures, "pdfs": len(records),
                  "blockTargetsChecked": sum(len(item["positions"]) for item in records),
                  "failures": len(failures), "equationPositionClaimed": False}))
raise SystemExit(1 if failures else 0)
