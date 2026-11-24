package app.cloudmus.mobile;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.content.pm.PackageInfo;
import android.net.Uri;
import android.os.Build;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.Inet4Address;
import java.net.InetAddress;
import java.net.NetworkInterface;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.text.SimpleDateFormat;
import java.util.ArrayList;
import java.util.Collections;
import java.util.Date;
import java.util.List;
import java.util.Locale;
import java.util.TimeZone;
import java.util.regex.Matcher;
import java.util.regex.Pattern;

// Developer tools: this device offers an APK as an update to another one on
// the same network (a head unit), without a release. It serves a releases
// feed in GitHub's format (/releases.json) and the APK, chosen by the user or
// this app's own when none is. The other device takes the address in its own
// Developer tools ("Update server"), App::MobileCore.
//
// The release is named one patch above the APK's version, which a build
// between tags ("0.7.0-12-gabc") isn't allowed to be: so it's newer than the
// same build on the other device, and Android takes it as long as its
// versionCode isn't lower.
public final class UpdateServer {
    private static final String TAG = "UpdateServer";
    static final int CHOOSE_REQUEST = 5;
    private static final int PORT = 8765;
    // The other device asks for the APK of its own ABI; CI builds arm64 only.
    private static final String ABI_SUFFIX = "-arm64-v8a.apk";

    private static ServerSocket server;
    private static Thread thread;
    private static File chosen;
    private static String chosenVersion = "";

    private UpdateServer() {
    }

