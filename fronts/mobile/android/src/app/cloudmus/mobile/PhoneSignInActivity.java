package app.cloudmus.mobile;

import android.app.Activity;
import android.content.Intent;
import android.net.Network;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.ScrollView;
import android.widget.TextView;

import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import org.json.JSONException;
import org.json.JSONObject;

// The phone's side of a sign-in for another device (PhoneSignInServer),
// opened by cloudmus://sign-in?hosts=…&key=…: takes the offer, asks the user,
// signs in in the built-in browser with a session of its own, and sends the
// outcome back. Nothing of the sign-in stays on the phone.
//
// Anyone can make such a link, so: only addresses on a local network, a
// question naming who asks and where the sign-in will be before anything
// opens, and always a fresh browser session — nothing the phone is already
// signed in to can be handed over.
public final class PhoneSignInActivity extends Activity {
    private static final String TAG = "PhoneSignIn";
    private static final int BROWSER = 1;
    private static final int TIMEOUT_MS = 5000;
    private static final int MAX_ANSWER = 64 * 1024;

    private TextView text;
    private ProgressBar progress;
    private Button primary;
    private Button secondary;
    private String key;
    // The address that answered, and the network it answered on (null: the
    // system's choice).
    private String host;
    private Network network;
    private JSONObject method;
    private String sourceName;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        setContentView(layout());
        open(getIntent());
    }

    @Override
    protected void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        setIntent(intent);
        open(intent);
    }

    private void open(Intent intent) {
        Uri link = intent.getData();
        String hosts = link != null ? link.getQueryParameter("hosts") : null;
        key = link != null ? link.getQueryParameter("key") : null;
        method = null;
        if (hosts == null || key == null) {
            done(R.string.phone_expired);
            return;
        }
        busy(R.string.phone_reaching);
        String forKey = key;
        new Thread(() -> fetch(hosts.split(","), forKey), "phone-sign-in-fetch").start();
    }

    // All at once: the link holds every address the other device has, most of
    // them out of reach from here, and each could take a timeout. The first
    // to answer is the one.
    private void fetch(String[] hosts, String forKey) {
        AtomicBoolean answered = new AtomicBoolean();
        AtomicInteger left = new AtomicInteger(hosts.length);
        for (String candidate : hosts) {
            new Thread(() -> {
                Network via = null;
                byte[] sealed = null;
                try {
                    via = networkFor(candidate);
                    sealed = exchange(candidate, via, "GET", "/sign-in/offer", null);
                } catch (Exception unreachable) {
                    CmLog.i(TAG, candidate + ": " + unreachable.getMessage());
                }
                if (sealed == null) {
                    if (left.decrementAndGet() == 0 && !answered.get())
                        runOnUiThread(() -> {
                            if (forKey.equals(key))
                                done(R.string.phone_unreachable);
                        });
                    return;
                }
                if (answered.getAndSet(true))
                    return;
                CmLog.i(TAG, candidate + " answered" + (via != null ? " on network " + via : ""));
                try {
                    JSONObject offer = new JSONObject(PhoneSignInLink.open(forKey, PhoneSignInServer.OFFER, sealed));
                    JSONObject offered = offer.getJSONObject("method");
                    String name = offer.getString("sourceName");
                    Network on = via;
                    runOnUiThread(() -> {
                        if (forKey.equals(key))
                            ask(candidate, on, name, offered);
                    });
                } catch (Exception stale) {
                    CmLog.w(TAG, "the offer: " + stale.getMessage());
                    runOnUiThread(() -> done(R.string.phone_expired));
                }
            }, "phone-sign-in-fetch").start();
        }
    }

    // The phone's network the address is on, when it has one with that
    // subnet: a Wi-Fi with no internet isn't where the system sends anything
    // while there's mobile data.
    private Network networkFor(String host) throws IOException {
        return LocalAddresses.networkFor(getApplicationContext(), address(host).getAddress());
    }

    private void ask(String from, Network on, String name, JSONObject offered) {
        String site = Uri.parse(offered.optString("url")).getHost();
        if (site == null) {
            done(R.string.phone_expired);
            return;
        }
        host = from;
        network = on;
        sourceName = name;
        method = offered;
        text.setText(getString(R.string.phone_confirm, from.split(":")[0], name, site));
        progress.setVisibility(View.GONE);
        button(primary, R.string.phone_sign_in, view -> signIn());
        button(secondary, R.string.phone_cancel, view -> finish());
    }

    private void signIn() {
        try {
            // Never the phone's own session.
            method.put("freshSession", true);
            // A proxy on the other device's own loopback is out of reach here.
            JSONObject connection = method.optJSONObject("connection");
            if (connection != null && isLoopback(connection.optString("host")))
                method.remove("connection");
        } catch (JSONException error) {
            CmLog.e(TAG, "the method", error);
            done(R.string.phone_expired);
            return;
        }
        Intent browser = SignInWebActivity.intent(this, null, method.toString(), sourceName);
        startActivityForResult(browser, BROWSER);
    }

    private static boolean isLoopback(String host) {
        return host.equals("localhost") || host.startsWith("127.") || host.equals("::1");
    }

    @Override
    protected void onActivityResult(int code, int status, Intent data) {
        super.onActivityResult(code, status, data);
        if (code != BROWSER)
            return;
        String result = status == RESULT_OK && data != null
                ? data.getStringExtra(SignInWebActivity.EXTRA_RESULT) : null;
        if (result == null) {
            text.setText(R.string.phone_unfinished);
            button(primary, R.string.phone_sign_in, view -> signIn());
            button(secondary, R.string.phone_close, view -> finish());
            return;
        }
        busy(R.string.phone_sending);
        String to = host;
        Network via = network;
        String with = key;
        new Thread(() -> {
            try {
                exchange(to, via, "POST", "/sign-in/result",
                        PhoneSignInLink.seal(with, PhoneSignInServer.RESULT, result));
                runOnUiThread(() -> done(R.string.phone_sent));
            } catch (Exception error) {
                CmLog.w(TAG, "send: " + error.getMessage());
                runOnUiThread(() -> done(R.string.phone_send_failed));
            }
        }, "phone-sign-in-send").start();
    }

    // "a.b.c.d:port", on a local network only: anyone can make such a link.
    private static InetSocketAddress address(String host) throws IOException {
        int colon = host.lastIndexOf(':');
        if (colon < 0 || !host.substring(0, colon).matches("[0-9.]{7,15}"))
            throw new IOException("not an address");
        InetSocketAddress address;
        try {
            address = new InetSocketAddress(host.substring(0, colon), Integer.parseInt(host.substring(colon + 1)));
        } catch (IllegalArgumentException malformed) {
            throw new IOException("not an address");
        }
        if (address.getAddress() == null || !address.getAddress().isSiteLocalAddress())
            throw new IOException("not on a local network");
        return address;
    }

    // One request over a plain socket: Android keeps HttpURLConnection from
    // unencrypted addresses, and what's sent here is sealed already. Only to
    // an address on a local network; over `via` when given.
    private static byte[] exchange(String host, Network via, String verb, String path, byte[] body)
            throws IOException {
        InetSocketAddress address = address(host);
        try (Socket socket = via != null ? via.getSocketFactory().createSocket() : new Socket()) {
            socket.connect(address, TIMEOUT_MS);
            socket.setSoTimeout(TIMEOUT_MS * 2);
            OutputStream out = socket.getOutputStream();
            String head = verb + " " + path + " HTTP/1.1\r\nHost: " + host + "\r\nContent-Length: "
                    + (body != null ? body.length : 0) + "\r\nConnection: close\r\n\r\n";
            out.write(head.getBytes(StandardCharsets.ISO_8859_1));
            if (body != null)
                out.write(body);
            out.flush();
            ByteArrayOutputStream answer = new ByteArrayOutputStream();
            InputStream in = socket.getInputStream();
            byte[] buffer = new byte[8 * 1024];
            int got;
            while ((got = in.read(buffer)) >= 0) {
                answer.write(buffer, 0, got);
                if (answer.size() > MAX_ANSWER)
                    throw new IOException("too long");
            }
            byte[] bytes = answer.toByteArray();
            // ISO-8859-1 keeps a byte a character.
            String all = new String(bytes, StandardCharsets.ISO_8859_1);
            int start = all.indexOf("\r\n\r\n");
            if (start < 0 || !all.startsWith("HTTP/1.1 200"))
                throw new IOException("refused");
            return java.util.Arrays.copyOfRange(bytes, start + 4, bytes.length);
        }
    }

    private View layout() {
        float dp = getResources().getDisplayMetrics().density;
        int padding = (int) (24 * dp);
        LinearLayout column = new LinearLayout(this);
        column.setOrientation(LinearLayout.VERTICAL);
        column.setGravity(Gravity.CENTER_HORIZONTAL);
        column.setPadding(padding, padding, padding, padding);
        TextView title = new TextView(this);
        title.setText(R.string.phone_title);
        title.setTextSize(22);
        title.setTextColor(SignInWebActivity.primaryText(this));
        title.setGravity(Gravity.CENTER);
        column.addView(title);
        text = new TextView(this);
        text.setTextSize(16);
        text.setTextColor(SignInWebActivity.primaryText(this));
        text.setGravity(Gravity.CENTER);
        text.setPadding(0, padding, 0, padding);
        column.addView(text);
        progress = new ProgressBar(this);
        column.addView(progress);
        primary = new Button(this);
        secondary = new Button(this);
        ViewGroup.LayoutParams wide = new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT,
                ViewGroup.LayoutParams.WRAP_CONTENT);
        column.addView(primary, wide);
        column.addView(secondary, wide);
        ScrollView scroll = new ScrollView(this);
        scroll.setFillViewport(true);
        scroll.setFitsSystemWindows(true);
        scroll.addView(column);
        return scroll;
    }

    private void button(Button button, int label, View.OnClickListener action) {
        button.setText(label);
        button.setOnClickListener(action);
        button.setVisibility(View.VISIBLE);
    }

    private void busy(int message) {
        text.setText(message);
        progress.setVisibility(View.VISIBLE);
        primary.setVisibility(View.GONE);
        secondary.setVisibility(View.GONE);
    }

    private void done(int message) {
        text.setText(message);
        progress.setVisibility(View.GONE);
        primary.setVisibility(View.GONE);
        button(secondary, R.string.phone_close, view -> finish());
    }
}
