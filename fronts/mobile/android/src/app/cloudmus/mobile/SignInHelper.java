package app.cloudmus.mobile;

import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.app.PendingIntent;
import android.content.pm.PackageInfo;
import android.content.pm.PackageInstaller;
import android.content.pm.PackageManager;
import android.os.Build;

import java.io.File;
import java.io.FileInputStream;
import java.io.InputStream;
import java.io.OutputStream;

// CloudMus Sign-in (fronts/mobile/signin-helper): a separate app with a modern
// browser engine, for signing in on a device whose WebView is too old for the
// sign-in page. The user installs it by hand, or has CloudMus install the APK
// it downloaded (ViewModel::SignInHelperDownload). Only one signed with
// CloudMus's own key is used: it's handed the sign-in, and hands back a
// signed-in session.
public final class SignInHelper {
    private static final String TAG = "SignInHelper";
    static final String PACKAGE = "app.cloudmus.signin";

    private SignInHelper() {
    }

    // "installed", "foreign" (another key's build is there), or "".
    public static String state(Context context) {
        PackageManager packages = context.getPackageManager();
        try {
            packages.getPackageInfo(PACKAGE, 0);
        } catch (PackageManager.NameNotFoundException absent) {
            return "";
        }
        return packages.checkSignatures(context.getPackageName(), PACKAGE) == PackageManager.SIGNATURE_MATCH
                ? "installed" : "foreign";
    }

    // Whether this build may install it itself: a store's may not
    // (REQUEST_INSTALL_PACKAGES). "yes" or "".
    public static String installable(Context context) {
        try {
            PackageInfo own = context.getPackageManager().getPackageInfo(context.getPackageName(),
                    PackageManager.GET_PERMISSIONS);
            if (own.requestedPermissions != null) {
                for (String permission : own.requestedPermissions) {
                    if ("android.permission.REQUEST_INSTALL_PACKAGES".equals(permission))
                        return "yes";
                }
            }
        } catch (PackageManager.NameNotFoundException impossible) {
            CmLog.e(TAG, "this app", impossible);
        }
        return "";
    }

    // Hands `path` to the package installer, off the calling thread; the
    // outcome comes back to StatusReceiver.
    public static void install(Context context, String path) {
        Context app = context.getApplicationContext();
        new Thread(() -> {
            try {
                commit(app, new File(path));
            } catch (Exception error) {
                CmLog.e(TAG, "install " + path, error);
                installFinished(error.getMessage() != null ? error.getMessage() : error.toString());
            }
        }, "sign-in-helper-install").start();
    }

    private static void commit(Context context, File apk) throws Exception {
        PackageInstaller installer = context.getPackageManager().getPackageInstaller();
        PackageInstaller.SessionParams params
                = new PackageInstaller.SessionParams(PackageInstaller.SessionParams.MODE_FULL_INSTALL);
        // Whatever the file holds, only this package comes of it.
        params.setAppPackageName(PACKAGE);
        int id = installer.createSession(params);
        try (PackageInstaller.Session session = installer.openSession(id)) {
            try (InputStream in = new FileInputStream(apk);
                    OutputStream out = session.openWrite("signin.apk", 0, apk.length())) {
                byte[] buffer = new byte[256 * 1024];
                int read;
                while ((read = in.read(buffer)) > 0)
                    out.write(buffer, 0, read);
                session.fsync(out);
            }
            int flags = PendingIntent.FLAG_UPDATE_CURRENT
                    | (Build.VERSION.SDK_INT >= 31 ? PendingIntent.FLAG_MUTABLE : 0);
            PendingIntent pending = PendingIntent.getBroadcast(context, id,
                    new Intent(context, StatusReceiver.class), flags);
            CmLog.i(TAG, "installing " + apk + " (session " + id + ")");
            session.commit(pending.getIntentSender());
        }
    }

    // The installer's answers: its own question to the user, then how it went.
    public static final class StatusReceiver extends BroadcastReceiver {
        @Override
        public void onReceive(Context context, Intent intent) {
            int status = intent.getIntExtra(PackageInstaller.EXTRA_STATUS, PackageInstaller.STATUS_FAILURE);
            String message = intent.getStringExtra(PackageInstaller.EXTRA_STATUS_MESSAGE);
            CmLog.i(TAG, "install status " + status + (message != null ? ": " + message : ""));
            switch (status) {
            case PackageInstaller.STATUS_PENDING_USER_ACTION: {
                Intent confirm = intent.getParcelableExtra(Intent.EXTRA_INTENT);
                if (confirm == null)
                    return;
                confirm.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                try {
                    context.startActivity(confirm);
                } catch (Exception error) {
                    CmLog.e(TAG, "ask to install", error);
                    installFinished(error.toString());
                }
                return;
            }
            case PackageInstaller.STATUS_SUCCESS:
            case PackageInstaller.STATUS_FAILURE_ABORTED: // the user said no
                installFinished("");
                return;
            default:
                installFinished(message != null && !message.isEmpty() ? message : "status " + status);
            }
        }
    }

    // `error` empty: installed, or the user turned it down.
    private static native void nativeInstallFinished(String error);

    private static void installFinished(String error) {
        try {
            nativeInstallFinished(error);
        } catch (UnsatisfiedLinkError noViews) {
            // The views look again when they're back.
            CmLog.i(TAG, "install finished with no UI to tell: " + error);
        }
    }
}
