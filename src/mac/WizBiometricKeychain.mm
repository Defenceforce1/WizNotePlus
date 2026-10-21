#include "WizBiometricKeychain.h"
#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#import <Security/Security.h>
#import <Security/SecTask.h>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace WizBiometricKeychain {
namespace {
NSString* const service = @"WizNotePlus.local-certificate-biometric.v1";
NSString* ns(const QString& value)
{
    const auto bytes = value.toUtf8();
    return [[NSString alloc] initWithBytes:bytes.constData() length:bytes.size() encoding:NSUTF8StringEncoding];
}
Error statusError(OSStatus status)
{
    if (status == errSecSuccess) return Error::None;
    if (status == errSecUserCanceled) return Error::Canceled;
    if (status == errSecItemNotFound) return Error::Missing;
    if (status == errSecDuplicateItem) return Error::AlreadyExists;
    if (status == errSecMissingEntitlement) return Error::SigningRequired;
    if (status == errSecAuthFailed || status == errSecInteractionNotAllowed) return Error::AccessDenied;
    return Error::Failed;
}
NSMutableDictionary* query(const QString& identity)
{
    return [@{(__bridge id)kSecClass: (__bridge id)kSecClassGenericPassword,
        (__bridge id)kSecAttrService: service,
        (__bridge id)kSecAttrAccount: ns(identity),
        (__bridge id)kSecUseDataProtectionKeychain: @YES,
        (__bridge id)kSecAttrSynchronizable: @NO} mutableCopy];
}
bool biometricAvailable()
{
    LAContext* context = [[LAContext alloc] init];
    const BOOL allowed = [context canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:nil];
    [context invalidate];
    return allowed;
}
}

bool hasSigningPrerequisites()
{
    @autoreleasepool {
        SecCodeRef self = nullptr;
        if (SecCodeCopySelf(kSecCSDefaultFlags, &self) != errSecSuccess) return false;
        CFDictionaryRef information = nullptr;
        const OSStatus status = SecCodeCopySigningInformation(self, kSecCSSigningInformation, &information);
        const bool valid = SecCodeCheckValidity(self, kSecCSDefaultFlags, nullptr) == errSecSuccess;
        CFRelease(self);
        if (status != errSecSuccess || !information) return false;
        NSDictionary* info = CFBridgingRelease(information);
        NSNumber* flags = info[(__bridge id)kSecCodeInfoFlags];
        if (!valid || ![flags isKindOfClass:[NSNumber class]] || (flags.unsignedIntValue & kSecCodeSignatureAdhoc)) return false;
        SecTaskRef task = SecTaskCreateFromSelf(kCFAllocatorDefault);
        if (!task) return false;
        id application = CFBridgingRelease(SecTaskCopyValueForEntitlement(task, CFSTR("com.apple.application-identifier"), nullptr));
        id team = CFBridgingRelease(SecTaskCopyValueForEntitlement(task, CFSTR("com.apple.developer.team-identifier"), nullptr));
        id groups = CFBridgingRelease(SecTaskCopyValueForEntitlement(task, CFSTR("keychain-access-groups"), nullptr));
        CFRelease(task);
        // Do not fabricate entitlements or fall back to file-based keychains.
        // Even a true result is only a prerequisite; SecItem enforces access.
        return [application isKindOfClass:[NSString class]] && [application length] > 0
            && [team isKindOfClass:[NSString class]] && [team length] > 0
            && [groups isKindOfClass:[NSArray class]] && [groups containsObject:application];
    }
}

