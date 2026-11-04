package dev.cloudmus.mobile;

import android.content.ContentProvider;
import android.content.ContentValues;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;

import java.io.File;
import java.io.FileNotFoundException;

// Playlist covers for a media browser, which is another app and can't read our
// files. Only the files of the core's cover folder, by name, and only to read;
// they're pictures from the music services, so anyone may have them.
public final class CoverProvider extends ContentProvider {
    private static final String AUTHORITY = "dev.cloudmus.mobile.covers";
    // Keep in step with Covers::CoverArtCache.
    private static final String FOLDER = "cloudmus/cover-files";

    // `file` is in the cover folder: that's where the core keeps them.
    static Uri uriFor(String file) {
        return new Uri.Builder().scheme("content").authority(AUTHORITY).appendPath(new File(file).getName()).build();
    }

    @Override
    public ParcelFileDescriptor openFile(Uri uri, String mode) throws FileNotFoundException {
        String name = uri.getLastPathSegment();
        if (!"r".equals(mode) || name == null || uri.getPathSegments().size() != 1 || name.equals("..") || name.contains("/"))
            throw new FileNotFoundException(uri.toString());
        File file = new File(new File(getContext().getCacheDir(), FOLDER), name);
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
