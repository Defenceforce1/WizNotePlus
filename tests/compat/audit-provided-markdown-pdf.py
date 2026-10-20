#!/usr/bin/env python3
"""Read-only raster/target audit for the three explicitly provided small cases.

The right-hand number bands and coloured block edges are properties of these
fixtures, not a general PDF repair or an arbitrary-paper-layout algorithm.
"""
import argparse
import json
import shutil
import subprocess
import tempfile
from pathlib import Path

from PIL import Image
from pypdf import PdfReader
from pypdf.generic import NameObject

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--case", type=int, choices=[1, 2, 3], required=True)
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
tool = shutil.which("pdftoppm")
if not tool:
    raise RuntimeError("Poppler pdftoppm is required; no network installation attempted")
failures, records = [], []
probe = json.loads((args.directory / "native-pdf-results.json").read_text())
paper_copy = probe.get("pageMode") == "a4"
if probe.get("failures") or probe.get("blockedRequests") or not all(probe.get(k) for k in ("htmlSame", "modifiedSame")):
    failures.append("probe resource/source preservation failed")
round_trip = probe.get("providedSourceRoundTrip", {})
if not all(round_trip.get(k) for k in ("canonicalGetterSame", "htmlBodySame", "editingReady")):
    failures.append("provided canonical source/body round-trip failed")
required = [[], ["wiz-math-ex2", "wiz-math-thm1", "mjx-eqn-eq2", "mjx-eqn-eq1"],
            ["mjx-eqn-eq1", "mjx-eqn-eq1", "wiz-math-thm:positive1", "wiz-math-def:real",
             "mjx-eqn-eq1", "wiz-math-def:real", "wiz-math-thm:positive"]][args.case - 1]


def name(value):
    text = str(value)
    return text[1:] if isinstance(value, NameObject) and text.startswith("/") else text


with tempfile.TemporaryDirectory(prefix="wiz-provided-pdf-audit-") as temporary:
    for filename in ["references-5mm.pdf", "references-20mm.pdf", "references-visible-heading.pdf"]:
        reader = PdfReader(args.directory / filename)
        # pypdf keys include the syntactic PDF Name slash on these engine PDFs.
        # Fixture IDs are all explicitly known and never start with a slash.
        named = {str(key).removeprefix("/"): value for key, value in reader.named_destinations.items()}
        gotos = []
        for page in reader.pages:
            for reference in page.get("/Annots", []):
                annotation = reference.get_object()
                action = annotation.get("/A", {})
                action = action.get_object() if hasattr(action, "get_object") else action
                destination = annotation.get("/Dest")
                if action.get("/S") == "/GoTo":
                    destination = action["/D"]
                if destination is not None:
                    gotos.append(name(destination))
        if sorted(gotos) != sorted(required):
            failures.append(filename + ": exact reference names/count do not match provided source")
        if any(key not in named for key in gotos):
            failures.append(filename + ": unresolved named destination")
        rasters = {}
        def raster(index):
            if index not in rasters:
                prefix = Path(temporary) / (Path(filename).stem + "-" + str(index))
                subprocess.run([tool, "-f", str(index + 1), "-singlefile", "-scale-to", "3200", "-png",
                                str(args.directory / filename), str(prefix)], check=True, capture_output=True)
                rasters[index] = Image.open(prefix.with_suffix(".png")).convert("RGB")
            return rasters[index]
        page = reader.pages[0]
        image = raster(0)
        scale = image.height / float(page.mediabox.height)
        pixels = image.load()
        # The normal-size paper copy places case 2's nested third equation at
        # ~284pt (20mm margin). The former 250pt reader-width scan clipped the
        # test region, not the PDF. This fixed fixture-only ceiling excludes
        # following prose and still demands exactly three painted bands.
        ceiling = [180, 320 if paper_copy else 250, 150][args.case - 1]
        rows = []
        for y in range(min(image.height, int(ceiling * scale))):
            if any(max(pixels[x, y]) < 180 for x in range(int(image.width * .78), int(image.width * .94))):
                rows.append(y)
        groups = []
        for y in rows:
            if not groups or y > groups[-1][-1] + 2:
                groups.append([])
            groups[-1].append(y)
        bands = [{"topPt": g[0] / scale, "bottomPt": g[-1] / scale} for g in groups]
        if len(bands) != [2, 3, 1][args.case - 1]:
            failures.append(filename + ": expected equation-number ink bands are missing or unexpected")
        targets = []
        for key in sorted(set(required)):
            if key not in named:
                continue
            destination = named[key]
            destination_page = reader.get_destination_page_number(destination)
            if destination_page < 0 or (destination_page != 0 and not paper_copy):
                failures.append(filename + ": target not on independently expected first page: " + key)
                continue
            target_page = reader.pages[destination_page]
            target_image = raster(destination_page)
            target_scale = target_image.height / float(target_page.mediabox.height)
            target_pixels = target_image.load()
            top = float(target_page.mediabox.height) - float(destination["/Top"])
            if key.startswith("mjx-eqn-"):
                index = (1 if key == "mjx-eqn-eq2" else 0)
                error = abs(top - bands[index]["topPt"]) if len(bands) > index and destination_page == 0 else 999
                if error > 1:
                    failures.append(filename + ": equation target/painted number error exceeds 1pt: " + key)
                targets.append({"id": key, "page": destination_page + 1, "topPt": top, "numberInkErrorPt": error})
            elif filename == "references-visible-heading.pdf":
                # This explicit diagnostic moves IDs off the block to an added
                # visible heading span. Its target must not be compared with
                # the unchanged block's coloured edge. Normal outputs below
                # still require the original edge to agree within one point.
                targets.append({"id": key, "topPt": top,
                                "positionNotAudited": "explicit added-heading diagnostic; not a faithful export"})
            else:
                left = float(destination["/Left"])
                coloured_rows = []
                for y in range(max(0, int((top - 2) * target_scale)), min(target_image.height, int((top + 8) * target_scale))):
                    for x in range(max(0, int((left - 3) * target_scale)), min(target_image.width, int((left + 5) * target_scale))):
                        r, g, b = target_pixels[x, y]
                        colour = (g - r > 25 and g - b > 10) if "def:" in key else (
                            r - g > 25 and g - b > 20) if key == "wiz-math-ex2" else (b - r > 30 and b - g > 20)
                        if colour and max(r, g, b) < 240:
                            coloured_rows.append(y)
                            break
                error = abs(min(coloured_rows) / target_scale - top) if coloured_rows else 999
                # Rounded coloured corners can start a little below the actual
                # box top; report the measurement, never shift the destination.
                if error > 1:
                    failures.append(filename + ": target/painted block edge error exceeds 1pt: " + key)
                targets.append({"id": key, "page": destination_page + 1, "topPt": top, "blockEdgeErrorPt": error})
        records.append({"file": filename, "pages": len(reader.pages), "gotos": gotos,
                        "numberInkBands": bands, "targets": targets})
result = {"auditVersion": 3, "readOnly": True, "case": args.case, "paperCopy": paper_copy, "accepted": not failures,
          "failures": failures, "records": records,
          "scope": "three known pasted cases; pixels/targets, not arbitrary TeX, attachments or whole-library acceptance"}
with args.output.open("x", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
    stream.write("\n")
print(json.dumps({"accepted": not failures, "case": args.case, "failures": failures}, ensure_ascii=False))
raise SystemExit(1 if failures else 0)
