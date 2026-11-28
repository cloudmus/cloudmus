package app.cloudmus.mobile;

import android.content.Context;
import android.os.Handler;
import android.os.Looper;
import android.text.TextUtils;

import java.io.ByteArrayOutputStream;
import java.io.File;
import java.io.FileInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.List;

import org.json.JSONObject;

// Has a phone on the same network sign in to a source for this device, whose
// own browser can't (a head unit's years-old WebView). It offers one source's
// `browser` method, while its QR code is on the screen:
//
//   GET  /sign-in          a page for the phone's browser, which a camera
//                          opens, with a link into CloudMus there
//   GET  /sign-in/offer    {sourceName, method}, sealed (PhoneSignInLink)
//   GET  /CloudMus.apk     this app's own APK, for a phone without CloudMus:
//                          the page offers it, and no internet is needed
//   POST /sign-in/result   what the phone's browser ended up with, sealed
//
// The key is in the QR code alone — in the address's fragment, which a
// browser doesn't send. One result is taken, then the offer is over; so it is
// after LIFETIME_MS.
public final class PhoneSignInServer {
    private static final String TAG = "PhoneSignIn";
    private static final long LIFETIME_MS = 10 * 60 * 1000;
    private static final int MAX_HEAD = 16 * 1024;
    private static final int MAX_BODY = 256 * 1024;
    private static final String APK_PATH = "/CloudMus.apk";
    static final String OFFER = "offer";
    static final String RESULT = "result";

    private static Offer current;

    private PhoneSignInServer() {
    }

    private static final class Offer {
        ServerSocket socket;
        String request;
        String key;
        String hosts;
        byte[] sealed;
        String page;
        // This app's installed APK, offered by the page.
        File apk;
        Runnable expiry;
    }

    private static native void nativeSignedIn(String request, String result);

    private static native void nativeExpired();

    // The page's addresses with the key, one a line, the likeliest to reach
    // first (LocalAddresses), for the QR code: which network the phone is on
    // isn't known, so the user may switch to the next. "" with no network.
    public static synchronized String start(Context context, String request, String method, String title) {
        stop();
        List<LocalAddresses.Address> addresses = LocalAddresses.find(context);
        if (addresses.isEmpty()) {
            CmLog.w(TAG, "no network to offer the sign-in on");
            return "";
        }
        try {
            Offer offer = new Offer();
            offer.socket = new ServerSocket(0);
            offer.request = request;
            offer.key = PhoneSignInLink.newKey();
            List<String> hosts = new ArrayList<>();
            for (LocalAddresses.Address address : addresses)
                hosts.add(address.host + ":" + offer.socket.getLocalPort());
            offer.hosts = TextUtils.join(",", hosts);
            JSONObject body = new JSONObject().put("sourceName", title).put("method", new JSONObject(method));
            offer.sealed = PhoneSignInLink.seal(offer.key, OFFER, body.toString());
            offer.page = page(context, title, offer.hosts);
            offer.apk = new File(context.getApplicationInfo().sourceDir);
            offer.expiry = () -> expire(offer);
            new Handler(Looper.getMainLooper()).postDelayed(offer.expiry, LIFETIME_MS);
            current = offer;
            Thread thread = new Thread(() -> accept(offer), "phone-sign-in");
            thread.setDaemon(true);
            thread.start();
            CmLog.i(TAG, "offering " + title + " on port " + offer.socket.getLocalPort() + ": "
                    + TextUtils.join(", ", addresses));
            List<String> links = new ArrayList<>();
            for (String host : hosts)
                links.add("http://" + host + "/sign-in#" + offer.key);
            return TextUtils.join("\n", links);
        } catch (Exception error) {
            CmLog.e(TAG, "offer", error);
            return "";
        }
    }

    public static synchronized void stop() {
        if (current == null)
            return;
        new Handler(Looper.getMainLooper()).removeCallbacks(current.expiry);
        try {
            current.socket.close();
        } catch (IOException ignored) {
        }
        current = null;
        CmLog.i(TAG, "stopped");
    }

    private static void expire(Offer offer) {
        synchronized (PhoneSignInServer.class) {
            if (current != offer)
                return;
            stop();
        }
        try {
            nativeExpired();
        } catch (UnsatisfiedLinkError noViews) {
            // nothing to tell
        }
    }

    private static void accept(Offer offer) {
        while (!offer.socket.isClosed()) {
            try {
                Socket client = offer.socket.accept();
                Thread thread = new Thread(() -> serve(offer, client), "phone-sign-in-client");
                thread.setDaemon(true);
                thread.start();
            } catch (IOException closed) {
                break;
            }
        }
    }

