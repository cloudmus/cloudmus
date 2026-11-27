package app.cloudmus.mobile;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.graphics.Bitmap;
import android.net.Uri;
import android.os.Bundle;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.webkit.CookieManager;
import android.webkit.WebChromeClient;
import android.webkit.WebResourceRequest;
import android.webkit.WebSettings;
import android.webkit.WebStorage;
import android.webkit.WebView;
import android.webkit.WebViewClient;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import androidx.webkit.ProxyConfig;
import androidx.webkit.ProxyController;
import androidx.webkit.WebViewFeature;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;

// A source's `browser` sign-in method (docs/protocol.md §10.2): its page in a
// WebView until the sign-in is through, then what the browser ended up with —
// the cookies of the addresses the source names, the final address and the
// User-Agent — goes back to whoever asked: the views
// (Platform::signedInWithBrowser) if it came with a request id, else the
// activity that started it for a result (PhoneSignInActivity). It knows no
// service: everything comes from the method.
//
// The browser connects as the source does (the method's "connection"): the
// system's way, directly, or through the source's proxy, by way of a
// ProxyTunnel.
public final class SignInWebActivity extends Activity {
    private static final String TAG = "SignInWeb";
    private static final String EXTRA_REQUEST = "request";
    private static final String EXTRA_METHOD = "method";
    private static final String EXTRA_TITLE = "title";
    // setResult()'s: what the browser ended up with.
    static final String EXTRA_RESULT = "result";

    private WebView web;
    private ProgressBar progress;
    private TextView address;
    private JSONObject method;
    private boolean answered;
    private ProxyTunnel tunnel;
    // The WebView's proxy has been set for this sign-in.
    private boolean overridden;

    static boolean takesProxy() {
        try {
            return WebViewFeature.isFeatureSupported(WebViewFeature.PROXY_OVERRIDE);
        } catch (RuntimeException noWebView) {
            return false;
        }
    }

    static Intent intent(Context context, String request, String method, String title) {
        return new Intent(context, SignInWebActivity.class)
                .putExtra(EXTRA_REQUEST, request)
                .putExtra(EXTRA_METHOD, method)
                .putExtra(EXTRA_TITLE, title);
    }

    // `result` empty: the user left.
    private static native void nativeFinished(String request, String result);

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        String url;
        try {
            method = new JSONObject(String.valueOf(getIntent().getStringExtra(EXTRA_METHOD)));
            url = method.getString("url");
        } catch (JSONException error) {
            CmLog.e(TAG, "no sign-in method", error);
            answer("");
            return;
        }
        // Sign-in pages only over an encrypted connection.
        if (!"https".equals(Uri.parse(url).getScheme())) {
            CmLog.w(TAG, "not https: " + Uri.parse(url).getScheme());
            answer("");
            return;
        }
        try {
            web = new WebView(this);
        } catch (RuntimeException error) {
            // A WebView being updated, or missing after all.
            CmLog.e(TAG, "no WebView", error);
            answer("");
            return;
        }
        setContentView(layout());

        WebSettings settings = web.getSettings();
        settings.setJavaScriptEnabled(true);
        settings.setDomStorageEnabled(true);
        // The browser's own User-Agent without what marks an embedded one:
        // some sign-in pages (Google's) refuse those.
        settings.setUserAgentString(settings.getUserAgentString().replace("; wv", "").replace("Version/4.0 ", ""));
        CookieManager cookies = CookieManager.getInstance();
        cookies.setAcceptCookie(true);
        // A sign-in may hand the session from one domain to another.
        cookies.setAcceptThirdPartyCookies(web, true);
        web.setWebViewClient(new Client());
        web.setWebChromeClient(new WebChromeClient() {
            @Override
            public void onProgressChanged(WebView view, int percent) {
                progress.setProgress(percent);
                progress.setVisibility(percent < 100 ? View.VISIBLE : View.INVISIBLE);
            }
        });

