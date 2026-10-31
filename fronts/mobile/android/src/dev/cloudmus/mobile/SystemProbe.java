package dev.cloudmus.mobile;

import android.content.Context;
import android.content.Intent;
import android.content.pm.ApplicationInfo;
import android.content.pm.ComponentInfo;
import android.content.pm.PackageInfo;
import android.content.pm.PackageManager;
import android.content.pm.ResolveInfo;
import android.content.pm.ServiceInfo;
import android.os.Bundle;

import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Pattern;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

// What the car's media system is made of, for the log: a head unit's media
// widget and steering-wheel buttons are the vendor's code, and there's no adb
// to look at it. The widget lists Yandex Music and VK Video but not us,
// without ever binding to our service — the diff between their manifests and
// ours, and the vendor's own APKs (exportApks()), show what it goes by.
final class SystemProbe {
    private static final String TAG = "SystemProbe";
    // The apps the head unit's widget does list; ours is compared to them.
    private static final String[] LISTED_APPS = {"ru.yandex.music", "com.vk.vkvideo"};
    // Vendor packages: the launcher with the widget, the "media source" service.
    private static final Pattern VENDOR = Pattern.compile("gwm|haval|nobo|launcher|media|source|music|radio",
        Pattern.CASE_INSENSITIVE);
    private static final String[] MEDIA_ACTIONS = {
        "android.media.browse.MediaBrowserService",
        "androidx.media3.session.MediaLibraryService",
        "androidx.media3.session.MediaSessionService",
    };

    private SystemProbe() {
    }

    static void logAll(Context context) {
        try {
            PackageManager pm = context.getPackageManager();
            logHome(pm);
            logMediaServices(pm);
            logMediaButtonReceivers(pm);
            for (String name : LISTED_APPS)
                logPackage(pm, name, "listed in the car's widget");
            logPackage(pm, context.getPackageName(), "ours");
            for (String name : vendorPackages(pm))
                logPackage(pm, name, "vendor/media");
            logHolders(pm, "android.permission.MEDIA_CONTENT_CONTROL");
        } catch (RuntimeException error) {
            CmLog.e(TAG, "probe failed", error);
        }
    }

