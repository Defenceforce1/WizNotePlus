"""Prepare an isolated lifetime-counting copy of the actual HTML parser.
Usage: python3 this_file --source-root /path/to/source --output /new/test/root
Compile the generated src/html/WizHtmlReader.cpp and main.cpp with the normal
project flags and links, replacing the corresponding two application objects.
Put generated src first on the include path. No main window or account is opened.
Only constructor/destructor counters are injected; deletion/parsing logic stays
byte-equivalent apart from line endings. Counter fields do not alter object layout.
No test hooks are written to product sources. The output directory must be new.
"""
from pathlib import Path
import argparse, hashlib, json
p=argparse.ArgumentParser()
p.add_argument('--source-root',type=Path,required=True)
p.add_argument('--output',type=Path,required=True)
a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
src=a.source_root.resolve()/'src/html'
h=(src/'WizHtmlReader.h').read_text(encoding='utf-8-sig')
c=(src/'WizHtmlReader.cpp').read_text(encoding='utf-8-sig')
needle='class WizHtmlElemAttr\n{'
assert h.count(needle)==1
h=h.replace(needle,'extern long wizHtmlAttributesCreated, wizHtmlAttributesDestroyed;\n'+needle)
needle='    WizHtmlElemAttr(const WizHtmlElemAttr &rSource);'
assert h.count(needle)==1
h=h.replace(needle,needle+'\n    ~WizHtmlElemAttr() { ++wizHtmlAttributesDestroyed; }')
for signature in ['WizHtmlElemAttr::WizHtmlElemAttr(const CString& strAttribName, const CString& strAttribValue )', 'WizHtmlElemAttr::WizHtmlElemAttr(const WizHtmlElemAttr &rSource)']:
 assert c.count(signature)==1
 begin=c.index('{',c.index(signature))+1
 c=c[:begin]+'\n    ++wizHtmlAttributesCreated;'+c[begin:]
# Retain the relative helper include without copying or traversing the source tree.
folder=a.output/'src/html';folder.mkdir(parents=True)
(a.output/'src/share').symlink_to(a.source_root.resolve()/'src/share',target_is_directory=True)
(folder/'WizHtmlReader.h').write_text(h)
(folder/'WizHtmlReader.cpp').write_text(c)
(a.output/'main.cpp').write_bytes((Path(__file__).parent/'html-attribute-lifetime.cpp').read_bytes())
(a.output/'manifest.json').write_text(json.dumps({'parserSha256':hashlib.sha256((src/'WizHtmlReader.cpp').read_bytes()).hexdigest(),'headerSha256':hashlib.sha256((src/'WizHtmlReader.h').read_bytes()).hexdigest(),'instrumentation':'constructor/destructor counts only; actual delete and parse methods unchanged','productSourceWritten':False},indent=2)+'\n')
