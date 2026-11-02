package org.supermariowar.app;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.content.res.AssetManager;
import android.os.Bundle;
import android.widget.TextView;
import java.io.*;

/** Extract on a worker before SDL loads native libraries; never request shared storage. */
public class MainActivity extends Activity {
    static final String EXTRA_RESTARTED = "org.supermariowar.app.RESTARTED";
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        // Icon tapped while the game is already running in this task (e.g. after
        // Home): just reveal the running game, don't start another one.
        Intent launch = getIntent();
        if (!isTaskRoot() && launch != null && Intent.ACTION_MAIN.equals(launch.getAction())
                && launch.hasCategory(Intent.CATEGORY_LAUNCHER)) {
            finish();
            return;
        }
        // A finished game left this process behind and Android reused it. The
        // game's native state cannot start twice in one process (missing menus),
        // so restart into a fresh process before going any further.
        if (GameActivity.gamesInProcess > 0 && !GameActivity.running) {
            startActivity(new Intent(this, MainActivity.class)
                    .putExtra(EXTRA_RESTARTED, true)
                    .addFlags(Intent.FLAG_ACTIVITY_NEW_TASK | Intent.FLAG_ACTIVITY_CLEAR_TASK));
            android.os.Process.killProcess(android.os.Process.myPid());
            return;
        }
        TextView status = new TextView(this);
        status.setText(getString(R.string.preparing) + "\n\n" + getString(R.string.android_port_credits));
        status.setGravity(android.view.Gravity.CENTER);
        setContentView(status);
        new Thread(() -> {
            try {
                prepareData();
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    startActivity(new Intent(this, GameActivity.class).putExtra(EXTRA_RESTARTED,
                            getIntent() != null && getIntent().getBooleanExtra(EXTRA_RESTARTED, false)));
                    finish();
                });
            } catch (IOException ex) {
                android.util.Log.e("SMW", "Asset extraction failed", ex);
                runOnUiThread(() -> {
                    if (isFinishing() || isDestroyed()) return;
                    new AlertDialog.Builder(this).setTitle(R.string.prepare_error)
                        .setMessage(ex.getMessage()).setPositiveButton(R.string.close, (d, w) -> finish()).show();
                });
            }
        }, "smw-assets").start();
    }

    private void prepareData() throws IOException {
        File game = new File(getFilesDir(), "game");
        File marker = new File(game, ".assets-" + BuildConfig.VERSION_CODE);
        if (marker.isFile()) return;
        // An interrupted copy is retried at next launch. Settings live separately in SDL's pref path.
        copyAssets(getAssets(), "data", new File(game, "data"));
        if (!marker.createNewFile()) throw new IOException("Cannot mark game assets ready");
    }

    private static void copyAssets(AssetManager assets, String source, File destination) throws IOException {
        String[] children = assets.list(source);
        if (children != null && children.length > 0) {
            if (!destination.isDirectory() && !destination.mkdirs())
                throw new IOException("Cannot create " + destination);
            for (String child : children) copyAssets(assets, source + "/" + child, new File(destination, child));
        } else {
            try (InputStream in = assets.open(source); OutputStream out = new FileOutputStream(destination)) {
                byte[] buffer = new byte[32768];
                int count;
                while ((count = in.read(buffer)) != -1) out.write(buffer, 0, count);
            }
        }
    }
}
