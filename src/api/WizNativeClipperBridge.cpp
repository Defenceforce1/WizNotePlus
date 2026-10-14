#include "WizNativeClipperBridge.h"
#include "database/WizDatabaseManager.h"
#include "database/WizDatabase.h"
#include "html/WizGumboHelper.h"
#include "share/WizNetworkSecurity.h"
#include <QTcpSocket>
#include <QJsonDocument>
#include <QJsonArray>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QDir>
#include <QTimer>
#include <QRandomGenerator>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QSet>
#include <QBuffer>
#include <QImageReader>
#include <QScopedValueRollback>

namespace {
constexpr int maximumBody = 2 * 1024 * 1024;
bool validWebUrl(const QString& text) {
    const QUrl url(text, QUrl::StrictMode);
    return text.size() <= 4096 && url.isValid() && !url.host().isEmpty()
        && url.userInfo().isEmpty() && (url.scheme() == "https" || url.scheme() == "http");
}
bool validLocation(const QString& folder) {
    if (!folder.startsWith('/') || !folder.endsWith('/') || folder.size() > 512
        || folder.contains('\0') || folder.contains('\\') || folder.startsWith("/Deleted Items/", Qt::CaseInsensitive)) return false;
    for (const auto& part : folder.split('/', Qt::SkipEmptyParts))
        if (part == "." || part == ".." || part.contains(QRegularExpression("[\\x00-\\x1f]"))) return false;
    return !folder.contains("//");
}
QString randomToken() {
    QByteArray bytes;
    for (int i = 0; i < 4; ++i) {
        const quint64 value = QRandomGenerator::system()->generate64();
        bytes.append(reinterpret_cast<const char*>(&value), sizeof(value));
    }
    return QString::fromLatin1(bytes.toHex());
}
bool equalToken(const QByteArray& expected, const QByteArray& received) {
    if (expected.isEmpty() || expected.size() != received.size()) return false;
    unsigned char difference = 0;
    for (int i = 0; i < expected.size(); ++i) difference |= expected[i] ^ received[i];
    return difference == 0;
}
QString cleanStyle(const QString& source) {
    static const QSet<QString> properties = {"color", "background-color", "font-family", "font-size", "font-weight",
        "font-style", "line-height", "text-align", "text-decoration", "white-space", "vertical-align",
        "margin", "margin-top", "margin-bottom", "padding", "border", "border-color", "border-width",
        "border-style", "border-collapse", "max-width", "width", "height"};
    QStringList output;
    for (const QString& item : source.split(';')) {
        const int colon = item.indexOf(':');
        if (colon < 0) continue;
        const QString property = item.left(colon).trimmed().toLower();
        const QString value = item.mid(colon + 1).trimmed();
        // No functions, URLs, escapes, selectors, imports or CSS bindings.
        const bool colorFunction = (property == "color" || property == "background-color" || property == "border-color")
            && QRegularExpression("^rgba?\\([0-9.,%\\s]+\\)$").match(value).hasMatch();
        if (properties.contains(property) && value.size() <= 160
            && (QRegularExpression("^[a-zA-Z0-9 #.,%+\\-\"']+$").match(value).hasMatch() || colorFunction))
            output.append(property + ":" + value);
    }
    return output.join(';');
}
struct CleanContext {
    QUrl source;
    bool remoteImages = false, failed = false;
    int nodes = 0;
    qint64 imagePixels = 0;
    QString imageSource(const QString& raw) {
        if (raw.startsWith("data:image/")) {
            static const QRegularExpression inlineImage("^data:image/(png|jpeg|gif|webp);base64,([A-Za-z0-9+/=]+)$");
            const auto match = inlineImage.match(raw);
            if (!match.hasMatch() || raw.size() > 750000) return {};
            QByteArray bytes = QByteArray::fromBase64(match.captured(2).toLatin1());
            QBuffer buffer(&bytes); buffer.open(QIODevice::ReadOnly);
            QImageReader reader(&buffer);
            const QSize size = reader.size();
            const qint64 pixels = qint64(size.width()) * size.height();
            if (!reader.canRead() || size.width() <= 0 || size.height() <= 0 || pixels > 16000000
                || pixels + imagePixels > 32000000 || reader.read().isNull()) return {};
            imagePixels += pixels; return raw;
        }
        const QUrl url = source.resolved(QUrl(raw));
        const QString host = url.host().toLower();
        QHostAddress address;
        if (url.scheme() != "https" || !validWebUrl(url.toString()) || host == "localhost"
            || host.endsWith(".local") || address.setAddress(host)) return {};
        remoteImages = true;
        return url.toString();
    }
    QString serialize(GumboNode* node, int depth = 0) {
        if (!node || failed) return {};
        if (++nodes > 30000 || depth > 80) { failed = true; return {}; }
        if (node->type == GUMBO_NODE_TEXT || node->type == GUMBO_NODE_WHITESPACE)
            return QString::fromUtf8(node->v.text.text).toHtmlEscaped();
        if (node->type != GUMBO_NODE_ELEMENT && node->type != GUMBO_NODE_TEMPLATE) return {};
        QString tag = QString::fromLatin1(gumbo_normalized_tagname(node->v.element.tag));
        if (node->v.element.tag == GUMBO_TAG_UNKNOWN) {
            // Gumbo's HTML tag enum omits many valid MathML tags (e.g. msup).
            // Recover only the tag name, still subject to the same allowlist.
            GumboStringPiece original = node->v.element.original_tag;
            gumbo_tag_from_original_text(&original);
            tag = QString::fromUtf8(original.data, int(original.length)).toLower();
        }
        if (tag == "svg" || tag == "canvas") {
            remoteImages = true;
            return QStringLiteral("<p>[图片或图形未嵌入，请对照原网页]</p>");
        }
        static const QSet<QString> discard = {"script", "style", "iframe", "frame", "object", "embed", "svg",
            "template", "head", "link", "meta", "base", "input", "button", "select", "textarea"};
        if (discard.contains(tag)) return {};
        static const QSet<QString> allowed = {"p", "div", "span", "article", "section", "main", "aside", "header", "footer",
            "h1", "h2", "h3", "h4", "h5", "h6", "a", "img", "br", "hr", "wbr", "strong", "b", "em", "i", "u", "s",
            "small", "sub", "sup", "pre", "code", "blockquote", "ul", "ol", "li", "dl", "dt", "dd", "table", "thead",
            "tbody", "tfoot", "tr", "td", "th", "caption", "colgroup", "col", "figure", "figcaption", "details", "summary",
            "math", "mi", "mn", "mo", "mrow", "mfrac", "msqrt", "msub", "msup", "msubsup", "mtext", "mover", "munder",
            "munderover", "mtable", "mtr", "mtd", "semantics", "annotation"};
        QString acceptedImage;
        if (tag == "img") {
            const QString raw = Utils::Gumbo::getAttribute(node, "src");
            if (!raw.startsWith("data:image/")) {
                const QUrl url = source.resolved(QUrl(raw));
                remoteImages = true;
                if (raw.isEmpty() || !validWebUrl(url.toString()))
                    return QStringLiteral("<p>[图片未嵌入，请对照原网页]</p>");
                // The old collector downloads remote img automatically. A clip
                // must not fetch arbitrary resources from the main process.
                const QString alt = Utils::Gumbo::getAttribute(node, "alt").left(256);
                return "<p><a href=\"" + url.toString().toHtmlEscaped() + "\">[图片链接"
                    + (alt.isEmpty() ? QString() : "：" + alt.toHtmlEscaped()) + "]</a></p>";
            }
            acceptedImage = imageSource(raw);
            if (acceptedImage.isEmpty()) {
                remoteImages = true;
                return QStringLiteral("<p>[图片未嵌入，请对照原网页]</p>");
            }
        }
        QString contents;
        const auto& children = node->v.element.children;
        for (unsigned int i = 0; i < children.length; ++i)
            contents += serialize(static_cast<GumboNode*>(children.data[i]), depth + 1);
        if (!allowed.contains(tag)) return contents;
        QString attributes;
        const auto& attrs = node->v.element.attributes;
        for (unsigned int i = 0; i < attrs.length; ++i) {
            const auto* attr = static_cast<GumboAttribute*>(attrs.data[i]);
            const QString name = QString::fromUtf8(attr->name).toLower();
            QString value = QString::fromUtf8(attr->value);
            if (name == "href" && tag == "a") {
                if (value.startsWith('#') && value.size() < 256) {}
                else {
                    value = source.resolved(QUrl(value)).toString();
                    if (!validWebUrl(value)) continue;
                }
            } else if (name == "src" && tag == "img") value = acceptedImage;
            else if (name == "style") value = cleanStyle(value);
            else if (name == "id" || name == "class") {
                if (value.size() > 256 || !QRegularExpression("^[a-zA-Z0-9_ .:\\-]+$").match(value).hasMatch()) continue;
            } else if (name == "colspan" || name == "rowspan" || name == "width" || name == "height") {
                if (!QRegularExpression("^[0-9]{1,4}%?$").match(value).hasMatch()) continue;
            } else if (name == "alt" || name == "title" || name == "lang" || name == "dir" || name == "encoding") {
                if (value.size() > 1024) continue;
            } else continue;
            if (!value.isEmpty()) attributes += " " + name + "=\"" + value.toHtmlEscaped() + "\"";
        }
        static const QSet<QString> emptyTags = {"img", "br", "hr", "wbr", "col"};
        return "<" + tag + attributes + ">" + (emptyTags.contains(tag) ? QString() : contents + "</" + tag + ">");
    }
};
}

