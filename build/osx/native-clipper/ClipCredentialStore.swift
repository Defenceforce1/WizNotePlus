// SPDX-License-Identifier: GPL-3.0-or-later
import Foundation
import Security

enum ClipCredentialStore {
    private static var query: [String: Any] {
        [kSecClass as String: kSecClassGenericPassword,
         kSecAttrService as String: "io.github.defenceforce1.wiznoteplus.clipper.local-bridge",
         kSecAttrAccount as String: "local-connection"]
    }
    static func load() -> ClipConnection? {
        var request = query
        request[kSecReturnData as String] = true
        request[kSecMatchLimit as String] = kSecMatchLimitOne
        var item: CFTypeRef?
        guard SecItemCopyMatching(request as CFDictionary, &item) == errSecSuccess,
              let bytes = item as? Data,
              let dictionary = (try? JSONSerialization.jsonObject(with: bytes)) as? [String: Any] else { return nil }
        return try? ClipConnection(dictionary)
    }
    static func save(_ connection: ClipConnection) -> Bool {
        guard let bytes = try? JSONSerialization.data(withJSONObject:
            ["token": connection.token, "accountID": connection.accountID]) else { return false }
        let attributes = [kSecValueData as String: bytes]
        let status = SecItemUpdate(query as CFDictionary, attributes as CFDictionary)
        if status == errSecSuccess { return true }
        if status != errSecItemNotFound { return false }
        var request = query
        request[kSecValueData as String] = bytes
        return SecItemAdd(request as CFDictionary, nil) == errSecSuccess
    }
}
