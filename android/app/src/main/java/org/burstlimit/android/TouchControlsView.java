package org.burstlimit.android;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.RectF;
import android.util.SparseArray;
import android.view.MotionEvent;
import android.view.View;
import java.util.ArrayList;

/** Multitouch Xbox layout. Coordinates and Y axes follow SDL, not XInput. */
final class TouchControlsView extends View {
    interface InputSink {
        void submit(int buttons, float lx, float ly, float rx, float ry, float lt, float rt);
    }
    private static final int BUTTON = 0, LEFT_STICK = 1, RIGHT_STICK = 2,
            DPAD = 3, LEFT_TRIGGER = 4, RIGHT_TRIGGER = 5;
    private static final class Control {
        final String label;
        final int kind, button, color;
        final float x, y, radius, halfWidth;
        Control(String label, int kind, int button, float x, float y, float radius,
                float halfWidth, int color) {
            this.label = label; this.kind = kind; this.button = button;
            this.x = x; this.y = y; this.radius = radius;
            this.halfWidth = halfWidth; this.color = color;
        }
        boolean hit(float px, float py) {
            if (kind == DPAD) return Math.abs(px-x) <= radius && Math.abs(py-y) <= radius;
            if (halfWidth > radius) return Math.abs(px-x) <= halfWidth && Math.abs(py-y) <= radius;
            return Math.hypot(px-x, py-y) <= radius * 1.15f;
        }
    }
    private static final class Finger {
        final Control control;
        float x, y;
        Finger(Control control, float x, float y) { this.control = control; this.x = x; this.y = y; }
    }
    private final InputSink sink;
    private final Paint paint = new Paint(Paint.ANTI_ALIAS_FLAG);
    private final ArrayList<Control> controls = new ArrayList<>();
    private final SparseArray<Finger> fingers = new SparseArray<>();
    private float unit = 1;

