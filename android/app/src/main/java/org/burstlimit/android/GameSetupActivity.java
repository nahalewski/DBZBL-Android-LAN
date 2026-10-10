package org.burstlimit.android;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.ParcelFileDescriptor;
import android.provider.OpenableColumns;
import android.view.Gravity;
import android.view.View;
import android.view.WindowManager;
import android.widget.Button;
import android.widget.LinearLayout;
import android.widget.ProgressBar;
import android.widget.TextView;
import java.io.File;
import java.util.Locale;
import java.util.concurrent.atomic.AtomicBoolean;

/** Native Android setup runs before SDL, Vulkan or the guest game starts. */
public final class GameSetupActivity extends Activity {
    private static final int PICK_GAME = 100;
    private Job job;
    private boolean resumed, started, pickerOpen;
    private TextView details;
    private ProgressBar progress;
    private Button choose, cancel;
    private final Handler handler = new Handler(Looper.getMainLooper());
    private final Runnable refresh = new Runnable() {
        @Override public void run() { render(); if (!isFinishing()) handler.postDelayed(this, 150); }
    };
    private static final class Job {
        final AtomicBoolean cancel = new AtomicBoolean();
        volatile boolean checking = true, busy, installed;
        boolean prompted;
        volatile long done, total;
        volatile String message = "Checking game files…";
        volatile String failure;
    }
    public static File dataRoot(android.content.Context context) {
        File root = context.getExternalFilesDir(null);
        return new File(root != null ? root : context.getFilesDir(), "game_data_root");
    }
    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        if (!BuildConfig.FLAVOR.equals("game")) { startGame(); return; }
        getWindow().addFlags(WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        pickerOpen = state != null && state.getBoolean("picker_open");
        job = (Job) getLastNonConfigurationInstance();
        boolean fresh = job == null;
        if (fresh) job = new Job();
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL); form.setGravity(Gravity.CENTER);
        int inset = Math.round(24 * getResources().getDisplayMetrics().density);
        form.setPadding(inset, inset, inset, inset);
        TextView title = new TextView(this); title.setText("Set up Burst Limit"); title.setTextSize(26);
        form.addView(title);
        TextView help = new TextView(this);
        help.setText("Choose your Dragon Ball Z: Burst Limit USA Xbox 360 ISO or a ZIP containing the extracted game.\n\nGame files are installed once in app storage. Your original file is kept. Setup needs about 3.4 GB of free space.");
        help.setTextSize(16); help.setPadding(0, inset, 0, inset);
        form.addView(help);
        details = new TextView(this); details.setTextSize(16); form.addView(details);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setMax(1000); form.addView(progress, new LinearLayout.LayoutParams(-1, inset));
        choose = new Button(this); choose.setText("Choose ISO or ZIP"); choose.setTag("choose-game-file");
        form.addView(choose); choose.setOnClickListener(v -> pickFile());
        cancel = new Button(this); cancel.setText("Cancel setup"); form.addView(cancel);
        cancel.setOnClickListener(v -> { job.cancel.set(true); cancel.setEnabled(false); });
        setContentView(form);
        if (fresh) {
            Job current = job; File root = dataRoot(this);
            new Thread(() -> {
                GameDataImporter.cleanInterruptedSetup(root);
                current.installed = GameDataImporter.verify(root);
                current.message = "Choose your USA Xbox 360 game file to begin.";
                current.checking = false;
            }, "BurstLimit-check-game").start();
        }
    }
    @Override public Object onRetainNonConfigurationInstance() { return job; }
    @Override protected void onSaveInstanceState(Bundle state) {
        state.putBoolean("picker_open", pickerOpen); super.onSaveInstanceState(state);
    }
    @Override protected void onResume() { super.onResume(); resumed = true; if (job != null) handler.post(refresh); }
    @Override protected void onPause() { resumed = false; handler.removeCallbacks(refresh); super.onPause(); }
    @Override protected void onDestroy() {
        handler.removeCallbacksAndMessages(null);
        if (isFinishing() && job != null && job.busy) job.cancel.set(true);
        super.onDestroy();
    }
    private void render() {
        if (job.installed) { if (resumed) startGame(); return; }
        details.setText(job.message);
        boolean working = job.checking || job.busy;
        progress.setVisibility(working ? View.VISIBLE : View.GONE);
        progress.setIndeterminate(job.total == 0);
        progress.setProgress(job.total == 0 ? 0 : (int) Math.min(1000, job.done * 1000 / job.total));
        choose.setEnabled(!working && !pickerOpen);
        cancel.setVisibility(job.busy ? View.VISIBLE : View.GONE);
        cancel.setEnabled(!job.cancel.get());
        if (resumed && !working && !job.prompted && !pickerOpen && job.failure == null) pickFile();
        if (resumed && job.failure != null) {
            String error = job.failure; job.failure = null;
            new AlertDialog.Builder(this).setTitle("Game setup incomplete").setMessage(error)
                    .setPositiveButton("Choose another file", (d, w) -> pickFile())
                    .setNegativeButton("Close", null).show();
        }
    }
    private void pickFile() {
        if (pickerOpen || job.checking || job.busy) return;
        job.prompted = true;
        Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT).addCategory(Intent.CATEGORY_OPENABLE)
                .setType("*/*").addFlags(Intent.FLAG_GRANT_READ_URI_PERMISSION);
        // ISO MIME types vary by file provider; filtering would hide valid .iso files.
        try { pickerOpen = true; startActivityForResult(intent, PICK_GAME); }
        catch (android.content.ActivityNotFoundException e) {
            pickerOpen = false; job.failure = "No file picker is available. Enable the Android Files app and try again.";
        }
    }
    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        if (request != PICK_GAME) return;
        pickerOpen = false;
        if (result != RESULT_OK || data == null || data.getData() == null) return;
        Uri uri = data.getData(); String name = "";
        try (Cursor cursor = getContentResolver().query(uri, new String[] {OpenableColumns.DISPLAY_NAME}, null, null, null)) {
            if (cursor != null && cursor.moveToFirst()) name = cursor.getString(0);
        } catch (RuntimeException ignored) { }
        final String extension = name == null ? "" : name.toLowerCase(Locale.ROOT);
        if (!extension.endsWith(".iso") && !extension.endsWith(".zip")) {
            job.failure = "Choose a finished .iso or a .zip containing the extracted USA game."; return;
        }
        final android.content.Context context = getApplicationContext(); File target = dataRoot(this); Job current = job;
        current.cancel.set(false); current.done = current.total = 0;
        current.message = "Opening game file…"; current.busy = true;
        new Thread(() -> {
            try (ParcelFileDescriptor descriptor = context.getContentResolver().openFileDescriptor(uri, "r")) {
                if (descriptor == null) throw new java.io.IOException("Couldn't open the selected file. Download it to this device and try again.");
                GameDataImporter.Progress report = (done, total, file) -> {
                    current.done = done; current.total = total;
                    current.message = total == 0 ? file : "Installing " + (done * 100 / total) + "%\n" + file;
                };
                if (extension.endsWith(".iso")) {
                    // Duplicate the descriptor so each wrapper owns exactly one FD.
                    try (ParcelFileDescriptor.AutoCloseInputStream input = new ParcelFileDescriptor.AutoCloseInputStream(
                            ParcelFileDescriptor.dup(descriptor.getFileDescriptor()))) {
                        GameDataImporter.importIso(input.getChannel(), target, current.cancel, report);
                    }
                } else {
                    GameDataImporter.importZip(() -> {
                        java.io.InputStream in = context.getContentResolver().openInputStream(uri);
                        if (in == null) throw new java.io.IOException("Couldn't reopen the selected ZIP.");
                        return in;
                    }, target, current.cancel, report);
                }
                current.message = "Game installed."; current.installed = true;
            } catch (Exception e) {
                current.failure = current.cancel.get() ? "Setup canceled. Your original game file was kept." :
                        e instanceof java.util.zip.ZipException ? "Couldn't read the ZIP. It may be incomplete or damaged. Download it to this device and try again." :
                        e.getMessage() == null ? "Couldn't read this game file. Download it to this device and try again." : e.getMessage();
                current.message = "Choose your game file to try again.";
            } finally { current.busy = false; }
        }, "BurstLimit-install-game").start();
    }
    private void startGame() {
        if (started) return;
        started = true;
        Intent game = new Intent(this, BurstLimitActivity.class);
        if (getIntent().getExtras() != null) game.putExtras(getIntent().getExtras());
        startActivity(game); finish();
    }
}
