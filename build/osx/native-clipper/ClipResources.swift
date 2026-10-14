// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import ImageIO
import UniformTypeIdentifiers
import Darwin
import CFNetwork

enum ClipResourcePolicy {
    static func existingLocalHTTPSProxy() -> [AnyHashable: Any]? {
        guard let settings = CFNetworkCopySystemProxySettings()?.takeRetainedValue() as? [String: Any],
              (settings["HTTPSEnable"] as? NSNumber)?.boolValue == true,
              let host = settings["HTTPSProxy"] as? String, ["127.0.0.1", "localhost", "::1"].contains(host),
              let port = (settings["HTTPSPort"] as? NSNumber)?.intValue, (1...65535).contains(port) else { return nil }
        // Use only the already user-configured local forward proxy. Do not
        // create/change a proxy, read a proxy password or add a trust override.
        return ["HTTPSEnable": 1, "HTTPSProxy": host, "HTTPSPort": port]
    }
    static func urlIsAllowed(_ url: URL) -> Bool {
        guard url.scheme == "https", url.user == nil, url.password == nil,
              url.port == nil || url.port == 443, let host = url.host?.lowercased(),
              host.contains("."), !host.hasSuffix("."), !host.hasSuffix(".local"), !host.hasSuffix(".localhost"),
              !host.hasSuffix(".internal"), !host.contains(":"), host != "localhost",
              host.range(of: "^[a-z0-9][a-z0-9.-]+$", options: .regularExpression) != nil,
              host.range(of: "^[0-9.]+$", options: .regularExpression) == nil else { return false }
        return true
    }
    static func ipv4IsPublic(_ bytes: [UInt8]) -> Bool {
        guard bytes.count == 4 else { return false }
        let a = bytes[0], b = bytes[1]
        return a != 0 && a != 10 && a != 127 && a < 224 && !(a == 169 && b == 254)
            && !(a == 172 && (16...31).contains(b)) && !(a == 192 && [0, 168].contains(b))
            && !(a == 100 && (64...127).contains(b)) && !(a == 198 && [18, 19, 51].contains(b))
            && !(a == 203 && b == 0)
    }
    static func dnsIsPublic(_ host: String) -> Bool {
        var result: UnsafeMutablePointer<addrinfo>?
        var hints = addrinfo(); hints.ai_family = AF_UNSPEC; hints.ai_socktype = SOCK_STREAM
        guard getaddrinfo(host, "443", &hints, &result) == 0, let first = result else { return false }
        defer { freeaddrinfo(first) }
        var node: UnsafeMutablePointer<addrinfo>? = first; var found = false
        while let entry = node {
            let value = entry.pointee
            guard let address = value.ai_addr else { return false }
            if value.ai_family == AF_INET {
                let v4 = UnsafeRawPointer(address).assumingMemoryBound(to: sockaddr_in.self).pointee.sin_addr
                let bytes = withUnsafeBytes(of: v4) { Array($0) }
                if !ipv4IsPublic(bytes) { return false }; found = true
            } else if value.ai_family == AF_INET6 {
                let v6 = UnsafeRawPointer(address).assumingMemoryBound(to: sockaddr_in6.self).pointee.sin6_addr
                let bytes = withUnsafeBytes(of: v6) { Array($0) }
                // Public global unicast only, rejecting mapped IPv4/local/link-local.
                if bytes.count != 16 || (bytes[0] & 0xe0) != 0x20 { return false }; found = true
            } else { return false }
            node = value.ai_next
        }
        return found
    }
    static func pngData(_ data: Data) -> Data? {
        guard data.count <= 1024 * 1024,
              let source = CGImageSourceCreateWithData(data as CFData, nil),
              let type = CGImageSourceGetType(source) as String?,
              ["public.png", "public.jpeg", "com.compuserve.gif", "org.webmproject.webp"].contains(type),
              let properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nil) as? [String: Any],
              let width = properties[kCGImagePropertyPixelWidth as String] as? Int,
              let height = properties[kCGImagePropertyPixelHeight as String] as? Int,
              width > 0, height > 0, width <= 16000, height <= 16000, width * height <= 16000000,
              let image = CGImageSourceCreateThumbnailAtIndex(source, 0, [
                kCGImageSourceCreateThumbnailFromImageAlways: true,
                kCGImageSourceThumbnailMaxPixelSize: 1800,
                kCGImageSourceCreateThumbnailWithTransform: true] as CFDictionary) else { return nil }
        let output = NSMutableData()
        guard let dest = CGImageDestinationCreateWithData(output, UTType.png.identifier as CFString, 1, nil) else { return nil }
        CGImageDestinationAddImage(dest, image, nil)
        guard CGImageDestinationFinalize(dest), output.length <= 500000 else { return nil }
        return output as Data
    }
}

