package app.cloudmus.mobile;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.storage.StorageManager;
import android.os.storage.StorageVolume;
import android.provider.DocumentsContract;

import java.io.File;

// A source's path setting (SettingField.qml's folder button), from the
// system's picker. A source reads it by path, as on the desktop, so the
// document the picker hands back goes back to the path it stands for; one
// that has none (a cloud's, another app's) comes back empty, and the user is
// told to pick from the device's storage.
final class PathPicker {
    private static final String TAG = "PathPicker";
    static final int REQUEST = 6;

    private PathPicker() {
    }

    // `path` empty: picked, but not on the device's storage.
    private static native void nativePicked(String path, boolean cancelled);

    static void pick(Context context, boolean directory) {
        if (!(context instanceof Activity))
            return;
        Activity activity = (Activity) context;
        activity.runOnUiThread(() -> {
            Intent intent = directory ? new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE)
                : new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE).setType("*/*");
            try {
                activity.startActivityForResult(intent, REQUEST);
            } catch (Exception error) {
                // No picker at all (some head units).
                CmLog.e(TAG, "no picker", error);
                answer("", true);
            }
        });
    }

    // MainActivity, with the picker's answer.
    static void picked(Context context, int result, Intent data) {
        Uri uri = result == Activity.RESULT_OK && data != null ? data.getData() : null;
        if (uri == null) {
            answer("", true);
            return;
        }
        String path = path(context, uri);
        CmLog.i(TAG, "picked " + uri + " -> " + (path.isEmpty() ? "no path" : path));
        answer(path, false);
    }

    private static void answer(String path, boolean cancelled) {
        try {
            nativePicked(path, cancelled);
        } catch (UnsatisfiedLinkError notLoaded) {
            // Qt's gone: no field to fill.
        }
    }

    private static String path(Context context, Uri uri) {
        try {
            boolean tree = DocumentsContract.isTreeUri(uri);
            String id = tree ? DocumentsContract.getTreeDocumentId(uri) : DocumentsContract.getDocumentId(uri);
            String authority = uri.getAuthority();
            if ("com.android.providers.downloads.documents".equals(authority) && id.startsWith("raw:"))
                return id.substring("raw:".length());
            if (!"com.android.externalstorage.documents".equals(authority))
                return "";
            // "<volume>:<path in it>": "primary:Music/Rock", "1A2B-3C4D:Music".
            int colon = id.indexOf(':');
            if (colon < 0)
                return "";
            String volume = id.substring(0, colon);
            String relative = id.substring(colon + 1);
            File root;
            if ("primary".equalsIgnoreCase(volume))
                root = Environment.getExternalStorageDirectory();
            else if ("home".equalsIgnoreCase(volume))
                root = Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_DOCUMENTS);
            else
                root = volumeRoot(context, volume);
            return (relative.isEmpty() ? root : new File(root, relative)).getAbsolutePath();
        } catch (Exception error) {
            CmLog.e(TAG, "no path for " + uri, error);
            return "";
        }
    }

    // A removable volume, by the uuid the picker names it by.
    private static File volumeRoot(Context context, String uuid) {
        if (Build.VERSION.SDK_INT >= 30) {
            for (StorageVolume volume : context.getSystemService(StorageManager.class).getStorageVolumes()) {
                if (uuid.equalsIgnoreCase(volume.getUuid()) && volume.getDirectory() != null)
                    return volume.getDirectory();
            }
        }
        return new File("/storage/" + uuid);
    }
}
