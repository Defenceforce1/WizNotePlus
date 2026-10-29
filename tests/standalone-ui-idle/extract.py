from pathlib import Path
import sys
src,out=map(Path,sys.argv[1:])
def method(text,marker):
 start=text.index(marker);opening=text.index('{',start);level=0
 for i in range(opening,len(text)):
  if text[i]=='{':level+=1
  elif text[i]=='}':
   level-=1
   if level==0:return text[start:i+1]
 raise ValueError(marker)
s=(src/'src/gui/documentviewer/WizEditorToolBar.cpp').read_text()
parts=[method(s,'void WizEditorToolBar::'+n) for n in ['on_delegate_selectionChanged(const QString& currentStyle)','on_delay_updateToolbar()']]
(out/'toolbar.inc').write_text('\n'.join(parts).replace('WizEditorToolBar::','ToolbarHarness::'))
combo=s[s.index('class CWizToolComboBox :'):]
p=method(combo,'void setText(const QString& strText)');(out/'combo.inc').write_text(p)
c=(src/'src/gui/categoryviewer/WizCategoryView.cpp').read_text()
if 'void WizCategoryView::scheduleShortcutSave()' in c:
 p=method(c,'void WizCategoryView::scheduleShortcutSave()').replace('WizCategoryView::','ShortcutHarness::')
 (out/'shortcut.inc').write_text(p)
else:(out/'shortcut.inc').write_text('void ShortcutHarness::scheduleShortcutSave(){QTimer::singleShot(200,this,&ShortcutHarness::saveShortcutState);}')

# Reuse the actual immediate-save preamble, so cancellation is not a mirrored mock.
save=method(c,'void WizCategoryView::saveShortcutState()')
preamble=save[save.index('{')+1:save.index('WizCategoryViewItemBase *pShortcutRoot')]
(out/'shortcut-save-preamble.inc').write_text(preamble)
