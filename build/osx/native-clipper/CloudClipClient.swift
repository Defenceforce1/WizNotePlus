// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

struct CloudClipAccount {
    let token: String
    let kbGuid: String
    let userGuid: String
    let userID: String
    var dictionary: [String: Any] { ["token": token, "kbGuid": kbGuid, "userGuid": userGuid, "userID": userID] }
    init(_ value: [String: Any], userID fallback: String = "") throws {
        let user = value["user"] as? [String: Any] ?? value
        guard let token = value["token"] as? String, !token.isEmpty, token.utf8.count <= 4096,
              token.utf8.allSatisfy({ $0 > 32 && $0 < 127 && $0 != 59 && $0 != 44 }),
              let kb = value["kbGuid"] as? String, UUID(uuidString: kb) != nil,
              let guid = (user["userGuid"] ?? value["userGuid"]) as? String, UUID(uuidString: guid) != nil else {
            throw ClipperError.message("服务器未返回有效的个人账号信息；未建立上传连接")
        }
        self.token = token; kbGuid = kb; userGuid = guid
        userID = (user["userId"] ?? value["userID"]) as? String ?? fallback
        guard !userID.isEmpty else { throw ClipperError.message("缺少目的账号") }
    }
}

private final class CloudNoRedirects: NSObject, URLSessionTaskDelegate {
    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse, newRequest request: URLRequest,
                    completionHandler: @escaping (URLRequest?) -> Void) { completionHandler(nil) }
}

