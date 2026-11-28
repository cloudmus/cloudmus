package app.cloudmus.mobile;

import android.app.Activity;
import android.graphics.Bitmap;
import android.graphics.Canvas;
import android.graphics.RectF;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;
import android.view.View;
import android.window.SplashScreenView;

// Android 12+'s start screen, kept up until StartSplash.qml has drawn the same
// logo in the same place: Android takes it down at the activity's first frame,
// long before Qt draws anything, and the screen blinked empty in between.
// Older systems have the window's background for it (Appearance).
final class StartScreen {
    private static final String TAG = "StartScreen";
    // If the UI never says it has drawn, the screen still goes.
    private static final long LONGEST_MS = 5000;

    private static SplashScreenView view;
    private static String iconRect = "";
    private static boolean released;

    private StartScreen() {
    }

    static void hold(Activity activity) {
        if (Build.VERSION.SDK_INT < 31)
            return;
        activity.getSplashScreen().setOnExitAnimationListener(splash -> {
            synchronized (StartScreen.class) {
                if (released) {
                    splash.remove();
                    return;
                }
                view = splash;
                iconRect = measure(activity, splash);
            }
            CmLog.i(TAG, "held, icon at " + iconRect);
            new Handler(Looper.getMainLooper()).postDelayed(() -> {
                if (view == splash)
                    CmLog.w(TAG, "the UI hasn't drawn in " + LONGEST_MS + " ms");
                release();
            }, LONGEST_MS);
        });
    }

    // Where the logo is, in the coordinates of the view Qt draws in (its
    // window starts there). How large Android draws the icon in its view is up
    // to the system (1.5 times the view on a Pixel), so it's drawn here to see
    // where the disc came out; if that draws nothing (the icon can be another
    // process's surface), the Pixel's way is assumed.
    private static String measure(Activity activity, SplashScreenView splash) {
        View icon = splash.getIconView();
        View content = activity.findViewById(android.R.id.content);
        if (icon == null || icon.getWidth() == 0 || content == null)
            return "";
        int[] at = new int[2];
        int[] origin = new int[2];
        icon.getLocationInWindow(at);
        content.getLocationInWindow(origin);
        int w = icon.getWidth();
        int h = icon.getHeight();
        float left;
        float top;
        float side;
        RectF disc = drawnDisc(icon);
        if (disc != null) {
            // The disc is the logo but 1/64 on each side (logo_disc.xml).
            side = disc.width() * 64f / 62f;
            left = at[0] + disc.centerX() - side / 2;
            top = at[1] + disc.centerY() - side / 2;
            CmLog.i(TAG, "icon view " + w + "x" + h + ", logo drawn " + Math.round(side) + " across");
        } else {
            // splash_icon.xml's 4/9 of an icon 1.5 times the view.
            side = Math.min(w, h) * 2f / 3f;
            left = at[0] + (w - side) / 2;
            top = at[1] + (h - side) / 2;
            CmLog.i(TAG, "icon view " + w + "x" + h + " draws nothing here, logo assumed " + Math.round(side));
        }
        return (left - origin[0]) + " " + (top - origin[1]) + " " + side + " " + side;
    }

    // The opaque part of what the icon's view draws, around it too (it draws
    // past its bounds), relative to the view; null if nothing.
    private static RectF drawnDisc(View icon) {
        int w = icon.getWidth();
        int h = icon.getHeight();
        Bitmap bitmap;
        try {
            bitmap = Bitmap.createBitmap(w * 2, h * 2, Bitmap.Config.ARGB_8888);
            Canvas canvas = new Canvas(bitmap);
            canvas.translate(w / 2f, h / 2f);
            icon.draw(canvas);
        } catch (RuntimeException error) {
            CmLog.w(TAG, "can't draw the icon: " + error);
            return null;
        }
        int[] row = new int[bitmap.getWidth()];
        int minX = Integer.MAX_VALUE, minY = Integer.MAX_VALUE, maxX = -1, maxY = -1;
        for (int y = 0; y < bitmap.getHeight(); ++y) {
            bitmap.getPixels(row, 0, row.length, 0, y, row.length, 1);
            for (int x = 0; x < row.length; ++x) {
                if ((row[x] >>> 24) < 128)
                    continue;
                minX = Math.min(minX, x);
                maxX = Math.max(maxX, x);
                minY = Math.min(minY, y);
                maxY = Math.max(maxY, y);
            }
        }
        bitmap.recycle();
        // Nothing, or not a disc: no telling.
        if (maxX < 0 || Math.abs((maxX - minX) - (maxY - minY)) > 4)
            return null;
        return new RectF(minX - w / 2f, minY - h / 2f, maxX + 1 - w / 2f, maxY + 1 - h / 2f);
    }

    static synchronized String iconRect() {
        return view != null ? iconRect : "";
    }

    static void release() {
        new Handler(Looper.getMainLooper()).post(() -> {
            SplashScreenView held;
            synchronized (StartScreen.class) {
                released = true;
                held = view;
                view = null;
            }
            if (held != null) {
                held.remove();
                CmLog.i(TAG, "released");
            }
        });
    }
}
