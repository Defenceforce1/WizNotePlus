from pathlib import Path
import sys
source=Path(__file__).resolve().parents[2]/"src/gui/documentviewer/WizDocumentView.cpp"
s=source.read_text();start=s.index("void WizDocumentView::viewNote(");end=s.index("void WizDocumentView::reviewCurrentNote()",start)
Path(sys.argv[1]).write_text(s[start:end])
