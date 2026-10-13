#ifndef WIZ_QT_COMPAT_H
#define WIZ_QT_COMPAT_H

#include <QEvent>
#include <QIODevice>
#include <QRegularExpression>
#include <QMutex>
#include <QRegExp>
#include <QTextCodec>
#include <QTextStream>
#include <QRandomGenerator>
#include <algorithm>

#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
#include <QEnterEvent>
#include <QStringConverter>
using WizEnterEvent = QEnterEvent;
using WizRecursiveMutex = QRecursiveMutex;
#else
using WizEnterEvent = QEvent;
class WizRecursiveMutex : public QMutex
{
public:
    WizRecursiveMutex() : QMutex(QMutex::Recursive) {}
};
#endif

inline void WizSetUtf8Encoding(QTextStream& stream)
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
    stream.setEncoding(QStringConverter::Utf8);
#else
    stream.setCodec("UTF-8");
#endif
}

// Core5Compat keeps legacy encodings such as GB18030 available for imports.
inline QString WizReadEncodedText(QIODevice& device, const char* encoding)
{
    const QByteArray bytes = device.readAll();
    QTextCodec* fallback = QTextCodec::codecForName(encoding);
    if (!fallback)
        fallback = QTextCodec::codecForName("UTF-8");
    return QTextCodec::codecForUtfText(bytes, fallback)->toUnicode(bytes);
}

#endif