Error enroll(const Scope& scope, const QString& password, bool explicitConsent)
{
    const auto validation = validateEnrollment(scope, password, explicitConsent);
    if (validation != Error::None) return validation;
    if (!hasSigningPrerequisites()) return Error::SigningRequired;
    @autoreleasepool {
        if (!biometricAvailable()) return Error::BiometryUnavailable;
        CFErrorRef error = nullptr;
        SecAccessControlRef access = SecAccessControlCreateWithFlags(kCFAllocatorDefault,
            kSecAttrAccessibleWhenUnlockedThisDeviceOnly, kSecAccessControlBiometryCurrentSet, &error);
        if (error) CFRelease(error);
        if (!access) return Error::Failed;
        auto attributes = query(itemIdentity(scope));
        attributes[(__bridge id)kSecAttrAccessControl] = (__bridge id)access;
        const auto secret = password.toUtf8();
        attributes[(__bridge id)kSecValueData] = [NSData dataWithBytes:secret.constData() length:secret.size()];
        // Create-only. Never delete/update an existing credential to make add
        // succeed. Explicit removal is a separate user decision.
        const OSStatus status = SecItemAdd((__bridge CFDictionaryRef)attributes, nullptr);
        CFRelease(access);
        return statusError(status);
    }
}

Error forget(const Scope& scope, bool explicitConsent)
{
    if (!explicitConsent) return Error::ConsentRequired;
    const auto identity = itemIdentity(scope);
    if (identity.isEmpty()) return Error::InvalidScope;
    if (!hasSigningPrerequisites()) return Error::SigningRequired;
    @autoreleasepool {
        // Exact service+account+non-sync data-protection item only, no broad wipe.
        return statusError(SecItemDelete((__bridge CFDictionaryRef)query(identity)));
    }
}

