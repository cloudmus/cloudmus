package dev.cloudmus.mobile;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.media.AudioManager;
import android.os.Build;
import android.os.Bundle;
import android.view.KeyEvent;

import org.qtproject.qt.android.bindings.QtActivity;

public class MainActivity extends QtActivity {
    private static final String TAG = "MainActivity";

    @Override
    public void onCreate(Bundle savedInstanceState) {
        CmLog.mark("activity onCreate");
        CmLog.i(TAG, "onCreate, intent " + getIntent());
        UiBridge.pythonProbeRequested = getIntent().getBooleanExtra("pythonProbe", false);
        super.onCreate(savedInstanceState);
        preferFastestRefreshRate();
        // Shared Download/Cloudmus is a plain folder up to Android 10.
        if (Build.VERSION.SDK_INT >= 23 && Build.VERSION.SDK_INT < 30
            && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
        }
    }

    // Android leaves a surface that doesn't ask at 60 Hz, even on a 90 or
    // 120 Hz screen: scrolling under the finger then stutters. The fastest
    // mode at the current resolution.
    private void preferFastestRefreshRate() {
        android.view.Display display = getWindowManager().getDefaultDisplay();
        android.view.Display.Mode current = display.getMode();
        android.view.Display.Mode best = current;
        StringBuilder modes = new StringBuilder();
        for (android.view.Display.Mode mode : display.getSupportedModes()) {
            modes.append(' ').append(mode.getPhysicalWidth()).append('x').append(mode.getPhysicalHeight())
                .append('@').append(mode.getRefreshRate());
            if (mode.getPhysicalWidth() == current.getPhysicalWidth()
                && mode.getPhysicalHeight() == current.getPhysicalHeight()
                && mode.getRefreshRate() > best.getRefreshRate())
                best = mode;
        }
        android.view.WindowManager.LayoutParams params = getWindow().getAttributes();
        params.preferredDisplayModeId = best.getModeId();
        getWindow().setAttributes(params);
        CmLog.i(TAG, "display modes:" + modes + "; current " + current.getRefreshRate() + " Hz, asked for "
                + best.getRefreshRate() + " Hz");
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        CmLog.i(TAG, "permissions result: " + java.util.Arrays.toString(permissions) + " "
                + java.util.Arrays.toString(results));
        CmLog.storageChanged();
    }

    // With the app on screen the head unit hands the steering-wheel buttons
    // to it as plain keys, and Qt swallowed them: the media session never
    // saw them. They go to the media session the way they would with the
    // app in the background — through the system, which picks the session.
    // Vendor (unknown) codes are only logged.
    @Override
    public boolean dispatchKeyEvent(KeyEvent event) {
        int code = event.getKeyCode();
        if (isMediaKey(code)) {
            CmLog.i(TAG, "media key to the session: " + event);
            getSystemService(AudioManager.class).dispatchMediaKeyEvent(event);
            return true;
        }
        if (code == KeyEvent.KEYCODE_UNKNOWN || code > KeyEvent.getMaxKeyCode())
            CmLog.i(TAG, "key " + event);
        return super.dispatchKeyEvent(event);
    }

    // KeyEvent.isMediaSessionKey() is Android 10+.
    private static boolean isMediaKey(int code) {
        switch (code) {
            case KeyEvent.KEYCODE_HEADSETHOOK:
            case KeyEvent.KEYCODE_MEDIA_PLAY:
            case KeyEvent.KEYCODE_MEDIA_PAUSE:
            case KeyEvent.KEYCODE_MEDIA_PLAY_PAUSE:
            case KeyEvent.KEYCODE_MEDIA_STOP:
            case KeyEvent.KEYCODE_MEDIA_NEXT:
            case KeyEvent.KEYCODE_MEDIA_PREVIOUS:
            case KeyEvent.KEYCODE_MEDIA_REWIND:
            case KeyEvent.KEYCODE_MEDIA_FAST_FORWARD:
            case KeyEvent.KEYCODE_MEDIA_SKIP_FORWARD:
            case KeyEvent.KEYCODE_MEDIA_SKIP_BACKWARD:
            case KeyEvent.KEYCODE_MEDIA_STEP_FORWARD:
            case KeyEvent.KEYCODE_MEDIA_STEP_BACKWARD:
                return true;
            default:
                return false;
        }
    }

    @Override
    protected void onNewIntent(Intent intent) {
        CmLog.i(TAG, "onNewIntent " + intent);
        super.onNewIntent(intent);
    }

    // The UI's own start may have come too early for Android, and the service
    // ends when the music is closed: on screen, it's up.
    @Override
    protected void onResume() {
        super.onResume();
        UiBridge.startService(this);
    }

    @Override
    protected void onStart() {
        CmLog.i(TAG, "onStart");
        super.onStart();
        // After the screen is up, not before: on a GWM head unit it's a
        // blink of Media Center, once, to put the player in the car's widget.
        new android.os.Handler(android.os.Looper.getMainLooper()).postDelayed(() -> {
            if (!isFinishing() && !isDestroyed())
                GwmMediaCenter.connectIfNeeded(this);
        }, 2000);
    }

    @Override
    protected void onStop() {
        CmLog.i(TAG, "onStop");
        super.onStop();
    }

    @Override
    protected void onDestroy() {
        CmLog.i(TAG, "onDestroy, finishing " + isFinishing());
        super.onDestroy();
    }
}
