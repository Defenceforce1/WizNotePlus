#pragma once
// Product Welcome HTML/business JS, local synthetic transport only.
static int runTodoWelcome(QTemporaryDir& root, const QString& resources, const QString& output)
{
    QFile marker(output + "/.wiz-synthetic-todo");
    if (!marker.open(QIODevice::ReadOnly) || marker.readAll() != "synthetic-only\n") return 4;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok; qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
        return ok;
    };
    QFile provider(output + "/provider-input.js"), original(resources + "welcomepage/index.html");
    if (!provider.open(QIODevice::ReadOnly) || !original.open(QIODevice::ReadOnly)) return 5;
    QFile transport(root.filePath("todo-provider.js"));
    if (!transport.open(QIODevice::WriteOnly)) return 5;
    transport.write(provider.readAll()); transport.close();
    QString html = QString::fromUtf8(original.readAll());
    const auto base = QUrl::fromLocalFile(resources + "welcomepage/").toString().toHtmlEscaped();
    html.replace("src=\"js/", "src=\"" + base + "js/");
    html.replace("href=\"css/", "href=\"" + base + "css/");
    html.replace("../webengine/wizwebchannel.js", QUrl::fromLocalFile(transport.fileName()).toString());
    if (!prepareWelcomeJquery4(html, root, output)) return 5;
    QFile fixture(root.filePath("todo-welcome.html"));
    if (!fixture.open(QIODevice::WriteOnly)) return 5;
    fixture.write(html.toUtf8()); fixture.close();
    QWebEngineProfile profile; profile.setCachePath(root.filePath("cache")); profile.setPersistentStoragePath(root.filePath("profile"));
    LocalOnly local; profile.setUrlRequestInterceptor(&local);
    QWebEngineView view; view.setAttribute(Qt::WA_DontShowOnScreen); view.resize(1100, 800);
    auto* page = new QWebEnginePage(&profile, &view); view.setPage(page);
    bool loaded = false, ok = false;
    QObject::connect(&view, &QWebEngineView::loadFinished, [&](bool success) { loaded = true; ok = success; });
    view.show(); view.load(QUrl::fromLocalFile(fixture.fileName()));
    if (!check(wait([&] { return loaded; }) && ok, "actual local Welcome assets load")) return 6;
    if (!check(wait([&] { return js(page, "window.syntheticTodo&&getComputedStyle(document.body).visibility==='visible'").toBool(); }), "actual Welcome initialization completes")) return 7;
    check(js(page, "todoForm.hidden&&listTodo.textContent==='暂无待办'&&todoAdd.textContent==='添加'").toBool(), "simple Chinese Add entry with initially hidden form");
    js(page, "todoAdd.click();");
    check(js(page, "!todoForm.hidden&&todoTitle.value===''&&todoDate.value==='2026-10-23'&&!todoTitle.readOnly").toBool(), "Add opens title and local-date fields");
    js(page, "todoCancel.click();");
    check(js(page, "todoForm.hidden&&syntheticTodo.created.length===0").toBool(), "Cancel creates no note");
    js(page, "todoAdd.click();todoTitle.value='   ';todoForm.requestSubmit();");
    check(js(page, "syntheticTodo.created.length===0").toBool(), "blank title does not invoke native creation");
    js(page, "todoTitle.value='合成待办 <b>原样标题</b>';todoDate.value='2026-12-30';todoForm.requestSubmit();");
    check(wait([&] { return js(page, "syntheticTodo.created.length===1&&todoForm.hidden&&listTodo.textContent.includes('2026-12-30')").toBool(); }), "future task beyond seven days appears immediately after successful creation");
    check(js(page, "!listTodo.querySelector('b')&&listTodo.textContent.includes('<b>原样标题</b>')").toBool(), "task title displayed as literal text");
    js(page, "listTodo.querySelector('a').click();");
    check(wait([&] { return js(page, "syntheticTodo.opened.join(',')==='T1'").toBool(); }), "task title opens corresponding ordinary note");
    js(page, "listTodo.querySelector('.todo-date').click();");
    check(js(page, "!todoForm.hidden&&todoTitle.readOnly&&todoDate.value==='2026-12-30'&&todoSave.textContent==='保存'").toBool(), "click date edits existing task without renaming note");
    js(page, "todoDate.value='2026-10-20';todoForm.requestSubmit();");
    check(wait([&] { return js(page, "todoForm.hidden&&listTodo.querySelector('.todo-overdue')&&syntheticTodo.created.length===1").toBool(); }), "date change keeps one note and overdue task visible");
    js(page, "syntheticTodo.failDate=true;listTodo.querySelector('button:last-child').click();");
    check(wait([&] { return js(page, "!todoBusy&&!document.getElementById('todoStatus').hidden&&!!listTodo.querySelector('a')").toBool(); }), "failed completion retains displayed task and reports failure");
    js(page, "syntheticTodo.failDate=false;listTodo.querySelector('button:last-child').click();");
    check(wait([&] { return js(page, "!todoBusy&&listTodo.textContent==='暂无待办'").toBool(); }), "completion removes task from Welcome");
    check(js(page, "syntheticTodo.records.length===1&&syntheticTodo.records[0].body==='unchanged synthetic note'").toBool(), "completion leaves ordinary note and body untouched");
    js(page, "todoAdd.click();todoTitle.value='创建失败保留表单';syntheticTodo.failCreate=true;todoForm.requestSubmit();");
    check(wait([&] { return js(page, "!todoBusy&&!todoForm.hidden&&todoTitle.value==='创建失败保留表单'&&!document.getElementById('todoStatus').hidden").toBool(); }), "ordinary creation failure retains form input");
    js(page, "syntheticTodo.failCreate=false;syntheticTodo.partial=true;todoForm.requestSubmit();");
    check(wait([&] { return js(page, "!todoBusy&&todoGuid==='T2'&&todoTitle.readOnly&&document.getElementById('todoStatus').textContent.includes('不会重复')").toBool(); }), "partial metadata failure retains created identity for retry");
    js(page, "syntheticTodo.partial=false;todoForm.requestSubmit();");
    check(wait([&] { return js(page, "!todoBusy&&todoForm.hidden&&syntheticTodo.records.length===2&&listTodo.textContent.includes('创建失败保留表单')").toBool(); }), "retry saves date on same note without duplicate creation");
    js(page, "todoAdd.click();todoTitle.value='排队双击';syntheticTodo.pauseCreate=true;todoForm.requestSubmit();todoForm.dispatchEvent(new Event('submit',{cancelable:true}));");
    check(wait([&] { return js(page, "todoBusy&&todoSave.disabled&&typeof syntheticTodo.finishCreate==='function'").toBool(); }), "in-flight creation disables controls and duplicate submit");
    check(js(page, "syntheticTodo.created.filter(r=>r.title==='排队双击').length===1").toBool(), "duplicate submit invokes create once");
    js(page, "syntheticTodo.finishCreate();syntheticTodo.pauseCreate=false;");
    check(wait([&] { return js(page, "!todoBusy&&todoForm.hidden&&syntheticTodo.records.length===3").toBool(); }), "pending create finishes normally");
    js(page, "syntheticTodo.holdNext=true;window.oldRefreshFinished=false;listEvents().then(()=>oldRefreshFinished=true);");
    check(wait([&] { return js(page, "typeof syntheticTodo.held==='function'").toBool(); }), "old refresh may finish late");
    js(page, "syntheticTodo.records.find(r=>r.guid==='T3').task='0';listEvents();");
    check(wait([&] { return js(page, "!listTodo.textContent.includes('排队双击')").toBool(); }), "new refresh reflects current completion");
    js(page, "syntheticTodo.held();syntheticTodo.held=null;");
    check(wait([&] { return js(page, "oldRefreshFinished&&!listTodo.textContent.includes('排队双击')").toBool(); }), "late refresh cannot restore completed task");
    check(js(page, "dateToStr(new Date('2026-10-23T00:10:00+08:00'))==='2026-10-23'").toBool(), "local midnight date does not shift to prior UTC day");
    check(js(page, "$('#datepicker').hasClass('hasDatepicker')&&$('#tabs').tabs('option','active')===0").toBool(), "existing calendar and tabs retained");
    check(view.grab().save(output + "/todo-welcome.png"), "capture synthetic Welcome for visual review");
    js(page, "todoAdd.click();todoTitle.value='下一项待办';");
    check(view.grab().save(output + "/todo-form.png"), "capture minimal creation form");
    checkWelcomeJquery4(page, output, check);
    check(local.blocked.load() == 0, "no external requests or account access");
    check(!view.windowHandle() || !view.windowHandle()->isExposed(), "no foreground test window");
    QJsonObject result{{"checks", checks}, {"failures", failures}, {"syntheticTransportOnly", true},
        {"actualDatabaseOrAccountAccepted", false}, {"externalBlocked", local.blocked.load()},
        {"jquery",js(page,"jQuery.fn.jquery").toString()},
        {"migrationDiagnostic",QFile::exists(output+"/migration-input.js")},
        {"migrationWarnings",QJsonArray::fromVariantList(js(page,"jQuery.migrateWarnings||[]").toList())},
        {"query", js(page, "syntheticTodo.queries.find(q=>q.includes(\"PARAM_NAME='WIZ_TODO'\"))").toString()}};
    QFile report(output + "/runtime.json"); if (!report.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 8;
    report.write(QJsonDocument(result).toJson(QJsonDocument::Indented));
    profile.setUrlRequestInterceptor(nullptr);
    return failures ? 9 : 0;
}