    // Picks the APK to offer with the system's file picker (MainActivity
    // gets the answer: chosen()).
    public static void choose(Context context) {
        if (!(context instanceof Activity))
            return;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT)
            .addCategory(Intent.CATEGORY_OPENABLE)
            .setType("*/*")
            .putExtra(Intent.EXTRA_MIME_TYPES, new String[] {"application/vnd.android.package-archive", "application/octet-stream"});
        ((Activity) context).startActivityForResult(intent, CHOOSE_REQUEST);
    }

    // MainActivity, with the picked document: copied into the app's files,
    // where the server reads it.
    static void chosen(Context context, Uri uri) {
        Context app = context.getApplicationContext();
        new Thread(() -> {
            File target = new File(app.getFilesDir(), "served/CloudMus.apk");
            target.getParentFile().mkdirs();
            try (InputStream in = app.getContentResolver().openInputStream(uri);
                 OutputStream out = new FileOutputStream(target)) {
                byte[] buffer = new byte[256 * 1024];
                int read;
                while ((read = in.read(buffer)) > 0)
                    out.write(buffer, 0, read);
            } catch (Exception error) {
                CmLog.e(TAG, "copy " + uri, error);
                changed();
                return;
            }
            PackageInfo info = app.getPackageManager().getPackageArchiveInfo(target.getPath(), 0);
            synchronized (UpdateServer.class) {
                chosen = info != null ? target : null;
                chosenVersion = info != null ? info.versionName : "";
            }
            CmLog.i(TAG, "offering " + uri + ": " + (info != null ? info.versionName : "not an APK"));
            changed();
        }, "update-server-copy").start();
    }

    public static synchronized void start(Context context) {
        if (server != null)
            return;
        Context app = context.getApplicationContext();
        try {
            ServerSocket socket = new ServerSocket();
            socket.setReuseAddress(true);
            try {
                socket.bind(new java.net.InetSocketAddress(PORT));
            } catch (Exception busy) {
                socket.bind(new java.net.InetSocketAddress(0));
            }
            server = socket;
        } catch (Exception error) {
            CmLog.e(TAG, "listen", error);
            return;
        }
        final ServerSocket listening = server;
        thread = new Thread(() -> {
            while (!listening.isClosed()) {
                try {
                    Socket client = listening.accept();
                    new Thread(() -> serve(app, client), "update-server-client").start();
                } catch (Exception closed) {
                    break;
                }
            }
        }, "update-server");
        thread.start();
        CmLog.i(TAG, "serving on port " + server.getLocalPort());
        changed();
    }

    public static synchronized void stop(Context context) {
        if (server == null)
            return;
        try {
            server.close();
        } catch (Exception ignored) {
        }
        server = null;
        thread = null;
        CmLog.i(TAG, "stopped");
        changed();
    }

    // For the page: "" while stopped; else the addresses to give the other
    // device, one a line, then the version offered on the last line.
    public static synchronized String status(Context context) {
        if (server == null)
            return "";
        StringBuilder text = new StringBuilder();
        for (String address : addresses())
            text.append(address).append(':').append(server.getLocalPort()).append('\n');
        Offer offer = offer(context.getApplicationContext());
        text.append(offer != null ? offer.version : "");
        return text.toString();
    }

    private static List<String> addresses() {
        List<String> found = new ArrayList<>();
        try {
            for (NetworkInterface face : Collections.list(NetworkInterface.getNetworkInterfaces())) {
                if (!face.isUp() || face.isLoopback())
                    continue;
                for (InetAddress address : Collections.list(face.getInetAddresses())) {
                    if (address instanceof Inet4Address)
                        found.add(address.getHostAddress());
                }
            }
        } catch (Exception error) {
            CmLog.e(TAG, "addresses", error);
        }
        return found;
    }

    private static final class Offer {
        File file;
        String version;
        String tag;
    }

    private static Offer offer(Context context) {
        Offer offer = new Offer();
        synchronized (UpdateServer.class) {
            if (chosen != null && chosen.isFile()) {
                offer.file = chosen;
                offer.version = chosenVersion;
            }
        }
        if (offer.file == null) {
            offer.file = new File(context.getApplicationInfo().sourceDir);
            try {
                offer.version = context.getPackageManager().getPackageInfo(context.getPackageName(), 0).versionName;
            } catch (Exception error) {
                return null;
            }
        }
        Matcher match = Pattern.compile("^(\\d+)\\.(\\d+)\\.(\\d+)").matcher(offer.version);
        if (!match.find())
            return null;
        offer.tag = match.group(1) + "." + match.group(2) + "." + (Integer.parseInt(match.group(3)) + 1);
        return offer;
    }

    private static void serve(Context context, Socket client) {
        try (Socket socket = client) {
            socket.setSoTimeout(30000);
            BufferedReader reader = new BufferedReader(new InputStreamReader(socket.getInputStream(), StandardCharsets.ISO_8859_1));
            String request = reader.readLine();
            String line;
            while ((line = reader.readLine()) != null && !line.isEmpty()) {
                // the headers: nothing in them matters here
            }
            OutputStream out = socket.getOutputStream();
            String path = request != null && request.split(" ").length > 1 ? request.split(" ")[1] : "";
            CmLog.i(TAG, socket.getInetAddress().getHostAddress() + " asks for " + path);
            Offer offer = offer(context);
            String host = socket.getLocalAddress().getHostAddress() + ":" + socket.getLocalPort();
            if (offer != null && path.equals("/releases.json")) {
                byte[] body = feed(offer, host).getBytes(StandardCharsets.UTF_8);
                head(out, 200, "application/json", body.length);
                out.write(body);
            } else if (offer != null && path.equals("/" + assetName(offer))) {
                head(out, 200, "application/vnd.android.package-archive", offer.file.length());
                try (InputStream in = new FileInputStream(offer.file)) {
                    byte[] buffer = new byte[256 * 1024];
                    int read;
                    while ((read = in.read(buffer)) > 0)
                        out.write(buffer, 0, read);
                }
            } else {
                byte[] body = "not here\n".getBytes(StandardCharsets.UTF_8);
                head(out, 404, "text/plain", body.length);
                out.write(body);
            }
            out.flush();
        } catch (Exception error) {
            CmLog.e(TAG, "serve", error);
        }
    }

    private static String assetName(Offer offer) {
        return "CloudMus-" + offer.tag + ABI_SUFFIX;
    }

    private static String feed(Offer offer, String host) {
        SimpleDateFormat iso = new SimpleDateFormat("yyyy-MM-dd'T'HH:mm:ss'Z'", Locale.US);
        iso.setTimeZone(TimeZone.getTimeZone("UTC"));
        String notes = "- Test build " + offer.version + ", offered by " + Build.MANUFACTURER + " " + Build.MODEL;
        return "[{\"tag_name\":\"" + offer.tag + "\",\"draft\":false,\"prerelease\":false,"
            + "\"published_at\":\"" + iso.format(new Date()) + "\","
            + "\"html_url\":\"http://" + host + "/releases.json\","
            + "\"body\":\"" + notes.replace("\\", "\\\\").replace("\"", "\\\"") + "\","
            + "\"assets\":[{\"name\":\"" + assetName(offer) + "\",\"size\":" + offer.file.length() + ","
            + "\"browser_download_url\":\"http://" + host + "/" + assetName(offer) + "\"}]}]";
    }

    private static void head(OutputStream out, int status, String type, long length) throws java.io.IOException {
        String reason = status == 200 ? "OK" : "Not Found";
        String text = "HTTP/1.1 " + status + " " + reason + "\r\nContent-Type: " + type + "\r\nContent-Length: " + length
            + "\r\nConnection: close\r\n\r\n";
        out.write(text.getBytes(StandardCharsets.ISO_8859_1));
    }

    private static native void nativeChanged();

    private static void changed() {
        try {
            nativeChanged();
        } catch (UnsatisfiedLinkError noUi) {
            // nothing to tell
        }
    }
}
