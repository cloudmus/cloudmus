package app.cloudmus.mobile;

import android.app.Activity;
import android.content.ActivityNotFoundException;
import android.content.ComponentName;
import android.content.Intent;
import android.os.Bundle;

// Has CloudMus Sign-in carry out a source's `browser` method, and gives what
// it ends up with to the source. A screen of its own with nothing on it:
// another app's answer comes only to an activity.
//
// In the core's process (:playback), kept in the foreground meanwhile
// (SignInService): the helper is another app, so CloudMus is in the
// background while the user signs in, and Android stops its player's service
// a minute into that, the core and the sources with it. The answer goes to the
// core directly: the views may be unloaded by then.
public final class SignInHelperActivity extends Activity {
    private static final String TAG = "SignInHelper";
    // `request`: the source's id.
    static final String EXTRA_REQUEST = "request";
    static final String EXTRA_METHOD = "method";
    static final String EXTRA_TITLE = "title";
    private static final int SIGN_IN = 1;

    @Override
    public void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // Recreated: the helper is up already.
        if (savedInstanceState != null)
            return;
        // Not to an impostor: the method may carry a proxy's password.
        if (!"installed".equals(SignInHelper.state(this))) {
            CmLog.w(TAG, "no sign-in helper of this key");
            answer("");
            return;
        }
        // The names are the helper's SignInActivity's.
        Intent helper = new Intent("app.cloudmus.signin.SIGN_IN")
                .setComponent(new ComponentName(SignInHelper.PACKAGE, SignInHelper.PACKAGE + ".SignInActivity"))
                .putExtra("method", getIntent().getStringExtra(EXTRA_METHOD))
                .putExtra("title", getIntent().getStringExtra(EXTRA_TITLE));
        SignInService.update(this, getString(R.string.sign_in_waiting, getIntent().getStringExtra(EXTRA_TITLE)),
                getString(R.string.sign_in_waiting_text), getString(R.string.sign_in_channel));
        try {
            startActivityForResult(helper, SIGN_IN);
        } catch (ActivityNotFoundException | SecurityException error) {
            CmLog.e(TAG, "open the sign-in helper", error);
            answer("");
        }
    }

    @Override
    protected void onActivityResult(int code, int status, Intent data) {
        super.onActivityResult(code, status, data);
        if (code != SIGN_IN)
            return;
        String result = status == RESULT_OK && data != null ? data.getStringExtra("result") : null;
        CmLog.i(TAG, result != null ? "signed in" : "not signed in");
        answer(result != null ? result : "");
    }

    // Gone without an answer (the helper closed by Android, the task cleared):
    // nothing is signing in any more.
    @Override
    protected void onDestroy() {
        if (isFinishing())
            SignInService.update(this, "", "", "");
        super.onDestroy();
    }

    // `result` empty: the user left.
    private void answer(String result) {
        if (!result.isEmpty()) {
            try {
                CoreBridge.nativeBrowserSignedIn(getIntent().getStringExtra(EXTRA_REQUEST), result);
            } catch (UnsatisfiedLinkError noCore) {
                CmLog.w(TAG, "no core to take the sign-in");
            }
        }
        SignInService.update(this, "", "", "");
        finish();
    }
}
