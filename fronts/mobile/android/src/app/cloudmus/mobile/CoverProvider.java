package app.cloudmus.mobile;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.File;
import java.io.FileNotFoundException;

// Covers for the system's player and a media browser (the playing track, the
// queue, the playlists), which are other apps and can't read our files. Only
// the files of the cover folders below, by name, and only to read: the
// core's copies of the music services' pictures, and the local covers it
// uses where they are — the ones Local Folder takes out of the files, and
// MusicStorage's of the media library and of audio opened from other apps.
public final class CoverProvider extends ContentProvider {
    private static final String AUTHORITY = "app.cloudmus.mobile.covers";
    // Keep in step with Covers::CoverArtCache.
    private static final String FOLDER = "cloudmus/cover-files";
    // A local cover's folder in the cache, by the uri's first segment. Keep in
    // step with Local Folder's config::cover_dir() and MusicStorage.
    private static final String[][] LOCAL = {
        {"local", "cloudmus/backends/local-folder/covers"},
        {"media", "android-media-covers"},
        {"opened", "external-covers"},
    };

    // `file` is in one of the cover folders: that's where the core has them.
    static Uri uriFor(String file) {
        File picture = new File(file);
        Uri.Builder uri = new Uri.Builder().scheme("content").authority(AUTHORITY);
        String parent = picture.getParent() == null ? "" : picture.getParent();
        for (String[] folder : LOCAL) {
            if (parent.endsWith("/" + folder[1]))
                uri.appendPath(folder[0]);
        }
        return uri.appendPath(picture.getName()).build();
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        java.util.List<String> segments = uri.getPathSegments();
        String name = uri.getLastPathSegment();
        if (!"r".equals(mode) || name == null || segments.isEmpty() || segments.size() > 2 || name.equals("..")
            || name.equals(".") || name.contains("/"))
            throw new FileNotFoundException(uri.toString());
        String folder = segments.size() == 1 ? FOLDER : null;
        for (String[] local : LOCAL) {
            if (segments.size() == 2 && local[0].equals(segments.get(0)))
                folder = local[1];
        }
        if (folder == null)
            throw new FileNotFoundException(uri.toString());
        File file = new File(new File(getContext().getCacheDir(), folder), name);
        return ParcelFileDescriptor.open(file, ParcelFileDescriptor.MODE_READ_ONLY);
    }

    @Override
    public String getType(Uri uri) {
        return "image/*";
    }

    @Override
    public boolean onCreate() {
        return true;
    }

    @Override
    public Cursor query(Uri uri, String[] projection, String selection, String[] selectionArgs, String sortOrder) {
        return null;
    }

    @Override
    public Uri insert(Uri uri, ContentValues values) {
        return null;
    }

    @Override
    public int delete(Uri uri, String selection, String[] selectionArgs) {
        return 0;
    }

    @Override
    public int update(Uri uri, ContentValues values, String selection, String[] selectionArgs) {
        return 0;
    }
}
