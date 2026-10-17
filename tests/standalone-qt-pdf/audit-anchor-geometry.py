#!/usr/bin/env python3
"""Read-only comparison of PDF named points and the actual painted targets.

Reproducing an error is not fixing it. Do not turn this analysis into a general
PDF coordinate compensator or write estimates back into any PDF.
"""
import argparse
import json
import logging
from pathlib import Path

import pdfplumber
from pypdf import PdfReader
from pypdf.generic import ContentStream, NameObject

logging.getLogger("pdfminer").setLevel(logging.ERROR)
parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--require-accurate", action="store_true",
                    help="Fail acceptance if any target differs by >1 pt/page or source link is misaligned")
parser.add_argument("--cases", nargs="+", help="Audit an explicit subset; all cases are required for engine acceptance")
args = parser.parse_args()
manifest = json.loads((args.directory / "anchor-geometry-results.json").read_text())
if manifest["failures"] or not manifest["offTheRecord"] or manifest["registeredScripts"] or manifest["blockedRequests"]:
    raise RuntimeError("The independent generator did not pass its isolation invariants")
expected_cases = {
    "a4-zero-narrow", "a4-5mm-narrow", "a4-20mm-narrow", "a4-5mm-wide",
    "a4-asymmetric-wide", "letter-5mm-wide", "a4-landscape-5mm",
    "a4-css20-api0", "css-mixed-api5",
}
names = [sample["name"] for sample in manifest["records"]]
if len(names) != len(expected_cases) or set(names) != expected_cases:
    raise RuntimeError("All nine unique geometry cases are required, even for subset controls")
for sample in manifest["records"]:
    if (not sample["htmlSame"] or not sample["saved"] or not sample["layoutValid"]
            or sample["file"] != sample["name"] + ".pdf"):
        raise RuntimeError("Incomplete/invalid geometry case: " + sample["name"])


def same_color(item, color):
    actual = item.get("non_stroking_color")
    return (item.get("fill") and isinstance(actual, (list, tuple)) and len(actual) == 3
            and all(abs(a - b) < .001 for a, b in zip(actual, color)))


colors = ((.8, .2, .2), (.2, .6, .2), (.2, .2, .8))
records = []
if args.cases and set(args.cases) - {x["name"] for x in manifest["records"]}:
    raise ValueError("Unknown geometry case")
for sample in manifest["records"]:
    if args.cases and sample["name"] not in args.cases:
        continue
    path = args.directory / sample["file"]
    reader = PdfReader(path)
    # /Dests dictionary keys can be PDF NameObjects (leading slash is syntax),
    # unlike Names-tree strings. Never strip a real slash from a string target.
    destinations = {(str(k)[1:] if isinstance(k, NameObject) else str(k)): v
                    for k, v in reader.named_destinations.items()}
    with pdfplumber.open(path) as pdf:
        pages = [{"width": float(p.mediabox.width), "height": float(p.mediabox.height)} for p in reader.pages]
        targets = []
        for index, color in enumerate(colors, 1):
            name = f"target-{index}"
            destination = destinations.get(name)
            if destination is None:
                raise RuntimeError(f"Missing exact named destination {sample['file']}: {name}")
            candidates = [(i, item) for i, page in enumerate(pdf.pages) for item in page.rects + page.curves
                          if same_color(item, color) and item.get("width", 0) > 50 and item.get("height", 0) > 10]
            if len(candidates) != 1:
                raise RuntimeError(f"Ambiguous or split painted target {sample['file']}: {name}: {len(candidates)}")
            visible_page, painted = candidates[0]
            target_page = reader.get_destination_page_number(destination)
            top = pages[target_page]["height"] - float(destination["/Top"])
            left = float(destination["/Left"])
            targets.append({"id": name, "destinationPage": target_page + 1, "paintedPage": visible_page + 1,
                            "samePage": target_page == visible_page,
                            "destinationLeftPt": left, "destinationTopPt": top,
                            "paintedLeftPt": painted["x0"], "paintedTopPt": painted["top"],
                            "deltaLeftPt": left - painted["x0"] if target_page == visible_page else None,
                            "deltaTopPt": top - painted["top"] if target_page == visible_page else None})
        links = []
        for page_index, page in enumerate(reader.pages):
            for ref in page.get("/Annots", []):
                annot = ref.get_object()
                if annot.get("/Subtype") != "/Link":
                    continue
                action = annot.get("/A")
                action = action.get_object() if action is not None else {}
                dest = annot.get("/Dest", action.get("/D"))
                name = str(dest)[1:] if isinstance(dest, NameObject) else str(dest)
                if name not in destinations:
                    raise RuntimeError(f"Unresolved/unsupported link {sample['file']}: {name}")
                rect = [float(x) for x in annot["/Rect"]]
                box = (rect[0], pages[page_index]["height"] - rect[3], rect[2], pages[page_index]["height"] - rect[1])
                navs = [item for item in pdf.pages[page_index].rects + pdf.pages[page_index].curves
                        if same_color(item, (230/255, 230/255, 230/255))]
                contained = any(box[0] >= item["x0"] - 1 and box[2] <= item["x1"] + 1
                                and box[1] >= item["top"] - 1 and box[3] <= item["bottom"] + 1 for item in navs)
                aligned = any(abs(box[0] - item["x0"]) <= 1
                              and abs((box[1] + box[3] - item["top"] - item["bottom"]) / 2) <= 1
                              for item in navs)
                links.append({"page": page_index + 1, "name": name, "rectanglePt": box,
                              "containedInPaintedNav": contained, "alignedWithPaintedNav": aligned})
        if len(links) != 3:
            raise RuntimeError(f"Expected three source links, got {len(links)} in {sample['file']}")
        if {link["name"] for link in links} != {f"target-{i}" for i in range(1, 4)}:
            raise RuntimeError(f"Source links do not cover all three targets: {sample['file']}")
        matrices = [[float(x) for x in operands] for operands, op in ContentStream(reader.pages[0].get_contents(), reader).operations
                    if op == b"cm"][:4]
        records.append({"name": sample["name"], "file": sample["file"], "pages": pages,
                        "targets": targets, "sourceLinks": links, "firstPageMatrices": matrices,
                        "htmlSame": sample["htmlSame"], "apiMarginsMm": sample["apiMarginsMm"],
                        "positionalFailureObserved": any(not x["samePage"] or abs(x["deltaTopPt"]) > 1
                                                         or abs(x["deltaLeftPt"]) > 1 for x in targets)})

result = {"readOnly": True, "kind": "standalone native anchor geometry, no editor/math/Wiz",
          "warning": "Error reproduction is NOT a repair; no PDF was rewritten.",
          "records": records,
          "allSourceLinksContained": all(link["containedInPaintedNav"] for r in records for link in r["sourceLinks"]),
          "allSourceLinksAligned": all(link["alignedWithPaintedNav"] for r in records for link in r["sourceLinks"]),
          "casesWithPositionError": sum(r["positionalFailureObserved"] for r in records)}
result["acceptanceRequested"] = args.require_accurate
result["subsetOnly"] = bool(args.cases)
result["acceptancePassed"] = (result["casesWithPositionError"] == 0
                              and result["allSourceLinksContained"] and result["allSourceLinksAligned"])
with args.output.open("x", encoding="utf-8") as output:
    json.dump(result, output, ensure_ascii=False, indent=2)
    output.write("\n")
print(json.dumps({k: v for k, v in result.items() if k != "records"}, ensure_ascii=False))
if args.require_accurate and not result["acceptancePassed"]:
    raise SystemExit(1)
