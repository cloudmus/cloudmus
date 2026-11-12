package dev.cloudmus.mobile;

import android.content.Context;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.util.zip.ZipEntry;
import java.util.zip.ZipInputStream;

// CPython and the backends, packed in the APK's assets (tools/bundle-python.sh),
// unpacked once per build: into the app's files, since Python reads them as
// files and loads its extension modules from there.
final class PythonRuntime {
    private static final String TAG = "Python";

    private PythonRuntime() {
    }

    // A full disk can leave a head unit unable to boot: rather no sources. Keep
    // in step with Playback::DiskSpace::kCriticalBytes.
    private static final long MIN_FREE_BYTES = 1024L * 1024 * 1024;

    // The directory for PYTHONHOME. Blocks for seconds on the first run.
    static synchronized File prepare(Context context) throws IOException {
        String stamp;
        try (BufferedReader reader = new BufferedReader(
                 new InputStreamReader(context.getAssets().open("python.stamp")))) {
            stamp = reader.readLine().trim();
        }
        File base = new File(context.getFilesDir(), "python");
        File home = new File(base, stamp);
        File done = new File(home, ".done");
        if (done.exists())
            return home;

        long started = System.currentTimeMillis();
        deleteTree(base);
        byte[] buffer = new byte[64 * 1024];
        try (ZipInputStream zip = new ZipInputStream(context.getAssets().open("python.zip"))) {
            for (ZipEntry entry; (entry = zip.getNextEntry()) != null;) {
                if (context.getFilesDir().getUsableSpace() < MIN_FREE_BYTES) {
                    deleteTree(base);
                    throw new IOException("too little free space to unpack");
                }
                File file = new File(home, entry.getName());
                if (!file.getCanonicalPath().startsWith(home.getCanonicalPath() + File.separator))
                    throw new IOException("a bad path in the archive: " + entry.getName());
                file.getParentFile().mkdirs();
                try (FileOutputStream out = new FileOutputStream(file)) {
                    for (int read; (read = zip.read(buffer)) > 0;)
                        out.write(buffer, 0, read);
                }
            }
        }
        if (!done.createNewFile())
            throw new IOException("can't mark the unpacked runtime");
        CmLog.i(TAG, "unpacked in " + (System.currentTimeMillis() - started) + " ms");
        return home;
    }

    private static void deleteTree(File file) {
        File[] children = file.listFiles();
        if (children != null) {
            for (File child : children)
                deleteTree(child);
        }
        file.delete();
    }
}
