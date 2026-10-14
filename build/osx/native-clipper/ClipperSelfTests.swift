// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit
import WebKit

func cloudClipperSelfTest() -> Int32 {
    var checks = 0, failures = 0
    func check(_ ok: Bool, _ title: String) { checks += 1; if !ok { failures += 1 }; print("\(ok ? "PASS" : "FAIL") \(title)") }
    let kb = "00000000-0000-0000-0000-000000000001", user = "00000000-0000-0000-0000-000000000002"
    let account = try! CloudClipAccount(["token": "synthetic-only-token", "kbGuid": kb, "userGuid": user, "userID": "synthetic@example.invalid"])
    check((try? CloudClipAccount(account.dictionary))?.kbGuid == kb, "own account roundtrip")
    var invalid = account.dictionary; invalid["token"] = "secret\r\nInjected: header"
    check((try? CloudClipAccount(invalid)) == nil, "reject token header injection")
    invalid = account.dictionary; invalid["kbGuid"] = "../../other"
    check((try? CloudClipAccount(invalid)) == nil, "reject arbitrary KB path")
    for folder in ["/../", "/Deleted Items/", "/a//b/", "/a\\b/", "missing slash"] {
        check(!CloudClipClient.folderIsValid(folder), "reject unsafe folder")
    }
    let id = UUID().uuidString.lowercased()
    let body = try! CloudClipClient.gatherBody(account: account, requestID: id, title: "公式测试", folder: "/网页剪藏/",
        sourceURL: "https://example.invalid/math", html: "<math><msup><mi>x</mi><mn>2</mn></msup></math><img src=\"data:image/png;base64,TEST\">")
    let frame = body["data"] as! String
    check(frame.contains("SaveResources='true'") && body["customId"] as? String == id, "HTML resource gather with stable document ID")
    let regex = try! NSRegularExpression(pattern: "0_FrameHtml='([^']+)'")
    let match = regex.firstMatch(in: frame, range: NSRange(frame.startIndex..., in: frame))!
    let encoded = String(frame[Range(match.range(at: 1), in: frame)!])
    let decoded = String(data: Data(base64Encoded: encoded)!, encoding: .utf8)!
    check(decoded.contains("<math>") && decoded.contains("data:image/png") && decoded.contains("<!DOCTYPE html>"), "upload retains HTML/math/images, not Markdown")
    var requests: [URLRequest] = [], statusMode = "new"
    let client = CloudClipClient(transport: { request, callback in
        requests.append(request)
        switch request.url!.path {
        case "/as/user/login": callback(.success(["code": 200, "result": account.dictionary]))
        case "/ks/category/all/" + kb: callback(.success(["code": 200, "result": ["/有效/", "/../", "/有效/"]]))
        case "/ks/gather": callback(.success(["code": 200, "id": "synthetic-task"]))
        default: callback(.success(["code": 200, "status": statusMode]))
        }
    })
    var logged = false
    client.login(userID: account.userID, password: "synthetic-password") { logged = (try? $0.get())?.kbGuid == kb }
    check(logged && requests[0].httpMethod == "POST", "independent login through injected transport only")
    check(requests[0].url!.scheme == "https" && requests[0].url!.host == "note.wiz.cn"
        && !requests[0].url!.absoluteString.contains("password"), "fixed official HTTPS; no secrets in URL")
    var folders: [String] = []
    client.folders(account: account) { folders = (try? $0.get()) ?? [] }
    check(folders == ["/有效/", "/网页剪藏/"], "destination folder validation and deduplication")
    var task: String?
    client.upload(body: body, account: account) { task = try? $0.get() }
    check(task == "synthetic-task" && requests.filter({ $0.httpMethod == "POST" && $0.url!.path == "/ks/gather" }).count == 1, "submit once, no automatic POST retry")
    check(requests.last!.value(forHTTPHeaderField: "X-Wiz-Token") == account.token, "cloud token scoped to official API")
    var pending = false
    client.status(id: task, requestID: id, account: account) { if case .success(nil) = $0 { pending = true } }
    check(pending, "queue acceptance is not called saved")
    statusMode = "0"; var complete = false
    client.status(id: task, requestID: id, account: account) { if case .success(0?) = $0 { complete = true } }
    check(complete, "only terminal server completion confirms save")
    statusMode = "unexpected"; var rejected = false
    client.status(id: nil, requestID: id, account: account) { if case .failure = $0 { rejected = true } }
    check(rejected, "unknown save status never claims success")
    for raw in ["http://example.invalid/a.png", "https://127.0.0.1/a.png", "https://example.local/a.png", "https://user:pass@example.invalid/a.png", "https://example.invalid:8849/a.png"] {
        check(!ClipResourcePolicy.urlIsAllowed(URL(string: raw)!), "reject unsafe image destination")
    }
    check(ClipResourcePolicy.ipv4IsPublic([8,8,8,8]) && !ClipResourcePolicy.ipv4IsPublic([10,1,2,3])
        && !ClipResourcePolicy.ipv4IsPublic([169,254,169,254]), "private and metadata IPs rejected")
    check(ClipResourcePolicy.pngData(Data("<script>not image</script>".utf8)) == nil, "reject non-image content before embedding")
    let resources = ClipResources(fetch: { url, completion in
        check(url.host == "example.invalid", "resource fetch is isolated from cloud transport")
        completion(Data([1,2,3])) // Prevalidated test transport result, not production image decoder.
    })
    var embedded = false
    resources.embed(html: "<p>文字</p><img src=\"https://example.invalid/a.png\" data-wiz-clip-resource=\"0\">", urls: ["https://example.invalid/a.png"], progress: { _,_ in }) { html, missing in
        embedded = html.contains("data:image/png;base64,") && !html.contains("data-wiz-clip-resource") && missing == 0
    }
    check(embedded, "captured remote resource replaced with embedded image")
    var skipped = false
    resources.embed(html: "<p>selected text only</p>", urls: ["https://example.invalid/a.png"], progress: { _,_ in }) { html, _ in skipped = !html.contains("img") }
    check(skipped, "selection does not fetch images from whole-page-only manifest entries")
    print("Independent cloud/resource checks: \(checks), failures: \(failures). No live account, keychain or network used.")
    return failures == 0 ? 0 : 1
}

