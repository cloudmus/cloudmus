package dev.cloudmus.mobile;

import android.Manifest;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.os.Build;
import android.os.Bundle;

import org.qtproject.qt.android.bindings.QtActivity;

public class MainActivity extends QtActivity {
    private static final String TAG = "MainActivity";

    @Override
    public void onCreate(Bundle savedInstanceState) {
        CmLog.mark("activity onCreate");
        CmLog.i(TAG, "onCreate, intent " + getIntent());
        super.onCreate(savedInstanceState);
        // Shared Download/Cloudmus is a plain folder up to Android 10.
        if (Build.VERSION.SDK_INT >= 23 && Build.VERSION.SDK_INT < 30
            && checkSelfPermission(Manifest.permission.WRITE_EXTERNAL_STORAGE) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[] {Manifest.permission.WRITE_EXTERNAL_STORAGE}, 1);
        }
    }

    @Override
    public void onRequestPermissionsResult(int code, String[] permissions, int[] results) {
        super.onRequestPermissionsResult(code, permissions, results);
        CmLog.i(TAG, "permissions result: " + java.util.Arrays.toString(permissions) + " "
                + java.util.Arrays.toString(results));
        CmLog.storageChanged();
    }

    @Override
    protected void onNewIntent(Intent intent) {
        CmLog.i(TAG, "onNewIntent " + intent);
        super.onNewIntent(intent);
    }

    @Override
    protected void onStart() {
        CmLog.i(TAG, "onStart");
        super.onStart();
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