// Streaming cap applies before decoding, not after an unbounded dataTask buffer.
private final class ClipImageDownload: NSObject, URLSessionDataDelegate {
    private var session: URLSession?
    private var bytes = Data()
    private var completion: ((Data?) -> Void)?
    private var started: Date = Date()
    func start(_ url: URL, proxy: [AnyHashable: Any]? = nil, completion: @escaping (Data?) -> Void) {
        self.completion = completion; started = Date()
        let config = URLSessionConfiguration.ephemeral
        if let proxy { config.connectionProxyDictionary = proxy }
        config.httpCookieStorage = nil; config.httpShouldSetCookies = false; config.urlCredentialStorage = nil
        config.timeoutIntervalForRequest = 8; config.timeoutIntervalForResource = 10
        session = URLSession(configuration: config, delegate: self, delegateQueue: nil)
        session?.dataTask(with: url).resume()
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, willPerformHTTPRedirection response: HTTPURLResponse,
                    newRequest request: URLRequest, completionHandler: @escaping (URLRequest?) -> Void) {
        // No implicit request to a private network or a credentialed redirect.
        completionHandler(nil)
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive response: URLResponse,
                    completionHandler: @escaping (URLSession.ResponseDisposition) -> Void) {
        guard let http = response as? HTTPURLResponse, http.statusCode == 200,
              response.expectedContentLength <= 1024 * 1024,
              ["image/png", "image/jpeg", "image/gif", "image/webp"].contains(response.mimeType ?? "") else {
            completionHandler(.cancel); return
        }; completionHandler(.allow)
    }
    func urlSession(_ session: URLSession, dataTask: URLSessionDataTask, didReceive data: Data) {
        if bytes.count + data.count > 1024 * 1024 || Date().timeIntervalSince(started) > 10 { dataTask.cancel(); return }
        bytes.append(data)
    }
    func urlSession(_ session: URLSession, task: URLSessionTask, didCompleteWithError error: Error?) {
        let callback = completion; completion = nil
        let image = error == nil ? ClipResourcePolicy.pngData(bytes) : nil
        session.finishTasksAndInvalidate(); self.session = nil
        DispatchQueue.main.async { callback?(image) }
    }
}

final class ClipResources {
    typealias Fetch = (URL, @escaping (Data?) -> Void) -> Void
    private let fetch: Fetch
    init(fetch: Fetch? = nil) {
        self.fetch = fetch ?? { url, completion in
            // getaddrinfo can outlive the session timeout. Deliver exactly one
            // result and keep a stalled DNS lookup from blocking the share UI.
            let lock = NSLock(); var delivered = false
            func finish(_ data: Data?) {
                lock.lock(); let first = !delivered; delivered = true; lock.unlock()
                if first { completion(data) }
            }
            DispatchQueue.main.asyncAfter(deadline: .now() + 12) { finish(nil) }
            DispatchQueue.global(qos: .utility).async {
                let proxy = ClipResourcePolicy.existingLocalHTTPSProxy()
                // A forward proxy resolves the original public hostname itself;
                // rejecting the local fake-DNS result would break VPNs. Force
                // this request through that existing proxy, not a direct fake IP.
                guard let host = url.host, proxy != nil || ClipResourcePolicy.dnsIsPublic(host) else {
                    DispatchQueue.main.async { finish(nil) }; return
                }
                DispatchQueue.main.async {
                    lock.lock(); let expired = delivered; lock.unlock()
                    if !expired { let task = ClipImageDownload(); task.start(url, proxy: proxy, completion: finish) }
                }
            }
        }
    }
    // IDs originate from Safari's detached snapshot, never from original page IDs.
    func embed(html: String, urls: [String], progress: @escaping (Int, Int) -> Void,
               completion: @escaping (String, Int) -> Void) {
        var output = html, index = 0, missing = 0, embedded = 0
        let deadline = Date().addingTimeInterval(40)
        func step() {
            guard index < urls.count else { completion(output, missing); return }
            let current = index; index += 1
            let marker = "data-wiz-clip-resource=\"\(current)\""
            let expression = try! NSRegularExpression(pattern: "<img\\b[^>]*" + NSRegularExpression.escapedPattern(for: marker) + "[^>]*>", options: [.caseInsensitive])
            let matches = expression.matches(in: output, range: NSRange(output.startIndex..., in: output))
            if matches.isEmpty { step(); return } // e.g. other mode, no request
            func apply(_ data: Data?) {
                let usable = data.flatMap { embedded + $0.count <= 4 * 1024 * 1024 ? $0 : nil }
                if let usable { embedded += usable.count } else { missing += matches.count }
                for match in matches.reversed() {
                    guard let range = Range(match.range, in: output) else { continue }
                    let alt = "图片 \(current + 1)"
                    let replacement = usable.map { "<img src=\"data:image/png;base64,\($0.base64EncodedString())\" alt=\"\(alt)\" style=\"max-width:100%;height:auto\">" }
                        ?? "<span>[\(alt)未能嵌入，请查看原网页]</span>"
                    output.replaceSubrange(range, with: replacement)
                }
                progress(index, urls.count); step()
            }
            guard Date() < deadline, let url = URL(string: urls[current]), ClipResourcePolicy.urlIsAllowed(url) else { apply(nil); return }
            fetch(url, apply)
        }
        step()
    }
}
