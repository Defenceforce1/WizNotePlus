#!/usr/bin/env python3
"""Read-only internal-link audit. Never repair/rewrite a generated PDF.

Exact PDF-name matching determines resolution. A decoded-name candidate or a
file:// URI with a fragment is reported separately, not treated as a valid GoTo.
"""
import argparse
import json
from pathlib import Path
from urllib.parse import unquote, urlparse

from pypdf import PdfReader
from pypdf.generic import ArrayObject, ByteStringObject, NameObject, TextStringObject

parser = argparse.ArgumentParser()
parser.add_argument("directory", type=Path)
parser.add_argument("--files", nargs="+", default=["references-5mm.pdf", "references-20mm.pdf", "references-visible-heading.pdf"])
parser.add_argument("--output", type=Path)
args = parser.parse_args()


def name(value):
    if isinstance(value, ByteStringObject):
        return bytes(value).decode("utf-8", errors="replace")
    text = str(value)
    return text[1:] if isinstance(value, NameObject) and text.startswith("/") else text


records = []
for filename in args.files:
    if Path(filename).name != filename or not filename.endswith(".pdf"):
        raise ValueError("PDF basenames within the synthetic directory required")
    reader = PdfReader(args.directory / filename)
    destinations = {str(key).lstrip("/"): value for key, value in reader.named_destinations.items()}
    named = []
    for key, dest in destinations.items():
        page = reader.get_destination_page_number(dest)
        top = dest.get("/Top")
        named.append({"name": key, "page": page + 1 if page is not None else None,
                      "topFromPageTopPt": float(reader.pages[page].mediabox.height) - float(top)
                      if page is not None and top is not None else None})
    links = []
    for page_index, page in enumerate(reader.pages):
        for ref in page.get("/Annots", []):
            annotation = ref.get_object()
            if annotation.get("/Subtype") != "/Link":
                continue
            action = annotation.get("/A")
            action = action.get_object() if action else {}
            destination = annotation.get("/Dest")
            if action.get("/S") == "/GoTo":
                destination = action.get("/D")
            item = {"sourcePage": page_index + 1, "rect": [float(x) for x in annotation.get("/Rect", [])]}
            if destination is not None:
                destination = destination.get_object() if hasattr(destination, "get_object") else destination
                if isinstance(destination, ArrayObject):
                    item.update(kind="goto-array", destinationItems=len(destination))
                elif isinstance(destination, (TextStringObject, ByteStringObject, NameObject)):
                    key = name(destination)
                    item.update(kind="goto-named", name=key, resolved=key in destinations,
                                decodedNameCandidate=unquote(key) if unquote(key) in destinations and key not in destinations else None)
                else:
                    item.update(kind="unsupported-destination", type=type(destination).__name__)
            elif action.get("/S") == "/URI":
                uri = str(action.get("/URI", ""))
                parsed = urlparse(uri)
                item.update(kind="uri", uri=uri, localFragment=bool(parsed.fragment and parsed.scheme in ("file", "")))
            else:
                item.update(kind="other", action=str(action.get("/S", "")))
            links.append(item)
    metadata = reader.metadata or {}
    records.append({"file": filename, "pages": len(reader.pages), "creator": metadata.get("/Creator"),
                    "producer": metadata.get("/Producer"), "namedDestinations": named, "links": links,
                    "unresolvedNamed": [x for x in links if x["kind"] == "goto-named" and not x["resolved"]],
                    "localFragmentUris": [x for x in links if x.get("localFragment")]})
result = json.dumps({"readOnly": True, "warning": "Resolved destinations do not prove positional accuracy.", "records": records}, ensure_ascii=False, indent=2)
if args.output:
    with args.output.open("x", encoding="utf-8") as output:
        output.write(result + "\n")
print(result)
