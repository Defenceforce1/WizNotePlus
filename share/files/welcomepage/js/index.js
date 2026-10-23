var objApp = null;
var objDatabase = null;

const ITEM_LIMITS = 10;
let todoRevision = 0;
let todoBusy = false;
let todoGuid = "";

const en_US = {
    "labelOverview": "Overview",
    "labelNews": "News",
    "labelRecentDocuments": "Recent Documents",
    "labelRecentAccessedDocuments": "Recent Read",
    "labelUnreadDocuments": "Unread Documents",
    "unreadEmpty": "No documents",
    "labelTodo": "Todo",
    "todoAdd": "Add",
    "todoSave": "Add",
    "todoCancel": "Cancel",
    "todoTitle": "Title",
    "todoDate": "Date",
    "todoEdit": "Save",
    "todoDone": "Complete",
    "todoEmpty": "No pending tasks",
    "todoFailed": "Could not save. Please retry.",
    "todoPartial": "Note created; date not saved. Retry to keep this same note.",
    "Title": "Welcome to Wiz"
}

const zh_CN = {
    "labelOverview": "近期概况",
    "labelNews": "为知笔记动态",
    "labelRecentDocuments": "近期文档",
    "labelRecentAccessedDocuments": "近期阅读",
    "labelUnreadDocuments": "未读文档",
    "unreadEmpty": "暂无文档",
    "labelTodo": "待办事宜",
    "todoAdd": "添加",
    "todoSave": "添加",
    "todoCancel": "取消",
    "todoTitle": "待办标题",
    "todoDate": "日期",
    "todoEdit": "保存",
    "todoDone": "完成",
    "todoEmpty": "暂无待办",
    "todoFailed": "保存失败，请重试。",
    "todoPartial": "笔记已创建，日期保存失败；请重试，不会重复创建笔记。",
    "Title": "欢迎使用为知笔记"
}

function welcomeUiLocale(locale) {
    // Match the native UI policy: Chinese uses Simplified; all other UI uses
    // English. Legacy locale preferences are not rewritten or restored.
    return /^zh(?:[-_]|$)/i.test(String(locale || "")) ? "zh_CN" : "en_US";
}

function dateToStr(dt) {
    return String(dt.getFullYear()).padStart(4, "0") + "-" + formatInt(dt.getMonth() + 1) + "-" + formatInt(dt.getDate());
}

function strToDate(str) {
    var timePos = str.indexOf(" ");
    if (timePos != -1) {
        str = str.substring(0, timePos);
    }
    var a = str.split('-');
    if (a.length != 3)
        return null;

    var year = parseInt(a[0], 10);
    var month = parseInt(a[1], 10);
    var day = parseInt(a[2], 10);

    var ret = new Date();
    ret.setFullYear(year, month - 1, day);
    return ret;
}

async function viewDocument(doc_guid) {
    const doc = await objDatabase.DocumentFromGUID(doc_guid);
    if (doc != null) {
        objApp.Window.ViewDocument(doc, true);
    }
    if (doc) doc.deleteLater();
}

function getMetaInt(objDatabase, metaName, metaKey, defVal) {
    var value = objDatabase.Meta(metaName, metaKey);
    if (value == null)
        return defVal;
    if (value == "")
        return defVal;
    try {
        return parseInt(value, 10);
    }
    catch (e) {
        return defVal;
    }
}

async function listDocuments() {
    const documents = await objDatabase.GetRecentDocuments("", ITEM_LIMITS, 0);
    listRecent.innerHTML = "";
    for (const doc of documents) {
        const item = document.createElement("li");
        const docDate = doc.DateModified == "1970-01-01T08:00:00" ? doc.DateCreated : doc.DateModified;
        const date = new Date(docDate);
        const dateString = date.toLocaleDateString();
        // Update UI
        item.innerHTML = "<a href=\"javascript:void(0);\" onclick=\"viewDocument('" + doc.GUID + "');\" >" + doc.Title + "</a>&nbsp;(" + dateString + ")";
        listRecent.appendChild(item);
        // Free up memory space of C++ Object
        doc.deleteLater();
    }
}

