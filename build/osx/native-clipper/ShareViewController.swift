// SPDX-License-Identifier: GPL-3.0-or-later
import AppKit
import UniformTypeIdentifiers

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
    private var input: ClipInput?
    private var connection: ClipConnection?
    private var pending: [String: Any]?
    private var loaded = false
    private var completed = false
    override func loadView() {
        // Compact native form, following the original Mac share flow instead of
        // presenting a large browser-extension panel. No old nib/assets copied.
        view = NSView(frame: NSRect(x: 0, y: 0, width: 460, height: 410))
        preferredContentSize = NSSize(width: 460, height: 410)
        let heading = NSTextField(labelWithString: "添加到 WizNotePlus")
        heading.font = .boldSystemFont(ofSize: 16); heading.alignment = .center
        heading.frame = NSRect(x: 24, y: 371, width: 412, height: 22)
        view.addSubview(heading)
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
        previewLabel.frame = NSRect(x: 24, y: 191, width: 412, height: 16); view.addSubview(previewLabel)
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
        pair.frame = NSRect(x: 233, y: 63, width: 135, height: 30); pair.target = self; pair.action = #selector(pairClient); view.addSubview(pair)
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
                    self.connection = ClipCredentialStore.load()
                    if self.connection != nil { self.refreshDestination() }
                    else { self.status.stringValue = "先在客户端“工具 → 连接原生网页剪藏器”获取连接码" }
                } catch { self.status.stringValue = error.localizedDescription }
            }
        }
    }
    @objc private func updateClipPreview() {
        guard let input else { return }
        preview.string = mode.selectedSegment == 1 ? input.selectionText : input.text
        if preview.string.isEmpty { preview.string = "已收到 HTML 正文；这里仅展示纯文字摘要，保存时保留正文结构。" }
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
        guard !completed, let input, let connection, let folder = folders.selectedItem?.title else { return }
        if pending == nil {
            let html = mode.selectedSegment == 1 ? input.selectionHTML : input.pageHTML
            guard !html.isEmpty, !titleField.stringValue.trimmingCharacters(in: .whitespacesAndNewlines).isEmpty else {
                status.stringValue = "缺少正文或标题"; return
            }
            pending = ["accountID": connection.accountID, "requestID": UUID().uuidString.lowercased(),
                "title": titleField.stringValue, "folder": folder, "url": input.url, "html": html]
        }
        save.isEnabled = false; titleField.isEnabled = false; folders.isEnabled = false; mode.isEnabled = false
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
        extensionContext?.cancelRequest(withError: NSError(domain: NSCocoaErrorDomain, code: NSUserCancelledError))
    }
}
