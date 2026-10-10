package org.burstlimit.android;

/** Preserve the user's choice across automatic hardware-controller takeover. */
final class ControlsPolicy {
    boolean enabled = true;
    boolean resumed;
    boolean gamepadConnected;
    boolean panelOpen;
    // A neutral player-one pad remains when controls are manually hidden, so
    // changing a display preference does not open the game's disconnect dialog.
    boolean attachTouchController() { return resumed && !gamepadConnected; }
    boolean showTouchControls() { return attachTouchController() && enabled && !panelOpen; }
}
