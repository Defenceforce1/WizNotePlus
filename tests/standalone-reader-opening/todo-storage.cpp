#include "share/WizTodo.h"
#include "database/sqlite3.h"
#include <QCoreApplication>
#include <QDebug>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    if (argc != 3) return 4;
    QFile queryFile(QString::fromLocal8Bit(argv[1]));
    if (!queryFile.open(QIODevice::ReadOnly)) return 4;
    const auto query = QJsonDocument::fromJson(queryFile.readAll()).object().value("query").toString();
    if (query.isEmpty()) return 4;
    QTemporaryDir root; if (!root.isValid()) return 4;
    sqlite3* db = nullptr;
    const auto file = root.filePath("synthetic-todo.db").toUtf8();
    if (sqlite3_open(file.constData(), &db) != SQLITE_OK) return 5;
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok; qInfo().noquote() << (ok ? "PASS" : "FAIL") << label; return ok;
    };
    const auto exec = [&](const QString& sql) {
        char* error = nullptr;
        const int status = sqlite3_exec(db, sql.toUtf8().constData(), nullptr, nullptr, &error);
        sqlite3_free(error); return status == SQLITE_OK;
    };
    const auto scalar = [&](const QString& sql) {
        sqlite3_stmt* statement = nullptr; QString result;
        if (sqlite3_prepare_v2(db, sql.toUtf8().constData(), -1, &statement, nullptr) == SQLITE_OK
            && sqlite3_step(statement) == SQLITE_ROW) {
            const auto* text = sqlite3_column_text(statement, 0);
            if (text) result = QString::fromUtf8(reinterpret_cast<const char*>(text));
        }
        sqlite3_finalize(statement); return result;
    };
    check(exec("CREATE TABLE WIZ_DOCUMENT (DOCUMENT_GUID TEXT PRIMARY KEY,DOCUMENT_LOCATION TEXT,DOCUMENT_TITLE TEXT,BODY TEXT)"), "temporary synthetic note table only");
    check(exec("CREATE TABLE WIZ_DOCUMENT_PARAM (DOCUMENT_GUID char(36) NOT NULL,PARAM_NAME varchar(50) NOT NULL,PARAM_VALUE varchar(3000),WIZ_VERSION int DEFAULT -1,PRIMARY KEY(DOCUMENT_GUID,PARAM_NAME))"), "same existing parameter schema without migrations");
    check(WizTodo::validDate("2028-02-29") && !WizTodo::validDate("2026-02-29")
          && !WizTodo::validDate("2026-2-03") && !WizTodo::validDate("2026-10-23suffix"), "calendar dates require canonical valid YYYY-MM-DD");
    check(WizTodo::validTitle(" 合成待办 🙂 ") && !WizTodo::validTitle("   ")
          && !WizTodo::validTitle(QString(201, 'a')) && !WizTodo::validTitle("a\nb"), "title validation matches compact form limits");
    for (const auto& guid : {"today", "overdue", "far-future", "done", "trash", "legacy", "old-legacy", "plain"})
        if (!exec(QString("INSERT INTO WIZ_DOCUMENT VALUES ('%1','%2','%1','preserved body')").arg(guid, QString(guid) == "trash" ? "/Deleted Items/" : "/My Notes/"))) return 6;
    check(WizTodo::writeDate("today", "2026-10-23", exec), "current-day task writes through actual storage helper");
    check(scalar("SELECT count(*) FROM WIZ_DOCUMENT_PARAM WHERE DOCUMENT_GUID='today' AND WIZ_VERSION=-1") == "3", "all date and task fields marked for existing parameter sync");
    check(scalar("SELECT PARAM_VALUE FROM WIZ_DOCUMENT_PARAM WHERE DOCUMENT_GUID='today' AND PARAM_NAME='CALENDAR_END'") == "2026-10-23 23:59:59", "task uses local full-day bounds");
    check(WizTodo::writeDate("overdue", "2026-10-20", exec) && WizTodo::writeDate("far-future", "2026-12-30", exec)
        && WizTodo::writeDate("done", "2026-10-23", exec) && WizTodo::writeDate("trash", "2026-10-23", exec), "overdue future and disposable fixture tasks stored");
    check(exec("INSERT INTO WIZ_DOCUMENT_PARAM VALUES ('today','UNRELATED','keep',42)"), "unrelated parameter sentinel installed");
    check(WizTodo::writeDate("today", "2026-10-24", exec), "existing task date can change");
    check(scalar("SELECT PARAM_VALUE FROM WIZ_DOCUMENT_PARAM WHERE DOCUMENT_GUID='today' AND PARAM_NAME='UNRELATED'") == "keep", "date update preserves unrelated metadata");
    check(exec("CREATE TRIGGER reject_todo_end BEFORE INSERT ON WIZ_DOCUMENT_PARAM WHEN NEW.DOCUMENT_GUID='today' AND NEW.PARAM_NAME='CALENDAR_END' BEGIN SELECT RAISE(ABORT,'ordinary injected write failure');END"), "bounded ordinary date-write failure fixture");
    check(!WizTodo::writeDate("today", "2026-11-01", exec), "failed third-field write returns failure");
    check(scalar("SELECT PARAM_VALUE FROM WIZ_DOCUMENT_PARAM WHERE DOCUMENT_GUID='today' AND PARAM_NAME='CALENDAR_START'") == "2026-10-24 00:00:00"
        && scalar("SELECT PARAM_VALUE FROM WIZ_DOCUMENT_PARAM WHERE DOCUMENT_GUID='today' AND PARAM_NAME='CALENDAR_END'") == "2026-10-24 23:59:59", "ordinary failure rolls back prior fields without partial date");
    check(exec("DROP TRIGGER reject_todo_end") && WizTodo::writeDate("today", "2026-10-25", exec), "failure releases transaction and retry succeeds");
    check(WizTodo::writeDate("done", "", exec), "completion updates metadata without deleting note");
    check(scalar("SELECT count(*) FROM WIZ_DOCUMENT WHERE DOCUMENT_GUID='done' AND BODY='preserved body'") == "1", "completion preserves exact synthetic note body");
    check(scalar("SELECT count(*) FROM WIZ_DOCUMENT_PARAM WHERE DOCUMENT_GUID='done' AND WIZ_VERSION=-1") == "3"
        && scalar("SELECT PARAM_VALUE FROM WIZ_DOCUMENT_PARAM WHERE DOCUMENT_GUID='done' AND PARAM_NAME='WIZ_TODO'") == "0", "completion uses synchronized values rather than untracked row deletion");
    check(!WizTodo::writeDate("today", "2026-02-30", exec) && !WizTodo::writeDate("", "2026-10-23", exec), "invalid request cannot mutate storage");
    check(exec("INSERT INTO WIZ_DOCUMENT_PARAM VALUES ('legacy','CALENDAR_START','2026-10-24 09:00:00',0),('legacy','CALENDAR_END','2026-10-24 10:00:00',0),('old-legacy','CALENDAR_START','2026-01-01 09:00:00',0),('old-legacy','CALENDAR_END','2026-01-01 10:00:00',0)"), "legacy calendar fixtures remain separate from explicit tasks");
    const auto selected = scalar("SELECT group_concat(DOCUMENT_GUID,',') FROM (SELECT DOCUMENT_GUID FROM WIZ_DOCUMENT WHERE " + query + ")");
    check(selected == "overdue,legacy,today,far-future", "actual Welcome SQL shows overdue future tasks and upcoming legacy event only, date sorted");
    check(exec("SAVEPOINT outer_operation") && WizTodo::writeDate("today", "2026-10-26", exec)
        && exec("ROLLBACK TO outer_operation") && exec("RELEASE outer_operation"), "date helper composes with outer storage transaction");
    check(scalar("SELECT PARAM_VALUE FROM WIZ_DOCUMENT_PARAM WHERE DOCUMENT_GUID='today' AND PARAM_NAME='CALENDAR_START'") == "2026-10-25 00:00:00", "outer rollback still works");
    sqlite3_close(db); db = nullptr;
    check(sqlite3_open(file.constData(), &db) == SQLITE_OK, "temporary storage reopens");
    check(scalar("SELECT PARAM_VALUE FROM WIZ_DOCUMENT_PARAM WHERE DOCUMENT_GUID='far-future' AND PARAM_NAME='CALENDAR_START'") == "2026-12-30 00:00:00", "date persists across actual SQLite close and reopen");
    check(scalar("SELECT count(*) FROM WIZ_DOCUMENT WHERE BODY='preserved body'") == "8", "all synthetic bodies untouched by date and completion operations");
    sqlite3_close(db);
    QFile report(QString::fromLocal8Bit(argv[2]));
    if (!report.open(QIODevice::WriteOnly | QIODevice::NewOnly)) return 7;
    report.write(QJsonDocument(QJsonObject{{"checks",checks},{"failures",failures},{"bundledSQLite",sqlite3_libversion()},
        {"syntheticOnly",true},{"actualClientNoteCreationAccepted",false},{"actualWelcomeQueryReplayed",true}}).toJson(QJsonDocument::Indented));
    return failures ? 1 : 0;
}