struct ReadOperation::Private {
    LAContext* __strong context = [[LAContext alloc] init];
    std::atomic<bool> canceled{false}, started{false};
};
ReadOperation::ReadOperation() : d(new Private) {}
ReadOperation::~ReadOperation() { cancel(); }
void ReadOperation::cancel()
{
    d->canceled.store(true);
    [d->context invalidate];
}
Result ReadOperation::read(const Scope& scope)
{
    if (d->canceled.load() || d->started.exchange(true)) return {Error::Canceled, {}};
    const auto identity = itemIdentity(scope);
    if (identity.isEmpty()) return {Error::InvalidScope, {}};
    if (!hasSigningPrerequisites()) return {Error::SigningRequired, {}};
    @autoreleasepool {
        if (d->canceled.load()) return {Error::Canceled, {}};
        if (![d->context canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:nil])
            return {d->canceled.load() ? Error::Canceled : Error::BiometryUnavailable, {}};
        // New context for each one-shot read, no previous-device-unlock reuse,
        // no OS password/Watch OR condition. Manual cert password is separate.
        d->context.touchIDAuthenticationAllowableReuseDuration = 0;
        d->context.localizedFallbackTitle = @"";
        d->context.localizedCancelTitle = @"取消";
        d->context.localizedReason = @"使用 Touch ID 解锁当前账号的加密笔记";
        // Query only this item's attributes first, never its secret/UI.
        // Let LA evaluate that actual ACL asynchronously: canceling an implicit
        // SecItemCopyMatching prompt need not promptly release its worker.
        LAContext* metadataContext = [[LAContext alloc] init];
        metadataContext.interactionNotAllowed = YES;
        auto metadataQuery = query(identity);
        metadataQuery[(__bridge id)kSecReturnAttributes] = @YES;
        metadataQuery[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
        metadataQuery[(__bridge id)kSecUseAuthenticationContext] = metadataContext;
        CFTypeRef metadataFound = nullptr;
        const OSStatus metadataStatus = SecItemCopyMatching((__bridge CFDictionaryRef)metadataQuery, &metadataFound);
        id metadataValue = CFBridgingRelease(metadataFound);
        [metadataContext invalidate];
        if (d->canceled.load()) return {Error::Canceled, {}};
        id controlValue = nil;
        if (metadataStatus == errSecSuccess) {
            if (![metadataValue isKindOfClass:[NSDictionary class]]) return {Error::Failed, {}};
            controlValue = metadataValue[(__bridge id)kSecAttrAccessControl];
            if (!controlValue || CFGetTypeID((__bridge CFTypeRef)controlValue) != SecAccessControlGetTypeID())
                return {Error::Failed, {}};
        } else if (metadataStatus == errSecInteractionNotAllowed) {
            // macOS can protect attributes too. Evaluate the SAME constraint
            // used by this service's create-only enrollment, without changing
            // the stored item. SecItem below still enforces its actual ACL and
            // may fail; no second prompt or weaker protection is permitted.
            CFErrorRef accessError = nullptr;
            controlValue = CFBridgingRelease(SecAccessControlCreateWithFlags(kCFAllocatorDefault,
                kSecAttrAccessibleWhenUnlockedThisDeviceOnly, kSecAccessControlBiometryCurrentSet, &accessError));
            if (accessError) CFRelease(accessError);
            if (!controlValue) return {Error::Failed, {}};
        } else {
            qWarning("Touch ID noninteractive attribute lookup failed (%d)", int(metadataStatus));
            return {statusError(metadataStatus), {}};
        }
        struct Evaluation {
            std::mutex mutex;
            std::condition_variable changed;
            bool complete = false;
            Error error = Error::Failed;
        };
        const auto evaluation = std::make_shared<Evaluation>();
        [d->context evaluateAccessControl:(__bridge SecAccessControlRef)controlValue
            operation:LAAccessControlOperationUseItem localizedReason:d->context.localizedReason
            reply:^(BOOL success, NSError* error) {
                if (!success) qWarning("Touch ID asynchronous access evaluation failed (%ld)", long(error.code));
                std::lock_guard<std::mutex> lock(evaluation->mutex);
                evaluation->error = success ? Error::None
                    : ([error.domain isEqualToString:LAErrorDomain]
                        && (error.code == LAErrorUserCancel || error.code == LAErrorAppCancel || error.code == LAErrorSystemCancel)
                        ? Error::Canceled : Error::AccessDenied);
                evaluation->complete = true;
                evaluation->changed.notify_all();
                // No QWidget, database, operation or password is captured; a
                // late OS reply owns only this isolated completion state.
            }];
        {
            std::unique_lock<std::mutex> lock(evaluation->mutex);
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(120);
            while (!evaluation->complete && !d->canceled.load()
                   && std::chrono::steady_clock::now() < deadline)
                evaluation->changed.wait_for(lock, std::chrono::milliseconds(25));
            if (d->canceled.load() || !evaluation->complete) {
                lock.unlock(); [d->context invalidate]; return {Error::Canceled, {}};
            }
            if (evaluation->error != Error::None) return {evaluation->error, {}};
        }
        if (d->canceled.load()) return {Error::Canceled, {}};
        // Use this authenticated context without allowing a second prompt.
        d->context.interactionNotAllowed = YES;
        auto attributes = query(identity);
        attributes[(__bridge id)kSecReturnData] = @YES;
        attributes[(__bridge id)kSecMatchLimit] = (__bridge id)kSecMatchLimitOne;
        attributes[(__bridge id)kSecUseAuthenticationContext] = d->context;
        if (d->canceled.load()) return {Error::Canceled, {}};
        CFTypeRef found = nullptr;
        const OSStatus status = SecItemCopyMatching((__bridge CFDictionaryRef)attributes, &found);
        id value = CFBridgingRelease(found);
        if (d->canceled.load()) return {Error::Canceled, {}};
        if (status != errSecSuccess) {
            qWarning("Touch ID noninteractive credential read failed (%d)", int(status));
            return {statusError(status), {}};
        }
        if (![value isKindOfClass:[NSData class]] || [value length] == 0 || [value length] > 1024)
            return {Error::CredentialMismatch, {}};
        const QByteArray bytes(static_cast<const char*>([value bytes]), [value length]);
        const QString password = QString::fromUtf8(bytes);
        if (password.toUtf8() != bytes || WizCertificateCreation::verify(scope.certificate, password)
            != WizCertificateCreation::Error::None) return {Error::CredentialMismatch, {}};
        if (d->canceled.load()) return {Error::Canceled, {}};
        return {Error::None, password};
    }
}
}
