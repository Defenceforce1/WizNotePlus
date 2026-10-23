#pragma once
// Optional upstream migration diagnostic exists only in disposable test fixtures.
static bool prepareWelcomeJquery4(QString& html, QTemporaryDir& root, const QString& output)
{
    if (!html.contains("jquery-4.0.0.min.js")) return true;
    html.replace("<div id=\"tabs\"", "<div id=\"dialog_link\" hidden></div><div id=\"tabs\"");
    QFile input(output + "/migration-input.js");
    if (!input.exists()) return true;
    if (!input.open(QIODevice::ReadOnly)) return false;
    QFile script(root.filePath("migration-input.js"));
    if (!script.open(QIODevice::WriteOnly)) return false;
    script.write(input.readAll()); script.close();
    const int core = html.indexOf("jquery-4.0.0.min.js"), end = html.indexOf("</script>", core);
    if (core < 0 || end < 0) return false;
    html.insert(end + 9, "\n<script src=\"" + QUrl::fromLocalFile(script.fileName()).toString() + "\"></script>");
    return true;
}

template<class Check>
static void checkWelcomeJquery4(QWebEnginePage* page, const QString& output, Check check)
{
    if (js(page, "jQuery.fn.jquery").toString() != "4.0.0") return;
    const bool diagnostic = QFile::exists(output + "/migration-input.js");
    check(js(page, "typeof jQuery.ajax==='function'&&typeof jQuery.Deferred==='function'&&typeof jQuery.fn.queue==='function'").toBool(),
          "jQuery4 full build retains Ajax Deferred and queue rather than slim");
    check(js(page, diagnostic ? "jQuery.migrateVersion==='4.0.2'" : "typeof jQuery.migrateVersion==='undefined'").toBool(),
          diagnostic ? "temporary official migration diagnostic loaded" : "production candidate works without migration shim");
    check(js(page, "!jQuery.migrateWarnings||jQuery.migrateWarnings.length===0").toBool(),
          "executed Welcome and task paths emit no migration warnings");
    check(js(page, "(()=>{const a=$('#dialog_link');a.trigger('mouseenter');const entered=a.hasClass('ui-state-hover');a.trigger('mouseleave');return entered&&!a.hasClass('ui-state-hover');})()").toBool(),
          "actual ready handler preserves mouseenter and mouseleave classes");
}
