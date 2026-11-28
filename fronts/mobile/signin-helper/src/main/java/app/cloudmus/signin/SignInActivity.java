package app.cloudmus.signin;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.pm.PackageManager;
import android.content.res.ColorStateList;
import android.content.res.TypedArray;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.util.Log;
import android.view.Gravity;
import android.view.View;
import android.view.ViewGroup;
import android.widget.ImageButton;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;

import org.json.JSONArray;
import org.json.JSONException;
import org.json.JSONObject;
import org.mozilla.geckoview.AllowOrDeny;
import org.mozilla.geckoview.GeckoResult;
import org.mozilla.geckoview.GeckoRuntime;
import org.mozilla.geckoview.GeckoSession;
import org.mozilla.geckoview.GeckoView;
import org.mozilla.geckoview.StorageController;
import org.mozilla.geckoview.WebExtension;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;

// CloudMus Sign-in: a source's `browser` sign-in method (docs/protocol.md
// §10.2) in GeckoView, for a device whose own WebView is too old for the
// sign-in page (a head unit's). CloudMus starts it for a result, as it would
// its own SignInWebActivity, and gets the same answer: the final address, the
// User-Agent and the Cookie header of each of the method's cookieUrls. It
// knows no service: everything comes from the method.
//
// Only CloudMus, signed with this app's own key, may ask: the answer is a
// signed-in session.
//
// The cookies come from a built-in WebExtension (assets/extension): GeckoView
// has no cookie API of its own, and a session's are httpOnly.
public final class SignInActivity extends Activity {
    private static final String TAG = "CloudMusSignIn";
    private static final String CALLER = "app.cloudmus.mobile";
    // The main app's SignInHelperActivity names these.
    static final String EXTRA_METHOD = "method";
    static final String EXTRA_TITLE = "title";
    static final String EXTRA_RESULT = "result";

    private static final long RESEND_MS = 300;
    private static final long EXTENSION_MS = 30 * 1000;

    // One per process, as GeckoView allows; so is the extension, and its
    // port: the process outlives a sign-in, and the next one's screen takes
    // them over (current).
    private static GeckoRuntime runtime;
    private static WebExtension.Port port;
    private static boolean listening;
    private static SignInActivity current;

    private JSONObject method;
    private GeckoSession session;
    private ProgressBar progress;
    private TextView address;
    private boolean canGoBack;
    private boolean answered;
    private String lastUrl = "";
    private String signInUrl;
    // The method's doneUrlPrefix was reached: the next cookies end it.
    private String doneUrl;
    private boolean cleared;
    private boolean connected;
    private boolean loaded;
    private final Handler timer = new Handler(Looper.getMainLooper());
    private final Runnable again = this::sendConnection;
    private final Runnable giveUp = () -> {
        Log.e(TAG, "no answer from the extension");
        answer(null);
    };

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        if (!trustedCaller()) {
            finish();
            return;
        }
        String url;
        try {
            method = new JSONObject(String.valueOf(getIntent().getStringExtra(EXTRA_METHOD)));
            url = method.getString("url");
        } catch (JSONException error) {
            Log.e(TAG, "no sign-in method", error);
            answer(null);
            return;
        }
        // Sign-in pages only over an encrypted connection.
        if (!"https".equals(Uri.parse(url).getScheme())) {
            Log.w(TAG, "not https: " + Uri.parse(url).getScheme());
            answer(null);
            return;
        }
        if (runtime == null)
            runtime = GeckoRuntime.create(getApplicationContext());

        GeckoView view = new GeckoView(this);
        setContentView(layout(view));
        session = new GeckoSession();
        session.setProgressDelegate(new Progress());
        session.setNavigationDelegate(new Navigation());
        session.setPromptDelegate(new Prompts());
        session.open(runtime);
        view.setSession(session);
        current = this;
        listen();

