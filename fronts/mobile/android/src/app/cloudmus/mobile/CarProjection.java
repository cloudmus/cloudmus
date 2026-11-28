package app.cloudmus.mobile;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.database.Cursor;
import android.net.Uri;
import android.os.Build;
import android.os.Handler;
import android.os.Looper;

import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

// Whether the phone is showing on a car's screen through Android Auto. Android
// Auto keeps its media connection to the player after the car is gone, so that
// says nothing; what does is the provider Android Auto answers for the Car App
// Library's CarConnection, asked again when it broadcasts a change. Done by
// hand, the library's version wants LiveData and a lifecycle.
final class CarProjection {
    private static final String TAG = "CarProjection";
    private static final Uri URI = Uri.parse("content://androidx.car.app.connection");
    private static final String STATE = "CarConnectionState";
    private static final String ACTION_UPDATED = "androidx.car.app.connection.action.CAR_CONNECTION_UPDATED";
    // CarConnection.CONNECTION_TYPE_PROJECTION.
    private static final int PROJECTION = 2;

    interface Listener {
        void onProjectionChanged(boolean projecting);
    }

    private final Context context;
    private final Listener listener;
    private final Handler main = new Handler(Looper.getMainLooper());
    // The provider is in another app, which may have to start first.
    private final ExecutorService worker = Executors.newSingleThreadExecutor();
    private boolean projecting;
    private boolean started;

    private final BroadcastReceiver receiver = new BroadcastReceiver() {
        @Override
        public void onReceive(Context context, Intent intent) {
            ask();
        }
    };

    CarProjection(Context context, Listener listener) {
        this.context = context;
        this.listener = listener;
    }

    boolean projecting() {
        return projecting;
    }

    void start() {
        if (started)
            return;
        started = true;
        IntentFilter filter = new IntentFilter(ACTION_UPDATED);
        // Android Auto's broadcast comes from another app.
        if (Build.VERSION.SDK_INT >= Build.VERSION_CODES.TIRAMISU)
            context.registerReceiver(receiver, filter, Context.RECEIVER_EXPORTED);
        else
            context.registerReceiver(receiver, filter);
        ask();
    }

    void stop() {
        if (!started)
            return;
        started = false;
        context.unregisterReceiver(receiver);
        worker.shutdownNow();
    }

    private void ask() {
        worker.execute(() -> {
            boolean now = query();
            main.post(() -> {
                if (!started || now == projecting)
                    return;
                projecting = now;
                CmLog.i(TAG, now ? "on a car's screen" : "off the car's screen");
                listener.onProjectionChanged(now);
            });
        });
    }

    // No Android Auto, or one that doesn't answer: not on a car's screen.
    private boolean query() {
        try (Cursor cursor = context.getContentResolver().query(URI, new String[] { STATE }, null, null, null)) {
            if (cursor == null || !cursor.moveToFirst())
                return false;
            int column = cursor.getColumnIndex(STATE);
            return column >= 0 && cursor.getInt(column) == PROJECTION;
        } catch (RuntimeException error) {
            CmLog.w(TAG, "can't ask Android Auto: " + error);
            return false;
        }
    }
}