    TouchControlsView(Context context, InputSink sink) {
        super(context);
        this.sink = sink;
        setFocusable(false);
        setContentDescription("Xbox touch controls: two sticks, D-pad, A B X Y, shoulders, triggers, Back and Start");
        setImportantForAccessibility(IMPORTANT_FOR_ACCESSIBILITY_NO);
    }
    private void button(String label, int button, float x, float y, float radius, float halfWidth, int color) {
        controls.add(new Control(label, BUTTON, button, x, y, radius, halfWidth, color));
    }
    @Override protected void onSizeChanged(int w, int h, int oldw, int oldh) {
        reset();
        controls.clear();
        // Size by available area and density; retain useful targets on the
        // Fold cover screen without making tablet controls excessively large.
        unit = Math.min(getResources().getDisplayMetrics().density, Math.min(w / 600f, h / 560f));
        float left = 104*unit, right = w-104*unit, row = h-245*unit;
        int white = Color.rgb(235, 242, 249);
        controls.add(new Control("L", LEFT_STICK, -1, left, row, 56*unit, 56*unit, white));
        controls.add(new Control("R", RIGHT_STICK, -1, right, h-95*unit, 48*unit, 48*unit, white));
        controls.add(new Control("", DPAD, -1, left, h-95*unit, 61*unit, 61*unit, white));
        button("A", 0, right, row+58*unit, 30*unit, 30*unit, Color.rgb(110, 218, 121));
        button("B", 1, right+58*unit, row, 30*unit, 30*unit, Color.rgb(248, 121, 114));
        button("X", 2, right-58*unit, row, 30*unit, 30*unit, Color.rgb(105, 177, 255));
        button("Y", 3, right, row-58*unit, 30*unit, 30*unit, Color.rgb(255, 220, 98));
        float shoulder = Math.max(90*unit, h-370*unit);
        controls.add(new Control("LT", LEFT_TRIGGER, -1, 59*unit, shoulder, 24*unit, 38*unit, white));
        button("LB", 9, 150*unit, shoulder, 24*unit, 38*unit, white);
        button("RB", 10, w-150*unit, shoulder, 24*unit, 38*unit, white);
        controls.add(new Control("RT", RIGHT_TRIGGER, -1, w-59*unit, shoulder, 24*unit, 38*unit, white));
        button("L3", 7, 218*unit, h-133*unit, 22*unit, 29*unit, white);
        button("R3", 8, w-218*unit, h-133*unit, 22*unit, 29*unit, white);
        button("Back", 4, w/2f-47*unit, h-55*unit, 23*unit, 40*unit, white);
        button("Start", 6, w/2f+47*unit, h-55*unit, 23*unit, 40*unit, white);
    }
    void reset() {
        fingers.clear();
        sink.submit(0, 0, 0, 0, 0, 0, 0);
        invalidate();
    }
    @Override protected void onDetachedFromWindow() { reset(); super.onDetachedFromWindow(); }
    @Override protected void onWindowVisibilityChanged(int visibility) {
        super.onWindowVisibilityChanged(visibility);
        if (visibility != VISIBLE && sink != null) reset();
    }
    private Control hit(float x, float y) {
        for (Control control : controls) if (control.hit(x, y)) return control;
        return null;
    }
    @Override public boolean onTouchEvent(MotionEvent event) {
        if (!isEnabled()) return false;
        int action = event.getActionMasked(), index = event.getActionIndex();
        if (action == MotionEvent.ACTION_DOWN || action == MotionEvent.ACTION_POINTER_DOWN) {
            Control control = hit(event.getX(index), event.getY(index));
            if (control != null) {
                // Each stick has one owner; buttons may have multiple owners.
                boolean owned = false;
                for (int i=0; i<fingers.size(); ++i)
                    if (fingers.valueAt(i).control == control) owned = true;
                if (!owned || control.kind == BUTTON)
                    fingers.put(event.getPointerId(index), new Finger(control, event.getX(index), event.getY(index)));
            }
            if (action == MotionEvent.ACTION_DOWN && control == null) return false;
        }
        if (action == MotionEvent.ACTION_CANCEL) { reset(); return true; }
        for (int i=0; i<event.getPointerCount(); ++i) {
            Finger finger = fingers.get(event.getPointerId(i));
            if (finger != null) { finger.x = event.getX(i); finger.y = event.getY(i); }
        }
        if (action == MotionEvent.ACTION_UP || action == MotionEvent.ACTION_POINTER_UP)
            fingers.remove(event.getPointerId(index));
        publish();
        invalidate();
        return true;
    }
    private float[] stick(Finger finger) {
        float x = (finger.x-finger.control.x)/finger.control.radius;
        float y = (finger.y-finger.control.y)/finger.control.radius;
        float length = (float)Math.hypot(x, y);
        if (length > 1) { x /= length; y /= length; }
        if (length < .12f) return new float[]{0, 0};
        return new float[]{x, y};
    }
    private int directions(Finger finger) {
        float dx = (finger.x-finger.control.x)/finger.control.radius;
        float dy = (finger.y-finger.control.y)/finger.control.radius;
        int mask = 0;
        if (dx < -.27f) mask |= 1 << 13;
        if (dx > .27f) mask |= 1 << 14;
        if (dy < -.27f) mask |= 1 << 11;
        if (dy > .27f) mask |= 1 << 12;
        return mask;
    }
    private void publish() {
        int buttons = 0;
        float lx=0, ly=0, rx=0, ry=0, lt=0, rt=0;
        for (int i=0; i<fingers.size(); ++i) {
            Finger finger = fingers.valueAt(i);
            switch (finger.control.kind) {
                case BUTTON:
                    if (finger.control.hit(finger.x, finger.y)) buttons |= 1 << finger.control.button;
                    break;
                case LEFT_STICK: { float[] axis=stick(finger); lx=axis[0]; ly=axis[1]; break; }
                case RIGHT_STICK: { float[] axis=stick(finger); rx=axis[0]; ry=axis[1]; break; }
                case DPAD: buttons |= directions(finger); break;
                case LEFT_TRIGGER: lt=finger.control.hit(finger.x, finger.y) ? 1 : 0; break;
                case RIGHT_TRIGGER: rt=finger.control.hit(finger.x, finger.y) ? 1 : 0; break;
            }
        }
        sink.submit(buttons, lx, ly, rx, ry, lt, rt);
    }
    private void shape(Canvas canvas, Control control, boolean pressed) {
        paint.setStyle(Paint.Style.FILL);
        paint.setColor(pressed ? Color.argb(185, 63, 83, 106) : Color.argb(106, 14, 22, 34));
        RectF rect = new RectF(control.x-control.halfWidth, control.y-control.radius,
                control.x+control.halfWidth, control.y+control.radius);
        if (control.halfWidth > control.radius) canvas.drawRoundRect(rect, 18*unit, 18*unit, paint);
        else canvas.drawCircle(control.x, control.y, control.radius, paint);
        paint.setStyle(Paint.Style.STROKE);
        paint.setStrokeWidth(1.5f*unit);
        paint.setColor(Color.argb(190, Color.red(control.color), Color.green(control.color), Color.blue(control.color)));
        if (control.halfWidth > control.radius) canvas.drawRoundRect(rect, 18*unit, 18*unit, paint);
        else canvas.drawCircle(control.x, control.y, control.radius, paint);
        paint.setStyle(Paint.Style.FILL);
    }
    private void text(Canvas canvas, String label, float x, float y, float size, int color) {
        paint.setColor(color); paint.setTextSize(size*unit); paint.setTextAlign(Paint.Align.CENTER);
        paint.setTypeface(android.graphics.Typeface.create("sans-serif-medium", 0));
        canvas.drawText(label, x, y-(paint.ascent()+paint.descent())/2, paint);
    }
    @Override protected void onDraw(Canvas canvas) {
        for (Control control : controls) {
            Finger active = null;
            for (int i=0; i<fingers.size(); ++i) if (fingers.valueAt(i).control == control) active=fingers.valueAt(i);
            if (control.kind == DPAD) {
                int bits = active == null ? 0 : directions(active);
                String[] labels={"↑", "↓", "←", "→"};
                int[] dx={0,0,-1,1}, dy={-1,1,0,0};
                for (int i=0; i<4; ++i) {
                    Control key = new Control(labels[i], BUTTON, 11+i,
                            control.x+dx[i]*36*unit, control.y+dy[i]*36*unit, 23*unit, 23*unit, control.color);
                    shape(canvas, key, (bits & (1 << (11+i))) != 0);
                    text(canvas, key.label, key.x, key.y, 22, key.color);
                }
                continue;
            }
            shape(canvas, control, active != null);
            if (control.kind == LEFT_STICK || control.kind == RIGHT_STICK) {
                float[] axis = active == null ? new float[]{0,0} : stick(active);
                float x=control.x+axis[0]*control.radius*.55f, y=control.y+axis[1]*control.radius*.55f;
                paint.setColor(Color.argb(130, 210, 226, 245));
                canvas.drawCircle(x, y, control.radius*.43f, paint);
                text(canvas, control.label, x, y, 16, Color.WHITE);
            } else text(canvas, control.label, control.x, control.y,
                    control.label.length()==1 ? 26 : 16, control.color);
        }
    }
}