async function listDocumentsRecentAccessed() {
    const documents = await objDatabase.GetRecentDocuments("", ITEM_LIMITS, 3);
    listRecentAccessed.innerHTML = "";
    for (const doc of documents) {
        const item = document.createElement("li");
        const docDate = doc.DateModified == "1970-01-01T08:00:00" ? doc.DateCreated : doc.DateModified;
        const date = new Date(docDate);
        const dateString = date.toLocaleDateString();
        // Update UI
        item.innerHTML = "<a href=\"javascript:void(0);\" onclick=\"viewDocument('" + doc.GUID + "');\" >" + doc.Title + "</a>&nbsp;(" + dateString + ")";
        listRecentAccessed.appendChild(item);
        // Free up memory space of C++ Object
        doc.deleteLater();
    }
}

async function listUnreadDocuments() {
    let sql = `DOCUMENT_LOCATION not like '/Deleted Items/%'` + 
        // select zero read count documents
        ` and DOCUMENT_READ_COUNT=0` + 
        // Do not select calendar events
        ` and DOCUMENT_GUID not in (select DOCUMENT_GUID from WIZ_DOCUMENT_PARAM where PARAM_NAME = 'CALENDAR_START')` +
        // Random select
        ` order by random() limit ${ITEM_LIMITS}`;

    const documents = await objDatabase.DocumentsFromSQLWhere(sql);
    listUnread.innerHTML = "";
    if (documents.length == 0) {
        const item = document.createElement("li");
        item.textContent = i18next.t("unreadEmpty");
        listUnread.appendChild(item);
        return;
    }
    for (const doc of documents) {
        const item = document.createElement("li");
        const docDate = doc.DateModified == "1970-01-01T08:00:00" ? doc.DateCreated : doc.DateModified;
        const date = new Date(docDate);
        const dateString = date.toLocaleDateString();
        //
        item.innerHTML = "<a href=\"javascript:void(0);\" onclick=\"viewDocument('" + doc.GUID + "');\" >" + doc.Title + "</a>&nbsp;(" + dateString + ")";
        listUnread.appendChild(item);
        //
        doc.deleteLater();
    }
}

async function listEvents() {
    const revision = ++todoRevision;
    const begin = new Date();
    const end = new Date();
    end.setDate(end.getDate() + 7);
    // Explicit tasks remain visible when overdue or more than seven days away.
    // Retain the old seven-day window for legacy calendar notes only. Use local
    // dates, not UTC dates (which shift the day near midnight in many timezones).
    const task = `DOCUMENT_GUID in (select DOCUMENT_GUID from WIZ_DOCUMENT_PARAM where PARAM_NAME='WIZ_TODO' and PARAM_VALUE='1')`;
    const legacy = `DOCUMENT_GUID not in (select DOCUMENT_GUID from WIZ_DOCUMENT_PARAM where PARAM_NAME='WIZ_TODO')`
        + ` and DOCUMENT_GUID in (select DOCUMENT_GUID from WIZ_DOCUMENT_PARAM where PARAM_NAME='CALENDAR_START' and PARAM_VALUE <= '${dateToStr(end)} 23:59:59')`
        + ` and DOCUMENT_GUID in (select DOCUMENT_GUID from WIZ_DOCUMENT_PARAM where PARAM_NAME='CALENDAR_END' and PARAM_VALUE >= '${dateToStr(begin)} 00:00:00')`;
    const sql = `DOCUMENT_LOCATION not like '/Deleted Items/%' and ((${task}) or (${legacy}))`
        + ` order by (select PARAM_VALUE from WIZ_DOCUMENT_PARAM p where p.DOCUMENT_GUID=WIZ_DOCUMENT.DOCUMENT_GUID and PARAM_NAME='CALENDAR_START'), DOCUMENT_GUID`;
    const events = await objDatabase.DocumentsFromSQLWhere(sql) || [];
    const items = [];
    try {
        for (const doc of events) {
            if (revision !== todoRevision) return;
            const date = (await doc.GetParamValue("CALENDAR_START") || "").slice(0, 10);
            if (revision !== todoRevision) return;
            const isTask = await doc.GetParamValue("WIZ_TODO") === "1";
            if (revision !== todoRevision) return;
            const item = document.createElement("li");
            const link = document.createElement("a");
            const guid = doc.GUID, title = doc.Title;
            link.href = "#";
            link.textContent = title;
            link.onclick = event => { event.preventDefault(); viewDocument(guid); };
            item.appendChild(link);
            const dateLabel = document.createElement(isTask ? "button" : "span");
            dateLabel.textContent = date;
            dateLabel.className = "todo-date" + (isTask ? " todo-action" : "")
                + (date < dateToStr(begin) ? " todo-overdue" : "");
            if (isTask) {
                dateLabel.type = "button";
                dateLabel.title = i18next.t("todoDate");
                dateLabel.onclick = () => showTodoForm(guid, title, date);
                const done = document.createElement("button");
                done.type = "button";
                done.textContent = i18next.t("todoDone");
                done.className = "todo-action";
                done.onclick = () => completeTodo(guid);
                item.appendChild(dateLabel);
                item.appendChild(done);
            } else item.appendChild(dateLabel);
            items.push(item);
        }
    } finally {
        for (const doc of events) doc.deleteLater();
    }
    if (revision !== todoRevision) return;
    if (!items.length) {
        const empty = document.createElement("li");
        empty.textContent = i18next.t("todoEmpty");
        items.push(empty);
    }
    document.getElementById("listTodo").replaceChildren(...items);
}

