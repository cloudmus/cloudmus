package app.cloudmus.mobile;

import android.app.Activity;
import android.content.Context;
import android.content.Intent;
import android.net.Uri;

import com.google.android.play.core.appupdate.AppUpdateManager;
import com.google.android.play.core.appupdate.AppUpdateManagerFactory;
import com.google.android.play.core.appupdate.AppUpdateOptions;
import com.google.android.play.core.install.InstallStateUpdatedListener;

import ru.rustore.sdk.appupdate.listener.InstallStateUpdateListener;
import ru.rustore.sdk.appupdate.manager.RuStoreAppUpdateManager;
import ru.rustore.sdk.appupdate.manager.factory.RuStoreAppUpdateManagerFactory;

// Updates through the store the app came from (UpdateInstaller.installer()):
// Google Play's or RuStore's in-app updates. Flexible: the store downloads
// while the app goes on, then restarts it into the new version
// (completeUpdate()). Called from Platform in the UI process; answers through
// the native methods, on the main thread.
public final class StoreUpdates {
    private static final String TAG = "StoreUpdates";

    // What nativeState() is told.
    static final int STATE_DOWNLOADING = 0;
    static final int STATE_INSTALLING = 1;
    static final int STATE_FAILED = 2;
    static final int STATE_CANCELLED = 3;

    private static AppUpdateManager play;
    private static com.google.android.play.core.appupdate.AppUpdateInfo playInfo;
    private static InstallStateUpdatedListener playListener;
    private static RuStoreAppUpdateManager rustore;
    private static ru.rustore.sdk.appupdate.model.AppUpdateInfo rustoreInfo;
    private static InstallStateUpdateListener rustoreListener;

    private StoreUpdates() {
    }

    // This build updates through a store: "yes".
    public static String available(Context context) {
        return "yes";
    }

    // Asks `store` ("play" or "rustore") whether it has a newer version;
    // nativeChecked() gets its versionCode (0: none) or what went wrong.
    public static void check(Context context, String store, String manual) {
        final boolean asked = !manual.isEmpty();
        try {
            if ("play".equals(store)) {
                AppUpdateManager manager = playManager(context);
                manager.getAppUpdateInfo()
                    .addOnSuccessListener(info -> {
                        boolean available = info.updateAvailability()
                                == com.google.android.play.core.install.model.UpdateAvailability.UPDATE_AVAILABLE
                            && info.isUpdateTypeAllowed(com.google.android.play.core.install.model.AppUpdateType.FLEXIBLE);
                        CmLog.i(TAG, "Google Play: availability " + info.updateAvailability() + ", version code "
                            + info.availableVersionCode());
                        playInfo = available ? info : null;
                        checked(available ? info.availableVersionCode() : 0, "", asked);
                    })
                    .addOnFailureListener(error -> {
                        CmLog.e(TAG, "Google Play: check", error);
                        checked(0, String.valueOf(error.getMessage()), asked);
                    });
            } else if ("rustore".equals(store)) {
                RuStoreAppUpdateManager manager = rustoreManager(context);
                manager.getAppUpdateInfo()
                    .addOnSuccessListener(info -> {
                        boolean available = info.getUpdateAvailability()
                                == ru.rustore.sdk.appupdate.model.UpdateAvailability.UPDATE_AVAILABLE
                            && info.isUpdateTypeAllowed(ru.rustore.sdk.appupdate.model.AppUpdateType.FLEXIBLE);
                        CmLog.i(TAG, "RuStore: availability " + info.getUpdateAvailability() + ", version code "
                            + info.getAvailableVersionCode());
                        rustoreInfo = available ? info : null;
                        checked(available ? (int) info.getAvailableVersionCode() : 0, "", asked);
                    })
                    .addOnFailureListener(error -> {
                        CmLog.e(TAG, "RuStore: check", error);
                        checked(0, String.valueOf(error.getMessage()), asked);
                    });
            } else {
                checked(0, "", asked);
            }
        } catch (Throwable error) {
            CmLog.e(TAG, "check " + store, error);
            checked(0, error.toString(), asked);
        }
    }

