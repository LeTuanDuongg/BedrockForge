package dev.hybridmcpe.launcher;

import android.app.Application;
import android.content.Context;
import android.util.Log;

/** Supplies imported resources before game SDK initialization, without replacing any store/auth code. */
public final class HostApplication extends Application {
    @Override protected void attachBaseContext(Context base) {
        if (Application.getProcessName().endsWith(":game")) {
            try { base = new HostContext(base, HostFactory.version, HostFactory.gameLoader); }
            catch (Exception e) { throw new IllegalStateException("Game application context failed", e); }
        }
        super.attachBaseContext(base);
    }
    @Override public void onCreate() {
        super.onCreate();
        if (!Application.getProcessName().endsWith(":game")) return;
        try {
            // These public SDK entry points normally run through the original APK's startup providers.
            Class<?> firebase = Class.forName("com.google.firebase.FirebaseApp", true, HostFactory.gameLoader);
            Object app = firebase.getMethod("initializeApp", Context.class).invoke(null, this);
            if (app == null) throw new IllegalStateException("Imported Firebase configuration is unavailable");
            Class<?> playGames = Class.forName("com.google.android.gms.games.PlayGamesSdk", true, HostFactory.gameLoader);
            playGames.getMethod("initialize", Context.class).invoke(null, this);
            Log.i("HybridMCPE", "host_stage=original_sdk_initialization_completed pid=" + android.os.Process.myPid());
        } catch (Exception e) {
            Log.e("HybridMCPE", "host_stage=original_sdk_initialization_failed", e);
            throw new IllegalStateException("Original game SDK initialization failed", e);
        }
    }
}