function todoStatus(key) {
    const status = document.getElementById("todoStatus");
    status.textContent = key ? i18next.t(key) : "";
    status.hidden = !key;
}

function showTodoForm(guid = "", title = "", date = dateToStr(new Date())) {
    if (todoBusy) return;
    todoGuid = guid;
    todoStatus("");
    document.getElementById("todoForm").hidden = false;
    document.getElementById("todoTitle").value = title;
    document.getElementById("todoTitle").readOnly = !!guid;
    document.getElementById("todoDate").value = date;
    document.getElementById("todoSave").textContent = i18next.t(guid ? "todoEdit" : "todoSave");
    document.getElementById(guid ? "todoDate" : "todoTitle").focus();
}

function setTodoBusy(busy) {
    todoBusy = busy;
    for (const id of ["todoAdd", "todoTitle", "todoDate", "todoSave", "todoCancel"])
        document.getElementById(id).disabled = busy;
}

async function saveTodo(event) {
    event.preventDefault();
    if (todoBusy) return;
    const form = document.getElementById("todoForm");
    if (!form.reportValidity()) return;
    const title = document.getElementById("todoTitle").value.trim();
    if (!title) { document.getElementById("todoTitle").focus(); return; }
    const date = document.getElementById("todoDate").value;
    setTodoBusy(true);
    todoStatus("");
    try {
        let result;
        if (todoGuid) result = {ok: await objApp.Window.SetTodoDate(todoGuid, date)};
        else result = await objApp.Window.CreateTodo(title, date);
        if (!result || !result.ok) {
            if (result && result.guid) {
                todoGuid = result.guid;
                document.getElementById("todoTitle").readOnly = true;
            }
            todoStatus(result && result.error === "date-save-failed" ? "todoPartial" : "todoFailed");
            return;
        }
        form.hidden = true;
        todoGuid = "";
        await listEvents();
    } catch (error) {
        todoStatus("todoFailed");
    } finally { setTodoBusy(false); }
}

async function completeTodo(guid) {
    if (todoBusy) return;
    setTodoBusy(true);
    todoStatus("");
    try {
        if (!await objApp.Window.SetTodoDate(guid, "")) { todoStatus("todoFailed"); return; }
        if (todoGuid === guid) {
            document.getElementById("todoForm").hidden = true;
            todoGuid = "";
        }
        await listEvents();
    } catch (error) { todoStatus("todoFailed"); }
    finally { setTodoBusy(false); }
}

function formatInt(val) {
    if (val < 10)
        return "0" + val;
    else
        return "" + val;
}

