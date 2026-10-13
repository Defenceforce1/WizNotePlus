
function WizStartEditorAmend() {
  try {
    if (!WizEditor) {
      alert("WizEditor is null!");
      return;
    }
    //
    WizEditor.amend.on();
  }
  catch (e) {
    console.log(e.toString());
  }
}

function WizStopEditorAmend() {
  try {
    if (!WizEditor) {
      alert("WizEditor is null!");
      return;
    }
    //
    WizEditor.amend.off();
  }
  catch (e) {
    console.log(e.toString());
  }
}

////////////////////////////////////////////////////////////////////////////////

/*
  function for C++ execute
*/
function WizEditorInit(basePath, browserLang, userGUID, userAlias, ignoreTable, noteType, enableNightMode) {
  return new Promise( (resolve, reject) => {
    // Build web channel to C++ side
    new QWebChannel(qt.webChannelTransport, channel => {
      // Init WizNotePlus APIs
      const objectNames = ["WizExplorerApp", "WizQtEditor"];
      for (let i = 0; i < objectNames.length; i++) {
          const key = objectNames[i];
          window[key] = channel.objects[key];
      }
      // Init WizEditor
      try {
        if (!WizEditor) {
          alert("WizEditor is null!");
          return;
        }
        var user = {
          user_guid: userGUID,
          user_name: userAlias
        };
        //
        var editorOptions = {
          document: document,
          lang: browserLang,
          noteType: noteType,
          userInfo: user,
          clientType: "mac",
          ignoreTable: ignoreTable,
          dependencyUrl: basePath + 'dependency',
          nightMode: {
            enable: enableNightMode,
            color: '#a6a6a6',
            bgColor: '#272727',
          },
          editor: {
            callback: {
              onKeyDown: WizOnKeyDown,
              markerUndo: WizOnUndoStatusChanged,
              markerInitiated: WizOnMarkerInitiated,
            }
          },
          reader: {
            type: noteType,
            callback: {
              markdown: function () { WizApplyNoteStyle(); },
              mathJax: function () { WizApplyNoteStyle(); }
            }
          }
        }
        //
        WizEditor.init(editorOptions);
        WizEditor.addListener(WizEditor.ListenerType.SelectionChange, WizOnSelectionChange)
        resolve(true);
      }
      catch (e) {
        console.log(e.toString());
        reject(false);
      }
    });
  });
}

// Temporary engine style: excluded from saved note HTML by the original editor.
function WizApplyNoteStyle(css) {
  if (typeof css === 'string') window.WizNoteCss = css;
  if (!window.WizEditor || typeof WizEditor.insertCustomStyle !== 'function') return;
  if (window.WizNoteCss) WizEditor.insertCustomStyle('wiz_tmp_style_note_presentation', window.WizNoteCss, true);
  else WizEditor.removeStyleById('wiz_tmp_style_note_presentation');
}

function WizOnSelectionChange(style) {
  try {
    WizQtEditor.onSelectionChange(JSON.stringify(style));
  } catch (e) {

  }
}

function WizOnKeyDown(event) {
  try {
    //
    if (event.key === 'Enter') {
      if (!WizEditor.macFirstReturnKeyPressed) {
        WizEditor.macFirstReturnKeyPressed = true;
        //
        try {
          WizQtEditor.onReturn();
        } catch (err) {
          console.error(err);
        }
      }
    } else if (event.charCode === 99 && event.metaKey) {
        try {
            WizQtEditor.doCopy();
        } catch (err) {
            console.error(err);
        }
    }
  } catch (e) {
    console.error(e);
  } finally {
    return true;
  }
}

function WizOnUndoStatusChanged(data) {
  WizQtEditor.onMarkerUndoStatusChanged(data);
}

function WizOnMarkerInitiated(data) {
  WizQtEditor.onMarkerInitiated(data);
}



function WizAddCssForCode(cssFile) {
  console.log("WizAddCssForCode called , css file " + cssFile);
  var doc = document;
  if (!doc)
    return;
  //
  var oldLink = doc.getElementById('wiz_code_highlight_link');
  if (oldLink) {
    console.log("old css link find, try to remove");
    oldLink.parentNode.removeChild(oldLink);
  }
  //
  var link = doc.createElement('link');
  if (!link)
    return;
  try {
    link.type = 'text/css';
    link.rel = 'stylesheet';
    link.id = 'wiz_code_highlight_link';
    link.href = cssFile;
    //
    if (!doc.head) {
      doc.insertBefore(doc.createElement('head'), doc.body);
    }
    doc.head.appendChild(link);
  }
  catch (e) {

  }
}
