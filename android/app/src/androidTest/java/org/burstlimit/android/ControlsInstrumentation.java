package org.burstlimit.android;

import android.app.Instrumentation;
import android.os.Bundle;
import android.os.SystemClock;
import android.view.InputDevice;
import android.view.MotionEvent;

/** Device tests without an additional test framework or network dependencies. */
public final class ControlsInstrumentation extends Instrumentation {
    private boolean setupTests;
    private int buttons;
    private float lx, ly, rx, ry, lt, rt;
    private TouchControlsView view;
    private void check(boolean condition, String message) {
        if (!condition) throw new AssertionError(message);
    }
    private void event(int action, int[] ids, float... xy) {
        MotionEvent.PointerProperties[] properties = new MotionEvent.PointerProperties[ids.length];
        MotionEvent.PointerCoords[] coords = new MotionEvent.PointerCoords[ids.length];
        for (int i=0; i<ids.length; ++i) {
            properties[i] = new MotionEvent.PointerProperties();
            properties[i].id = ids[i];
            properties[i].toolType = MotionEvent.TOOL_TYPE_FINGER;
            coords[i] = new MotionEvent.PointerCoords();
            coords[i].x = xy[i*2]; coords[i].y = xy[i*2+1];
            coords[i].pressure = 1; coords[i].size = 1;
        }
        long now = SystemClock.uptimeMillis();
        MotionEvent motion = MotionEvent.obtain(now, now, action, ids.length, properties,
                coords, 0, 0, 1, 1, 0, 0, InputDevice.SOURCE_TOUCHSCREEN, 0);
        view.onTouchEvent(motion);
        motion.recycle();
    }
    private void gestureTests() {
        view = new TouchControlsView(getTargetContext(), (b, x, y, rX, rY, lT, rT) -> {
            buttons=b; lx=x; ly=y; rx=rX; ry=rY; lt=lT; rt=rT;
        });
        view.layout(0, 0, 600, 560);
        event(MotionEvent.ACTION_DOWN, new int[]{7}, 104, 315);
        event(MotionEvent.ACTION_MOVE, new int[]{7}, 240, 315);
        check(lx == 1 && ly == 0, "Left stick clamps to full right");
        event(MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                new int[]{7, 19}, 240, 315, 496, 373);
        check(lx == 1 && buttons == 1, "Stick and A can be held together");
        event(MotionEvent.ACTION_POINTER_UP | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                new int[]{7, 19}, 240, 315, 496, 373);
        check(lx == 1 && buttons == 0, "A release preserves the other finger's stick");
        event(MotionEvent.ACTION_CANCEL, new int[]{7}, 240, 315);
        check(lx == 0 && buttons == 0 && lt == 0 && rt == 0, "Cancel releases all input");
        event(MotionEvent.ACTION_DOWN, new int[]{2}, 140, 429);
        check(buttons == ((1 << 11) | (1 << 14)), "Diagonal D-pad up/right");
        event(MotionEvent.ACTION_UP, new int[]{2}, 140, 429);
        check(buttons == 0, "D-pad releases");
        event(MotionEvent.ACTION_DOWN, new int[]{5}, 59, 190);
        check(lt == 1 && rt == 0, "Left trigger");
        event(MotionEvent.ACTION_POINTER_DOWN | (1 << MotionEvent.ACTION_POINTER_INDEX_SHIFT),
                new int[]{5, 6}, 59, 190, 541, 190);
        check(lt == 1 && rt == 1, "Independent simultaneous triggers");
        view.reset();
        check(lt == 0 && rt == 0, "Hide/pause reset clears held triggers");
        event(MotionEvent.ACTION_DOWN, new int[]{5}, 347, 505);
        check(buttons == (1 << 6), "Start button");
        view.layout(0, 0, 1000, 700);
        check(buttons == 0, "Resize clears input");
        view.setEnabled(false);
        event(MotionEvent.ACTION_DOWN, new int[]{5}, 496, 373);
        check(buttons == 0, "Disabled controls send no input");
    }
    private void policyTests() {
        ControlsPolicy policy = new ControlsPolicy();
        policy.resumed = true;
        check(policy.showTouchControls(), "Touch controls default on");
        policy.gamepadConnected = true;
        check(!policy.showTouchControls() && !policy.attachTouchController(), "Gamepad hotplug hides and detaches touch input");
        policy.gamepadConnected = false;
        check(policy.showTouchControls(), "Last gamepad disconnect restores touch controls");
        policy.enabled = false;
        policy.gamepadConnected = true;
        policy.gamepadConnected = false;
        check(!policy.showTouchControls(), "Disconnected gamepad respects manual disable");
        check(policy.attachTouchController(), "Hiding controls preserves a neutral player-one pad");
        policy.enabled = true;
        policy.panelOpen = true;
        check(!policy.showTouchControls() && policy.attachTouchController(), "Settings panel clears touch input without disconnecting player one");
        policy.panelOpen = false;
        policy.resumed = false;
        check(!policy.attachTouchController(), "Pause detaches touch input");
    }
    private void lanTests() {
        for (String valid : new String[] {"192.168.0.56", "10.0.0.1", "172.16.1.100"})
            check(LanSettings.validAddress(valid), "Valid LAN address: " + valid);
        for (String invalid : new String[] {"", "0.0.0.0", "224.0.0.1", "255.255.255.255",
                "192.168.0", "192.168.0.256", "192.168.0.13extra", "192.168.00.13", "localhost", "-1.2.3.4"})
            check(!LanSettings.validAddress(invalid), "Reject malformed/non-unicast address: " + invalid);
        check(!LanSettings.validAddress(null), "Missing address");
        check(LanSettings.validRole("host") && LanSettings.validRole("join") &&
                LanSettings.validRole("offline") && LanSettings.validRole("auto") &&
                !LanSettings.validRole("invalid"), "LAN roles");
        check(LanSettings.validGamertag("Goku 123") && LanSettings.validGamertag("123456789012345"), "Gamertag names");
        for (String invalid : new String[] {"", " ", "1234567890123456", "bad\nname", "é"})
            check(!LanSettings.validGamertag(invalid), "Reject invalid gamertag");
        android.content.SharedPreferences preferences = getTargetContext().getSharedPreferences("lan-policy-test", 0);
        preferences.edit().clear().putString("role", "join").putString("host", "192.168.0.148").commit();
        LanSettings.migrate(preferences);
        check("auto".equals(preferences.getString("role", "")) &&
                "192.168.0.148".equals(preferences.getString("host", "")), "Upgrade enables auto find and preserves optional manual address");
        preferences.edit().putString("role", "offline").commit();
        LanSettings.migrate(preferences);
        check("offline".equals(preferences.getString("role", "")), "Explicit offline selection survives later starts");
        preferences.edit().clear().commit();
    }
    @Override public void onCreate(Bundle arguments) {
        setupTests = arguments != null && "true".equals(arguments.getString("game_setup"));
        super.onCreate(arguments); start();
    }
    @Override public void onStart() {
        Bundle results = new Bundle();
        try {
            if (setupTests) results.putString("game_setup", GameSetupInstrumentation.runTests(getTargetContext()));
            Throwable[] gestureFailure = new Throwable[1];
            runOnMainSync(() -> {
                try { gestureTests(); } catch (Throwable failure) { gestureFailure[0] = failure; }
            });
            if (gestureFailure[0] != null) throw gestureFailure[0];
            policyTests();
            lanTests();
            results.putString("controls", "PASS: multitouch, sticks, D-pad, triggers, Start, cancel, resize, disable, pause and hotplug policy");
            results.putString("lan", "PASS: IPv4 validation and LAN roles");
            finish(android.app.Activity.RESULT_OK, results);
        } catch (Throwable failure) {
            results.putString("controls", "FAIL: " + failure);
            finish(android.app.Activity.RESULT_CANCELED, results);
        }
    }
}
