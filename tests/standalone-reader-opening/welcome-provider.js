// Synthetic provider replacing only the QWebChannel transport in a local fixture.
// Real welcome HTML, UI libraries, locale files and index.js are not rewritten.
(function(){
const NativeDate=Date;class FixedDate extends NativeDate{constructor(...a){super(...(a.length?a:['2026-10-23T04:00:00Z']));}static now(){return new NativeDate('2026-10-23T04:00:00Z').getTime();}}window.Date=FixedDate;
const state=window.syntheticWelcome={queries:[],opened:[],selected:[],released:0,handlers:{},recentRevision:0};
const signal=name=>({connect(fn){(state.handlers[name]||(state.handlers[name]=[])).push(fn);}});
function doc(id,title){return{GUID:id,Title:title,DateCreated:'2026-10-22T04:00:00Z',DateModified:'2026-10-23T04:00:00Z',deleteLater(){state.released++;},async GetParamValue(name){return name==='CALENDAR_START'?'2026-10-23 09:00:00':'2026-10-23 10:00:00';}};}
const database={
 async GetRecentDocuments(filter,limit,flags){state.queries.push({method:'recent',filter,limit,flags});return flags===3?[doc('R1','合成近期阅读')]:[doc('N1',state.recentRevision?'合成更新笔记':'合成近期笔记'),doc('N2','第二篇合成笔记')];},
 async DocumentsFromSQLWhere(sql){state.queries.push({method:'where',sql});if(sql.includes('DOCUMENT_READ_COUNT'))return[doc('U1','合成未读笔记')];if(sql.includes('CALENDAR_START'))return[doc('E1','合成待办笔记')];return[doc('D1','合成日期笔记一'),doc('D2','合成日期笔记二')];},
 async DocumentFromGUID(id){return doc(id,'合成点击笔记');}
};
for(const name of ['documentCreated','documentDeleted','documentModified','documentDataModified','documentAccessDateModified'])database[name]=signal(name);
const app={Database:database,async Locale(){return'zh_CN';},Window:{ViewDocument(doc,flag){state.opened.push({guid:doc.GUID,flag});}},DocumentsCtrl:{SetDocuments(guids){state.selected.push(guids);}}};
window.qt={webChannelTransport:{}};window.QWebChannel=function(transport,callback){callback({objects:{WizExplorerApp:app}});};
})();
