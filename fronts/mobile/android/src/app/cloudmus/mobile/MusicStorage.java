package app.cloudmus.mobile;

import android.Manifest;
import android.content.ContentResolver;
import android.content.ContentUris;
import android.content.ContentValues;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.database.ContentObserver;
import android.database.Cursor;
import android.media.MediaCodec;
import android.media.MediaExtractor;
import android.media.MediaFormat;
import android.media.MediaMetadataRetriever;
import android.media.MediaMuxer;
import android.media.MediaScannerConnection;
import android.net.Uri;
import android.os.Build;
import android.os.Environment;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.os.StatFs;
import android.provider.MediaStore;
import android.provider.OpenableColumns;
import android.system.Os;
import android.util.AtomicFile;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.OutputStream;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.TimeUnit;

final class MusicStorage {
    private static final String TAG = "MusicStorage";
    private static final String SHARED = "Music/CloudMus";
    private static final long RESERVE = 1024L * 1024 * 1024;
    private static final String INDEX = "android-music.json";
    // Only the index worker reads or replaces this cache.
    private static Map<String, JSONObject> scopedMetadata = new LinkedHashMap<>();
    private static final ScheduledExecutorService worker = Executors.newSingleThreadScheduledExecutor(
        runnable -> new Thread(runnable, "music-storage"));
    private static volatile Context app;
    private static final java.util.Set<String> externalInputs = java.util.concurrent.ConcurrentHashMap.newKeySet();
    private static ContentObserver observer;
    private static String lastIndex;
    // The index is rebuilt soon after a change, then at most once per
    // INDEX_INTERVAL_MS: saving a playlist changes MediaStore every second or
    // so, and each rebuild has every source list its tracks again. Its own
    // lock: publish() holds the class's for as long as a copy takes, and the
    // observer that calls refresh() runs on the main thread.
    private static final long INDEX_INTERVAL_MS = 3000;
    private static final Object indexLock = new Object();
    private static boolean indexQueued; // guarded by indexLock
    private static long lastIndexRun; // guarded by indexLock
    // Saved tracks whose file the backend can read itself, by revision
    // (see addScopedMetadata()): asked of the file system once. Index worker only.
    private static java.util.Set<String> readableRevisions = new java.util.HashSet<>();

    private MusicStorage() { }