async function listDocumentsByDate(dt) {
    if (typeof (objApp) == "undefined")
        return 0;

    var begin_date_string = dt.getFullYear() + "-" + formatInt(dt.getMonth() + 1) + "-" + formatInt(dt.getDate()) + " 00:00:00";
    var end_date_string = dt.getFullYear() + "-" + formatInt(dt.getMonth() + 1) + "-" + formatInt(dt.getDate()) + " 23:59:59";

    var sql = "DT_CREATED >='" + begin_date_string + "' and DT_CREATED <='" + end_date_string + "'"; //  and DOCUMENT_TYPE='journal'";

    var documents = await objDatabase.DocumentsFromSQLWhere(sql);

    if (documents == null)
        return 0;

    const guidList = new Array();
    for (const doc of documents) {
        guidList.push(doc.GUID);
        doc.deleteLater();
    }

    objApp.DocumentsCtrl.SetDocuments(guidList);

    return 0;
}

function viewDocumentsByDate(dateText) {
    var date = strToDate(dateText);
    try {
        listDocumentsByDate(date);
    }
    catch (err) {
    }
}

$(document).ready(function() {
    // Initialize UI
    $('#tabs').tabs();
    $('#datepicker').datepicker({
        inline: true,
        dateFormat: 'yy-mm-dd',
        onSelect: function(date) { viewDocumentsByDate(date); },
        prevText: '',
        nextText: ''
    });
    $('#dialog_link, ul#icons li')
        .on('mouseenter', function() { $(this).addClass('ui-state-hover'); })
        .on('mouseleave', function() { $(this).removeClass('ui-state-hover'); });

    // Initialize WizNote APIs
    new QWebChannel(qt.webChannelTransport, async function(channel) {
        var objectNames = ["WizExplorerApp"];
        for (var i = 0; i < objectNames.length; i++) {
            var key = objectNames[i];
            window[key] = channel.objects[key];
        }
        // Get APIs
        objApp = WizExplorerApp;
        objDatabase = WizExplorerApp.Database;
        // Localize html
        const lang = welcomeUiLocale(await objApp.Locale());
        const t = await i18next.init({
            lng: lang,
            resources: {
                "en_US": {
                    translation: en_US
                },
                "zh_CN": {
                    translation: zh_CN
                }
            }
        });
        for (const id of Object.keys(en_US)) {
            if (id == "Title") {
                document.title = t("Title");
                continue;
            }

            const elem = document.getElementById(id);
            if (elem) {
                if (elem.tagName === "INPUT") {
                    elem.setAttribute("aria-label", t(id));
                    if (id === "todoTitle") elem.placeholder = t(id);
                } else elem.textContent = t(id);
            }
        }
        $('#datepicker').datepicker(
            'option',
            Object.assign({}, $.datepicker.regional[lang.replace(/_/, "-")] || $.datepicker.regional[""],
                {dateFormat: 'yy-mm-dd', prevText: '', nextText: ''})
        );

        objDatabase.documentCreated.connect(listDocuments);
        objDatabase.documentCreated.connect(listEvents);
        objDatabase.documentDeleted.connect(listDocuments);
        objDatabase.documentDeleted.connect(listEvents);
        objDatabase.documentModified.connect(listDocuments);
        objDatabase.documentModified.connect(listEvents);
        objDatabase.documentDataModified.connect(listDocuments);
        objDatabase.documentAccessDateModified.connect(listDocumentsRecentAccessed);
        if (objDatabase.documentParamModified)
            objDatabase.documentParamModified.connect(listEvents);

        document.getElementById("todoAdd").disabled = false;
        document.getElementById("todoAdd").onclick = () => showTodoForm();
        document.getElementById("todoForm").onsubmit = saveTodo;
        document.getElementById("todoCancel").onclick = () => {
            if (todoBusy) return;
            document.getElementById("todoForm").hidden = true;
            todoGuid = "";
            todoStatus("");
        };

        await listDocuments();
        await listDocumentsRecentAccessed();
        await listEvents();
        await listUnreadDocuments();
        // Display page
        $('body').css('visibility', 'visible');
    });
})
