"""Read-only audit of the synthetic --reader-equations-base fixture in both modes.

Not a general TeX, long-formula re-layout, attachment or account acceptance test.
"""
import argparse
import json
from pathlib import Path
import statistics
import subprocess
import tempfile
from urllib.parse import unquote

from PIL import Image
from pypdf import PdfReader
import pdfplumber

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--poppler", required=True)
parser.add_argument("--output", required=True, type=Path)
args = parser.parse_args()
failures, records = [], []
normal_fonts = {"reading": [], "a4": []}
for mode in ("reading", "a4"):
    for viewport in (850, 1200):
        folder = args.directory / f"{mode}-{viewport}"
        evidence = json.loads((folder / "native-pdf-results.json").read_text())
        if evidence["pageMode"] != mode or evidence["cssViewportWidth"] != viewport:
            failures.append(f"{folder.name}: actual selected mode/viewport differs")
        if evidence["failures"] or not all(evidence[k] for k in ("sourceBefore", "sourceAfter", "htmlSame", "modifiedSame")) or evidence["blockedRequests"]:
            failures.append(f"{folder.name}: reader/source/resource regression")
        required = sorted([link["id"] for link in evidence["referenceDom"]["links"]] + ["control-target"])
        for filename, margin in (("references-5mm.pdf", 5), ("references-20mm.pdf", 20), ("references-visible-heading.pdf", 5)):
            path = folder / filename
            pdf = PdfReader(path)
            normalize = lambda key: unquote(str(key).removeprefix("/"))
            names = {normalize(k): v for k, v in pdf.named_destinations.items()}
            gotos = []
            for page in pdf.pages:
                for raw in page.get("/Annots", []):
                    annotation = raw.get_object()
                    action = annotation.get("/A", {})
                    action = action.get_object() if hasattr(action, "get_object") else action
                    dest = action.get("/D") if action.get("/S") == "/GoTo" else annotation.get("/Dest")
                    if dest is not None:
                        gotos.append(normalize(dest))
            if sorted(gotos) != required or any(k not in names for k in gotos):
                failures.append(f"{path}: missing, extra or unresolved links")
            width, height = map(float, (pdf.pages[0].mediabox.width, pdf.pages[0].mediabox.height))
            expected_width = viewport * .75 + 2 * margin * 72 / 25.4 if mode == "reading" else 210 * 72 / 25.4
            if abs(width - expected_width) > 1 or abs(height - 297 * 72 / 25.4) > 1:
                failures.append(f"{path}: selected physical page geometry differs")
            if any(abs(float(p.mediabox.width) - width) > .01 for p in pdf.pages):
                failures.append(f"{path}: inconsistent page widths")
            with tempfile.TemporaryDirectory(prefix="wiz-pdf-mode-") as temporary:
                prefix = Path(temporary) / "page"
                subprocess.run([args.poppler, "-f", "1", "-singlefile", "-scale-to", "3200", "-png", str(path), str(prefix)], check=True, capture_output=True)
                image = Image.open(prefix.with_suffix(".png")).convert("RGB")
                scale = image.height / height
                pixels = image.load()
                number_errors = []
                for key in ("mjx-eqn-eq1", "mjx-eqn-eq2"):
                    if key not in names or pdf.get_destination_page_number(names[key]) != 0:
                        failures.append(f"{path}: number destination absent from expected first page")
                        continue
                    top = height - float(names[key]["/Top"])
                    rows = [y for y in range(max(0, int((top - 2) * scale)), min(image.height, int((top + 18) * scale)))
                            if any(max(pixels[x, y]) < 180 for x in range(int(image.width * .78), int(image.width * .95)))]
                    error = abs(min(rows) / scale - top) if rows else 999
                    if error > 1:
                        failures.append(f"{path}: numbered ink/destination exceeds 1pt ({key})")
                    number_errors.append(error)
            with pdfplumber.open(path) as text_pdf:
                chars = [c for p in text_pdf.pages for c in p.chars]
                if "Synthetic line 30" not in "\n".join(p.extract_text() or "" for p in text_pdf.pages):
                    failures.append(f"{path}: final synthetic paragraph missing")
                sizes = [c["size"] for c in chars if c["text"] == "S"]
                if not sizes:
                    failures.append(f"{path}: no normal-text font evidence")
                else:
                    normal_fonts[mode].append(statistics.median(sizes))
            records.append({"file": str(path), "pages": len(pdf.pages), "widthPt": width, "expectedWidthPt": expected_width,
                            "heightPt": height, "gotos": gotos, "numberInkErrorPt": number_errors})
for mode, sizes in normal_fonts.items():
    # This fixed fixture uses 16 CSS px normal text, i.e. 12 physical points.
    if len(sizes) != 6 or any(abs(size - 12) > .02 for size in sizes):
        failures.append(f"{mode}: normal text differs from the fixture's 12pt size")
result = {"accepted": not failures, "failures": failures, "records": records, "normalFontSizesPt": normal_fonts,
          "modeAccepted": {mode: not any(f.startswith(mode + ":") or f"{mode}-850" in f or f"{mode}-1200" in f for f in failures)
                           for mode in normal_fonts},
          "scope": "synthetic reading-width/menu path; not full A4 math re-layout acceptance"}
with args.output.open("x", encoding="utf-8") as stream:
    json.dump(result, stream, ensure_ascii=False, indent=2)
    stream.write("\n")
print(json.dumps({"accepted": not failures, "pdfs": len(records), "failures": failures}, ensure_ascii=False))
raise SystemExit(bool(failures))
