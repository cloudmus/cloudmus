package app.cloudmus.mobile;

import org.qtproject.qt.android.bindings.QtApplication;

// Runs first in every process of the app (the UI's and :playback), before any
// component: the log is up for everything after, the receiver's start
// included.
public class CmApp extends QtApplication {
    @Override
    public void onCreate() {
        CmLog.init(this);
        super.onCreate();
    }

    @Override
    public void onTrimMemory(int level) {
        CmLog.w("lifecycle", "onTrimMemory(" + level + ")");
        super.onTrimMemory(level);
    }

    @Override
    public void onLowMemory() {
        CmLog.w("lifecycle", "onLowMemory");
        super.onLowMemory();
    }
}
