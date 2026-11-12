package app.cloudmus.mobile;

import android.app.Activity;
import android.app.UiModeManager;
import android.content.Context;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.drawable.ColorDrawable;
import android.os.Build;
import android.view.View;
import android.view.Window;
import android.view.WindowInsets;

// The system's part of the theme: the bars and what's behind the window. The
// window goes on under the navigation bar, which is see-through, so the generated
// background reaches the bottom of the screen; the views keep clear of the bars by
// the insets told to them (Platform). The same under the status bar, which is
// see-through too: the views paint what's under it, the generated background
// where a page has one (see Main.qml), and its icons are light or dark to suit.
final class Appearance {
    private static final String PREFS = "appearance";

    // In pixels, for the views.
    private static int insetTop;
    private static int insetLeft;
    private static int insetRight;
    private static int insetBottom;
    // What last came to the system's theme, and whether a page's own picture is
    // under the status bar: its icons then are light whatever the theme.
    private static boolean themeDark = true;
    private static boolean overAura;

    private Appearance() {
    }

    // Before anything is drawn: what was chosen last time.
    static void setUp(Activity activity) {
        SharedPreferences prefs = activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        Window window = activity.getWindow();
        window.setNavigationBarColor(Color.TRANSPARENT);
        window.setStatusBarColor(Color.TRANSPARENT);
        if (Build.VERSION.SDK_INT >= 29) {
            window.setNavigationBarContrastEnforced(false);
            window.setStatusBarContrastEnforced(false);
        }
        View decor = window.getDecorView();
        if (prefs.contains("dark"))
            paint(activity, prefs.getBoolean("dark", true), prefs.getInt("bar", 0), prefs.getInt("back", 0));
        else
            layOutUnderNavigationBar(decor, 0);
        decor.setOnApplyWindowInsetsListener((view, insets) -> {
            remember(insets);
            // A turn of the screen (and Qt's own flags with it) can take the icons'
            // color back to what the system likes: say it again once that's done.
            view.post(() -> paintIcons(activity));
            return view.onApplyWindowInsets(insets);
        });
    }

    private static void remember(WindowInsets insets) {
        int top = insets.getSystemWindowInsetTop();
        int left = insets.getSystemWindowInsetLeft();
        int right = insets.getSystemWindowInsetRight();
        int bottom = insets.getSystemWindowInsetBottom();
        // A keyboard counts too, and has a say of its own: only the bar's.
        bottom = Math.min(bottom, insets.getStableInsetBottom());
        if (top == insetTop && left == insetLeft && right == insetRight && bottom == insetBottom)
            return;
        insetTop = top;
        insetLeft = left;
        insetRight = right;
        insetBottom = bottom;
        UiBridge.insetsChanged();
    }

    static String insets() {
        return insetTop + " " + insetLeft + " " + insetRight + " " + insetBottom;
    }

    // `scheme`: "auto", "light" or "dark", as picked; `dark`: what that came to.
    static void apply(Activity activity, String scheme, boolean dark, String barColor, String backColor) {
        int bar = Color.parseColor(barColor);
        int back = Color.parseColor(backColor);
        activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit()
            .putBoolean("dark", dark).putInt("bar", bar).putInt("back", back).apply();
        paint(activity, dark, bar, back);
        // So that the system's own first frame of the next start is right too.
        // Older systems show the system's theme until this app's window is up.
        if (Build.VERSION.SDK_INT >= 31) {
            int mode = "light".equals(scheme) ? UiModeManager.MODE_NIGHT_NO
                : "dark".equals(scheme) ? UiModeManager.MODE_NIGHT_YES : UiModeManager.MODE_NIGHT_AUTO;
            UiModeManager manager = activity.getSystemService(UiModeManager.class);
            if (manager != null)
                manager.setApplicationNightMode(mode);
        }
    }

    // A page with its own picture under the status bar, or not any more.
    static void setOverAura(Activity activity, boolean over) {
        if (over == overAura)
            return;
        overAura = over;
        CmLog.i("Appearance", "status bar icons: " + (themeDark || over ? "light" : "dark") + ", over a picture: " + over);
        SharedPreferences prefs = activity.getSharedPreferences(PREFS, Context.MODE_PRIVATE);
        paint(activity, themeDark, prefs.getInt("bar", 0), prefs.getInt("back", 0));
    }

    // `bar` is no longer the status bar's color (it's see-through: Main.qml
    // paints under it), but still comes with the rest of the theme.
    private static void paint(Activity activity, boolean dark, int bar, int back) {
        Window window = activity.getWindow();
        themeDark = dark;
        window.setBackgroundDrawable(new ColorDrawable(back));
        int light = 0;
        if (Build.VERSION.SDK_INT < 30) {
            light = dark || overAura ? 0 : View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR;
            if (!dark && Build.VERSION.SDK_INT >= 27)
                light |= View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR;
        }
        layOutUnderNavigationBar(window.getDecorView(), light);
        paintIcons(activity);
    }

    // From Android 11 the icons' color is the window controller's, and the old
    // flags above only get in its way (a call without them makes the icons dark
    // or light again, as they were not asked for).
    private static void paintIcons(Activity activity) {
        if (Build.VERSION.SDK_INT < 30)
            return;
        android.view.WindowInsetsController controller = activity.getWindow().getInsetsController();
        if (controller == null)
            return;
        int status = android.view.WindowInsetsController.APPEARANCE_LIGHT_STATUS_BARS;
        int navigation = android.view.WindowInsetsController.APPEARANCE_LIGHT_NAVIGATION_BARS;
        controller.setSystemBarsAppearance(themeDark || overAura ? 0 : status, status);
        controller.setSystemBarsAppearance(themeDark ? 0 : navigation, navigation);
    }

    private static void layOutUnderNavigationBar(View decor, int otherFlags) {
        decor.setSystemUiVisibility(View.SYSTEM_UI_FLAG_LAYOUT_STABLE | View.SYSTEM_UI_FLAG_LAYOUT_HIDE_NAVIGATION
            | otherFlags);
    }
}