// Official discovery publishes gather/status. Reproduce its HTML frame protocol,
// not Markdown and not a speculative switch to a community-only create endpoint.
// No arbitrary cloud host, certificate override, auto POST retries or credentials
// in URLs. Authentication is sent only to the fixed official HTTPS origin.
final class CloudClipClient {
    typealias Transport = (URLRequest, @escaping (Result<[String: Any], Error>) -> Void) -> Void
    private let transport: Transport
    private let session: URLSession?
    init(transport: Transport? = nil) {
        if let transport { self.transport = transport; session = nil; return }
        let config = URLSessionConfiguration.ephemeral
        config.httpShouldSetCookies = false; config.httpCookieStorage = nil; config.urlCredentialStorage = nil
        config.timeoutIntervalForRequest = 20; config.timeoutIntervalForResource = 30
        let session = URLSession(configuration: config, delegate: CloudNoRedirects(), delegateQueue: nil)
        self.session = session
        self.transport = { request, completion in
            session.dataTask(with: request) { data, response, error in
                let result: Result<[String: Any], Error>
                if let error { result = .failure(ClipperError.message("无法连接为知服务器；未确认保存结果（\((error as NSError).code)）")) }
                else if let http = response as? HTTPURLResponse, (200...299).contains(http.statusCode),
                        let data, data.count <= 1024 * 1024,
                        let value = try? JSONSerialization.jsonObject(with: data) as? [String: Any] { result = .success(value) }
                else { result = .failure(ClipperError.message("服务器响应无效；未确认上传成功")) }
                DispatchQueue.main.async { completion(result) }
            }.resume()
        }
    }
    static func folderIsValid(_ value: String) -> Bool {
        value.hasPrefix("/") && value.hasSuffix("/") && value.utf8.count <= 512
            && !value.contains("//") && !value.contains("\\") && !value.lowercased().hasPrefix("/deleted items/")
            && !value.unicodeScalars.contains(where: { $0.value < 32 })
            && !value.split(separator: "/").contains(where: { $0 == "." || $0 == ".." })
    }
    static func responseCode(_ value: [String: Any]) -> Int? {
        for key in ["code", "returnCode", "return_code"] {
            if let n = value[key] as? Int { return n }
        }; return nil
    }
    private func send(_ path: String, method: String = "GET", query: [URLQueryItem] = [],
                      body: [String: Any]? = nil, account: CloudClipAccount? = nil,
                      completion: @escaping (Result<[String: Any], Error>) -> Void) {
        do {
            guard ["/as/user/login", "/as/user/info", "/ks/gather", "/ks/gather/status"].contains(path)
                || (path.hasPrefix("/ks/category/all/") && UUID(uuidString: String(path.dropFirst(17))) != nil)
                else { throw ClipperError.message("无效官方接口") }
            var parts = URLComponents(string: "https://note.wiz.cn" + path)!
            parts.queryItems = query + [URLQueryItem(name: "clientType", value: "webclip_chrome"),
                URLQueryItem(name: "clientVersion", value: "4.0.9"), URLQueryItem(name: "apiVersion", value: "10")]
            var request = URLRequest(url: parts.url!); request.httpMethod = method
            request.setValue("application/json; charset=utf-8", forHTTPHeaderField: "Content-Type")
            if let account {
                request.setValue(account.token, forHTTPHeaderField: "X-Wiz-Token")
                // Legacy gather uses the token cookie; never import Safari cookies.
                request.setValue("token=" + account.token, forHTTPHeaderField: "Cookie")
            }
            if let body {
                let data = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
                guard data.count <= 12 * 1024 * 1024 else { throw ClipperError.message("上传内容过大，请改用选区") }
                request.httpBody = data
            }
            transport(request) { result in
                completion(result.flatMap { value in
                    guard Self.responseCode(value) == 200 else {
                        return .failure(ClipperError.message("为知接口返回错误 \(Self.responseCode(value) ?? -1)；未确认保存成功。请检查账号权限或重新登录。"))
                    }; return .success(value)
                })
            }
        } catch { completion(.failure(error)) }
    }
    func login(userID: String, password: String, completion: @escaping (Result<CloudClipAccount, Error>) -> Void) {
        guard !userID.isEmpty, !password.isEmpty else { completion(.failure(ClipperError.message("请输入为知账号和密码"))); return }
        send("/as/user/login", method: "POST", body: ["userId": userID, "password": password]) { result in
            completion(result.flatMap { value in
                do { return .success(try CloudClipAccount(value["result"] as? [String: Any] ?? [:], userID: userID)) }
                catch { return .failure(error) }
            })
        }
    }
    func folders(account: CloudClipAccount, completion: @escaping (Result<[String], Error>) -> Void) {
        send("/ks/category/all/" + account.kbGuid, account: account) { result in
            completion(result.flatMap { value in
                guard let folders = value["result"] as? [String] else { return .failure(ClipperError.message("无法读取服务器文件夹")) }
                return .success(Array(Set(folders.filter(Self.folderIsValid) + ["/网页剪藏/"])).sorted())
            })
        }
    }
    static func gatherBody(account: CloudClipAccount, requestID: String, title: String, folder: String,
                           sourceURL: String, html: String) throws -> [String: Any] {
        guard UUID(uuidString: requestID) != nil, folderIsValid(folder), !html.isEmpty,
              html.utf8.count <= ClipInput.maximumBytes, !title.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty,
              title.count <= 512, let source = URLComponents(string: sourceURL),
              ["http", "https"].contains(source.scheme ?? ""), source.host != nil,
              source.user == nil, source.password == nil,
              !account.userID.contains("'"), !account.userID.contains("\""),
              !account.userID.contains("<"), !account.userID.contains(">") else { throw ClipperError.message("上传内容或目的文件夹无效") }
        func encoded(_ value: String) -> String { Data(value.utf8).base64EncodedString() }
        let document = "<!DOCTYPE html><html><head><meta charset=\"UTF-8\"></head><body>" + html + "</body></html>"
        let data = "myWiz='\(account.userGuid)@userguid' SaveResources='true' document_guid='\(requestID)' "
            + "param-fcount='1' param-location='\(encoded(sourceURL))' param-title='\(encoded(title))' "
            + "0_FrameURL='\(encoded(sourceURL))' 0_FrameHtml='\(encoded(document))' "
            + "save-command='save_content' userid='\(account.userID)' location='\(encoded(folder))' comment='' tag=''"
        return ["type": "clipper", "customId": requestID, "data": data]
    }
    func upload(body: [String: Any], account: CloudClipAccount, completion: @escaping (Result<String, Error>) -> Void) {
        send("/ks/gather", method: "POST", body: body, account: account) { result in
            completion(result.flatMap { value in
                guard let id = value["id"], !(id is NSNull), !String(describing: id).isEmpty else {
                    return .failure(ClipperError.message("服务器未返回剪藏任务编号；请先在网页版检查，勿重复上传"))
                }; return .success(String(describing: id))
            })
        }
    }
    // Only status 0/101 confirms completion. A queued task is NOT a saved note.
    func status(id: String?, requestID: String, account: CloudClipAccount,
                completion: @escaping (Result<Int?, Error>) -> Void) {
        var query = [URLQueryItem(name: "custom_id", value: requestID)]
        if let id { query.append(URLQueryItem(name: "id", value: id)) }
        send("/ks/gather/status", query: query, account: account) { result in
            completion(result.flatMap { value in
                if let n = value["status"] as? Int, [0, 101].contains(n) { return .success(n) }
                if let text = value["status"] as? String {
                    if ["doing", "new"].contains(text) { return .success(nil) }
                    if let n = Int(text), [0, 101].contains(n) { return .success(n) }
                }
                return .failure(ClipperError.message("服务器未确认剪藏完成；请在网页版检查该笔记"))
            })
        }
    }
}