WizNativeClipperBridge::WizNativeClipperBridge(WizDatabaseManager& database, QString tokenFile,
    std::function<bool()> unavailable, QObject* parent)
    : QObject(parent), m_database(database), m_tokenFile(std::move(tokenFile)), m_unavailable(std::move(unavailable)), m_server(this)
{
    QFile file(m_tokenFile);
    if (!QFileInfo(file).isSymLink() && file.size() <= 1024 && file.open(QIODevice::ReadOnly)) {
        const auto saved = QJsonDocument::fromJson(file.readAll()).object();
        const QString token = saved.value("token").toString();
        if (saved.value("accountID").toString() == m_database.db().kbGUID()
            && QRegularExpression("^[a-f0-9]{64}$").match(token).hasMatch()) m_token = token;
    }
    connect(&m_server, &QTcpServer::newConnection, this, &WizNativeClipperBridge::acceptConnections);
}
bool WizNativeClipperBridge::start(quint16 port) { return m_server.listen(QHostAddress::LocalHost, port); }
QString WizNativeClipperBridge::createPairingCode() {
    if (!m_server.isListening() || m_database.db().kbGUID().isEmpty()) return {};
    m_pairingCode = QString::number(QRandomGenerator::system()->bounded(100000000)).rightJustified(8, '0');
    m_failedPairings = 0; m_pairingDeadline.restart(); return m_pairingCode;
}
QJsonObject WizNativeClipperBridge::accountStatus() const {
    WizDatabase& db = m_database.db();
    CWizStdStringArray folders; db.getAllLocationsWithExtra(folders);
    QSet<QString> unique{QStringLiteral("/网页剪藏/"), db.getDefaultNoteLocation()};
    for (const auto& folder : folders) if (validLocation(folder)) unique.insert(folder);
    QStringList ordered = unique.values(); ordered.sort(); QJsonArray output;
    for (const auto& folder : ordered) if (validLocation(folder)) output.append(folder);
    return {{"ok", true}, {"protocol", 1}, {"accountID", db.kbGUID()}, {"accountName", db.getUserId()}, {"folders", output}};
}
bool WizNativeClipperBridge::persistToken(const QString& token) {
    const QString parent = QFileInfo(m_tokenFile).absolutePath();
    if (QFileInfo(parent).isSymLink() || QFileInfo(m_tokenFile).isSymLink() || !QDir().mkpath(parent)) return false;
    QFile::setPermissions(parent, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QSaveFile file(m_tokenFile); file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || !file.setPermissions(QFile::ReadOwner | QFile::WriteOwner)) return false;
    const QByteArray data = QJsonDocument(QJsonObject{{"accountID", m_database.db().kbGUID()}, {"token", token}}).toJson();
    return file.write(data) == data.size() && file.commit();
}
void WizNativeClipperBridge::acceptConnections() {
    while (m_server.hasPendingConnections()) {
        auto* socket = m_server.nextPendingConnection();
        if (m_clients >= 4) { socket->abort(); socket->deleteLater(); continue; }
        ++m_clients; socket->setReadBufferSize(maximumBody + 8192);
        connect(socket, &QTcpSocket::disconnected, this, [this, socket] { --m_clients; socket->deleteLater(); });
        connect(socket, &QTcpSocket::readyRead, this, [this, socket] { readRequest(socket); });
        QTimer::singleShot(15000, socket, [socket] { socket->abort(); });
    }
}
void WizNativeClipperBridge::respond(QPointer<QTcpSocket> socket, int status, QJsonObject body) {
    if (!socket || socket->state() != QAbstractSocket::ConnectedState) return;
    body["ok"] = status >= 200 && status < 300;
    const QByteArray data = QJsonDocument(body).toJson(QJsonDocument::Compact);
    socket->setProperty("responded", true);
    socket->write("HTTP/1.1 " + QByteArray::number(status) + " Result\r\nContent-Type: application/json; charset=utf-8\r\n"
        "Cache-Control: no-store\r\nConnection: close\r\nContent-Length: " + QByteArray::number(data.size()) + "\r\n\r\n" + data);
    socket->disconnectFromHost();
}
void WizNativeClipperBridge::readRequest(QTcpSocket* socket) {
    if (socket->property("responded").toBool() || socket->property("processing").toBool()) return;
    const auto reject = [&](int code, const QString& message) { respond(socket, code, {{"error", message}}); };
    const QByteArray bytes = socket->property("buffer").toByteArray() + socket->readAll();
    if (bytes.size() > maximumBody + 8192) { reject(413, QStringLiteral("剪藏内容太大，请改用选中内容")); return; }
    socket->setProperty("buffer", bytes);
    const int end = bytes.indexOf("\r\n\r\n");
    if (end < 0) { if (bytes.size() > 8192) reject(431, QStringLiteral("请求头过大")); return; }
    if (end > 8192) { reject(431, QStringLiteral("请求头过大")); return; }
    const auto lines = bytes.left(end).split('\n');
    const auto first = lines[0].trimmed().split(' ');
    if (first.size() != 3 || first[0] != "POST" || first[2] != "HTTP/1.1") { reject(405, QStringLiteral("仅支持原生剪藏请求")); return; }
    QHash<QByteArray, QByteArray> headers;
    for (int i = 1; i < lines.size(); ++i) {
        const auto line = lines[i].trimmed(); const int colon = line.indexOf(':');
        if (colon < 1 || lines[i].startsWith(' ') || lines[i].startsWith('\t')) { reject(400, QStringLiteral("无效请求头")); return; }
        const auto name = line.left(colon).toLower();
        if (headers.contains(name)) { reject(400, QStringLiteral("重复请求头")); return; }
        headers.insert(name, line.mid(colon + 1).trimmed());
    }
    if (headers.contains("origin") || headers.contains("sec-fetch-site") || headers.contains("transfer-encoding")) {
        reject(403, QStringLiteral("此接口不接受网页直接调用")); return;
    }
    const auto host = "127.0.0.1:" + QByteArray::number(port());
    if (headers.value("host") != host || headers.value("content-type").toLower().split(';').first().trimmed() != "application/json") {
        reject(400, QStringLiteral("无效本机请求")); return;
    }
    bool valid = false;
    const QByteArray lengthText = headers.value("content-length");
    const int length = lengthText.toInt(&valid);
    if (!valid || !QRegularExpression("^[0-9]+$").match(QString::fromLatin1(lengthText)).hasMatch()
        || length <= 0 || length > maximumBody) { reject(413, QStringLiteral("无效或过大的剪藏内容")); return; }
    if (bytes.size() < end + 4 + length) return;
    if (bytes.size() != end + 4 + length) { reject(400, QStringLiteral("不支持串联请求")); return; }
    QJsonParseError error;
    const auto document = QJsonDocument::fromJson(bytes.mid(end + 4), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) { reject(400, QStringLiteral("无效剪藏数据")); return; }
    socket->setProperty("processing", true);
    const QPointer<QTcpSocket> guardedSocket(socket);
    // Leave Qt's readyRead notification stack before legacy note collection can
    // run nested loops. Otherwise even Qt can resume on a deleted socket.
    QTimer::singleShot(0, this, [this, guardedSocket, path = first[1], authorization = headers.value("authorization"), body = document.object()] {
        if (guardedSocket && guardedSocket->state() == QAbstractSocket::ConnectedState)
            dispatch(guardedSocket, path, authorization, body);
    });
}
void WizNativeClipperBridge::dispatch(QTcpSocket* socket, const QByteArray& path,
    const QByteArray& authorization, const QJsonObject& body) {
    // Saving legacy Wiz containers may enter nested event loops. A disconnected
    // client can be deleted during that work, so capture a guarded pointer first.
    const QPointer<QTcpSocket> guardedSocket(socket);
    if (m_saving || WizNetworkSecurity::isShuttingDown() || (m_unavailable && m_unavailable())) {
        respond(socket, 503, {{"error", QStringLiteral("客户端正在保存或退出，请稍后重试")}}); return;
    }
    if (path == "/v1/pair") {
        const QString code = body.value("code").toString();
        if (m_pairingCode.isEmpty() || !m_pairingDeadline.isValid() || m_pairingDeadline.elapsed() > 120000
            || code != m_pairingCode || !QRegularExpression("^[0-9]{8}$").match(code).hasMatch()) {
            if (++m_failedPairings >= 5) m_pairingCode.clear();
            respond(socket, 401, {{"error", QStringLiteral("连接码无效或过期，请在客户端重新获取")}}); return;
        }
        const QString token = m_token.isEmpty() ? randomToken() : m_token;
        if (!persistToken(token)) { respond(socket, 500, {{"error", QStringLiteral("无法保存连接信息，未建立配对")}}); return; }
        m_token = token; m_pairingCode.clear();
        auto result = accountStatus(); result["token"] = token;
        respond(socket, 200, result); return;
    }
    if (!equalToken("Bearer " + m_token.toLatin1(), authorization) || m_token.isEmpty()) {
        respond(socket, 401, {{"error", QStringLiteral("请先连接新版客户端")}}); return;
    }
    if (body.value("accountID").toString() != m_database.db().kbGUID()) {
        respond(socket, 409, {{"error", QStringLiteral("目的账号已变化，请重新连接，未保存笔记")}}); return;
    }
    if (path == "/v1/status") { respond(socket, 200, accountStatus()); return; }
    if (path != "/v1/clip") { respond(socket, 404, {{"error", QStringLiteral("未知接口")}}); return; }
    QScopedValueRollback<bool> saving(m_saving, true);
    int status = 200; auto result = saveClip(body, status); respond(guardedSocket, status, result);
}
bool WizNativeClipperBridge::sanitizeHtml(const QString& html, const QString& sourceUrl,
    QString& clean, bool& remoteImages, QString& error) {
    if (html.trimmed().isEmpty() || html.toUtf8().size() > maximumBody || !validWebUrl(sourceUrl)) {
        error = QStringLiteral("缺少有效网页正文或来源地址"); return false;
    }
    // Gumbo original_tag slices borrow the input buffer. Keep the UTF-8 string
    // alive through serialization; the QString helper uses a temporary string.
    const std::string input = html.toUtf8().toStdString();
    auto* parsed = Utils::Gumbo::parseFromString(input);
    if (!parsed || !parsed->root) { if (parsed) Utils::Gumbo::destroyGumboOutput(parsed); error = QStringLiteral("无法解析网页正文"); return false; }
    CleanContext context; context.source = QUrl(sourceUrl);
    const QString body = context.serialize(parsed->root);
    Utils::Gumbo::destroyGumboOutput(parsed);
    if (context.failed || body.trimmed().isEmpty() || body.toUtf8().size() > 4 * 1024 * 1024) { error = QStringLiteral("正文为空、过大或结构过于复杂，请剪藏选中段落"); return false; }
    clean = "<!doctype html><html><head><meta charset=\"utf-8\"></head><body>" + body + "</body></html>";
    remoteImages = context.remoteImages; return true;
}
QJsonObject WizNativeClipperBridge::saveClip(const QJsonObject& body, int& status) {
    const auto reject = [&](int code, const QString& message) { status = code; return QJsonObject{{"error", message}}; };
    const QString id = body.value("requestID").toString();
    const QString title = body.value("title").toString().trimmed();
    const QString sourceUrl = body.value("url").toString();
    const QString folder = body.value("folder").toString();
    if (!QRegularExpression("^[a-f0-9]{8}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{4}-[a-f0-9]{12}$").match(id).hasMatch()
        || title.isEmpty() || title.size() > 512 || title.contains(QChar(0)) || !validLocation(folder))
        return reject(400, QStringLiteral("标题、文件夹或保存请求标识无效"));
    if (!accountStatus().value("folders").toArray().contains(folder)) return reject(409, QStringLiteral("目的文件夹已变化，请刷新后选择"));
    WizDatabase& db = m_database.db();
    if (db.isEncryptAllData()) return reject(409, QStringLiteral("当前全部笔记加密模式尚未支持原生剪藏；未改变加密设置"));
    QString clean, error; bool remoteImages = false;
    if (!sanitizeHtml(body.value("html").toString(), sourceUrl, clean, remoteImages, error)) return reject(400, error);
    const QByteArray canonical = QJsonDocument(QJsonObject{{"html", clean}, {"title", title}, {"url", sourceUrl}, {"folder", folder}}).toJson(QJsonDocument::Compact);
    const QString digest = QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
    const QString name = "wizclip-" + id;
    CWizDocumentDataArray existing;
    if (!db.getDocumentsBySQLWhere("DOCUMENT_NAME='" + name + "'", existing)) return reject(500, QStringLiteral("无法检查保存状态"));
    WIZDOCUMENTDATA note;
    bool duplicate = false;
    if (!existing.empty()) {
        WIZDOCUMENTPARAMDATA parameter;
        note = existing.front();
        if (existing.size() != 1 || !db.getDocumentParam(note.strGUID, "NATIVECLIPPERDIGEST", parameter)
            || parameter.strValue != digest || !QFileInfo::exists(db.getDocumentFileName(note.strGUID)))
            return reject(409, QStringLiteral("相同请求已创建笔记，请先在客户端确认，未重复创建"));
        duplicate = true;
    } else {
        if (!db.createDocumentAndInit(clean, "", 0, title, name, folder, sourceUrl, note))
            return reject(500, QStringLiteral("未确认本地保存成功，请保留剪藏内容并在客户端检查"));
        if (!db.setDocumentParam(note.strGUID, "NATIVECLIPPERDIGEST", digest))
            return reject(500, QStringLiteral("笔记已创建但保存回执未完成，请先在客户端确认，勿重复剪藏"));
        emit noteSaved(note.strGUID);
    }
    return {{"guid", note.strGUID}, {"accountID", db.kbGUID()}, {"accountName", db.getUserId()},
        {"folder", note.strLocation}, {"title", note.strTitle}, {"duplicate", duplicate}, {"remoteImages", remoteImages}};
}
