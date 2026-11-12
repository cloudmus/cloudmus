package dev.cloudmus.mobile;

import android.app.Activity;
import android.content.ComponentName;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Handler;
import android.os.Looper;

// GWM head units (Haval): the car's media widget shows the track and the
// controls of a third-party player only after Media Center has picked it as
// its source, and Media Center picks one only when it's asked to open it
// (its dispatcher is the one writer of the pick, with a window: there's no
// quiet way). The pick is a persist.* property, so it survives restarts and
// one ask is enough. Nothing here touches another Android: without the
// vendor's Media Center, or if the property can't be read, it does nothing.
final class GwmMediaCenter {
    private static final String TAG = "GwmMediaCenter";
    private static final String PACKAGE = "com.gwm.app.car.media";
    private static final String DISPATCHER = "com.android.car.media.MediaDispatcherActivity";
    private static final String PROPERTY = "persist.gwm.carmediaapp.connectedthirdpackage";
    // How long Media Center is left in front before the player's screen is
    // brought back.
    private static final long BACK_AFTER_MS = 1500;

    // Once a process: a pick that didn't take isn't asked for again and again.
    private static boolean asked;

    private GwmMediaCenter() {
    }

    // A GWM unit: its Media Center is installed.
    static boolean present(Context context) {
        try {
            context.getPackageManager().getPackageInfo(PACKAGE, 0);
            return true;
        } catch (PackageManager.NameNotFoundException error) {
            return false;
        }
    }

    // The package Media Center last picked, "" if none; null where the
    // property can't be read (not this kind of unit, or the system says no).
    static String connectedPackage() {
        try {
            Object value = Class.forName("android.os.SystemProperties").getMethod("get", String.class)
                .invoke(null, PROPERTY);
            return value instanceof String ? (String) value : null;
        } catch (ReflectiveOperationException | RuntimeException error) {
            return null;
        }
    }

    // Asks Media Center to pick this player if it hasn't, and returns to the
    // player's screen. For when the screen is up: the user is there, and
    // Media Center's window is a blink.
    static void connectIfNeeded(Activity activity) {
        if (asked)
            return;
        asked = true;
        Context context = activity.getApplicationContext();
        if (!present(context))
            return;
        String connected = connectedPackage();
        if (connected == null) {
            CmLog.i(TAG, "Media Center is here, its pick can't be read: leaving it");
            return;
        }
        if (connected.equals(context.getPackageName())) {
            CmLog.i(TAG, "Media Center has picked this player already");
            return;
        }
        String component = new ComponentName(context, PlaybackService.class).flattenToString();
        Intent ask = new Intent("android.car.intent.action.MEDIA_TEMPLATE")
            .setClassName(PACKAGE, DISPATCHER)
            .putExtra("android.car.intent.extra.MEDIA_COMPONENT", component)
            .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        try {
            context.startActivity(ask);
        } catch (RuntimeException error) {
            CmLog.e(TAG, "can't ask Media Center", error);
            return;
        }
        CmLog.i(TAG, "asked Media Center to pick this player (it had \"" + connected + "\")");
        new Handler(Looper.getMainLooper()).postDelayed(() -> {
            Intent back = new Intent(context, MainActivity.class)
                .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_REORDER_TO_FRONT);
            try {
                context.startActivity(back);
            } catch (RuntimeException error) {
                CmLog.e(TAG, "can't come back to the player's screen", error);
            }
        }, BACK_AFTER_MS);
    }
}