    static void start(Context context) {
        app = context.getApplicationContext();
        // Before the first index: what the sources asked for last time.
        wantAll = app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).getBoolean(PREF_WANT_ALL, false);
        observer = new ContentObserver(new Handler(Looper.getMainLooper())) {
            @Override public void onChange(boolean selfChange) { refresh(); }
        };
        try {
            app.getContentResolver().registerContentObserver(MediaStore.Audio.Media.EXTERNAL_CONTENT_URI, true, observer);
            if (Build.VERSION.SDK_INT >= 29)
                app.getContentResolver().registerContentObserver(audioCollection(), true, observer);
        } catch (RuntimeException error) {
            CmLog.e(TAG, "watch MediaStore changes", error);
        }
        refresh();
    }

    static void stop() {
        if (observer != null)
            app.getContentResolver().unregisterContentObserver(observer);
        observer = null;
    }

    static void refresh() {
        if (app == null)
            return;
        synchronized (indexLock) {
            // One queued run takes in every change until it starts.
            if (indexQueued)
                return;
            indexQueued = true;
            long wait = Math.max(250, lastIndexRun + INDEX_INTERVAL_MS - android.os.SystemClock.elapsedRealtime());
            worker.schedule(MusicStorage::runIndex, wait, TimeUnit.MILLISECONDS);
        }
    }

    private static void runIndex() {
        synchronized (indexLock) {
            indexQueued = false;
            lastIndexRun = android.os.SystemClock.elapsedRealtime();
        }
        writeIndex();
    }

    // A source asked for all the device's music (media/wanted), not only
    // what this app saved: from the core, docs/protocol.md §7.10. Kept
    // across launches: the source asks again only once it's up, and an
    // index written before that, without the device's music, would be all
    // it has when the app restores what played (a wave of the device's
    // music, its tracks then not found).
    private static volatile boolean wantAll;
    private static final String PREFS = "music-storage";
    private static final String PREF_WANT_ALL = "wantAll";

    static void setWantAll(boolean all) {
        if (wantAll == all)
            return;
        wantAll = all;
        if (app != null)
            app.getSharedPreferences(PREFS, Context.MODE_PRIVATE).edit().putBoolean(PREF_WANT_ALL, all).apply();
        refresh();
    }

    // Deletes MediaStore items a source handed back (docs/protocol.md §7.11),
    // from the core. What this app saved goes at once; another app's needs the
    // user's yes (MediaStore.createDeleteRequest, from the activity): those
    // come back. The index is written anew either way.
    static synchronized String[] delete(String[] uris) {
        java.util.List<String> needConsent = new java.util.ArrayList<>();
        JSONObject own = owned();
        boolean ownChanged = false;
        for (String text : uris) {
            Uri uri = Uri.parse(text);
            try {
                app.getContentResolver().delete(uri, null, null);
                ownChanged |= own.remove(text) != null;
            } catch (SecurityException error) {
                if (Build.VERSION.SDK_INT >= 30)
                    needConsent.add(text);
                else
                    CmLog.w(TAG, "delete " + text + ": " + error.getClass().getSimpleName());
            } catch (RuntimeException error) {
                CmLog.e(TAG, "delete " + text, error);
            }
        }
        if (ownChanged) {
            try {
                atomicWrite(ownedFile(), own.toString());
            } catch (IOException error) {
                CmLog.e(TAG, "forget deleted tracks", error);
            }
        }
        refresh();
        return needConsent.toArray(new String[0]);
    }

    // Wanted, and not allowed yet: the app is to ask the user.
    static boolean permissionNeeded() {
        return wantAll && app != null && !granted(app, readPermission());
    }

    static String readPermission() {
        return Build.VERSION.SDK_INT >= 33 ? Manifest.permission.READ_MEDIA_AUDIO
            : Manifest.permission.READ_EXTERNAL_STORAGE;
    }

    private static boolean granted(Context context, String permission) {
        return context.checkSelfPermission(permission) == PackageManager.PERMISSION_GRANTED;
    }

    private static File publicMusic() {
        return new File(Environment.getExternalStoragePublicDirectory(Environment.DIRECTORY_MUSIC), "CloudMus");
    }

    private static File externalMusic() {
        File dir = app.getExternalFilesDir(Environment.DIRECTORY_MUSIC);
        return dir == null ? null : new File(dir, "CloudMus");
    }

    private static File internalMusic() { return new File(app.getFilesDir(), "music"); }

    private static void addRoot(JSONArray roots, String id, File path) throws Exception {
        if (path != null && path.isDirectory())
            roots.put(new JSONObject().put("id", id).put("path", path.getAbsolutePath()));
    }

    private static Uri audioCollection() {
        return Build.VERSION.SDK_INT >= 29 ? MediaStore.Audio.Media.getContentUri(MediaStore.VOLUME_EXTERNAL_PRIMARY)
            : MediaStore.Audio.Media.EXTERNAL_CONTENT_URI;
    }

    private static AtomicFile ownedFile() { return new AtomicFile(new File(app.getFilesDir(), "owned-music.json")); }

    private static void atomicWrite(AtomicFile file, String text) throws IOException {
        FileOutputStream out = file.startWrite();
        try {
            out.write(text.getBytes(StandardCharsets.UTF_8));
            file.finishWrite(out);
        } catch (IOException error) {
            file.failWrite(out);
            throw error;
        }
    }

    private static JSONObject owned() {
        try {
            return new JSONObject(new String(ownedFile().readFully(), StandardCharsets.UTF_8));
        } catch (java.io.FileNotFoundException ignored) {
            return new JSONObject();
        } catch (Exception error) {
            CmLog.e(TAG, "read published tracks", error);
            return new JSONObject();
        }
    }

    private static JSONObject record(Uri uri, String title, String mime, boolean own) throws Exception {
        return new JSONObject().put("id", "@android/media/" + uri.getEncodedPath()).put("uri", uri.toString())
            .put("title", title).put("mimeType", mime).put("own", own);
    }

    private static String cell(Cursor rows, String key) {
        int column = rows.getColumnIndex(key);
        return column < 0 || rows.isNull(column) ? "" : rows.getString(column);
    }

    private static void query(Map<String, JSONObject> result, String volume, boolean all) throws Exception {
        Uri collection = Build.VERSION.SDK_INT >= 29 ? MediaStore.Audio.Media.getContentUri(volume) : audioCollection();
        String[] columns = { "_id", "_display_name", "_data", "title", "artist", "album", "duration", "mime_type",
            "_size", "date_modified" };
        if (Build.VERSION.SDK_INT >= 29) {
            columns = java.util.Arrays.copyOf(columns, columns.length + 1);
            columns[columns.length - 1] = "relative_path";
        }
        String selection = !all && Build.VERSION.SDK_INT >= 29
            ? "owner_package_name = ? AND relative_path LIKE ? AND is_pending = 0" : null;
        String[] args = selection == null ? null : new String[] {app.getPackageName(), SHARED + "/%"};
        try (Cursor rows = app.getContentResolver().query(collection, columns, selection, args, "title COLLATE NOCASE")) {
            if (rows == null)
                throw new IOException("MediaStore returned no cursor");
            while (rows.moveToNext()) {
                Uri uri = ContentUris.withAppendedId(collection, rows.getLong(rows.getColumnIndexOrThrow("_id")));
                JSONObject saved = result.get(uri.toString());
                String title = cell(rows, "title");
                JSONObject item = record(uri, title.isEmpty() ? cell(rows, "_display_name") : title,
                    cell(rows, "mime_type"), !all || (saved != null && saved.optBoolean("own")));
                fillMediaRow(item, rows);
                result.put(uri.toString(), item);
            }
        }
    }

    private static void fillMediaRow(JSONObject item, Cursor rows) throws Exception {
        item.put("artist", cell(rows, "artist")).put("album", cell(rows, "album"))
            .put("durationMs", rows.getLong(rows.getColumnIndexOrThrow("duration")))
            .put("size", rows.getLong(rows.getColumnIndexOrThrow("_size")))
            .put("modified", rows.getLong(rows.getColumnIndexOrThrow("date_modified")));
        String path = cell(rows, "_data");
        if (!path.isEmpty())
            item.put("path", path);
        String folder = cell(rows, "relative_path");
        if (!folder.isEmpty())
            item.put("relativePath", folder);
    }

    private static JSONObject readScopedMetadata(Uri uri, JSONObject item, String revision) throws Exception {
        JSONObject info = new JSONObject();
        long duration = item.optLong("durationMs");
        long bitrate = 0;
        MediaExtractor extractor = new MediaExtractor();
        try {
            extractor.setDataSource(app, uri, null);
            for (int i = 0; i < extractor.getTrackCount(); i++) {
                MediaFormat format = extractor.getTrackFormat(i);
                String mime = format.getString(MediaFormat.KEY_MIME);
                if (mime == null || !mime.startsWith("audio/"))
                    continue;
                String codec;
                switch (mime) {
                    case "audio/mpeg": codec = "mp3"; break;
                    case "audio/mp4a-latm": codec = "aac"; break;
                    case "audio/raw": codec = "pcm"; break;
                    case "audio/3gpp": codec = "amr-nb"; break;
                    default: codec = mime.substring("audio/".length()); break;
                }
                info.put("codec", codec);
                if (format.containsKey(MediaFormat.KEY_BIT_RATE))
                    bitrate = format.getInteger(MediaFormat.KEY_BIT_RATE);
                if (duration <= 0 && format.containsKey(MediaFormat.KEY_DURATION))
                    duration = format.getLong(MediaFormat.KEY_DURATION) / 1000;
                break;
            }
        } catch (IOException | RuntimeException error) {
            CmLog.w(TAG, "read saved audio format: " + error.getClass().getSimpleName());
        } finally {
            extractor.release();
        }
        MediaMetadataRetriever retriever = new MediaMetadataRetriever();
        try {
            retriever.setDataSource(app, uri);
            if (bitrate <= 0) {
                String value = retriever.extractMetadata(MediaMetadataRetriever.METADATA_KEY_BITRATE);
                if (value != null)
                    bitrate = Long.parseLong(value);
            }
            byte[] cover = retriever.getEmbeddedPicture();
            if (cover != null && cover.length > 0 && cover.length <= 32 * 1024 * 1024) {
                File dir = new File(app.getCacheDir(), "android-media-covers");
                room(dir, cover.length);
                String name = UUID.nameUUIDFromBytes(revision.getBytes(StandardCharsets.UTF_8)).toString();
                AtomicFile file = new AtomicFile(new File(dir, name + ".img"));
                FileOutputStream out = file.startWrite();
                try {
                    out.write(cover);
                    file.finishWrite(out);
                } catch (IOException error) {
                    file.failWrite(out);
                    throw error;
                }
                info.put("coverUrl", Uri.fromFile(file.getBaseFile()).toString());
            }
        } catch (IOException | RuntimeException error) {
            CmLog.w(TAG, "read saved audio artwork or bitrate: " + error.getClass().getSimpleName());
        } finally {
            retriever.release();
        }
        if (bitrate <= 0 && duration > 0)
            bitrate = Math.round(item.optLong("size") * 8000.0 / duration);
        if (bitrate >= 500)
            info.put("bitrateKbps", Math.round(bitrate / 1000.0));
        return info;
    }

    private static void addScopedMetadata(Map<String, JSONObject> records) throws Exception {
        Map<String, JSONObject> current = new LinkedHashMap<>();
        java.util.Set<String> readable = new java.util.HashSet<>();
        for (JSONObject item : records.values()) {
            if (!item.optBoolean("own"))
                continue;
            String uri = item.getString("uri");
            String revision = uri + ":" + item.optLong("modified") + ":" + item.optLong("size");
            // The Rust backend can read this file directly. Each check goes
            // through the storage daemon: once per revision of a file.
            String path = item.optString("path");
            if (!path.isEmpty() && (readableRevisions.contains(revision) || new File(path).canRead())) {
                readable.add(revision);
                continue;
            }
            JSONObject info = scopedMetadata.get(revision);
            if (info == null || (info.has("coverUrl")
                    && !new File(Uri.parse(info.getString("coverUrl")).getPath()).isFile()))
                info = readScopedMetadata(Uri.parse(uri), item, revision);
            if (info.length() > 0)
                current.put(revision, info);
            for (java.util.Iterator<String> keys = info.keys(); keys.hasNext();) {
                String key = keys.next();
                item.put(key, info.get(key));
            }
        }
        scopedMetadata = current;
        readableRevisions = readable;
    }

    private static void writeIndex() {
        try {
            JSONArray roots = new JSONArray();
            if (Build.VERSION.SDK_INT <= 28)
                addRoot(roots, "shared", publicMusic());
            addRoot(roots, "external", externalMusic());
            addRoot(roots, "internal", internalMusic());
            Map<String, JSONObject> records = new LinkedHashMap<>();
            JSONObject own = owned();
            boolean all = wantAll && granted(app, readPermission());
            // The volumes in one query each: they list the app's saved tracks
            // (and with `all` everyone's), so most of the registry is found
            // there without asking MediaStore of each track.
            if (Build.VERSION.SDK_INT >= 29) {
                for (String volume : MediaStore.getExternalVolumeNames(app)) {
                    try {
                        query(records, volume, all);
                    } catch (Exception error) {
                        CmLog.e(TAG, "query audio volume " + volume + ", deviceMusic=" + all, error);
                    }
                }
            } else if (all) {
                try {
                    query(records, "external", true);
                } catch (Exception error) {
                    CmLog.e(TAG, "query audio volume external", error);
                }
            }
            for (java.util.Iterator<String> it = own.keys(); it.hasNext();) {
                JSONObject item = own.getJSONObject(it.next());
                String key = item.getString("uri");
                JSONObject found = records.get(key);
                if (found != null) {
                    found.put("own", true);
                    continue;
                }
                // Not in a volume's list (Android 9 and older without `all`, or
                // somewhere the query doesn't look): asked of on its own.
                // Saved by an older version as "saved".
                item.remove("saved");
                item.put("own", true);
                Uri uri = Uri.parse(key);
                String[] columns = { "title", "artist", "album", "duration", "_data", "_size", "date_modified" };
                try (Cursor cursor = app.getContentResolver().query(uri, columns, null, null, null)) {
                    if (cursor != null && cursor.moveToFirst()) {
                        String title = cell(cursor, "title");
                        if (!title.isEmpty())
                            item.put("title", title);
                        fillMediaRow(item, cursor);
                        records.put(key, item);
                    }
                } catch (RuntimeException error) {
                    CmLog.w(TAG, "check published media: " + error.getClass().getSimpleName());
                }
            }
            addScopedMetadata(records);
            // schema/media_library.yaml, for media.setLibrary.
            String index = new JSONObject().put("scope", all ? "all" : "own").put("folders", roots)
                .put("items", new JSONArray(records.values())).toString();
            if (!index.equals(lastIndex)) {
                atomicWrite(new AtomicFile(new File(app.getFilesDir(), INDEX)), index);
                lastIndex = index;
                CoreBridge.nativeMusicChanged();
            }
        } catch (Exception error) {
            CmLog.e(TAG, "update local music index", error);
        }
    }

    private static File existingAncestor(File dir) throws IOException {
        File current = dir;
        while (current != null && !current.exists())
            current = current.getParentFile();
        if (current == null)
            throw new IOException("no mounted ancestor for " + dir);
        return current;
    }

    private static long free(File dir) throws IOException {
        return new StatFs(existingAncestor(dir).getAbsolutePath()).getAvailableBytes();
    }

    private static void room(File dir, long bytes) throws IOException {
        long available = free(dir);
        if (available - bytes < RESERVE)
            throw new IOException("Not enough free space: " + available + " bytes available, " + bytes + " requested");
    }

    private static void copy(File source, OutputStream out) throws IOException {
        if (out == null)
            throw new IOException("provider did not open an output stream");
        byte[] buffer = new byte[128 * 1024];
        try (FileInputStream in = new FileInputStream(source)) {
            for (int count; (count = in.read(buffer)) != -1;)
                out.write(buffer, 0, count);
        }
        out.flush();
    }

    private static File saveFile(File source, File dir) throws Exception {
        room(dir, source.length());
        if (!dir.isDirectory() && !dir.mkdirs() && !dir.isDirectory())
            throw new IOException("mkdir failed for " + dir);
        File part = File.createTempFile(".cloudmus-", ".part", dir);
        File target = new File(dir, source.getName());
        try {
            try (FileOutputStream out = new FileOutputStream(part)) {
                copy(source, out);
                out.getFD().sync();
            }
            Os.rename(part.getAbsolutePath(), target.getAbsolutePath());
            return target;
        } finally {
            Files.deleteIfExists(part.toPath());
        }
    }

    private static String mime(String name) {
        String lower = name.toLowerCase(java.util.Locale.ROOT);
        if (lower.endsWith(".webm") || lower.endsWith(".weba")) return "audio/webm";
        if (lower.endsWith(".opus") || lower.endsWith(".ogg")) return "audio/ogg";
        if (lower.endsWith(".m4a")) return "audio/mp4";
        if (lower.endsWith(".flac")) return "audio/flac";
        if (lower.endsWith(".wav")) return "audio/wav";
        return "audio/mpeg";
    }

    private static Uri publishMedia(File source, String relative, boolean probe) throws Exception {
        ContentResolver resolver = app.getContentResolver();
        room(publicMusic(), source.length());
        String mime = mime(source.getName());
        ContentValues values = new ContentValues();
        values.put(MediaStore.MediaColumns.DISPLAY_NAME, source.getName());
        values.put(MediaStore.MediaColumns.MIME_TYPE, mime);
        values.put(MediaStore.MediaColumns.RELATIVE_PATH, relative);
        values.put(MediaStore.MediaColumns.IS_PENDING, 1);
        Uri uri = resolver.insert(audioCollection(), values);
        if (uri == null)
            throw new IOException("MediaStore insert returned no URI");
        boolean keep = false;
        try {
            try (OutputStream out = resolver.openOutputStream(uri, "w")) {
                copy(source, out);
            }
            if (!probe) {
                values.clear();
                values.put(MediaStore.MediaColumns.IS_PENDING, 0);
                if (resolver.update(uri, values, null, null) != 1)
                    throw new IOException("MediaStore could not publish the track");
                keep = true;
                try {
                    JSONObject entries = owned();
                    JSONObject item = record(uri, source.getName(), mime, true).put("relativePath", relative);
                    try (Cursor cursor = resolver.query(uri, new String[] {"title", "artist", "album", "duration"}, null, null, null)) {
                        if (cursor != null && cursor.moveToFirst()) {
                            String title = cell(cursor, "title");
                            if (!title.isEmpty())
                                item.put("title", title);
                            item.put("artist", cell(cursor, "artist")).put("album", cell(cursor, "album"))
                                .put("durationMs", cursor.getLong(cursor.getColumnIndexOrThrow("duration")));
                        }
                    } catch (RuntimeException error) {
                        CmLog.e(TAG, "read published track metadata", error);
                    }
                    entries.put(uri.toString(), item);
                    atomicWrite(ownedFile(), entries.toString());
                } catch (Exception error) {
                    CmLog.e(TAG, "write published tracks registry", error);
                }
            }
            return uri;
        } finally {
            if (!keep) {
                try {
                    if (resolver.delete(uri, null, null) != 1)
                        CmLog.w(TAG, "MediaStore could not remove pending probe or failed track");
                } catch (RuntimeException error) {
                    CmLog.e(TAG, "MediaStore delete pending row failed", error);
                }
            }
        }
    }

    private static String relative(String subfolder) {
        return subfolder.isEmpty() ? SHARED : SHARED + "/" + subfolder;
    }

    private static File subdirectory(File dir, String subfolder) throws IOException {
        File target = new File(dir, subfolder);
        File canonical = target.getCanonicalFile();
        if (!canonical.toPath().startsWith(dir.getCanonicalFile().toPath()))
            throw new IOException("destination escaped " + dir);
        return canonical;
    }

    // MediaStore rejects audio/webm on some builds. Copy Opus packets into
    // Ogg before the front writes tags and a cover to the saved file.
    static boolean remuxOpus(String input, String output) {
        if (Build.VERSION.SDK_INT < 29)
            return false;
        MediaExtractor extractor = new MediaExtractor();
        MediaMuxer muxer = null;
        File destination = new File(output);
        boolean completed = false;
        try {
            room(destination.getParentFile(), new File(input).length());
            extractor.setDataSource(input);
            for (int i = 0; i < extractor.getTrackCount(); ++i) {
                MediaFormat format = extractor.getTrackFormat(i);
                if (!MediaFormat.MIMETYPE_AUDIO_OPUS.equals(format.getString(MediaFormat.KEY_MIME)))
                    continue;
                extractor.selectTrack(i);
                muxer = new MediaMuxer(output, MediaMuxer.OutputFormat.MUXER_OUTPUT_OGG);
                int track = muxer.addTrack(format);
                muxer.start();
                ByteBuffer sample = ByteBuffer.allocateDirect(64 * 1024);
                MediaCodec.BufferInfo info = new MediaCodec.BufferInfo();
                while (extractor.getSampleTime() >= 0) {
                    if (extractor.getSampleSize() > sample.capacity())
                        throw new IOException("Opus packet exceeds buffer size");
                    sample.clear();
                    int size = extractor.readSampleData(sample, 0);
                    if (size <= 0)
                        break;
                    int flags = (extractor.getSampleFlags() & MediaExtractor.SAMPLE_FLAG_SYNC) != 0
                        ? MediaCodec.BUFFER_FLAG_KEY_FRAME : 0;
                    info.set(0, size, extractor.getSampleTime(), flags);
                    muxer.writeSampleData(track, sample, info);
                    extractor.advance();
                }
                muxer.stop();
                completed = destination.length() > 0;
                if (completed)
                    CmLog.i(TAG, "prepared WebM/Opus as Ogg for Music");
                return completed;
            }
        } catch (IOException | RuntimeException error) {
            CmLog.e(TAG, "WebM/Opus remux failed", error);
        } finally {
            extractor.release();
            if (muxer != null) {
                try {
                    muxer.release();
                } catch (RuntimeException error) {
                    CmLog.e(TAG, "release Opus muxer", error);
                }
            }
            if (!completed)
                destination.delete();
        }
        return false;
    }

    // Whether a track is saved already where publish() would put it now:
    // `<subfolder>/<baseName><one of extensions>` in shared Music/CloudMus (as
    // MediaStore lists it, or as a file), or in a fallback folder. Saving it
    // again would only add a copy beside it, which MediaStore names "... (1)".
    static synchronized boolean isSaved(String subfolder, String baseName, String[] extensions) {
        for (File base : new File[] {publicMusic(), externalMusic(), internalMusic()}) {
            if (base == null)
                continue;
            for (String extension : extensions) {
                try {
                    if (new File(subdirectory(base, subfolder), baseName + extension).isFile())
                        return true;
                } catch (IOException | RuntimeException error) {
                    CmLog.w(TAG, "look for a saved track in " + base + ": " + error.getMessage());
                }
            }
        }
        if (Build.VERSION.SDK_INT < 29)
            return false;
        // Scoped storage may not show the file itself; MediaStore lists the
        // app's own tracks, and the others with the permission to read music.
        StringBuilder names = new StringBuilder();
        String[] arguments = new String[extensions.length + 1];
        arguments[0] = relative(subfolder) + "/";
        for (int i = 0; i < extensions.length; ++i) {
            names.append(i == 0 ? "?" : ",?");
            arguments[i + 1] = baseName + extensions[i];
        }
        String selection = MediaStore.MediaColumns.RELATIVE_PATH + "=? AND " + MediaStore.MediaColumns.DISPLAY_NAME
            + " IN (" + names + ")";
        try (Cursor rows = app.getContentResolver().query(
                 audioCollection(), new String[] {MediaStore.MediaColumns._ID}, selection, arguments, null)) {
            return rows != null && rows.moveToFirst();
        } catch (RuntimeException error) {
            CmLog.e(TAG, "look for a saved track in MediaStore", error);
            return false;
        }
    }

    // Called on the native download worker after a complete file has been tagged.
    static synchronized String publish(String path, String subfolder) {
        File source = new File(path);
        String firstFailure;
        try {
            if (Build.VERSION.SDK_INT >= 29) {
                publishMedia(source, relative(subfolder), false);
                CmLog.i(TAG, "save stage=MediaStore location=" + relative(subfolder) + " bytes=" + source.length());
                refresh();
                return result(relative(subfolder), "", "");
            }
            if (!granted(app, Manifest.permission.WRITE_EXTERNAL_STORAGE))
                throw new SecurityException("WRITE_EXTERNAL_STORAGE denied");
            File target = saveFile(source, subdirectory(publicMusic(), subfolder));
            MediaScannerConnection.scanFile(app, new String[] {target.getPath()}, new String[] {mime(target.getName())}, null);
            CmLog.i(TAG, "save stage=shared-file location=" + target.getParent() + " bytes=" + source.length());
            refresh();
            return result(target.getParent(), "", "");
        } catch (Exception error) {
            CmLog.e(TAG, "save stage=shared location=" + relative(subfolder)
                + " state=" + Environment.getExternalStorageState()
                + " legacyWrite=" + granted(app, Manifest.permission.WRITE_EXTERNAL_STORAGE), error);
            firstFailure = error.toString();
        }
        String lastFailure = firstFailure;
        for (File base : new File[] {externalMusic(), internalMusic()}) {
            if (base == null)
                continue;
            try {
                File target = saveFile(source, subdirectory(base, subfolder));
                CmLog.i(TAG, "save stage=fallback location=" + target.getParent() + " bytes=" + source.length());
                refresh();
                return result(target.getParent(), firstFailure, "");
            } catch (Exception error) {
                lastFailure = error.toString();
                CmLog.e(TAG, "save stage=fallback location=" + base, error);
            }
        }
        return result("", firstFailure, lastFailure);
    }

    private static String result(String directory, String fallback, String error) {
        try {
            return new JSONObject().put("directory", directory).put("fallback", fallback).put("error", error).toString();
        } catch (Exception bad) {
            return "{}";
        }
    }

    static void probe() {
        worker.execute(() -> {
            Context context = app;
            Intent choose = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            probeMusicFolder(context);
            CmLog.i(TAG, "probe sdk=" + Build.VERSION.SDK_INT + " target=" + context.getApplicationInfo().targetSdkVersion
                + " model=" + Build.MANUFACTURER + "/" + Build.MODEL
                + " picker=" + (choose.resolveActivity(context.getPackageManager()) != null)
                + " read=" + granted(context, readPermission())
                + " write=" + granted(context, Manifest.permission.WRITE_EXTERNAL_STORAGE)
                + " mediaProvider=" + (context.getPackageManager().resolveContentProvider("media", 0) != null));
            File sample = new File(context.getCacheDir(), "cloudmus-probe-" + UUID.randomUUID() + ".mp3");
            try {
                room(context.getCacheDir(), 1);
                try (FileOutputStream out = new FileOutputStream(sample)) {
                    out.write(0);
                }
                for (File dir : new File[] {publicMusic(), externalMusic(), internalMusic()}) {
                    if (dir != null)
                        probeDestination(dir, sample);
                }
                for (File mount : context.getExternalFilesDirs(Environment.DIRECTORY_MUSIC)) {
                    if (mount != null)
                        CmLog.i(TAG, "volume appPath=" + mount + " state=" + Environment.getExternalStorageState(mount));
                }
                for (android.os.storage.StorageVolume volume : context.getSystemService(android.os.storage.StorageManager.class).getStorageVolumes()) {
                    CmLog.i(TAG, "volume primary=" + volume.isPrimary() + " removable=" + volume.isRemovable()
                        + " uuid=" + volume.getUuid() + " state=" + volume.getState());
                }
            } catch (Exception error) {
                CmLog.e(TAG, "probe could not prepare sample", error);
            } finally {
                sample.delete();
            }
        });
    }

    private static void probeMusicFolder(Context context) {
        File config = new File(context.getFilesDir(), "config/cloudmus/backends/local-folder/config.json");
        if (!config.isFile())
            return;
        try {
            JSONObject data = new JSONObject(new String(Files.readAllBytes(config.toPath()), StandardCharsets.UTF_8));
            JSONObject settings = data.optJSONObject("settings");
            String path = settings == null ? data.optString("musicDir") : settings.optString("musicDir");
            if (!path.isEmpty()) {
                File dir = new File(path);
                try (java.nio.file.DirectoryStream<java.nio.file.Path> entries = Files.newDirectoryStream(dir.toPath())) {
                    CmLog.i(TAG, "probe additional read ok path=" + dir + " empty=" + !entries.iterator().hasNext());
                }
            }
        } catch (Exception error) {
            CmLog.e(TAG, "probe additional read failed", error);
        }
    }

    private static void probeDestination(File dir, File sample) {
        try {
            String state = dir.equals(internalMusic()) ? "internal" : Environment.getExternalStorageState(dir);
            CmLog.i(TAG, "probe path=" + dir + " state=" + state + " freeBytes=" + free(dir));
            if (dir.equals(publicMusic()) && Build.VERSION.SDK_INT >= 29) {
                publishMedia(sample, SHARED, true);
            } else {
                File test = saveFile(sample, dir);
                Files.delete(test.toPath());
            }
            CmLog.i(TAG, "probe create/write/delete ok path=" + dir);
        } catch (Exception error) {
            CmLog.e(TAG, "probe create/write/delete failed path=" + dir, error);
        }
    }

    static int openContent(String input) {
        Uri uri = Uri.parse(input);
        if (!ContentResolver.SCHEME_CONTENT.equals(uri.getScheme()))
            return -1;
        ContentResolver resolver = app.getContentResolver();
        if (!"media".equals(uri.getAuthority()) && !externalInputs.contains(input)
            && resolver.getPersistedUriPermissions().stream().noneMatch(grant -> grant.isReadPermission() && grant.getUri().equals(uri)))
            return -1;
        try (ParcelFileDescriptor file = resolver.openFileDescriptor(uri, "r")) {
            return file == null ? -1 : file.detachFd();
        } catch (Exception error) {
            CmLog.w(TAG, "open content authority=" + uri.getAuthority() + " error=" + error.getClass().getSimpleName());
            return -1;
        }
    }

    static void openExternal(Uri incoming, int flags, String mime) {
        worker.execute(() -> {
            Uri uri = incoming;
            try {
                ContentResolver resolver = app.getContentResolver();
                boolean content = ContentResolver.SCHEME_CONTENT.equals(uri.getScheme());
                if (!content && !ContentResolver.SCHEME_FILE.equals(uri.getScheme()))
                    throw new IOException("unsupported URI scheme");
                String name = uri.getLastPathSegment();
                if (content) {
                    try (ParcelFileDescriptor file = resolver.openFileDescriptor(uri, "r")) {
                        if (file == null)
                            throw new IOException("provider returned no read descriptor");
                    }
                    try (Cursor row = resolver.query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                        if (row != null && row.moveToFirst())
                            name = cell(row, OpenableColumns.DISPLAY_NAME);
                    } catch (RuntimeException ignored) { }
                } else {
                    File file = new File(uri.getPath()).getCanonicalFile();
                    String path = file.getPath();
                    if (!(path.startsWith("/storage/") || path.startsWith("/mnt/media_rw/"))
                        || !file.isFile() || !file.canRead())
                        throw new IOException("file is unreadable");
                    name = file.getName();
                    uri = Uri.fromFile(file);
                }
                if ((mime == null || !mime.startsWith("audio/")) && !looksLikeAudio(name))
                    throw new IOException("file is not recognized as audio");
                boolean persistent = !content;
                if (content && (flags & Intent.FLAG_GRANT_PERSISTABLE_URI_PERMISSION) != 0) {
                    try {
                        resolver.takePersistableUriPermission(uri, Intent.FLAG_GRANT_READ_URI_PERMISSION);
                        persistent = true;
                    } catch (SecurityException ignored) { }
                }
                String title = name == null || name.isEmpty() ? "Audio" : name;
                String artist = "";
                String album = "";
                String cover = "";
                long duration = 0;
                try {
                    MediaMetadataRetriever metadata = new MediaMetadataRetriever();
                    try {
                        metadata.setDataSource(app, uri);
                        String tag = metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_TITLE);
                        if (tag != null && !tag.isEmpty())
                            title = tag;
                        tag = metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ARTIST);
                        if (tag != null)
                            artist = tag;
                        tag = metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_ALBUM);
                        if (tag != null)
                            album = tag;
                        tag = metadata.extractMetadata(MediaMetadataRetriever.METADATA_KEY_DURATION);
                        if (tag != null)
                            duration = Long.parseLong(tag);
                        cover = saveExternalCover(uri, metadata.getEmbeddedPicture());
                    } finally {
                        metadata.release();
                    }
                } catch (Exception ignored) { }
                if (content)
                    externalInputs.add(uri.toString());
                CoreBridge.nativeOpenAudio(uri.toString(), title, artist, album, cover, duration, persistent);
            } catch (Exception error) {
                CmLog.w(TAG, "open external audio scheme=" + uri.getScheme() + " authority=" + uri.getAuthority()
                    + " error=" + error.getClass().getSimpleName());
                CoreBridge.nativeOpenAudio("", "", "", "", "", 0, false);
            }
        });
    }

    // The picture in an opened file, kept where the player and the system's
    // media controls can show it; "" with none. One file per input, written
    // anew on each open, as the file may have changed.
    private static String saveExternalCover(Uri uri, byte[] cover) {
        if (cover == null || cover.length == 0 || cover.length > 32 * 1024 * 1024)
            return "";
        try {
            File dir = new File(app.getCacheDir(), "external-covers");
            room(dir, cover.length);
            String name = UUID.nameUUIDFromBytes(uri.toString().getBytes(StandardCharsets.UTF_8)).toString();
            AtomicFile file = new AtomicFile(new File(dir, name + ".img"));
            FileOutputStream out = file.startWrite();
            try {
                out.write(cover);
                file.finishWrite(out);
            } catch (IOException error) {
                file.failWrite(out);
                throw error;
            }
            return Uri.fromFile(file.getBaseFile()).toString();
        } catch (IOException | RuntimeException error) {
            CmLog.w(TAG, "save opened audio artwork: " + error.getClass().getSimpleName());
            return "";
        }
    }

    private static boolean looksLikeAudio(String name) {
        return name != null && name.toLowerCase(java.util.Locale.ROOT)
            .matches(".*\\.(mp3|mp2|flac|ogg|oga|opus|spx|m4a|m4b|wav|wma|aac|webm|weba|mka|aif|aiff|aifc|ape|wv|mpc|tta|dsf|dff|ac3|eac3)$");
    }
}
