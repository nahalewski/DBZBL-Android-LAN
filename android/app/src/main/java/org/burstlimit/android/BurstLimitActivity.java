package org.burstlimit.android;

import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.content.SharedPreferences;
import android.graphics.Color;
import android.graphics.Insets;
import android.hardware.input.InputManager;
import android.view.Gravity;
import android.view.InputDevice;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.CheckBox;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.TextView;
import android.widget.EditText;
import android.widget.Spinner;
import android.widget.ArrayAdapter;
import android.app.AlertDialog;
import android.widget.Toast;
import org.libsdl.app.SDLActivity;
import java.io.File;

public class BurstLimitActivity extends SDLActivity implements InputManager.InputDeviceListener {
    private static native boolean nativeSetTouchControlsEnabled(boolean enabled);
    private static native void nativeSetTouchState(int buttons, float lx, float ly,
            float rx, float ry, float lt, float rt);
    private static native boolean nativeConfigureLan(String localAddress, String hostAddress);
    private static native boolean nativeSetGamertag(String name);
    private static native String nativeGetGamertag();
    private final Handler controlsHandler = new Handler(Looper.getMainLooper());
    private final ControlsPolicy controlsPolicy = new ControlsPolicy();
    private final Runnable retryController = this::updateControls;
    private InputManager inputManager;
    private TouchControlsView touchControls;
    private LinearLayout controlsPanel;
    private CheckBox controlsToggle;
    private TextView controllerStatus;
    private Button controlsTab;
    private SharedPreferences controlsPreferences;
    private String activeLan = "LAN: automatic discovery";
    private TextView lanStatus;
    private TextView gamertagStatus;
    @Override protected String[] getLibraries() {
        return new String[] { "c++_shared", "SDL3", "rexruntime", "main" };
    }
    @Override protected String[] getArguments() {
        if (BuildConfig.FLAVOR.equals("probe")) {
            String peer = getIntent().getStringExtra("probe_udp_peer");
            String version = getIntent().getStringExtra("probe_lan_version");
            if (version != null && !version.isEmpty()) return new String[] {"--probe_lan_version=" + version};
            return LanSettings.validAddress(peer) ? new String[] {"--probe_udp_peer=" + peer} : new String[0];
        }
        File root = getExternalFilesDir(null);
        if (root == null) root = getFilesDir();
        java.util.ArrayList<String> arguments = new java.util.ArrayList<>(java.util.Arrays.asList(
            "--game_data_root=" + new File(root, "game_data_root").getAbsolutePath(),
            "--user_data_root=" + new File(root, "user_data").getAbsolutePath(),
            "--gpu_plugin=xenos", "--frame_rate=30",
            "--draw_resolution_scale_x=1", "--draw_resolution_scale_y=1",
            "--vsync=true",
            // The render-pass path renders the actual menus on Mali-G925;
            // dynamic rendering produced black post-title frames there.
            "--vulkan_dynamic_rendering=false"
        ));
        SharedPreferences settings = getSharedPreferences("lan", MODE_PRIVATE);
        String gamertag = getSharedPreferences("profile", MODE_PRIVATE).getString("gamertag", "");
        if (LanSettings.validGamertag(gamertag)) nativeSetGamertag(gamertag);
        String role = settings.getString("role", "auto");
        String host = "join".equals(role) ? settings.getString("host", "") : "";
        String local = "offline".equals(role) ? "" : LanSettings.localAddress();
        boolean configured = nativeConfigureLan(local, host);
        // Android currently implements the direct LAN path; the SDK's
        // internet lobby client is Windows-only.
        arguments.add("--online_mode=direct");
        arguments.add("--lan_discovery=" + !"offline".equals(role));
        if (BuildConfig.DEBUG && getIntent().getBooleanExtra("lan_trace_menu_state", false)) {
            arguments.add("--lan_trace_menu_state=true");
            arguments.add("--trace_guest_input=true");
        }
        if (!"offline".equals(role) && !local.isEmpty() && configured) {
            arguments.add("--online_input_delay=off");
            arguments.add("--online_tick_sleep=0");
            arguments.add("--online_fast_tick=true");
            activeLan = "join".equals(role) ? "LAN: host " + host : "LAN: auto find on " + local;
        } else {
            activeLan = "offline".equals(role) ? "Offline" : "LAN unavailable. Check Wi-Fi and restart the game.";
        }
        runOnUiThread(() -> { if (lanStatus != null) lanStatus.setText(activeLan); });
        runOnUiThread(() -> { if (gamertagStatus != null) gamertagStatus.setText("Gamertag: " + nativeGetGamertag()); });
        return arguments.toArray(new String[0]);
    }
    @Override public void onCreate(Bundle state) {
        if (BuildConfig.FLAVOR.equals("game")) LanSettings.migrate(getSharedPreferences("lan", MODE_PRIVATE));
        if (BuildConfig.FLAVOR.equals("game") && getIntent().hasExtra("lan_role")) {
            String role = getIntent().getStringExtra("lan_role");
            String host = getIntent().getStringExtra("lan_host");
            if (host == null) host = "";
            if (LanSettings.validRole(role) && (!"join".equals(role) || LanSettings.validAddress(host))) {
                getSharedPreferences("lan", MODE_PRIVATE).edit().putInt("settings_version", 2)
                        .putString("role", role).putString("host", host).commit();
            } else {
                Toast.makeText(this, "Invalid LAN role or host address", Toast.LENGTH_LONG).show();
            }
        }
        super.onCreate(state);
        getWindow().addFlags(android.view.WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);
        if (!BuildConfig.FLAVOR.equals("game")) return;
        controlsPreferences = getSharedPreferences("controls", MODE_PRIVATE);
        controlsPolicy.enabled = controlsPreferences.getBoolean("touch_enabled", true);
        inputManager = getSystemService(InputManager.class);
        inputManager.registerInputDeviceListener(this, controlsHandler);
        createControlsOverlay();
        refreshGamepads();
    }
    private int dp(float value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    private void createControlsOverlay() {
        FrameLayout overlay = new FrameLayout(this);
        overlay.setFocusable(false);
        overlay.setOnApplyWindowInsetsListener((view, insets) -> {
            Insets safe = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            view.setPadding(safe.left, safe.top, safe.right, safe.bottom);
            return insets;
        });
        mLayout.addView(overlay, new android.view.ViewGroup.LayoutParams(-1, -1));
        touchControls = new TouchControlsView(this, BurstLimitActivity::nativeSetTouchState);
        touchControls.setTag("touch-controls");
        touchControls.setVisibility(View.GONE);
        overlay.addView(touchControls, new FrameLayout.LayoutParams(-1, -1));
        controlsTab = new Button(this);
        controlsTab.setText("Controls ▾");
        controlsTab.setTextSize(14);
        controlsTab.setTextColor(Color.WHITE);
        controlsTab.setBackgroundTintList(android.content.res.ColorStateList.valueOf(Color.argb(210, 24, 34, 49)));
        controlsTab.setTag("controls-tab");
        controlsTab.setFocusable(false);
        FrameLayout.LayoutParams tabLayout = new FrameLayout.LayoutParams(dp(128), dp(48), Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        tabLayout.topMargin = dp(4);
        overlay.addView(controlsTab, tabLayout);
        controlsPanel = new LinearLayout(this);
        controlsPanel.setOrientation(LinearLayout.VERTICAL);
        controlsPanel.setPadding(dp(16), dp(8), dp(16), dp(16));
        controlsPanel.setBackgroundColor(Color.argb(242, 24, 34, 49));
        controlsPanel.setTag("controls-panel");
        controlsPanel.setVisibility(View.GONE);
        // Keep panel taps off the SDL surface even between its children.
        controlsPanel.setClickable(true);
        controlsToggle = new CheckBox(this);
        controlsToggle.setText("On-screen controls");
        controlsToggle.setTextColor(Color.WHITE);
        controlsToggle.setButtonTintList(android.content.res.ColorStateList.valueOf(Color.rgb(130, 194, 255)));
        controlsToggle.setMinHeight(dp(48));
        controlsToggle.setTag("controls-toggle");
        controlsToggle.setChecked(controlsPolicy.enabled);
        controlsPanel.addView(controlsToggle);
        controllerStatus = new TextView(this);
        controllerStatus.setTextSize(14);
        controllerStatus.setTextColor(Color.rgb(210, 220, 235));
        controllerStatus.setTag("controller-status");
        controlsPanel.addView(controllerStatus);
        Button lanButton = new Button(this);
        lanButton.setText("LAN play");
        lanButton.setTag("lan-settings");
        controlsPanel.addView(lanButton);
        lanStatus = new TextView(this);
        lanStatus.setTextColor(Color.rgb(210, 220, 235));
        lanStatus.setText(activeLan);
        lanStatus.setTag("lan-status");
        controlsPanel.addView(lanStatus);
        lanButton.setOnClickListener(view -> showLanSettings());
        Button gamertagButton = new Button(this);
        gamertagButton.setText("Gamertag");
        gamertagButton.setTag("gamertag-settings");
        controlsPanel.addView(gamertagButton);
        gamertagStatus = new TextView(this);
        gamertagStatus.setTextColor(Color.rgb(210, 220, 235));
        gamertagStatus.setText("Gamertag: " + nativeGetGamertag());
        gamertagStatus.setTag("gamertag-status");
        controlsPanel.addView(gamertagStatus);
        gamertagButton.setOnClickListener(view -> showGamertagSettings());
        FrameLayout.LayoutParams panelLayout = new FrameLayout.LayoutParams(dp(300), -2, Gravity.TOP | Gravity.CENTER_HORIZONTAL);
        panelLayout.topMargin = dp(54);
        overlay.addView(controlsPanel, panelLayout);
        controlsTab.setOnClickListener(view -> {
            controlsPolicy.panelOpen = !controlsPolicy.panelOpen;
            touchControls.reset();
            updateControls();
            mSurface.requestFocus();
        });
        controlsToggle.setOnCheckedChangeListener((button, checked) -> {
            controlsPolicy.enabled = checked;
            controlsPreferences.edit().putBoolean("touch_enabled", checked).apply();
            touchControls.reset();
            updateControls();
            mSurface.requestFocus();
        });
        overlay.requestApplyInsets();
    }
    private void showGamertagSettings() {
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(20), dp(8), dp(20), dp(8));
        TextView help = new TextView(this);
        help.setText("Choose a name using 1–15 letters, numbers or printable symbols. Applies immediately. Players already in a lobby may see the old name until you rejoin.");
        form.addView(help);
        EditText name = new EditText(this);
        name.setSingleLine(true);
        name.setHint("Gamertag");
        name.setText(nativeGetGamertag());
        name.setFilters(new android.text.InputFilter[] {new android.text.InputFilter.LengthFilter(15)});
        form.addView(name);
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("Gamertag").setView(form)
                .setPositiveButton("Save", null).setNegativeButton("Cancel", null).create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            String value = name.getText().toString().trim();
            if (!LanSettings.validGamertag(value)) {
                name.setError("Use 1–15 printable characters");
                return;
            }
            if (!nativeSetGamertag(value)) {
                name.setError("Could not apply this name. Try again.");
                return;
            }
            getSharedPreferences("profile", MODE_PRIVATE).edit().putString("gamertag", value).apply();
            gamertagStatus.setText("Gamertag: " + value);
            dialog.dismiss();
        }));
        dialog.setOnDismissListener(ignored -> mSurface.requestFocus());
        dialog.show();
    }
    private void showLanSettings() {
        SharedPreferences settings = getSharedPreferences("lan", MODE_PRIVATE);
        LinearLayout form = new LinearLayout(this);
        form.setOrientation(LinearLayout.VERTICAL);
        form.setPadding(dp(20), dp(8), dp(20), dp(8));
        TextView instructions = new TextView(this);
        instructions.setText("Use the same Wi-Fi on both devices.\nThis device: " + LanSettings.localAddress() +
                "\nHost: Online Battle → Player Match → Create Match, with Private Session off.\n" +
                "Join: Player Match → Custom Match automatically finds nearby rooms by host name. Both players select Ready.");
        form.addView(instructions);
        Spinner role = new Spinner(this);
        role.setAdapter(new ArrayAdapter<>(this, android.R.layout.simple_spinner_dropdown_item,
                new String[] {"LAN: automatic discovery", "Offline", "LAN: manual host address"}));
        String savedRole = settings.getString("role", "auto");
        role.setSelection("join".equals(savedRole) ? 2 : "offline".equals(savedRole) ? 1 : 0);
        form.addView(role);
        EditText host = new EditText(this);
        host.setHint("Host IPv4 address (manual mode only)");
        host.setSingleLine(true);
        host.setInputType(android.text.InputType.TYPE_CLASS_PHONE);
        host.setText(settings.getString("host", ""));
        form.addView(host);
        host.setVisibility(role.getSelectedItemPosition() == 2 ? View.VISIBLE : View.GONE);
        role.setOnItemSelectedListener(new android.widget.AdapterView.OnItemSelectedListener() {
            @Override public void onItemSelected(android.widget.AdapterView<?> parent, View view, int position, long id) {
                host.setVisibility(position == 2 ? View.VISIBLE : View.GONE);
            }
            @Override public void onNothingSelected(android.widget.AdapterView<?> parent) { }
        });
        AlertDialog dialog = new AlertDialog.Builder(this).setTitle("LAN play")
                .setView(form).setPositiveButton("Save for next launch", null)
                .setNegativeButton("Cancel", null).create();
        dialog.setOnShowListener(ignored -> dialog.getButton(AlertDialog.BUTTON_POSITIVE).setOnClickListener(view -> {
            String choice = new String[] {"auto", "offline", "join"}[role.getSelectedItemPosition()];
            String address = host.getText().toString().trim();
            if ("join".equals(choice) && !LanSettings.validAddress(address)) {
                host.setError("Enter the host's IPv4 address");
                return;
            }
            settings.edit().putString("role", choice).putString("host", address).apply();
            lanStatus.setText("Saved. Restart the game to apply LAN settings.");
            dialog.dismiss();
        }));
        dialog.setOnDismissListener(ignored -> mSurface.requestFocus());
        dialog.show();
    }
    private void refreshGamepads() {
        if (touchControls == null) return;
        String name = null;
        for (int id : InputDevice.getDeviceIds()) {
            InputDevice device = InputDevice.getDevice(id);
            if (device != null && !device.isVirtual() &&
                    (device.supportsSource(InputDevice.SOURCE_GAMEPAD) || device.supportsSource(InputDevice.SOURCE_JOYSTICK))) {
                name = device.getName();
                break;
            }
        }
        boolean connected = name != null;
        if (controlsPolicy.gamepadConnected != connected) touchControls.reset();
        controlsPolicy.gamepadConnected = connected;
        controllerStatus.setText(connected ? "Gamepad connected: " + name + "\nTouch controls are hidden automatically." :
                "USB and Bluetooth gamepads connect automatically. Touch controls return when the gamepad disconnects.");
        updateControls();
    }
    private void updateControls() {
        if (touchControls == null) return;
        controlsHandler.removeCallbacks(retryController);
        boolean wanted = controlsPolicy.attachTouchController();
        boolean ready = nativeSetTouchControlsEnabled(wanted);
        boolean visible = controlsPolicy.showTouchControls() && ready;
        if (!visible) touchControls.reset();
        touchControls.setEnabled(visible);
        touchControls.setVisibility(visible ? View.VISIBLE : View.GONE);
        controlsPanel.setVisibility(controlsPolicy.panelOpen ? View.VISIBLE : View.GONE);
        controlsTab.setText(controlsPolicy.panelOpen ? "Controls ▴" : "Controls ▾");
        if (wanted && !ready) controlsHandler.postDelayed(retryController, 200);
    }
    @Override public void onInputDeviceAdded(int id) { refreshGamepads(); }
    @Override public void onInputDeviceRemoved(int id) { refreshGamepads(); }
    @Override public void onInputDeviceChanged(int id) { refreshGamepads(); }
    @Override protected void onResume() {
        super.onResume();
        controlsPolicy.resumed = true;
        refreshGamepads();
    }
    @Override protected void onPause() {
        controlsPolicy.resumed = false;
        controlsPolicy.panelOpen = false;
        if (touchControls != null) { touchControls.reset(); updateControls(); }
        super.onPause();
    }
    @Override protected void onDestroy() {
        controlsHandler.removeCallbacksAndMessages(null);
        if (inputManager != null) inputManager.unregisterInputDeviceListener(this);
        if (touchControls != null) { touchControls.reset(); nativeSetTouchControlsEnabled(false); }
        super.onDestroy();
    }
}
