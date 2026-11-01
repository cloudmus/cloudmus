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
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Collections;
import java.util.List;
import java.util.Locale;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Pattern;
import java.util.zip.Deflater;
import java.util.zip.ZipEntry;
import java.util.zip.ZipOutputStream;

// What the car's media system is made of, for the log: a head unit's media
// widget and steering-wheel buttons are the vendor's code, and there's no adb
// to look at it. The widget lists Yandex Music and VK Video but not us,
// without ever binding to our service — the diff between their manifests and
// ours, and the head unit's own APKs (exportApks()), show what it goes by.
final class SystemProbe {
    private static final String TAG = "SystemProbe";
    // The apps the head unit's widget does list; ours is compared to them.
    private static final String[] LISTED_APPS = {"ru.yandex.music", "com.vk.vkvideo"};
    // Vendor packages: the launcher with the widget, the "media source" service.
    private static final Pattern VENDOR = Pattern.compile("gwm|haval|nobo|launcher|media|source|music|radio|yandex\\.auto",
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
            logMediaCenterSource();
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

    // The app Media Center last connected to, which the GWM media widget adds
    // to its list (UiBridge.connectToMediaCenter()). SystemProperties is
    // hidden API; empty when unset or not readable by us.
    private static void logMediaCenterSource() {
        String key = "persist.gwm.carmediaapp.connectedthirdpackage";
        try {
            Object value = Class.forName("android.os.SystemProperties").getMethod("get", String.class)
                .invoke(null, key);
            CmLog.i(TAG, key + " = \"" + value + "\"");
        } catch (ReflectiveOperationException error) {
            CmLog.i(TAG, key + ": can't read: " + error);
        }
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

    // Export progress for the UI, which polls it from another thread: bytes
    // (one launcher APK can outweigh all the rest), the package being copied.
    private static volatile long exportDoneBytes;
    private static volatile long exportTotalBytes;
    private static volatile String exportCurrent = "";

    // "<done bytes>\t<total bytes>\t<package>"; total 0 while it's listing.
    static String exportProgress() {
        return exportDoneBytes + "\t" + exportTotalBytes + "\t" + exportCurrent;
    }

    // What the export takes: on the GWM unit (Haval H7), the media card under
    // the launcher (minibar = GWMMediaPlayerCard), the vendor's media source
    // bus and its sources, the steering-wheel key server, the launcher, and
    // Yandex's "UMA" that the listed Yandex Music declares itself an
    // AudioSource for. All the vendor's system apps came to 3 GB, too much
    // to carry; index.txt still names every package for a next round.
    private static final String[] EXPORT_PREFIXES = {
        "com.gwm.app.minibar",
        "com.gwm.app.media.",
        "com.gwm.media.",
        "com.gwm.app.car.media",
        "com.gwm.app.hardkey.server",
        "com.gwm.app.launcher",
        // The app menu: some apps start from it, ours didn't.
        "com.gwm.app.taskmanager",
        "yandex.auto.uma",
    };

    private static String exportReason(ApplicationInfo app, Set<String> home) {
        String name = app.packageName;
        if ((app.flags & ApplicationInfo.FLAG_SYSTEM) == 0)
            return null;
        for (String prefix : EXPORT_PREFIXES) {
            if (name.startsWith(prefix))
                return "media/keys";
        }
        // Another head unit: its launcher, at least.
        if (home.contains(name) && !name.startsWith("com.android."))
            return "launcher";
        return null;
    }

    // The head unit's system APKs, for decompiling at home: how the widget
    // picks its apps and where the steering-wheel buttons go. index.txt
    // lists every installed package, exported or not, so a widget the
    // selection missed can still be named and fetched next time. System
    // APKs are world-readable; Android 11+ hides most packages from us
    // (package visibility), Android 9 on the head unit doesn't. Blocking:
    // from a worker thread.
    static String exportApks(Context context) {
        exportDoneBytes = 0;
        exportTotalBytes = 0;
        exportCurrent = "";
        PackageManager pm = context.getPackageManager();
        Set<String> home = new TreeSet<>();
        for (ResolveInfo info : pm.queryIntentActivities(
                 new Intent(Intent.ACTION_MAIN).addCategory(Intent.CATEGORY_HOME), 0))
            home.add(info.activityInfo.packageName);

        List<ApplicationInfo> apps = new ArrayList<>(pm.getInstalledApplications(0));
        Collections.sort(apps, (a, b) -> a.packageName.compareTo(b.packageName));
        List<ApplicationInfo> selected = new ArrayList<>();
        long total = 0;
        for (ApplicationInfo app : apps) {
            if (exportReason(app, home) != null) {
                selected.add(app);
                total += new File(app.sourceDir).length();
                for (File file : oatFiles(app))
                    total += file.length();
            }
        }
        exportTotalBytes = Math.max(1, total);
        CmLog.i(TAG, "APK export: " + selected.size() + " of " + apps.size() + " packages, " + total / 1024 / 1024
                + " MB");

        String result = CmLog.exportToDownloads("cloudmus-system-apks", out -> {
            try (ZipOutputStream zip = new ZipOutputStream(out)) {
                // APKs are zips already: deflating again only costs time.
                zip.setLevel(Deflater.NO_COMPRESSION);
                StringBuilder index = new StringBuilder();
                index.append(String.format(Locale.US, "%d packages installed (as visible to the app), %d exported\n"
                        + "columns: package, system/user, why exported (or -), size, apk path, outcome\n\n",
                    apps.size(), selected.size()));
                for (ApplicationInfo app : apps) {
                    String reason = exportReason(app, home);
                    File apk = new File(app.sourceDir);
                    index.append(app.packageName).append('\t')
                        .append((app.flags & ApplicationInfo.FLAG_SYSTEM) != 0 ? "system" : "user").append('\t')
                        .append(reason == null ? "-" : reason).append('\t').append(apk.length()).append('\t')
                        .append(apk);
                    if (app.splitSourceDirs != null && app.splitSourceDirs.length > 0)
                        index.append(" (+").append(app.splitSourceDirs.length).append(" splits, not exported)");
                    index.append('\t');
                    if (reason == null) {
                        index.append("skipped\n");
                        continue;
                    }
                    exportCurrent = app.packageName;
                    try {
                        add(zip, app.packageName + ".apk", apk);
                        index.append("exported\n");
                    } catch (IOException error) {
                        index.append("can't read: ").append(error).append('\n');
                    }
                    // A system app's code is mostly not in its APK but next
                    // to it, compiled ahead: oat/<arch>/*.odex, and the dex
                    // itself in the .vdex.
                    File apkDir = apk.getParentFile();
                    for (File file : oatFiles(app)) {
                        String relative = file.getPath().substring(apkDir.getPath().length() + 1);
                        index.append("  ").append(relative).append('\t').append(file.length()).append('\t');
                        try {
                            add(zip, app.packageName + "/" + relative, file);
                            index.append("exported\n");
                        } catch (IOException error) {
                            index.append("can't read: ").append(error).append('\n');
                        }
                    }
                }
                exportCurrent = "index.txt";
                zip.setLevel(Deflater.DEFAULT_COMPRESSION);
                zip.putNextEntry(new ZipEntry("index.txt"));
                zip.write(index.toString().getBytes(StandardCharsets.UTF_8));
                zip.closeEntry();
            }
        });
        exportCurrent = "";
        return result;
    }

    private static List<File> oatFiles(ApplicationInfo app) {
        List<File> files = new ArrayList<>();
        File apkDir = new File(app.sourceDir).getParentFile();
        if (apkDir != null)
            collect(new File(apkDir, "oat"), files);
        return files;
    }

    private static void collect(File dir, List<File> files) {
        File[] entries = dir.listFiles();
        if (entries == null)
            return;
        Arrays.sort(entries);
        for (File entry : entries) {
            if (entry.isDirectory())
                collect(entry, files);
            else
                files.add(entry);
        }
    }

    private static void add(ZipOutputStream zip, String entry, File file) throws IOException {
        try (InputStream in = new FileInputStream(file)) {
            zip.putNextEntry(new ZipEntry(entry));
            byte[] buffer = new byte[256 * 1024];
            int n;
            while ((n = in.read(buffer)) > 0) {
                zip.write(buffer, 0, n);
                exportDoneBytes += n;
            }
            zip.closeEntry();
        }
    }
}
