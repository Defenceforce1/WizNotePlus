// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit

if CommandLine.arguments.count == 2, CommandLine.arguments[1] == "--input-self-test" {
    var checks = 0, failures = 0
    func check(_ ok: Bool, _ text: String) { checks += 1; if !ok { failures += 1 }; print("\(ok ? "PASS" : "FAIL") \(text)") }
    let input: [String: Any] = ["title": "中文网页", "url": "https://example.invalid/article", "pageHTML": "<p>正文</p>",
        "selectionHTML": "<blockquote>选中</blockquote>", "text": "正文", "selectionText": "选中"]
    let valid = try? ClipInput(dictionary: input)
    check(valid?.title == "中文网页" && valid?.text == "正文", "Unicode title and body retained")
    check(valid?.selectionHTML == "<blockquote>选中</blockquote>", "selection retained separately from whole page")
    for url in ["file:///private", "javascript:alert(1)", "https://user:password@example.invalid/", "https:///missing"] {
        var bad = input; bad["url"] = url
        check((try? ClipInput(dictionary: bad)) == nil, "reject unsafe source URL")
    }
    var empty = input; empty["pageHTML"] = ""; empty["selectionHTML"] = ""
    check((try? ClipInput(dictionary: empty)) == nil, "link-only sharing is not called full clipping")
    var huge = input; huge["pageHTML"] = String(repeating: "x", count: ClipInput.maximumBytes + 1)
    check((try? ClipInput(dictionary: huge)) == nil, "oversized content rejected without silent truncation")
    check((try? ClipConnection(["token": String(repeating: "a", count: 64), "accountID": "synthetic"])) != nil, "valid local connection accepted")
    check((try? ClipConnection(["token": "bad", "accountID": "synthetic"])) == nil, "invalid token rejected")
    print("Native clipper input checks: \(checks), failures: \(failures)")
    exit(failures == 0 ? 0 : 1)
}
if CommandLine.arguments.count == 4, CommandLine.arguments[1] == "--bridge-self-test",
   let port = Int(CommandLine.arguments[2]), (1024...65535).contains(port), ![8848, 8849].contains(port) {
    // Test-only synthetic endpoint. Never load/save any real keychain item.
    let client = LocalClipClient(port: port)
    var done = false, resultCode: Int32 = 1
    client.send("pair", body: ["code": CommandLine.arguments[3]]) { result in
        do {
            let value = try result.get()
            guard value["accountID"] as? String == "synthetic-clipper-kb",
                  value["accountName"] as? String == "synthetic-clipper-account" else {
                throw ClipperError.message("Synthetic destination required")
            }
            let connection = try ClipConnection(value)
            let request: [String: Any] = ["accountID": connection.accountID, "requestID": UUID().uuidString.lowercased(),
                "title": "Swift 原生剪藏测试", "folder": "/网页剪藏/", "url": "https://example.invalid/swift-test",
                "html": "<h1>Swift → 本机客户端</h1><p>synthetic-swift-clip</p>"]
            client.send("clip", body: request, connection: connection) { saved in
                do {
                    let first = try saved.get()
                    guard let guid = first["guid"] as? String, !guid.isEmpty else { throw ClipperError.message("No saved note GUID") }
                    client.send("clip", body: request, connection: connection) { retried in
                        do {
                            let second = try retried.get()
                            guard second["guid"] as? String == guid, second["duplicate"] as? Bool == true else {
                                throw ClipperError.message("Retry was not idempotent")
                            }
                            print("PASS native Foundation client pairs, saves body and retries without duplicate")
                            resultCode = 0
                        } catch { print("FAIL \(error.localizedDescription)") }
                        done = true
                    }
                } catch { print("FAIL \(error.localizedDescription)"); done = true }
            }
        } catch { print("FAIL \(error.localizedDescription)"); done = true }
    }
    let deadline = Date().addingTimeInterval(20)
    while !done && Date() < deadline { RunLoop.main.run(until: Date().addingTimeInterval(0.02)) }
    exit(resultCode)
}
if CommandLine.arguments.count > 1 {
    fputs("Unknown test option; normal startup refused.\n", stderr); exit(2)
}

final class ClipperHost: NSObject, NSApplicationDelegate {
    var window: NSWindow?
    func applicationDidFinishLaunching(_ notification: Notification) {
        let window = NSWindow(contentRect: NSRect(x: 0, y: 0, width: 570, height: 310),
            styleMask: [.titled, .closable, .miniaturizable], backing: .buffered, defer: false)
        window.title = "WizNotePlus 网页剪藏"
        let text = NSTextField(wrappingLabelWithString:
            "新版 Apple Silicon 原生分享扩展\n\n1. 打开 WizNotePlus 新版客户端。\n2. 在客户端“工具 → 连接原生网页剪藏器”获取连接码。\n3. 在 Safari 网页点“分享”，选择“WizNotePlus 网页剪藏”。\n4. 首次输入连接码，确认账号/文件夹后保存。\n\n此应用不读取旧剪藏器账号，也不替换旧版。分享扩展可能需要在系统设置中单独启用；免费开发签名的多机加载仍需验证。")
        text.frame = NSRect(x: 24, y: 40, width: 520, height: 240); window.contentView?.addSubview(text)
        window.center(); window.makeKeyAndOrderFront(nil); self.window = window
        let menu = NSMenu(); let root = NSMenuItem(); menu.addItem(root)
        let appMenu = NSMenu(); appMenu.addItem(withTitle: "退出网页剪藏", action: #selector(NSApplication.terminate(_:)), keyEquivalent: "q")
        root.submenu = appMenu; NSApp.mainMenu = menu
    }
    func applicationShouldTerminateAfterLastWindowClosed(_ sender: NSApplication) -> Bool { true }
}
let application = NSApplication.shared
application.setActivationPolicy(.regular)
let delegate = ClipperHost(); application.delegate = delegate
application.run()
