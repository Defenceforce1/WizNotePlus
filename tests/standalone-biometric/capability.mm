#import <Foundation/Foundation.h>
#import <LocalAuthentication/LocalAuthentication.h>
#include <cstring>
#include <cstdio>

// Read-only capability diagnostic, deliberately not linked into WizNotePlus.
// No evaluatePolicy, biometric-domain-state, SecItem, password or account APIs.
int main(int argc, const char** argv)
{
    if (argc != 2 || std::strcmp(argv[1], "--capability-only") != 0) return 2;
    @autoreleasepool {
        LAContext* context = [[LAContext alloc] init];
        NSError* error = nil;
        const BOOL available = [context canEvaluatePolicy:LAPolicyDeviceOwnerAuthenticationWithBiometrics error:&error];
        NSString* kind = context.biometryType == LABiometryTypeTouchID ? @"touch-id"
            : (context.biometryType == LABiometryTypeFaceID ? @"face-id" : @"none-or-other");
        NSDictionary* result = @{
            @"schema": @1,
            @"mode": @"read-only-capability-no-authentication",
            @"biometryType": kind,
            @"canEvaluateBiometricsNow": @(available),
            @"localAuthenticationErrorCode": @(error ? error.code : 0),
            @"authenticationAttempted": @NO,
            @"keychainAccessed": @NO,
            @"biometricTemplateOrDomainStateRead": @NO,
            @"touchIdUnlockAccepted": @NO
        };
        NSData* json = [NSJSONSerialization dataWithJSONObject:result options:NSJSONWritingPrettyPrinted error:nil];
        [context invalidate];
        if (!json) return 1;
        std::fwrite(json.bytes, 1, json.length, stdout);
        std::fputc('\n', stdout);
    }
    return 0; // A completed capability query is not successful authentication.
}
