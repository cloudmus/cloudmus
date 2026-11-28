package app.cloudmus.mobile;

import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.net.Uri;

// GWM head units (Haval): the vendor's settings app takes over both the app
// info screen (ACTION_APPLICATION_DETAILS_SETTINGS) and the runtime
// permission request (REQUEST_PERMISSIONS), and its pages know only the
// camera, location and microphone: a storage request is refused without a
// dialog. Android's own Settings is still installed, with its app info screen
// exported, and from there the user can grant storage the usual way. Other
// devices never come here.
final class GwmAppSettings {
    private static final String TAG = "GwmAppSettings";
    private static final String VENDOR_SETTINGS = "com.gwm.app.settings";
    private static final String SETTINGS = "com.android.settings";
    private static final String[] SCREENS = {
        // This app's info page: Permissions → Storage.
        "com.android.settings.applications.InstalledAppDetails",
        // The list of apps, if the page itself is missing.
        "com.android.settings.Settings$ManageApplicationsActivity",
    };

    private GwmAppSettings() {
    }

    // A GWM unit: the vendor's settings app is installed.
    static boolean present(Context context) {
        try {
            context.getPackageManager().getPackageInfo(VENDOR_SETTINGS, 0);
            return true;
        } catch (PackageManager.NameNotFoundException error) {
            return false;
        }
    }

    // Opens Android's own screen for this app's permissions; false if none
    // of them could be opened.
    static boolean openAppPermissions(Context context) {
        Uri app = Uri.fromParts("package", context.getPackageName(), null);
        for (String screen : SCREENS) {
            Intent intent = new Intent(android.provider.Settings.ACTION_APPLICATION_DETAILS_SETTINGS, app)
                .setComponent(new ComponentName(SETTINGS, screen));
            try {
                context.startActivity(intent);
                CmLog.i(TAG, "opened " + screen);
                return true;
            } catch (RuntimeException error) {
                // ActivityNotFoundException, or a SecurityException on a unit
                // where the screen isn't exported.
                CmLog.w(TAG, "can't open " + screen + ": " + error);
            }
        }
        return false;
    }
}
