#ifndef WIZTODO_H
#define WIZTODO_H

#include <QDate>
#include <QList>
#include <QPair>
#include <QString>

// Note metadata only: no body access, reminder service or separate task store.
namespace WizTodo {
inline bool validDate(const QString& text)
{
    const auto date = QDate::fromString(text, Qt::ISODate);
    return text.size() == 10 && date.isValid() && date.toString(Qt::ISODate) == text;
}

inline bool validTitle(const QString& title)
{
    return !title.trimmed().isEmpty() && title.trimmed().size() <= 200
        && !title.contains(QChar::Null) && !title.contains('\n') && !title.contains('\r');
}

inline QList<QPair<QString, QString>> parameters(const QString& date)
{
    // Empty values, rather than deleting rows, go through the existing parameter
    // sync protocol. Completing a task must never delete its note or body.
    return {{QStringLiteral("WIZ_TODO"), date.isEmpty() ? QStringLiteral("0") : QStringLiteral("1")},
            {QStringLiteral("CALENDAR_START"), date.isEmpty() ? QString() : date + " 00:00:00"},
            {QStringLiteral("CALENDAR_END"), date.isEmpty() ? QString() : date + " 23:59:59"}};
}

inline QString quoted(QString value)
{
    return "'" + value.replace("'", "''") + "'";
}

template<class Execute>
bool writeDate(const QString& guid, const QString& date, Execute execute)
{
    if (guid.isEmpty() || guid.contains(QChar::Null) || (!date.isEmpty() && !validDate(date)))
        return false;
    // All three fields become visible together. Do not emit refresh/sync signals
    // until RELEASE succeeds; a rejected write must not advertise partial data.
    if (!execute(QStringLiteral("SAVEPOINT wiz_todo_date")))
        return false;
    const auto rollback = [&] {
        execute(QStringLiteral("ROLLBACK TO wiz_todo_date"));
        execute(QStringLiteral("RELEASE wiz_todo_date"));
        return false;
    };
    for (const auto& field : parameters(date)) {
        const auto sql = QStringLiteral("REPLACE INTO WIZ_DOCUMENT_PARAM "
            "(DOCUMENT_GUID,PARAM_NAME,PARAM_VALUE,WIZ_VERSION) VALUES (%1,%2,%3,-1)")
            .arg(quoted(guid), quoted(field.first), quoted(field.second));
        if (!execute(sql))
            return rollback();
    }
    if (!execute(QStringLiteral("RELEASE wiz_todo_date")))
        return rollback();
    return true;
}
}
#endif
