#include "WizStandaloneStorage.h"
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDirIterator>
#include <QLockFile>
#include <QSaveFile>
#include <QMessageBox>
#include <QPushButton>

namespace WizStandaloneStorage {
namespace {
const QByteArray consent("WizNotePlus-AppleSilicon-consent-v1\n");
bool sameOrInside(const QString& path, const QString& parent) {
    return path.compare(parent, Qt::CaseInsensitive) == 0
        || path.startsWith(parent + '/', Qt::CaseInsensitive);
}
bool noLinks(const QString& path, QString& error) {
    QString cursor = path;
    for (;;) {
        const QFileInfo info(cursor);
        if (info.isSymLink()) {
            error = QStringLiteral("独立资料库路径含符号链接，已停止，不会读取旧版数据。");
            return false;
        }
        const QString parent = info.absolutePath();
        if (parent == cursor) break;
        cursor = parent;
    }
    return true;
}
bool validFile(const QString& root, const QString& name, QString& error) {
    const QString path = QDir(root).filePath(name);
    if (!noLinks(path, error)) return false;
    const QFileInfo info(path);
    if (info.exists() && !info.isFile()) {
        error = QStringLiteral("独立资料库的控制文件类型不正确，已停止。");
        return false;
    }
    return true;
}
}
QString defaultRoot(const QString& home) {
    return QDir(home).filePath("Library/Application Support/WizNotePlus-AppleSilicon");
}
bool validateRoot(const QString& home, const QString& candidate, QString& normalized, QString& error) {
    normalized.clear(); error.clear();
    if (candidate.isEmpty() || !QDir::isAbsolutePath(candidate) || candidate.contains(QChar::Null)) {
        error = QStringLiteral("独立资料库必须是非空绝对路径，已停止。");
        return false;
    }
    const QString root = QDir::cleanPath(QDir::fromNativeSeparators(candidate));
    if (QFileInfo::exists(QDir(root).filePath(".standalone-copy-in-progress"))) {
        error = QStringLiteral("独立测试副本仍在准备或校验，暂不进入账号；源资料库未移动。");
        return false;
    }
    const QString cleanHome = QDir::cleanPath(home);
    if (!root.startsWith(cleanHome + '/', Qt::CaseInsensitive)
        || root == "/" || root.compare(cleanHome, Qt::CaseInsensitive) == 0
        || sameOrInside(cleanHome, root)
        || sameOrInside(root, QDir(cleanHome).filePath(".wiznote"))
        || sameOrInside(root, QDir(cleanHome).filePath("WizNote"))
        || sameOrInside(root, QDir(cleanHome).filePath("Library/Containers/cn.wiz.wiznoteformac"))
        || root.compare(QDir(cleanHome).filePath("Documents"), Qt::CaseInsensitive) == 0) {
        error = QStringLiteral("不能把旧版或系统目录作为新版独立资料库，已停止。");
        return false;
    }
    if (!noLinks(root, error)) return false;
    const QFileInfo info(root);
    if (info.exists() && !info.isDir()) {
        error = QStringLiteral("独立资料库不是目录，已停止。");
        return false;
    }
    QDirIterator entries(root, QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot,
                         QDirIterator::Subdirectories);
    while (entries.hasNext()) {
        entries.next(); const QFileInfo item = entries.fileInfo();
        if (item.isSymLink() || (!item.isFile() && !item.isDir())) {
            error = QStringLiteral("独立资料库包含链接或特殊文件，已停止，不会跟随到其他目录。");
            return false;
        }
    }
    // These paths are writable/cleanable by the program. Never follow a link
    // out of the isolated root, even if a previous configuration left one.
    for (const QString& child : {"cache", "log", "temp", "update", "webengine",
         "webengine/Default", "webengine/Default-cache", "webengine/WizNoteWebEngineProfile",
         "webengine/WizNoteWebEngineProfile-cache"}) {
        const QString path = QDir(root).filePath(child);
        if (!noLinks(path, error)) return false;
        if (QFileInfo::exists(path) && !QFileInfo(path).isDir()) {
            error = QStringLiteral("独立资料库的工作目录类型不正确，已停止。");
            return false;
        }
    }
    if (!validFile(root, ".wiznoteplus-session.lock", error)
        || !validFile(root, ".standalone-consent-v1", error)) return false;
    normalized = root;
    return true;
}
QString profileStorage(const QString& root, const QString& profile) {
    return QDir(root).filePath("webengine/" + profile);
}
QString profileCache(const QString& root, const QString& profile) {
    return QDir(root).filePath("webengine/" + profile + "-cache");
}
Session::Session() = default;
Session::~Session() = default;
bool Session::prepare(const QString& home, const QString& overrideRoot, QString& error) {
    if (m_lock) { error = QStringLiteral("启动会话已初始化。"); return false; }
    QString root;
    if (!validateRoot(home, overrideRoot.isNull() ? defaultRoot(home) : overrideRoot, root, error)) return false;
    const bool alreadyExists = QFileInfo::exists(root);
    if (!QDir().mkpath(root) || (!alreadyExists && !QFile::setPermissions(root,
            QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner))) {
        error = QStringLiteral("无法创建独立资料库；不会回退到旧版目录。");
        return false;
    }
    if (!QFileInfo(root).isReadable() || !QFileInfo(root).isWritable() || !QFileInfo(root).isExecutable()) {
        error = QStringLiteral("独立资料库不可读写；不会回退到旧版目录。");
        return false;
    }
    auto lock = std::make_unique<QLockFile>(QDir(root).filePath(".wiznoteplus-session.lock"));
    lock->setStaleLockTime(0);
    if (!lock->tryLock(0)) {
        error = QStringLiteral("独立资料库正在使用，或无法取得写入锁。请先正常退出另一份新版；不会打开旧版。");
        return false;
    }
    m_root = root; m_lock = std::move(lock);
    // All existing path consumers, including browser profiles, use this exact
    // root. Set it before QApplication/login/account settings are initialized.
    qputenv("WIZNOTE_DATA_STORE", m_root.toUtf8());
    return true;
}
bool Session::hasConsent() const {
    if (!m_lock) return false;
    QFile file(QDir(m_root).filePath(".standalone-consent-v1"));
    return file.size() == consent.size() && file.open(QIODevice::ReadOnly) && file.readAll() == consent;
}
bool Session::saveConsent(QString& error) {
    if (!m_lock || !validFile(m_root, ".standalone-consent-v1", error)) return false;
    QSaveFile file(QDir(m_root).filePath(".standalone-consent-v1"));
    if (!file.open(QIODevice::WriteOnly) || file.write(consent) != consent.size() || !file.commit()) {
        error = QStringLiteral("无法保存首次启动确认，已停止，不会进入账号。");
        return false;
    }
    return true;
}
void Session::release() { m_lock.reset(); m_root.clear(); }
bool confirmFirstLaunch(Session& session, QWidget* parent) {
    if (session.root().isEmpty()) return false;
    if (session.hasConsent()) return true;
    QMessageBox notice(QMessageBox::Information, QStringLiteral("WizNotePlus 独立资料库"),
        QStringLiteral("新版仅使用以下独立本地目录，不自动读取或搬动旧版资料库：\n%1\n\n"
            "若此目录已准备好此前的测试副本，将沿用其中笔记和 CSS。\n"
            "本地隔离不能阻止登录账号的云端同步；本版为未公证发布候选版。"
            "真实账号与长期同步尚未完整验证，请先使用测试账号；原账号启用前先备份并验证少量笔记。")
            .arg(session.root()), QMessageBox::Ok | QMessageBox::Cancel, parent);
    notice.setObjectName("wiz-standalone-first-launch");
    notice.setTextFormat(Qt::PlainText);
    notice.setDefaultButton(QMessageBox::Cancel);
    notice.button(QMessageBox::Ok)->setText(QStringLiteral("开始使用"));
    if (notice.exec() != QMessageBox::Ok) return false;
    QString error;
    if (!session.saveConsent(error)) {
        QMessageBox::critical(parent, QStringLiteral("无法启动独立资料库"), error);
        return false;
    }
    return true;
}
}