    private static void serve(Offer offer, Socket client) {
        try (Socket socket = client) {
            socket.setSoTimeout(15000);
            InputStream in = socket.getInputStream();
            String[] head = readHead(in).split("\r\n");
            String[] request = head[0].split(" ");
            if (request.length < 2)
                return;
            int length = 0;
            for (String line : head) {
                if (line.regionMatches(true, 0, "content-length:", 0, 15))
                    length = Integer.parseInt(line.substring(15).trim());
            }
            OutputStream out = socket.getOutputStream();
            CmLog.i(TAG, socket.getInetAddress().getHostAddress() + " " + request[0] + " " + request[1]);
            if ("GET".equals(request[0]) && "/sign-in".equals(request[1])) {
                respond(out, "200 OK", "text/html; charset=utf-8", offer.page.getBytes(StandardCharsets.UTF_8));
            } else if ("GET".equals(request[0]) && APK_PATH.equals(request[1])) {
                sendApk(offer, out);
            } else if ("GET".equals(request[0]) && "/sign-in/offer".equals(request[1])) {
                respond(out, "200 OK", "application/octet-stream", offer.sealed);
            } else if ("POST".equals(request[0]) && "/sign-in/result".equals(request[1]) && length > 0
                    && length <= MAX_BODY) {
                take(offer, readBody(in, length), out);
            } else {
                respond(out, "404 Not Found", "text/plain", new byte[0]);
            }
        } catch (Exception error) {
            CmLog.e(TAG, "serve", error);
        }
    }

    private static void take(Offer offer, byte[] body, OutputStream out) throws IOException {
        String result;
        try {
            result = PhoneSignInLink.open(offer.key, RESULT, body);
        } catch (java.security.GeneralSecurityException forged) {
            respond(out, "403 Forbidden", "text/plain", new byte[0]);
            return;
        }
        synchronized (PhoneSignInServer.class) {
            // Taken already, or no longer offered.
            if (current != offer) {
                respond(out, "410 Gone", "text/plain", new byte[0]);
                return;
            }
            respond(out, "200 OK", "text/plain", new byte[0]);
            stop();
        }
        try {
            nativeSignedIn(offer.request, result);
        } catch (UnsatisfiedLinkError noViews) {
            CmLog.w(TAG, "no views to answer");
        }
    }

    private static void sendApk(Offer offer, OutputStream out) throws IOException {
        String head = "HTTP/1.1 200 OK\r\nContent-Type: application/vnd.android.package-archive\r\nContent-Length: "
                + offer.apk.length() + "\r\nContent-Disposition: attachment; filename=\"CloudMus.apk\""
                + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        out.write(head.getBytes(StandardCharsets.ISO_8859_1));
        try (InputStream in = new FileInputStream(offer.apk)) {
            byte[] buffer = new byte[64 * 1024];
            int got;
            while ((got = in.read(buffer)) >= 0)
                out.write(buffer, 0, got);
        }
        out.flush();
    }

    // The key stays in the browser: its script puts the fragment into the link.
    // A phone without CloudMus gets it here too, from this device.
    private static String page(Context context, String title, String hosts) {
        String open = TextUtils.htmlEncode(context.getString(R.string.phone_page_open));
        String about = TextUtils.htmlEncode(context.getString(R.string.phone_page_about, title));
        String download = TextUtils.htmlEncode(context.getString(R.string.phone_page_download));
        String install = TextUtils.htmlEncode(context.getString(R.string.phone_page_install));
        return "<!doctype html><html><head><meta charset=\"utf-8\">"
                + "<meta name=\"viewport\" content=\"width=device-width, initial-scale=1\"><title>CloudMus</title>"
                + "<style>body{font:18px sans-serif;margin:32px 20px;text-align:center}"
                + "a{display:block;margin:32px auto;max-width:320px;padding:16px;border-radius:32px;"
                + "background:#C23D16;color:#fff;text-decoration:none;font-weight:600}"
                + "a.second{background:none;color:#C23D16;border:2px solid #C23D16;margin-bottom:12px}"
                + "small{display:block;max-width:320px;margin:0 auto;opacity:.7}</style></head><body>"
                + "<h1>CloudMus</h1><p>" + about + "</p><a id=\"open\" href=\"#\">" + open + "</a>"
                + "<a class=\"second\" href=\"" + APK_PATH + "\" download=\"CloudMus.apk\">" + download + "</a>"
                + "<small>" + install + "</small>"
                + "<script>document.getElementById('open').href='cloudmus://sign-in?hosts=" + hosts
                + "&key='+encodeURIComponent(location.hash.slice(1));</script></body></html>";
    }

    private static String readHead(InputStream in) throws IOException {
        StringBuilder head = new StringBuilder();
        while (head.length() < MAX_HEAD) {
            int next = in.read();
            if (next < 0)
                throw new IOException("closed early");
            head.append((char) next);
            int length = head.length();
            if (next == '\n' && length >= 4 && "\r\n\r\n".contentEquals(head.subSequence(length - 4, length)))
                return head.toString();
        }
        throw new IOException("head too long");
    }

    private static byte[] readBody(InputStream in, int length) throws IOException {
        ByteArrayOutputStream body = new ByteArrayOutputStream();
        byte[] buffer = new byte[16 * 1024];
        while (body.size() < length) {
            int got = in.read(buffer, 0, Math.min(buffer.length, length - body.size()));
            if (got < 0)
                throw new IOException("closed early");
            body.write(buffer, 0, got);
        }
        return body.toByteArray();
    }

    private static void respond(OutputStream out, String status, String type, byte[] body) throws IOException {
        String head = "HTTP/1.1 " + status + "\r\nContent-Type: " + type + "\r\nContent-Length: " + body.length
                + "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n";
        out.write(head.getBytes(StandardCharsets.ISO_8859_1));
        out.write(body);
        out.flush();
    }
}
