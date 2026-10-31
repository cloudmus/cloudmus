package dev.cloudmus.mobile;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.os.Build;
import android.os.Bundle;

// What starts the player with the system: the boot broadcasts, and a new
// install. Everything it gets is logged — head units differ in what they
// send when the car starts, and the log is how to find out.
public final class BootReceiver extends BroadcastReceiver {
    private static final String TAG = "BootReceiver";

    @Override
    public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        CmLog.mark("receiver: " + action);
        CmLog.i(TAG, "received " + intent + describe(intent.getExtras()));
        boolean autoplay = CoreBridge.autoplayEnabled(context);
        CmLog.i(TAG, "autoplay is " + (autoplay ? "on" : "off"));
        if (!autoplay)
            return;
        Intent start = new Intent(context, PlaybackService.class).setAction(PlaybackService.ACTION_AUTOPLAY);
        if (Build.VERSION.SDK_INT >= 26)
            context.startForegroundService(start);
        else
            context.startService(start);
    }

    private static String describe(Bundle extras) {
        if (extras == null)
            return "";
        StringBuilder text = new StringBuilder(", extras:");
        for (String key : extras.keySet())
            text.append(' ').append(key).append('=').append(extras.get(key));
        return text.toString();
    }
}
