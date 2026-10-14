// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit
import UniformTypeIdentifiers
import WebKit

@objc(WizNotePlusShareViewController)
final class ShareViewController: NSViewController {
    private let status = NSTextField(wrappingLabelWithString: "正在读取 Safari 分享内容…")
    private let titleField = NSTextField(string: "")
    private let account = NSTextField(wrappingLabelWithString: "尚未连接新版客户端")
    private let folders = NSPopUpButton(frame: .zero, pullsDown: false)
    private let mode = NSSegmentedControl(labels: ["整个网页", "选中内容"], trackingMode: .selectOne, target: nil, action: nil)
    private let code = NSSecureTextField(string: "")
    private let pair = NSButton(title: "连接客户端", target: nil, action: nil)
    private let save = NSButton(title: "保存", target: nil, action: nil)
    private let activity = NSProgressIndicator()
    private let preview = NSTextView(frame: .zero)
    private let client = LocalClipClient()
    private let cloud = CloudClipClient()
    private let resourceCapture = ClipResources()
    private let destination = NSSegmentedControl(labels: ["直接上传为知云端", "保存到本机客户端"], trackingMode: .selectOne, target: nil, action: nil)
    private var cloudAccount: CloudClipAccount?
    private var cloudBody: [String: Any]?
    private var cloudRequestID: String?
    private var cloudTaskID: String?
    private var cloudSubmitted = false
    private var cloudFolder = ""
    private var missingResources = 0
    private var previewPanel: NSPanel?
    private let layoutPreview = NSButton(title: "版式预览", target: nil, action: nil)
    private var input: ClipInput?
    private var connection: ClipConnection?
    private var pending: [String: Any]?
    private var loaded = false
    private var completed = false
    override func loadView() {
        // Compact native form, following the original Mac share flow instead of
        // presenting a large browser-extension panel. No old nib/assets copied.
        view = NSView(frame: NSRect(x: 0, y: 0, width: 460, height: 460))
        preferredContentSize = NSSize(width: 460, height: 460)
        let heading = NSTextField(labelWithString: "添加到 WizNotePlus")
        heading.font = .boldSystemFont(ofSize: 16); heading.alignment = .center
        heading.frame = NSRect(x: 24, y: 421, width: 412, height: 22)
        view.addSubview(heading)
        destination.frame = NSRect(x: 24, y: 375, width: 412, height: 28)
        destination.selectedSegment = 0; destination.target = self; destination.action = #selector(changeDestination)
        view.addSubview(destination)
        for (caption, y) in [("笔记标题", 333.0), ("账号", 295.0), ("文件夹", 257.0), ("保存内容", 219.0)] {
            let label = NSTextField(labelWithString: caption); label.alignment = .right
            label.textColor = .secondaryLabelColor; label.frame = NSRect(x: 20, y: y, width: 72, height: 22)
            view.addSubview(label)
        }
        titleField.frame = NSRect(x: 104, y: 330, width: 332, height: 26); titleField.placeholderString = "笔记标题"; view.addSubview(titleField)
        account.frame = NSRect(x: 104, y: 293, width: 332, height: 24); view.addSubview(account)
        folders.frame = NSRect(x: 104, y: 253, width: 332, height: 28); view.addSubview(folders)
        mode.frame = NSRect(x: 104, y: 215, width: 332, height: 28); mode.selectedSegment = 0
        mode.setLabel("保存整页", forSegment: 0); mode.setLabel("保存选中", forSegment: 1)
        mode.target = self; mode.action = #selector(updateClipPreview); view.addSubview(mode)
        let previewLabel = NSTextField(labelWithString: "内容摘要（保存时保留 HTML，不只是这些文字）")
        previewLabel.font = .systemFont(ofSize: 11); previewLabel.textColor = .secondaryLabelColor
        previewLabel.frame = NSRect(x: 24, y: 191, width: 320, height: 16); view.addSubview(previewLabel)
        layoutPreview.frame = NSRect(x: 345, y: 184, width: 91, height: 28)
        layoutPreview.target = self; layoutPreview.action = #selector(showLayoutPreview); view.addSubview(layoutPreview)
        let scroll = NSScrollView(frame: NSRect(x: 24, y: 101, width: 412, height: 83))
        scroll.hasVerticalScroller = true; scroll.borderType = .bezelBorder
        preview.isEditable = false; preview.isRichText = false
        preview.frame = NSRect(x: 0, y: 0, width: 390, height: 83)
        preview.font = .systemFont(ofSize: 12); preview.textContainerInset = NSSize(width: 6, height: 6)
        preview.isVerticallyResizable = true
        preview.maxSize = NSSize(width: CGFloat.greatestFiniteMagnitude, height: CGFloat.greatestFiniteMagnitude)
        preview.autoresizingMask = [.width]; preview.textContainer?.widthTracksTextView = true
        scroll.documentView = preview; view.addSubview(scroll)
        code.frame = NSRect(x: 24, y: 65, width: 198, height: 26); code.placeholderString = "客户端的 8 位连接码"; view.addSubview(code)
        pair.frame = NSRect(x: 233, y: 63, width: 135, height: 30); pair.target = self; pair.action = #selector(connectDestination); view.addSubview(pair)
        activity.style = .spinning; activity.isDisplayedWhenStopped = false
        activity.frame = NSRect(x: 397, y: 69, width: 18, height: 18); view.addSubview(activity)
        status.font = .systemFont(ofSize: 11); status.frame = NSRect(x: 24, y: 10, width: 226, height: 47); view.addSubview(status)
        save.frame = NSRect(x: 345, y: 17, width: 91, height: 30); save.bezelStyle = .rounded
        save.keyEquivalent = "\r"; save.target = self; save.action = #selector(saveNote); save.isEnabled = false; view.addSubview(save)
        let cancel = NSButton(title: "取消", target: self, action: #selector(cancelShare))
        cancel.bezelStyle = .rounded; cancel.keyEquivalent = "\u{1b}"; cancel.frame = NSRect(x: 254, y: 17, width: 83, height: 30); view.addSubview(cancel)
    }
    override func viewDidAppear() {
        super.viewDidAppear()
        if loaded { return }; loaded = true
        guard let items = extensionContext?.inputItems as? [NSExtensionItem] else {
            status.stringValue = "未收到分享内容"; return
        }
        let providers = items.flatMap { $0.attachments ?? [] }
        guard let provider = providers.first(where: { $0.hasItemConformingToTypeIdentifier(UTType.propertyList.identifier) }) else {
            status.stringValue = "只收到链接，没有网页正文。请在 Safari 网页中使用分享入口。"; return
        }
        provider.loadItem(forTypeIdentifier: UTType.propertyList.identifier, options: nil) { [weak self] item, error in
            DispatchQueue.main.async {
                guard let self else { return }
                do {
                    guard error == nil, let dictionary = item as? [String: Any],
                          let results = dictionary[NSExtensionJavaScriptPreprocessingResultsKey] as? [String: Any] else {
                        throw ClipperError.message("Safari 未返回网页正文，请重试分享")
                    }
                    self.input = try ClipInput(dictionary: results)
                    self.titleField.stringValue = self.input!.title
                    self.mode.setEnabled(!self.input!.pageHTML.isEmpty, forSegment: 0)
                    self.mode.setEnabled(!self.input!.selectionHTML.isEmpty, forSegment: 1)
                    self.mode.selectedSegment = self.input!.pageHTML.isEmpty ? 1 : 0
                    self.updateClipPreview()
                    let count = results["formulaCount"] as? Int ?? 0
                    self.preview.toolTip = "捕获 \(count) 个静态公式；\(self.input!.resources.count) 个远程图片资源"
                    self.cloudAccount = ClipCredentialStore.loadCloud()
                    self.connection = ClipCredentialStore.load()
                    self.changeDestination()
                } catch { self.status.stringValue = error.localizedDescription }
            }
        }
    }
    @objc private func updateClipPreview() {
        guard let input else { return }
        preview.string = mode.selectedSegment == 1 ? input.selectionText : input.text
        if preview.string.isEmpty { preview.string = "已收到 HTML 正文；这里仅展示纯文字摘要，保存时保留正文结构。" }
    }
    @objc private func showLayoutPreview() {
        guard let input, let window = view.window, !completed else { return }
        layoutPreview.isEnabled = false; activity.startAnimation(nil)
        let html = mode.selectedSegment == 1 ? input.selectionHTML : input.pageHTML
        resourceCapture.embed(html: html, urls: input.resources, progress: { [weak self] count, total in
            self?.status.stringValue = "预览：正在保存图片 \(count)/\(total)…"
        }) { [weak self] html, missing in
            guard let self, !self.completed else { return }
            self.layoutPreview.isEnabled = true; self.activity.stopAnimation(nil)
            let panel = NSPanel(contentRect: NSRect(x: 0, y: 0, width: 840, height: 620),
                styleMask: [.titled], backing: .buffered, defer: false)
            panel.title = "保存版式预览（仅本机，不上传）"
            let config = WKWebViewConfiguration(); config.websiteDataStore = .nonPersistent()
            config.defaultWebpagePreferences.allowsContentJavaScript = false
            let web = WKWebView(frame: NSRect(x: 0, y: 50, width: 840, height: 570), configuration: config)
            panel.contentView?.addSubview(web)
            let info = NSTextField(labelWithString: missing > 0 ? "\(missing) 张图片未能嵌入；公式按静态 MathML 显示" : "HTML＋图片／静态公式预览；不连接主软件，不上传云端")
            info.frame = NSRect(x: 16, y: 17, width: 660, height: 22); panel.contentView?.addSubview(info)
            let close = NSButton(title: "返回", target: self, action: #selector(closeLayoutPreview))
            close.frame = NSRect(x: 736, y: 12, width: 88, height: 30); panel.contentView?.addSubview(close)
            self.previewPanel = panel; window.beginSheet(panel)
            // Even a future sanitizer regression must not run page JS or load
            // external subresources. The preview has no cloud session/cookies.
            WKContentRuleListStore.default().compileContentRuleList(forIdentifier: "clipper-static-preview",
                encodedContentRuleList: "[{\"trigger\":{\"url-filter\":\"^https?://\"},\"action\":{\"type\":\"block\"}}]") { rules, error in
                guard let rules, error == nil else { self.status.stringValue = "无法建立离线预览保护"; self.closeLayoutPreview(); return }
                web.configuration.userContentController.add(rules)
                web.loadHTMLString("<!doctype html><meta charset=\"utf-8\"><body style=\"margin:24px;line-height:1.6\">" + html + "</body>", baseURL: nil)
            }
        }
    }
    @objc private func closeLayoutPreview() {
        guard let panel = previewPanel else { return }
        panel.sheetParent?.endSheet(panel); panel.orderOut(nil); previewPanel = nil
    }
    @objc private func connectDestination() {
        if destination.selectedSegment == 0 { loginCloud() } else { pairClient() }
    }
    @objc private func changeDestination() {
        guard pending == nil, !cloudSubmitted, cloudBody == nil else { return }
        save.isEnabled = false; folders.removeAllItems()
        if destination.selectedSegment == 0 {
            code.isHidden = true; pair.isHidden = false; pair.title = cloudAccount == nil ? "登录为知账号" : "切换账号"
            account.stringValue = cloudAccount?.userID ?? "未登录为知云端"
            status.stringValue = "独立上传，无需安装或打开主软件；需要联网"
            if cloudAccount != nil { refreshCloudFolders() }
        } else {
            code.isHidden = false; pair.isHidden = false; pair.title = "连接客户端"
            account.stringValue = "尚未连接新版客户端"
            if connection != nil { refreshDestination() }
            else { status.stringValue = "先在客户端“工具 → 连接原生网页剪藏器”获取连接码" }
        }
    }
    private func loginCloud() {
        guard let window = view.window else { return }
        let alert = NSAlert(); alert.messageText = "登录为知笔记云端"
        alert.informativeText = "账号和密码仅通过 HTTPS 发送到 note.wiz.cn，不读取旧剪藏器登录信息。密码不保存。"
        alert.addButton(withTitle: "登录"); alert.addButton(withTitle: "取消")
        if cloudSubmitted {
            alert.addButton(withTitle: "已核对上次任务，解除等待")
            alert.informativeText += "\n解除等待仅清除本机任务提示，不撤销服务器任务；未核对就再次剪藏可能生成重复笔记。"
        }
        let fields = NSView(frame: NSRect(x: 0, y: 0, width: 310, height: 102))
        let user = NSTextField(frame: NSRect(x: 0, y: 70, width: 310, height: 26)); user.placeholderString = "为知账号"
        user.stringValue = cloudAccount?.userID ?? ""
        let password = NSSecureTextField(frame: NSRect(x: 0, y: 36, width: 310, height: 26)); password.placeholderString = "密码"
        let remember = NSButton(checkboxWithTitle: "记住登录（令牌保存在本扩展钥匙串）", target: nil, action: nil)
        remember.frame = NSRect(x: 0, y: 0, width: 310, height: 24); remember.state = .off
        fields.addSubview(user); fields.addSubview(password); fields.addSubview(remember); alert.accessoryView = fields
        alert.beginSheetModal(for: window) { [weak self] response in
            if let self, response == .alertThirdButtonReturn, let account = self.cloudAccount {
                UserDefaults.standard.removeObject(forKey: "unfinishedCloudTask." + account.kbGuid)
                self.cloudSubmitted = false; self.cloudBody = nil; self.cloudTaskID = nil; self.cloudRequestID = nil
                self.titleField.isEnabled = true; self.folders.isEnabled = true; self.mode.isEnabled = true
                self.destination.isEnabled = true; self.layoutPreview.isEnabled = true; self.save.title = "保存"
                self.changeDestination(); password.stringValue = ""; return
            }
            guard let self, response == .alertFirstButtonReturn else { password.stringValue = ""; return }
            self.pair.isEnabled = false; self.activity.startAnimation(nil); self.status.stringValue = "正在登录为知服务器…"
            let secret = password.stringValue; password.stringValue = ""
            self.cloud.login(userID: user.stringValue.trimmingCharacters(in: .whitespacesAndNewlines), password: secret) { [weak self] result in
                guard let self, !self.completed else { return }; self.pair.isEnabled = true; self.activity.stopAnimation(nil)
                switch result {
                case .success(let account):
                    if self.cloudSubmitted && account.kbGuid != self.cloudAccount?.kbGuid {
                        self.status.stringValue = "上次上传尚未确认，请重新登录同一账号查询结果"; return
                    }
                    self.cloudAccount = account; self.account.stringValue = account.userID
                    let saved = remember.state != .on || ClipCredentialStore.saveCloud(account)
                    if remember.state != .on { ClipCredentialStore.forgetCloud() }
                    self.refreshCloudFolders()
                    if !saved { self.status.stringValue = "本次已登录；钥匙串保存失败，下次需重新登录" }
                case .failure(let error): self.status.stringValue = error.localizedDescription
                }
            }
        }
    }
    private func refreshCloudFolders() {
        guard let account = cloudAccount else { return }
        pair.title = "切换账号"; pair.isEnabled = false; activity.startAnimation(nil)
        cloud.folders(account: account) { [weak self] result in
            guard let self else { return }; self.pair.isEnabled = true; self.activity.stopAnimation(nil)
            guard self.destination.selectedSegment == 0 else { return }
            switch result {
            case .success(let items):
                self.folders.removeAllItems(); self.folders.addItems(withTitles: items)
                self.folders.selectItem(withTitle: "/网页剪藏/"); self.save.isEnabled = self.input != nil
                self.status.stringValue = "将上传到 \(account.userID)；完成后其他客户端可同步"
                if let task = UserDefaults.standard.dictionary(forKey: "unfinishedCloudTask." + account.kbGuid),
                   task["kbGuid"] as? String == account.kbGuid, let requestID = task["requestID"] as? String {
                    self.cloudSubmitted = true; self.cloudRequestID = requestID; self.cloudTaskID = task["id"] as? String
                    self.cloudFolder = task["folder"] as? String ?? ""
                    self.freezeEditing(); self.save.isEnabled = true; self.save.title = "查询上次结果"
                    self.status.stringValue = "有上次未确认的上传；先查询结果，不重复创建笔记"
                }
            case .failure(let error): self.save.isEnabled = false; self.status.stringValue = error.localizedDescription
            }
        }
    }
    private func freezeEditing() {
        titleField.isEnabled = false; folders.isEnabled = false; mode.isEnabled = false
        destination.isEnabled = false; pair.isEnabled = false; layoutPreview.isEnabled = false
    }
    private func saveCloudNote() {
        guard let account = cloudAccount, let input else { return }
        if cloudSubmitted { pollCloud(deadline: Date().addingTimeInterval(90)); return }
        guard let folder = folders.selectedItem?.title, !titleField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else { return }
        freezeEditing(); save.isEnabled = false; activity.startAnimation(nil)
        let html = mode.selectedSegment == 1 ? input.selectionHTML : input.pageHTML
        resourceCapture.embed(html: html, urls: input.resources, progress: { [weak self] count, total in
            self?.status.stringValue = "正在保存图片 \(count)/\(total)…"
        }) { [weak self] html, missing in
            guard let self, !self.completed else { return }; self.missingResources = missing
            do {
                let requestID = UUID().uuidString.lowercased(); self.cloudRequestID = requestID; self.cloudFolder = folder
                let body = try CloudClipClient.gatherBody(account: account, requestID: requestID, title: self.titleField.stringValue,
                    folder: folder, sourceURL: input.url, html: html)
                self.cloudBody = body; self.cloudSubmitted = true
                self.persistCloudTask(); self.status.stringValue = "正在上传到为知服务器…"
                self.cloud.upload(body: body, account: account) { [weak self] result in
                    guard let self else { return }
                    switch result {
                    case .success(let id): self.cloudTaskID = id; self.persistCloudTask(); self.pollCloud(deadline: Date().addingTimeInterval(90))
                    case .failure(let error):
                        self.activity.stopAnimation(nil); self.save.isEnabled = true; self.save.title = "查询上传结果"
                        self.status.stringValue = error.localizedDescription + "\n不自动重复上传；请先查询结果。"
                    }
                }
            } catch {
                self.activity.stopAnimation(nil); self.save.isEnabled = true; self.status.stringValue = error.localizedDescription
                self.titleField.isEnabled = true; self.folders.isEnabled = true; self.mode.isEnabled = true
                self.destination.isEnabled = true; self.pair.isEnabled = true; self.layoutPreview.isEnabled = true
            }
        }
    }
    private func persistCloudTask() {
        guard let account = cloudAccount, let requestID = cloudRequestID else { return }
        var task = ["kbGuid": account.kbGuid, "requestID": requestID, "folder": cloudFolder]
        if let id = cloudTaskID { task["id"] = id }
        UserDefaults.standard.set(task, forKey: "unfinishedCloudTask." + account.kbGuid) // No password, token or webpage body.
    }
    private func pollCloud(deadline: Date) {
        guard let account = cloudAccount, let requestID = cloudRequestID else { return }
        save.isEnabled = false; activity.startAnimation(nil); status.stringValue = "服务器正在处理；正在查询保存结果…"
        cloud.status(id: cloudTaskID, requestID: requestID, account: account) { [weak self] result in
            guard let self, !self.completed else { return }
            switch result {
            case .success(let status):
                if let status {
                    self.completed = true; self.activity.stopAnimation(nil)
                    UserDefaults.standard.removeObject(forKey: "unfinishedCloudTask." + account.kbGuid)
                    self.status.stringValue = "服务器已确认保存到 \(account.userID)：\(self.cloudFolder)"
                        + ((status == 101 || self.missingResources > 0) ? "\n部分图片未嵌入，请对照原网页。" : "")
                    DispatchQueue.main.asyncAfter(deadline: .now() + 3) { [weak self] in
                        self?.extensionContext?.completeRequest(returningItems: [], completionHandler: nil)
                    }
                } else if Date() < deadline {
                    DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in self?.pollCloud(deadline: deadline) }
                } else { self.cloudQueryFailed("服务器仍在处理；稍后查询，不重复上传") }
            case .failure(let error): self.cloudQueryFailed(error.localizedDescription)
            }
        }
    }
    private func cloudQueryFailed(_ text: String) {
        activity.stopAnimation(nil); save.isEnabled = true; save.title = "查询上传结果"; status.stringValue = text
        pair.isEnabled = true; pair.title = "重新登录"
    }
    private func refreshDestination() {
        guard let connection else { return }
        client.send("status", body: ["accountID": connection.accountID], connection: connection) { [weak self] result in
            guard let self else { return }
            switch result {
            case .success(let value): self.applyDestination(value)
            case .failure(let error):
                self.connection = nil; self.save.isEnabled = false
                self.code.isHidden = false; self.pair.isHidden = false; self.status.stringValue = error.localizedDescription
            }
        }
    }
    private func applyDestination(_ value: [String: Any]) {
        guard let name = value["accountName"] as? String, let items = value["folders"] as? [String], !items.isEmpty else {
            status.stringValue = "客户端目的账号或文件夹信息无效"; save.isEnabled = false; return
        }
        account.stringValue = name; account.toolTip = "目的账号：\(name)\n来源：\(input?.url ?? "")"
        folders.removeAllItems(); folders.addItems(withTitles: items)
        let previous = UserDefaults.standard.string(forKey: "lastFolder.\(connection!.accountID)") ?? "/网页剪藏/"
        if items.contains(previous) { folders.selectItem(withTitle: previous) }
        code.isHidden = true; pair.isHidden = true
        save.isEnabled = input != nil; status.stringValue = "请确认目的账号和文件夹，再保存"
    }
    @objc private func pairClient() {
        if pending != nil { status.stringValue = "上次保存结果未确认，请先在客户端检查该笔记"; return }
        let value = code.stringValue.trimmingCharacters(in: .whitespacesAndNewlines)
        guard value.count == 8, value.allSatisfy({ $0.isNumber && $0.isASCII }) else { status.stringValue = "请输入 8 位连接码"; return }
        pair.isEnabled = false; activity.startAnimation(nil); status.stringValue = "正在连接本机客户端…"
        client.send("pair", body: ["code": value]) { [weak self] result in
            guard let self else { return }; self.pair.isEnabled = true; self.activity.stopAnimation(nil)
            switch result {
            case .success(let dictionary):
                do {
                    let connection = try ClipConnection(dictionary); self.connection = connection
                    // A Safari probe must never leave test credentials in the
                    // real app keychain or overwrite a user's later pairing.
                    let synthetic = connection.accountID == "synthetic-clipper-kb"
                    let persisted = synthetic || ClipCredentialStore.save(connection); self.code.stringValue = ""
                    self.applyDestination(dictionary)
                    if !persisted { self.status.stringValue = "本次已连接；钥匙串未能保存连接信息，下次需重新输入连接码" }
                    if synthetic { self.status.stringValue = "独立测试账号，不会写入你的真实笔记；测试连接不保存" }
                } catch { self.status.stringValue = error.localizedDescription }
            case .failure(let error): self.status.stringValue = error.localizedDescription
            }
        }
    }
    @objc private func saveNote() {
        if destination.selectedSegment == 0 { saveCloudNote(); return }
        guard !completed, let input, let connection, let folder = folders.selectedItem?.title else { return }
        if pending == nil {
            let html = mode.selectedSegment == 1 ? input.selectionHTML : input.pageHTML
            guard !html.isEmpty, !titleField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
                status.stringValue = "缺少正文或标题"; return
            }
            pending = ["accountID": connection.accountID, "requestID": UUID().uuidString.lowercased(),
                "title": titleField.stringValue, "folder": folder, "url": input.url, "html": html]
        }
        save.isEnabled = false; freezeEditing()
        status.stringValue = "正在保存到本地客户端…"
        activity.startAnimation(nil)
        client.send("clip", body: pending!, connection: connection) { [weak self] result in
            guard let self else { return }; self.activity.stopAnimation(nil)
            switch result {
            case .success(let value):
                guard let savedFolder = value["folder"] as? String, let name = value["accountName"] as? String,
                      let guid = value["guid"] as? String, !guid.isEmpty else {
                    self.status.stringValue = "保存回执无效，请保留内容并检查客户端"; self.save.isEnabled = true; return
                }
                self.completed = true
                if connection.accountID != "synthetic-clipper-kb" {
                    UserDefaults.standard.set(savedFolder, forKey: "lastFolder.\(connection.accountID)")
                }
                self.status.stringValue = "已保存到 \(name)：\(savedFolder)"
                    + ((value["remoteImages"] as? Bool == true) ? "\n部分图片或图形未嵌入；可用原图链接已保留，请对照原网页。" : "")
                DispatchQueue.main.asyncAfter(deadline: .now() + 2) { [weak self] in
                    self?.extensionContext?.completeRequest(returningItems: [], completionHandler: nil)
                }
            case .failure(let error):
                self.save.title = "重试保存"; self.save.isEnabled = true; self.status.stringValue = error.localizedDescription
            }
        }
    }
    @objc private func cancelShare() {
        completed = true
        extensionContext?.cancelRequest(withError: NSError(domain: NSCocoaErrorDomain, code: NSUserCancelledError))
    }
}