        CmLog.i(TAG, "sign-in at " + Uri.parse(url).getHost());
        Runnable load = () -> {
            if (!answered && !isDestroyed())
                web.loadUrl(url);
        };
        Runnable connected = () -> connect(load);
        if (method.optBoolean("freshSession")) {
            WebStorage.getInstance().deleteAllData();
            // Only once they're gone: the page mustn't see the old session.
            cookies.removeAllCookies(removed -> connected.run());
        } else {
            connected.run();
        }
    }

    // Sets the WebView's way out as the source's, then `load`. A proxy that
    // can't be set ends the sign-in: nothing may go past it.
    private void connect(Runnable load) {
        if (answered || isDestroyed())
            return;
        JSONObject connection = method.optJSONObject("connection");
        String mode = connection != null ? connection.optString("mode") : "";
        boolean proxy = "proxy".equals(mode);
        // "direct" with no way to say so: such a WebView takes the system's.
        if (!proxy && (!"direct".equals(mode) || !takesProxy())) {
            load.run();
            return;
        }
        try {
            ProxyConfig.Builder config = new ProxyConfig.Builder();
            if (proxy) {
                if (!takesProxy())
                    throw new IllegalStateException("this WebView takes no proxy");
                tunnel = new ProxyTunnel("socks5".equals(connection.getString("type")), connection.getString("host"),
                        connection.getInt("port"), connection.optString("username"), connection.optString("password"));
                config.addProxyRule("127.0.0.1:" + tunnel.port());
            } else {
                config.addDirect();
            }
            overridden = true;
            ProxyController.getInstance().setProxyOverride(config.build(), this::runOnUiThread, load);
        } catch (Exception error) {
            CmLog.e(TAG, "the source's connection", error);
            answer("");
        }
    }

    // The theme's own color for text: a TextView made in code gets a fainter
    // one.
    static android.content.res.ColorStateList primaryText(Context context) {
        android.content.res.TypedArray values
                = context.obtainStyledAttributes(new int[] {android.R.attr.textColorPrimary});
        try {
            return values.getColorStateList(0);
        } finally {
            values.recycle();
        }
    }

    // A bar with a close button, what it's for and where it is — the user is
    // about to type a password — then the page.
    private View layout() {
        float dp = getResources().getDisplayMetrics().density;
        LinearLayout root = new LinearLayout(this);
        root.setOrientation(LinearLayout.VERTICAL);
        root.setFitsSystemWindows(true);

        LinearLayout bar = new LinearLayout(this);
        bar.setGravity(Gravity.CENTER_VERTICAL);
        ImageButton close = new ImageButton(this);
        close.setImageResource(android.R.drawable.ic_menu_close_clear_cancel);
        close.setBackground(null);
        close.setContentDescription(getString(android.R.string.cancel));
        close.setOnClickListener(view -> answer(""));
        bar.addView(close, new LinearLayout.LayoutParams((int) (56 * dp), (int) (56 * dp)));
        LinearLayout texts = new LinearLayout(this);
        texts.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(this);
        title.setText(getIntent().getStringExtra(EXTRA_TITLE));
        title.setTextSize(17);
        title.setTextColor(primaryText(this));
        title.setSingleLine(true);
        texts.addView(title);
        address = new TextView(this);
        address.setTextSize(13);
        address.setTextColor(primaryText(this));
        address.setSingleLine(true);
        address.setAlpha(0.7f);
        texts.addView(address);
        bar.addView(texts, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        root.addView(bar);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        root.addView(progress, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, (int) (4 * dp)));
        root.addView(web, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        return root;
    }

    private final class Client extends WebViewClient {
        @Override
        public boolean shouldOverrideUrlLoading(WebView view, WebResourceRequest request) {
            String url = request.getUrl().toString();
            if (finishedAt(url))
                return true;
            // Pages only: an app link (intent://, market://) would be an error
            // page in here.
            String scheme = request.getUrl().getScheme();
            return !"https".equals(scheme) && !"http".equals(scheme);
        }

        @Override
        public void onPageStarted(WebView view, String url, Bitmap favicon) {
            if (!finishedAt(url))
                address.setText(Uri.parse(url).getHost());
        }

        @Override
        public void onPageFinished(WebView view, String url) {
            if (!answered && hasDoneCookies())
                finishSignIn(url);
        }
    }

    // The method's doneUrlPrefix: the address is the answer, not loaded.
    private boolean finishedAt(String url) {
        String prefix = method.optString("doneUrlPrefix");
        if (answered || prefix.isEmpty() || !url.startsWith(prefix))
            return answered;
        web.stopLoading();
        finishSignIn(url);
        return true;
    }

    private boolean hasDoneCookies() {
        JSONArray wanted = method.optJSONArray("doneCookies");
        JSONArray addresses = method.optJSONArray("cookieUrls");
        if (wanted == null || wanted.length() == 0 || addresses == null)
            return false;
        String header = CookieManager.getInstance().getCookie(addresses.optString(0));
        if (header == null)
            return false;
        java.util.Set<String> names = new java.util.HashSet<>();
        for (String pair : header.split(";")) {
            int equals = pair.indexOf('=');
            if (equals > 0)
                names.add(pair.substring(0, equals).trim());
        }
        for (int i = 0; i < wanted.length(); ++i) {
            if (!names.contains(wanted.optString(i)))
                return false;
        }
        return true;
    }

    private void finishSignIn(String url) {
        try {
            JSONObject cookies = new JSONObject();
            JSONArray addresses = method.getJSONArray("cookieUrls");
            for (int i = 0; i < addresses.length(); ++i) {
                String header = CookieManager.getInstance().getCookie(addresses.getString(i));
                cookies.put(addresses.getString(i), header != null ? header : "");
            }
            JSONObject result = new JSONObject()
                    .put("finalUrl", url)
                    .put("userAgent", web.getSettings().getUserAgentString())
                    .put("cookies", cookies);
            CmLog.i(TAG, "signed in");
            answer(result.toString());
        } catch (JSONException error) {
            CmLog.e(TAG, "the browser's outcome", error);
            answer("");
        }
    }

    private void answer(String result) {
        if (answered)
            return;
        answered = true;
        // The session is the source's now: nothing of it stays here to be
        // used, and so changed, by anyone else.
        if (method != null && method.optBoolean("freshSession")) {
            CookieManager.getInstance().removeAllCookies(null);
            WebStorage.getInstance().deleteAllData();
        }
        String request = getIntent().getStringExtra(EXTRA_REQUEST);
        if (request == null) {
            setResult(result.isEmpty() ? RESULT_CANCELED : RESULT_OK, new Intent().putExtra(EXTRA_RESULT, result));
        } else {
            try {
                nativeFinished(request, result);
            } catch (UnsatisfiedLinkError noViews) {
                // The process was restarted under this screen: nobody to tell.
                CmLog.w(TAG, "no views to answer");
            }
        }
        finish();
    }

    @Override
    public void onBackPressed() {
        if (web != null && web.canGoBack())
            web.goBack();
        else
            answer("");
    }

    @Override
    protected void onDestroy() {
        if (isFinishing())
            answer("");
        if (web != null) {
            ((ViewGroup) web.getParent()).removeView(web);
            web.destroy();
            web = null;
        }
        if (overridden)
            ProxyController.getInstance().clearProxyOverride(this::runOnUiThread, () -> { });
        if (tunnel != null)
            tunnel.close();
        super.onDestroy();
    }
}
