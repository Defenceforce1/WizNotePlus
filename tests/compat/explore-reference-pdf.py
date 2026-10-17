#!/usr/bin/env python3
"""Read-only PDF destination/visible-heading comparison for synthetic fixtures.

This does not rewrite destinations or fix/replace a PDF. Results are diagnostic,
not evidence about Qt unless that engine actually generated the inputs.
"""
import argparse
import json
import logging
from pathlib import Path

import pdfplumber
from pypdf import PdfReader
from pypdf.generic import ContentStream

logging.getLogger("pdfminer").setLevel(logging.ERROR)
parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--engine", choices=("chrome", "qt"), default="chrome")
parser.add_argument("--output", type=Path)
parser.add_argument("--files", nargs="+", default=("references-5mm.pdf", "references-20mm.pdf", "references-visible-heading.pdf"))
args = parser.parse_args()
records = []
for filename in args.files:
    if Path(filename).name != filename or not filename.endswith(".pdf"):
        raise ValueError("Supply PDF basenames within the fixture directory")
    pdf_path = args.directory / filename
    reader = PdfReader(pdf_path)
    record = {"file": filename, "pages": len(reader.pages), "destinations": []}
    # Diagnostic evidence only: stream matrices are not a general-purpose CSS
    # scale detector and must not be used to compensate production coordinates.
    contents = reader.pages[0].get_contents()
    record["firstPageMatrices"] = [
        [float(value) for value in operands]
        for operands, operation in (ContentStream(contents, reader).operations if contents is not None else [])
        if operation == b"cm"
    ][:4]
    with pdfplumber.open(pdf_path) as pdf:
        page_words = [page.extract_words() for page in pdf.pages]
        for key, marker in (("thm:pdf-a", "PDF-TARGET-A."), ("def:pdf-b", "PDF-TARGET-B."), ("control-target", "CONTROL-TARGET")):
            destination_name = key if key == "control-target" else "wiz-math-" + key
            destination = next((d for name, d in reader.named_destinations.items()
                                if name.lstrip("/") == destination_name), None)
            if destination is None and key == "control-target":
                continue  # Older baseline fixture did not yet include this control.
            if destination is None:
                raise RuntimeError(f"Missing destination: {filename}: {key}")
            destination_page = reader.get_destination_page_number(destination)
            anchor_top = float(reader.pages[destination_page].mediabox.height) - float(destination["/Top"])
            # Read-only hypothesis check for this scientific-style fixture:
            # compare the existing destination to the actually painted block,
            # then estimate application of the content stream's print matrix.
            # Never write this estimate back to a PDF or use it as a universal
            # coordinate repair (visible-heading variants have different IDs).
            box_check = {}
            color = {"thm:pdf-a": (0.251, 0.3569, 0.6902),
                     "def:pdf-b": (0.1569, 0.4627, 0.349)}.get(key)
            page = pdf.pages[destination_page]
            boxes = [item for item in page.curves if color
                     and item.get("fill") and item.get("width", 0) > 300 and item.get("height", 0) > 20
                     and isinstance(item.get("non_stroking_color"), (tuple, list))
                     and len(item["non_stroking_color"]) == 3
                     and all(abs(a - b) < .0001 for a, b in zip(item["non_stroking_color"], color))]
            if len(boxes) == 1:
                # Destination tops are already page-relative. Later pages'
                # stream translations include document pagination; applying
                # those again double-counts it. This hypothesis deliberately
                # uses the first-page origin of the uniform-A4 fixture only.
                matrices = record["firstPageMatrices"][:2]
                if len(matrices) == 2:
                    outer, inner = matrices
                    if all(abs(x) < 1e-9 for x in (outer[1], outer[2], inner[1], inner[2])):
                        ratio = abs(outer[0] * inner[0]) / .75
                        translation = float(reader.pages[destination_page].mediabox.height) - outer[5] - outer[3] * inner[5]
                        estimate = anchor_top * ratio + translation
                        box_check = {"visiblePaintedBlockTopPt": round(boxes[0]["top"], 6),
                                     "uniformA4FirstPageOriginHypothesis": True,
                                     "observedPrintScaleRatio": ratio,
                                     "observedTopTranslationPt": translation,
                                     "readOnlyMatrixEstimateTopPt": round(estimate, 6),
                                     "estimateMinusPaintedBlockTopPt": round(estimate - boxes[0]["top"], 6)}
            target = None
            for page_index, words in enumerate(page_words):
                marker_word = next((word for word in words if marker in word["text"]), None)
                if marker_word is None:
                    continue
                # A long environment may split its visible heading and first
                # body marker across pages. Never assume both are on one page.
                heading_words = [(index, word) for index, candidates in enumerate(page_words)
                                 for word in candidates
                                 if (key in word["text"] or key == "control-target" and marker in word["text"])
                                 and (index, word["top"]) <= (page_index, marker_word["top"])]
                if not heading_words:
                    raise RuntimeError(f"Missing visible target heading: {filename}: {key}")
                heading_page, heading = max(heading_words, key=lambda entry: (entry[0], entry[1]["top"]))
                target = {"key": key, "destinationPage": destination_page + 1,
                          "visiblePage": heading_page + 1,
                          "bodyMarkerPage": page_index + 1,
                          "samePage": destination_page == heading_page,
                          "destinationTopPt": round(anchor_top, 3),
                          "headingTopPt": round(heading["top"], 3),
                          "bodyMarkerTopPt": round(marker_word["top"], 3),
                          "headingMinusDestinationPt": round(heading["top"] - anchor_top, 3)
                          if destination_page == heading_page else None,
                          "readOnlyPaintedBoxComparison": box_check}
                break
            if target is None:
                raise RuntimeError(f"Missing visible marker: {filename}: {key}")
            record["destinations"].append(target)
    records.append(record)
result = json.dumps({"kind": "coordinate analysis; inputs generated by " + (
    "actual Qt printToPdf synthetic fixture" if args.engine == "qt" else "isolated headless Chrome, NOT Qt"),
    "records": records}, ensure_ascii=False, indent=2)
if args.output:
    with args.output.open("x", encoding="utf-8") as output:
        output.write(result + "\n")
print(result)
