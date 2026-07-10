/* stub.m — minimum-viable tvOS app entry point.
 *
 * Sole purpose: be a well-formed enough .app that xcodebuild's
 * provisioning preflight (-allowProvisioningUpdates +
 * -allowProvisioningDeviceRegistration) downloads a development
 * provisioning profile for bundle id local.yos.tvos onto this Mac.
 *
 * After that, build-tools/tvos/deploy.sh ignores this stub entirely;
 * it cross-builds the real yos via meson, splices the downloaded
 * embedded.mobileprovision into the real bundle, and codesigns +
 * installs that. The stub is build-only on the host — never
 * shipped, never run.
 */
#import <UIKit/UIKit.h>

int main(int argc, char *argv[]) {
    @autoreleasepool {
        return UIApplicationMain(argc, argv, nil, nil);
    }
}
