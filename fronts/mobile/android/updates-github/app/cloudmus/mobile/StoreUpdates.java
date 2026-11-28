package app.cloudmus.mobile;

import android.content.Context;

// The GitHub build has no store to update it: UpdateInstaller does. The
// store build has the real one (updates-store/), with the stores' libraries.
public final class StoreUpdates {
    private StoreUpdates() {
    }

    public static String available(Context context) {
        return "";
    }

    public static void check(Context context, String store, String manual) {
    }

    public static void start(Context context, String store) {
    }

    public static void openStorePage(Context context, String store) {
    }
}