    // The store's own update, after check() found one; progress and outcome
    // in nativeState(). Once downloaded the store installs it and starts the
    // new version.
    public static void start(Context context, String store) {
        try {
            if ("play".equals(store) && playInfo != null) {
                if (!(context instanceof Activity)) {
                    state(STATE_FAILED, 0, 0, "no activity");
                    return;
                }
                AppUpdateManager manager = playManager(context);
                if (playListener == null) {
                    playListener = installState -> {
                        int status = installState.installStatus();
                        if (status == com.google.android.play.core.install.model.InstallStatus.DOWNLOADING) {
                            state(STATE_DOWNLOADING, installState.bytesDownloaded(),
                                installState.totalBytesToDownload(), "");
                        } else if (status == com.google.android.play.core.install.model.InstallStatus.DOWNLOADED) {
                            state(STATE_INSTALLING, 0, 0, "");
                            manager.completeUpdate();
                        } else if (status == com.google.android.play.core.install.model.InstallStatus.FAILED) {
                            state(STATE_FAILED, 0, 0, "error " + installState.installErrorCode());
                        } else if (status == com.google.android.play.core.install.model.InstallStatus.CANCELED) {
                            state(STATE_CANCELLED, 0, 0, "");
                        }
                    };
                    manager.registerListener(playListener);
                }
                manager.startUpdateFlow(playInfo, (Activity) context,
                        AppUpdateOptions.newBuilder(com.google.android.play.core.install.model.AppUpdateType.FLEXIBLE)
                            .build())
                    .addOnSuccessListener(result -> {
                        CmLog.i(TAG, "Google Play: update flow " + result);
                        if (result != Activity.RESULT_OK)
                            state(STATE_CANCELLED, 0, 0, "");
                    })
                    .addOnFailureListener(error -> {
                        CmLog.e(TAG, "Google Play: update flow", error);
                        state(STATE_FAILED, 0, 0, String.valueOf(error.getMessage()));
                    });
            } else if ("rustore".equals(store) && rustoreInfo != null) {
                RuStoreAppUpdateManager manager = rustoreManager(context);
                ru.rustore.sdk.appupdate.model.AppUpdateOptions flexible
                    = new ru.rustore.sdk.appupdate.model.AppUpdateOptions.Builder()
                          .appUpdateType(ru.rustore.sdk.appupdate.model.AppUpdateType.FLEXIBLE)
                          .build();
                if (rustoreListener == null) {
                    rustoreListener = installState -> {
                        int status = installState.getInstallStatus();
                        if (status == ru.rustore.sdk.appupdate.model.InstallStatus.DOWNLOADING) {
                            state(STATE_DOWNLOADING, installState.getBytesDownloaded(),
                                installState.getTotalBytesToDownload(), "");
                        } else if (status == ru.rustore.sdk.appupdate.model.InstallStatus.DOWNLOADED) {
                            state(STATE_INSTALLING, 0, 0, "");
                            manager.completeUpdate(flexible).addOnFailureListener(error -> {
                                CmLog.e(TAG, "RuStore: complete", error);
                                state(STATE_FAILED, 0, 0, String.valueOf(error.getMessage()));
                            });
                        } else if (status == ru.rustore.sdk.appupdate.model.InstallStatus.FAILED) {
                            state(STATE_FAILED, 0, 0, "error " + installState.getInstallErrorCode());
                        }
                    };
                    manager.registerListener(rustoreListener);
                }
                manager.startUpdateFlow(rustoreInfo, flexible)
                    .addOnSuccessListener(result -> {
                        CmLog.i(TAG, "RuStore: update flow " + result);
                        if (result != Activity.RESULT_OK)
                            state(STATE_CANCELLED, 0, 0, "");
                    })
                    .addOnFailureListener(error -> {
                        CmLog.e(TAG, "RuStore: update flow", error);
                        state(STATE_FAILED, 0, 0, String.valueOf(error.getMessage()));
                    });
            } else {
                // Not checked yet in this run, or gone: the store's page.
                openStorePage(context, store);
                state(STATE_CANCELLED, 0, 0, "");
            }
        } catch (Throwable error) {
            CmLog.e(TAG, "update through " + store, error);
            openStorePage(context, store);
            state(STATE_FAILED, 0, 0, error.toString());
        }
    }

    // The app's page in the store's app (or on the web).
    public static void openStorePage(Context context, String store) {
        String pkg = context.getPackageName();
        Uri app = "rustore".equals(store) ? Uri.parse("rustore://apps.rustore.ru/app/" + pkg)
                                          : Uri.parse("market://details?id=" + pkg);
        Uri web = "rustore".equals(store) ? Uri.parse("https://apps.rustore.ru/app/" + pkg)
                                          : Uri.parse("https://play.google.com/store/apps/details?id=" + pkg);
        for (Uri uri : new Uri[] { app, web }) {
            try {
                Intent intent = new Intent(Intent.ACTION_VIEW, uri);
                if (!(context instanceof Activity))
                    intent.addFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
                context.startActivity(intent);
                return;
            } catch (Exception error) {
                CmLog.i(TAG, "can't open " + uri + ": " + error);
            }
        }
    }

    private static AppUpdateManager playManager(Context context) {
        if (play == null)
            play = AppUpdateManagerFactory.create(context.getApplicationContext());
        return play;
    }

    private static RuStoreAppUpdateManager rustoreManager(Context context) {
        if (rustore == null)
            rustore = RuStoreAppUpdateManagerFactory.INSTANCE.create(context.getApplicationContext());
        return rustore;
    }

    private static void checked(int versionCode, String error, boolean manual) {
        try {
            nativeChecked(versionCode, error, manual);
        } catch (UnsatisfiedLinkError noUi) {
            CmLog.i(TAG, "checked with no UI to tell");
        }
    }

    private static void state(int state, long done, long total, String error) {
        try {
            nativeState(state, done, total, error);
        } catch (UnsatisfiedLinkError noUi) {
            CmLog.i(TAG, "state " + state + " with no UI to tell");
        }
    }

    private static native void nativeChecked(int versionCode, String error, boolean manual);
    private static native void nativeState(int state, long done, long total, String error);
}