        Log.i(TAG, "sign-in at " + Uri.parse(url).getHost());
        // Nothing loads before the extension is ready: not forever.
        timer.postDelayed(giveUp, EXTENSION_MS);
        signInUrl = url;
        if (method.optBoolean("freshSession")) {
            // Only once it's gone: the page mustn't see an earlier session.
            runtime.getStorageController().clearData(StorageController.ClearFlags.ALL)
                    .accept(done -> {
                        cleared = true;
                        loadWhenReady();
                    }, error -> {
                        Log.e(TAG, "clear the old session", error);
                        answer(null);
                    });
        } else {
            cleared = true;
        }
    }

    // The page, once the old session is gone and the extension goes out the
    // source's way.
    private void loadWhenReady() {
        if (cleared && connected && !loaded) {
            loaded = true;
            load(signInUrl);
        }
    }

    private void load(String url) {
        if (!answered && session != null)
            session.loadUri(url);
    }

    // CloudMus, with this app's own signature: started for a result, Android
    // names who asked.
    private boolean trustedCaller() {
        String caller = getCallingPackage();
        boolean trusted = CALLER.equals(caller)
                && getPackageManager().checkSignatures(getPackageName(), caller) == PackageManager.SIGNATURE_MATCH;
        if (!trusted)
            Log.w(TAG, "refused a caller: " + caller);
        return trusted;
    }

    // The extension's end, once per process: it takes the source's
    // connection first, then answers what's asked; to whichever screen is the
    // current sign-in's.
    private void listen() {
        if (listening) {
            sendConnection();
            return;
        }
        listening = true;
        runtime.getWebExtensionController()
                .ensureBuiltIn("resource://android/assets/extension/", "signin@cloudmus.app")
                .accept(extension -> extension.setMessageDelegate(new WebExtension.MessageDelegate() {
                    @Override
                    public void onConnect(WebExtension.Port opened) {
                        port = opened;
                        port.setDelegate(new WebExtension.PortDelegate() {
                            @Override
                            public void onPortMessage(Object message, WebExtension.Port from) {
                                SignInActivity to = current;
                                if (to != null)
                                    to.runOnUiThread(() -> to.received((JSONObject) message));
                            }

                            @Override
                            public void onDisconnect(WebExtension.Port from) {
                                if (port == from)
                                    port = null;
                            }
                        });
                        Log.i(TAG, "the extension is there");
                        SignInActivity to = current;
                        if (to != null)
                            to.runOnUiThread(to::sendConnection);
                    }
                }, "cloudmus"), error -> {
                    Log.e(TAG, "the extension", error);
                    listening = false;
                    SignInActivity to = current;
                    if (to != null)
                        to.answer(null);
                });
    }

    // Tells the extension how to go out, and again until it answers: what's
    // sent as a port opens may never arrive.
    private void sendConnection() {
        if (answered || connected || isFinishing())
            return;
        // Again in a moment, until the extension is there.
        timer.removeCallbacks(again);
        timer.postDelayed(again, RESEND_MS);
        if (port == null)
            return;
        try {
            JSONObject connection = method.optJSONObject("connection");
            port.postMessage(new JSONObject()
                    .put("type", "connection")
                    .put("connection", connection != null ? connection : JSONObject.NULL));
        } catch (JSONException error) {
            Log.e(TAG, "the connection", error);
            answer(null);
        }
    }

    private void received(JSONObject message) {
        switch (message.optString("type")) {
        case "connected":
            if (connected)
                break;
            Log.i(TAG, "connected");
            connected = true;
            timer.removeCallbacks(again);
            timer.removeCallbacks(giveUp);
            loadWhenReady();
            // A page may have finished while it was away.
            if (loaded)
                askForCookies();
            break;
        case "refused":
            Log.e(TAG, "the source's connection: " + message.optString("error"));
            answer(null);
            break;
        case "cookies":
            JSONObject cookies = message.optJSONObject("cookies");
            if (cookies != null)
                cookiesCame(cookies);
            break;
        default:
        }
    }

    private void askForCookies() {
        JSONArray wanted = method.optJSONArray("doneCookies");
        if (answered || port == null || (doneUrl == null && (wanted == null || wanted.length() == 0)))
            return;
        try {
            port.postMessage(new JSONObject().put("type", "cookies").put("urls", method.getJSONArray("cookieUrls")));
        } catch (JSONException error) {
            Log.e(TAG, "ask for the cookies", error);
        }
    }

    // Signed in once cookieUrls[0] has every one of doneCookies, or the
    // browser was about to open doneUrlPrefix.
    private void cookiesCame(JSONObject cookies) {
        if (answered)
            return;
        if (doneUrl != null) {
            finishSignIn(doneUrl, cookies);
            return;
        }
        JSONArray addresses = method.optJSONArray("cookieUrls");
        JSONArray wanted = method.optJSONArray("doneCookies");
        if (addresses == null || wanted == null || wanted.length() == 0)
            return;
        Set<String> names = new HashSet<>();
        for (String pair : cookies.optString(addresses.optString(0)).split(";")) {
            int equals = pair.indexOf('=');
            if (equals > 0)
                names.add(pair.substring(0, equals).trim());
        }
        for (int i = 0; i < wanted.length(); ++i) {
            if (!names.contains(wanted.optString(i)))
                return;
        }
        finishSignIn(lastUrl, cookies);
    }

    // The User-Agent goes with the cookies: a session may be tied to it.
    private void finishSignIn(String url, JSONObject cookies) {
        answered = true;
        session.getUserAgent().accept(agent -> {
            try {
                JSONObject result = new JSONObject()
                        .put("finalUrl", url)
                        .put("userAgent", agent != null ? agent : "")
                        .put("cookies", cookies);
                Log.i(TAG, "signed in");
                reply(result.toString());
            } catch (JSONException error) {
                Log.e(TAG, "the outcome", error);
                reply(null);
            }
        }, error -> reply(null));
    }

    // The method's doneUrlPrefix: the address is the answer, not loaded.
    // The cookies go along all the same.
    private boolean finishedAt(String url) {
        String prefix = method.optString("doneUrlPrefix");
        if (answered || prefix.isEmpty() || !url.startsWith(prefix))
            return false;
        doneUrl = url;
        askForCookies();
        return true;
    }

    private void answer(String result) {
        answered = true;
        reply(result);
    }

    private void reply(String result) {
        if (isFinishing())
            return;
        setResult(result != null ? RESULT_OK : RESULT_CANCELED, new Intent().putExtra(EXTRA_RESULT, result));
        finish();
    }

    private final class Progress implements GeckoSession.ProgressDelegate {
        @Override
        public void onPageStart(GeckoSession from, String url) {
            lastUrl = url;
            address.setText(Uri.parse(url).getHost());
            progress.setVisibility(View.VISIBLE);
        }

        @Override
        public void onProgressChange(GeckoSession from, int percent) {
            progress.setProgress(percent);
            progress.setVisibility(percent < 100 ? View.VISIBLE : View.INVISIBLE);
        }

        @Override
        public void onPageStop(GeckoSession from, boolean success) {
            progress.setVisibility(View.INVISIBLE);
            askForCookies();
        }
    }

    private final class Navigation implements GeckoSession.NavigationDelegate {
        @Override
        public void onCanGoBack(GeckoSession from, boolean can) {
            canGoBack = can;
        }

        @Override
        public GeckoResult<AllowOrDeny> onLoadRequest(GeckoSession from, LoadRequest request) {
            if (finishedAt(request.uri))
                return GeckoResult.deny();
            // Pages only: an app link (intent://, market://) leads nowhere here.
            String scheme = Uri.parse(request.uri).getScheme();
            boolean page = "https".equals(scheme) || "http".equals(scheme) || "about".equals(scheme)
                    || "data".equals(scheme) || "blob".equals(scheme);
            return page ? GeckoResult.allow() : GeckoResult.deny();
        }

        // A new window goes in this one: there's only the sign-in to see.
        @Override
        public GeckoResult<GeckoSession> onNewSession(GeckoSession from, String uri) {
            load(uri);
            return null;
        }
    }

    // What a page asks of the browser itself: a <select>'s list, alert() and
    // confirm(). GeckoView shows none of them on its own.
    private final class Prompts implements GeckoSession.PromptDelegate {
        @Override
        public GeckoResult<PromptResponse> onChoicePrompt(GeckoSession from, ChoicePrompt prompt) {
            if (prompt.type == ChoicePrompt.Type.MULTIPLE)
                return null;
            List<ChoicePrompt.Choice> choices = new ArrayList<>();
            flatten(prompt.choices, choices);
            String[] labels = new String[choices.size()];
            int checked = -1;
            for (int i = 0; i < labels.length; ++i) {
                labels[i] = choices.get(i).label;
                if (choices.get(i).selected)
                    checked = i;
            }
            GeckoResult<PromptResponse> result = new GeckoResult<>();
            new AlertDialog.Builder(SignInActivity.this)
                    .setTitle(prompt.title)
                    .setSingleChoiceItems(labels, checked, (dialog, which) -> {
                        result.complete(prompt.confirm(choices.get(which)));
                        dialog.dismiss();
                    })
                    .setOnCancelListener(dialog -> result.complete(prompt.dismiss()))
                    .show();
            return result;
        }

        @Override
        public GeckoResult<PromptResponse> onAlertPrompt(GeckoSession from, AlertPrompt prompt) {
            GeckoResult<PromptResponse> result = new GeckoResult<>();
            new AlertDialog.Builder(SignInActivity.this)
                    .setTitle(prompt.title)
                    .setMessage(prompt.message)
                    .setPositiveButton(android.R.string.ok, (dialog, which) -> result.complete(prompt.dismiss()))
                    .setOnCancelListener(dialog -> result.complete(prompt.dismiss()))
                    .show();
            return result;
        }

        @Override
        public GeckoResult<PromptResponse> onButtonPrompt(GeckoSession from, ButtonPrompt prompt) {
            GeckoResult<PromptResponse> result = new GeckoResult<>();
            new AlertDialog.Builder(SignInActivity.this)
                    .setTitle(prompt.title)
                    .setMessage(prompt.message)
                    .setPositiveButton(android.R.string.ok,
                            (dialog, which) -> result.complete(prompt.confirm(ButtonPrompt.Type.POSITIVE)))
                    .setNegativeButton(android.R.string.cancel,
                            (dialog, which) -> result.complete(prompt.confirm(ButtonPrompt.Type.NEGATIVE)))
                    .setOnCancelListener(dialog -> result.complete(prompt.dismiss()))
                    .show();
            return result;
        }

        // A list's groups, opened up: their items, the selectable ones.
        private void flatten(ChoicePrompt.Choice[] from, List<ChoicePrompt.Choice> into) {
            for (ChoicePrompt.Choice choice : from) {
                if (choice.items != null)
                    flatten(choice.items, into);
                else if (!choice.disabled && !choice.separator)
                    into.add(choice);
            }
        }
    }

    // A bar with a close button, what it's for and where it is — the user is
    // about to type a password — then the page. As CloudMus's own.
    private View layout(GeckoView view) {
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
        close.setOnClickListener(v -> answer(null));
        bar.addView(close, new LinearLayout.LayoutParams((int) (56 * dp), (int) (56 * dp)));
        LinearLayout texts = new LinearLayout(this);
        texts.setOrientation(LinearLayout.VERTICAL);
        TextView title = new TextView(this);
        title.setText(getIntent().getStringExtra(EXTRA_TITLE));
        title.setTextSize(17);
        title.setTextColor(primaryText());
        title.setSingleLine(true);
        texts.addView(title);
        address = new TextView(this);
        address.setTextSize(13);
        address.setTextColor(primaryText());
        address.setSingleLine(true);
        address.setAlpha(0.7f);
        texts.addView(address);
        bar.addView(texts, new LinearLayout.LayoutParams(0, ViewGroup.LayoutParams.WRAP_CONTENT, 1));
        root.addView(bar);

        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        root.addView(progress, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, (int) (4 * dp)));
        root.addView(view, new LinearLayout.LayoutParams(ViewGroup.LayoutParams.MATCH_PARENT, 0, 1));
        return root;
    }

    // The theme's own color for text: a TextView made in code gets a fainter
    // one.
    private ColorStateList primaryText() {
        TypedArray values = obtainStyledAttributes(new int[] {android.R.attr.textColorPrimary});
        try {
            return values.getColorStateList(0);
        } finally {
            values.recycle();
        }
    }

    @Override
    public void onBackPressed() {
        if (session != null && canGoBack)
            session.goBack();
        else
            answer(null);
    }

    @Override
    protected void onDestroy() {
        timer.removeCallbacksAndMessages(null);
        if (current == this)
            current = null;
        if (session != null) {
            session.close();
            session = null;
        }
        // The session is the source's now: nothing of it stays here.
        if (isFinishing() && runtime != null && method != null && method.optBoolean("freshSession"))
            runtime.getStorageController().clearData(StorageController.ClearFlags.ALL);
        super.onDestroy();
    }
}
