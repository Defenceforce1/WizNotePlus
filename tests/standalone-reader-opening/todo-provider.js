// Disposable Welcome fixture only. No account/database transport is opened.
(function () {
    const NativeDate = Date;
    class FixedDate extends NativeDate {
        constructor(...args) { super(...(args.length ? args : ['2026-10-23T04:00:00Z'])); }
        static now() { return new NativeDate('2026-10-23T04:00:00Z').getTime(); }
    }
    window.Date = FixedDate;
    const state = window.syntheticTodo = {queries: [], opened: [], created: [], updated: [], released: 0,
        handlers: {}, records: [], next: 1, failCreate: false, failDate: false, partial: false, held: null};
    const signal = name => ({connect(fn) { (state.handlers[name] || (state.handlers[name] = [])).push(fn); }});
    state.emit = name => (state.handlers[name] || []).forEach(fn => fn());
    const doc = record => ({GUID: record.guid, Title: record.title,
        DateCreated: '2026-10-23T04:00:00Z', DateModified: '2026-10-23T04:00:00Z',
        async GetParamValue(name) { return name === 'WIZ_TODO' ? record.task : record.date + ' 00:00:00'; },
        deleteLater() { state.released++; }});
    const database = {
        async GetRecentDocuments() { return []; },
        async DocumentsFromSQLWhere(sql) {
            state.queries.push(sql);
            if (!sql.includes("PARAM_NAME='WIZ_TODO'")) return [];
            const rows = state.records.filter(r => r.task === '1').sort((a, b) => a.date.localeCompare(b.date));
            if (state.holdNext) {
                state.holdNext = false;
                return await new Promise(resolve => { state.held = () => resolve(rows.map(doc)); });
            }
            return rows.map(doc);
        },
        async DocumentFromGUID(guid) {
            const record = state.records.find(r => r.guid === guid);
            return record ? doc(record) : null;
        }
    };
    for (const name of ['documentCreated', 'documentDeleted', 'documentModified', 'documentDataModified',
        'documentAccessDateModified', 'documentParamModified']) database[name] = signal(name);
    const windowApi = {
        ViewDocument(note) { state.opened.push(note.GUID); },
        async CreateTodo(title, date) {
            state.created.push({title, date});
            if (state.pauseCreate) await new Promise(resolve => { state.finishCreate = resolve; });
            if (state.failCreate) return {ok: false, error: 'note-create-failed'};
            const note = {guid: 'T' + state.next++, title, date, task: state.partial ? '0' : '1', body: 'unchanged synthetic note'};
            state.records.push(note);
            state.emit('documentCreated');
            return {ok: !state.partial, guid: note.guid, error: state.partial ? 'date-save-failed' : ''};
        },
        async SetTodoDate(guid, date) {
            state.updated.push({guid, date});
            if (state.failDate) return false;
            const note = state.records.find(r => r.guid === guid);
            if (!note) return false;
            note.date = date;
            note.task = date ? '1' : '0';
            state.emit('documentParamModified');
            return true;
        }
    };
    const app = {Database: database, Window: windowApi, async Locale() { return 'zh_CN'; },
        DocumentsCtrl: {SetDocuments() {}}};
    window.qt = {webChannelTransport: {}};
    window.QWebChannel = function (transport, callback) { callback({objects: {WizExplorerApp: app}}); };
})();
