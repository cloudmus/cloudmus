package dev.cloudmus.mobile;

import android.content.Context;

import androidx.media3.common.MediaItem;
import androidx.media3.common.MediaMetadata;

import org.json.JSONArray;
import org.json.JSONObject;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.util.ArrayList;
import java.util.List;

// The dev tracks as Media3 items, read from the same tracks.json the core
// plays from: what the system shows (notification, the car's media widget).
final class Tracks {
    static final class Entry {
        String id;
        long durationMs;
        MediaItem item;
    }

    private Tracks() {
    }

    static List<Entry> load(Context context) {
        List<Entry> entries = new ArrayList<>();
        File dir = CoreBridge.mediaDir(context);
        try {
            JSONArray array = new JSONArray(new String(readAll(new File(dir, "tracks.json"))));
            for (int i = 0; i < array.length(); ++i) {
                JSONObject object = array.getJSONObject(i);
                Entry entry = new Entry();
                entry.id = object.getString("id");
                entry.durationMs = object.optLong("durationMs");
                MediaMetadata.Builder metadata = new MediaMetadata.Builder()
                    .setTitle(object.optString("title"))
                    .setArtist(object.optString("artist"))
                    .setAlbumTitle("Dev tracks")
                    .setIsBrowsable(false)
                    .setIsPlayable(true);
                String cover = object.optString("cover");
                if (!cover.isEmpty()) {
                    // Bytes, not a file URI: the controller in the car can't read our files.
                    metadata.setArtworkData(readAll(new File(dir, cover)), MediaMetadata.PICTURE_TYPE_FRONT_COVER);
                }
                entry.item = new MediaItem.Builder().setMediaId(entry.id).setMediaMetadata(metadata.build()).build();
                entries.add(entry);
            }
        } catch (Exception error) {
            CmLog.e("Tracks", "can't read the track list", error);
        }
        return entries;
    }

    private static byte[] readAll(File file) throws IOException {
        try (FileInputStream in = new FileInputStream(file)) {
            ByteArrayOutputStream out = new ByteArrayOutputStream();
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0)
                out.write(buffer, 0, n);
            return out.toByteArray();
        }
    }
}
