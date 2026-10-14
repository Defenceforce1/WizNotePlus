// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation

struct ClipInput {
    let title: String
    let url: String
    let pageHTML: String
    let selectionHTML: String
    let text: String
    let selectionText: String
    static let maximumBytes = 2 * 1024 * 1024

    init(dictionary: [String: Any]) throws {
        guard let url = dictionary["url"] as? String, let address = URLComponents(string: url),
              ["http", "https"].contains(address.scheme?.lowercased() ?? ""),
              let host = address.host, !host.isEmpty, address.user == nil, address.password == nil,
              url.utf8.count <= 4096 else { throw ClipperError.message("无法获取有效的网页地址") }
        let page = dictionary["pageHTML"] as? String ?? ""
        let selection = dictionary["selectionHTML"] as? String ?? ""
        guard (!page.isEmpty || !selection.isEmpty), page.utf8.count <= Self.maximumBytes,
              selection.utf8.count <= Self.maximumBytes else {
            throw ClipperError.message("未收到可保存的正文，或正文过大。请尝试选中段落后重新分享。")
        }
        let title = dictionary["title"] as? String ?? host
        self.title = String(title.prefix(512)); self.url = url
        pageHTML = page; selectionHTML = selection
        text = String((dictionary["text"] as? String ?? "").prefix(50000))
        selectionText = String((dictionary["selectionText"] as? String ?? "").prefix(50000))
    }
}

enum ClipperError: Error, LocalizedError {
    case message(String)
    var errorDescription: String? { switch self { case .message(let text): return text } }
}

struct ClipConnection {
    let token: String
    let accountID: String
    init(_ value: [String: Any]) throws {
        guard let token = value["token"] as? String, token.count == 64,
              token.allSatisfy({ "0123456789abcdef".contains($0) }),
              let account = value["accountID"] as? String, !account.isEmpty else {
            throw ClipperError.message("客户端返回的连接信息无效")
        }
        self.token = token; accountID = account
    }
}

private final class NoClipRedirects: NSObject, URLSessionTaskDelegate {
    func urlSession(_ session: URLSession, task: URLSessionTask,
                    willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        // Neither pairing content nor the local bearer token may leave loopback.
        completionHandler(nil)
    }
}

final class LocalClipClient {
    private let port: Int
    private let session: URLSession
    init(port: Int = 8849) {
        self.port = port
        let config = URLSessionConfiguration.ephemeral
        config.connectionProxyDictionary = [:]
        config.httpShouldSetCookies = false
        config.httpCookieStorage = nil
        config.urlCredentialStorage = nil
        config.timeoutIntervalForRequest = 10
        config.timeoutIntervalForResource = 15
        session = URLSession(configuration: config, delegate: NoClipRedirects(), delegateQueue: nil)
    }
    func send(_ path: String, body: [String: Any], connection: ClipConnection? = nil,
              completion: @escaping (Result<[String: Any], Error>) -> Void) {
        guard ["pair", "status", "clip"].contains(path), (1024...65535).contains(port),
              let address = URL(string: "http://127.0.0.1:\(port)/v1/\(path)") else {
            completion(.failure(ClipperError.message("无效的本机剪藏连接"))); return
        }
        do {
            let bytes = try JSONSerialization.data(withJSONObject: body, options: [.sortedKeys])
            guard bytes.count <= ClipInput.maximumBytes else { throw ClipperError.message("剪藏内容太大，请改用选中段落") }
            var request = URLRequest(url: address)
            request.httpMethod = "POST"; request.httpBody = bytes
            request.setValue("application/json; charset=utf-8", forHTTPHeaderField: "Content-Type")
            if let connection { request.setValue("Bearer \(connection.token)", forHTTPHeaderField: "Authorization") }
            session.dataTask(with: request) { data, response, error in
                let result: Result<[String: Any], Error>
                if let error {
                    result = .failure(ClipperError.message("无法连接新版客户端。请确认它已打开；内容仍保留，可重试。\n\(error.localizedDescription)"))
                } else if let response = response as? HTTPURLResponse, let data, data.count <= 65536,
                          let value = (try? JSONSerialization.jsonObject(with: data)) as? [String: Any] {
                    if response.statusCode == 200, value["ok"] as? Bool == true { result = .success(value) }
                    else { result = .failure(ClipperError.message(value["error"] as? String ?? "客户端未确认保存成功，请保留内容")) }
                } else { result = .failure(ClipperError.message("客户端响应无效，未确认保存成功，请保留内容")) }
                DispatchQueue.main.async { completion(result) }
            }.resume()
        } catch { completion(.failure(error)) }
    }
}