    private static void logHome(PackageManager pm) {
        Intent home = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME);
        ResolveInfo current = pm.resolveActivity(home, PackageManager.MATCH_DEFAULT_ONLY);
        StringBuilder text = new StringBuilder("home: current ");
        text.append(current == null || current.activityInfo == null ? "none" : current.activityInfo.packageName + "/"
                + current.activityInfo.name);
        for (ResolveInfo info : pm.queryIntentActivities(home, 0))
            text.append("\n  candidate ").append(info.activityInfo.packageName).append('/').append(info.activityInfo.name);
        CmLog.i(TAG, text.toString());
    }

    private static void logMediaServices(PackageManager pm) {
        for (String action : MEDIA_ACTIONS) {
            List<ResolveInfo> found = pm.queryIntentServices(new Intent(action), PackageManager.GET_META_DATA);
            StringBuilder text = new StringBuilder("services for " + action + ": " + found.size());
            for (ResolveInfo info : found)
                text.append("\n  ").append(describe(info.serviceInfo, info.serviceInfo.permission));
            CmLog.i(TAG, text.toString());
        }
    }

    private static void logMediaButtonReceivers(PackageManager pm) {
        List<ResolveInfo> found = pm.queryBroadcastReceivers(new Intent(Intent.ACTION_MEDIA_BUTTON),
            PackageManager.GET_META_DATA);
        StringBuilder text = new StringBuilder("MEDIA_BUTTON receivers: " + found.size());
        for (ResolveInfo info : found)
            text.append("\n  ").append(describe(info.activityInfo, info.activityInfo.permission))
                .append(", priority ").append(info.priority);
        CmLog.i(TAG, text.toString());
    }

    private static void logPackage(PackageManager pm, String name, String why) {
        PackageInfo info;
        try {
            info = pm.getPackageInfo(name, PackageManager.GET_META_DATA | PackageManager.GET_SERVICES
                    | PackageManager.GET_RECEIVERS | PackageManager.GET_PERMISSIONS);
        } catch (PackageManager.NameNotFoundException error) {
            CmLog.i(TAG, "package " + name + " (" + why + "): not installed");
            return;
        }
        ApplicationInfo app = info.applicationInfo;
        StringBuilder text = new StringBuilder();
        text.append("package ").append(name).append(" (").append(why).append("): version ").append(info.versionName)
            .append(", ").append((app.flags & ApplicationInfo.FLAG_SYSTEM) != 0 ? "system" : "user")
            .append(", uid ").append(app.uid).append(", apk ").append(app.sourceDir);
        appendMetaData(text, "  app meta-data ", app.metaData);
        if (info.services != null) {
            for (ServiceInfo service : info.services)
                text.append("\n  service ").append(describe(service, service.permission));
        }
        if (info.receivers != null) {
            for (android.content.pm.ActivityInfo receiver : info.receivers)
                text.append("\n  receiver ").append(describe(receiver, receiver.permission));
        }
        if (info.requestedPermissions != null) {
            text.append("\n  permissions");
            for (String permission : info.requestedPermissions)
                text.append(' ').append(permission);
        }
        CmLog.i(TAG, text.toString());
    }

    private static void logHolders(PackageManager pm, String permission) {
        StringBuilder text = new StringBuilder("holding " + permission + ":");
        for (PackageInfo info : pm.getPackagesHoldingPermissions(new String[] {permission}, 0))
            text.append(' ').append(info.packageName);
        CmLog.i(TAG, text.toString());
    }

    private static Set<String> vendorPackages(PackageManager pm) {
        Set<String> names = new TreeSet<>();
        for (ApplicationInfo app : pm.getInstalledApplications(0)) {
            if (VENDOR.matcher(app.packageName).find())
                names.add(app.packageName);
        }
        Intent home = new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME);
        for (ResolveInfo info : pm.queryIntentActivities(home, 0))
            names.add(info.activityInfo.packageName);
        return names;
    }

    private static String describe(ComponentInfo component, String permission) {
        StringBuilder text = new StringBuilder();
        text.append(component.packageName).append('/').append(component.name).append(", exported ")
            .append(component.exported);
        if (permission != null)
            text.append(", permission ").append(permission);
        if (!component.processName.equals(component.packageName))
            text.append(", process ").append(component.processName);
        appendMetaData(text, ", meta-data ", component.metaData);
        return text.toString();
    }

    private static void appendMetaData(StringBuilder text, String prefix, Bundle metaData) {
        if (metaData == null || metaData.isEmpty())
            return;
        text.append(prefix.startsWith(" ") ? "\n" + prefix : prefix).append('{');
        boolean first = true;
        for (String key : new TreeSet<>(metaData.keySet())) {
            if (!first)
                text.append(", ");
            first = false;
            text.append(key).append('=').append(metaData.get(key));
        }
        text.append('}');
    }

    // The launcher's and the vendor's media APKs, for decompiling at home:
    // how the widget picks its apps and where the steering-wheel buttons go.
    // System APKs are world-readable.
    static String exportApks(Context context) {
        PackageManager pm = context.getPackageManager();
        Set<String> names = vendorPackages(pm);
        return CmLog.exportToDownloads("cloudmus-system-apks", out -> {
            try (ZipOutputStream zip = new ZipOutputStream(out)) {
                StringBuilder index = new StringBuilder();
                for (String name : names) {
                    ApplicationInfo app;
                    try {
                        app = pm.getApplicationInfo(name, 0);
                    } catch (PackageManager.NameNotFoundException error) {
                        continue;
                    }
                    if ((app.flags & ApplicationInfo.FLAG_SYSTEM) == 0) {
                        index.append(name).append(": user app, skipped\n");
                        continue;
                    }
                    File apk = new File(app.sourceDir);
                    try {
                        add(zip, name + ".apk", apk);
                        index.append(name).append(": ").append(apk).append(", ").append(apk.length())
                            .append(" bytes\n");
                    } catch (IOException error) {
                        index.append(name).append(": ").append(apk).append(", can't read: ").append(error)
                            .append('\n');
                    }
                }
                zip.putNextEntry(new ZipEntry("index.txt"));
                zip.write(index.toString().getBytes(java.nio.charset.StandardCharsets.UTF_8));
                zip.closeEntry();
                CmLog.i(TAG, String.format(Locale.US, "APK export:\n%s", index));
            }
        });
    }

    private static void add(ZipOutputStream zip, String entry, File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            zip.putNextEntry(new ZipEntry(entry));
            byte[] buffer = new byte[64 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0)
                zip.write(buffer, 0, n);
            zip.closeEntry();
        }
    }
}
