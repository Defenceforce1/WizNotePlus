# macOS biometric capability diagnostic

This independent Objective-C++ program only calls `canEvaluatePolicy` and reads
`biometryType`. Run with `--capability-only`; other arguments exit 2 before
creating an authentication context. It does not authenticate, open a biometric
panel, read biometric state/templates, access any keychain, start WizNotePlus,
read credentials, or change the signing/setup of an application.

Build locally using the existing Apple SDK:

```
xcrun clang++ -std=c++17 -fobjc-arc -framework Foundation -framework LocalAuthentication capability.mm -o /chosen/development/path/biometric-capability
/chosen/development/path/biometric-capability --capability-only
```

Exit 0 means the capability query completed. It does not mean Touch ID unlock
works. Availability is a momentary OS result; it can change when hardware,
enrollment, login state or biometric lockout changes. No biometric integration
is linked into the main client by this diagnostic.

The intended persistent-unlock design uses a current-biometry-protected,
device-only, non-synchronizing data-protection keychain item, explicit opt-in,
ordinary certificate password fallback, and account/server/certificate binding.
It must not degrade to an unprotected file-based keychain item after missing
signing entitlements. See the phase123 report for the unresolved prerequisites.