private final class CaptureTestRunner: NSObject, WKNavigationDelegate {
    var done = false, code: Int32 = 1
    let web = WKWebView(frame: NSRect(x: 0, y: 0, width: 900, height: 600), configuration: WKWebViewConfiguration())
    override init() { super.init(); web.navigationDelegate = self }
    func webView(_ webView: WKWebView, decidePolicyFor navigationAction: WKNavigationAction,
                 decisionHandler: @escaping (WKNavigationActionPolicy) -> Void) {
        decisionHandler(navigationAction.request.url?.scheme == "about" ? .allow : .cancel)
    }
    func webView(_ webView: WKWebView, didFailProvisionalNavigation navigation: WKNavigation!, withError error: Error) {
        print("FAIL synthetic navigation: \(error.localizedDescription)"); done = true
    }
    func webView(_ webView: WKWebView, didFinish navigation: WKNavigation!) {
        guard let file = Bundle.main.url(forResource: "ClipPage", withExtension: "js"), let js = try? String(contentsOf: file, encoding: .utf8) else { done = true; return }
        web.evaluateJavaScript(js + "\nvar captured; ExtensionPreprocessingJS.run({completionFunction:function(x){captured=x}}); captured;") { result, error in
            guard error == nil, let data = result as? [String: Any], let html = data["pageHTML"] as? String else {
                print("FAIL WebKit snapshot: \(error?.localizedDescription ?? "no HTML")"); self.done = true; return
            }
            let checks: [(String, Bool)] = [
                ("static MathJax/KaTeX MathML retained", html.components(separatedBy: "wiz-clip-formula").count - 1 == 2),
                ("fractions and superscripts retained", html.contains("<mfrac>") && html.contains("<msup>")),
                ("hidden MathML made visible", !html.contains("position:absolute") && !html.contains("aria-hidden")),
                ("renderer-only content removed", !html.contains("renderer-only-text") && !html.contains("<svg")),
                ("remote image manifest created", (data["resources"] as? [String]) == ["https://example.invalid/image.png"]),
                ("unsafe scripts/events/forms removed", !html.contains("<script") && !html.contains("onclick") && !html.contains("private-form-value")),
                ("embedded image kept", html.contains("data:image/png;base64,")),
                ("no implicit CSS URLs", !html.contains("url(")),
                ("no phantom empty selection", data["selectionHTML"] as? String == "")]
            for (name, ok) in checks { print("\(ok ? "PASS" : "FAIL") \(name)") }
            self.code = checks.allSatisfy({ $0.1 }) ? 0 : 1
            print("WebKit static capture checks: \(checks.count), failures: \(checks.filter({ !$0.1 }).count)")
            self.done = true
        }
    }
}
func captureClipperSelfTest(fixture: String) -> Int32 {
    // Only local named synthetic fixture; no credentials and no production endpoint.
    guard fixture.hasPrefix("/"), fixture.hasSuffix("/clipper-cloud-page.html"),
          let html = try? String(contentsOfFile: fixture, encoding: .utf8) else { return 2 }
    let app = NSApplication.shared; app.setActivationPolicy(.accessory); app.finishLaunching()
    let runner = CaptureTestRunner()
    let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 900, height: 600), styleMask: [.titled], backing: .buffered, defer: false)
    window.title = "剪藏静态捕获合成测试"; window.contentView = runner.web; window.orderFront(nil)
    WKContentRuleListStore.default().compileContentRuleList(forIdentifier: "synthetic-no-network",
        encodedContentRuleList: "[{\"trigger\":{\"url-filter\":\"^https?://\"},\"action\":{\"type\":\"block\"}}]") { rules, error in
        guard let rules, error == nil else { runner.done = true; return }
        runner.web.configuration.userContentController.add(rules)
        runner.web.loadHTMLString(html, baseURL: nil)
    }
    let deadline = Date().addingTimeInterval(20)
    while !runner.done && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.02)) }
    window.close()
    if !runner.done { print("FAIL synthetic snapshot timed out") }
    return runner.code
}
