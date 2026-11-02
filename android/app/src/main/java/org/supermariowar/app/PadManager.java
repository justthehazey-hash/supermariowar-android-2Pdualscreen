package org.supermariowar.app;

import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;

/**
 * Physical controllers -> player slots 1-4.
 *
 * Built-in handheld controls (AYN Odin 2 Portal) and Bluetooth/USB pads are all handled
 * here, before SDL sees them. Each controller reports abstract buttons for its player and
 * the game maps them per screen (menu vs. match), so no in-game "Controls" setup is needed.
 *
 * Slot rules, designed so several connected controllers need no fiddling:
 *  - A controller gets a player the first time any of its buttons/sticks is used.
 *  - It takes the slot it had before if that is still free (sleep/reconnect keeps P2 as P2).
 *  - Otherwise it takes the lowest free slot.
 *  - Built-in controls rank last: outside a match, an external pad that would otherwise get
 *    a higher number than the built-in controls takes the built-in's slot, and the built-in
 *    moves to the next free one (or sits out when all four are taken). Docked to a TV, the
 *    Bluetooth pads are therefore always Players 1, 2, ... even if the Portal was touched.
 *  - Holding L + R together for one second outside a match makes that controller Player 1
 *    (swapping with whoever had it).
 *  - Disconnecting frees the slot.
 *
 * Android-free so the host tests can drive it; times are in milliseconds.
 */
final class PadManager {
    static final int SLOTS = 4;
    static final String BUILT_IN = "built-in";
    static final long SWAP_HOLD_MS = 1000;

    // Abstract buttons understood by the native side (src/common/input.h, PAD_*).
    static final int UP = 0, DOWN = 1, LEFT = 2, RIGHT = 3;
    static final int A = 4, B = 5, X = 6, Y = 7, L = 8, R = 9, START = 10, SELECT = 11;
    static final int BUTTONS = 12;

    // android.view.KeyEvent key codes (stable public API values).
    static final int KEYCODE_BACK = 4;
    static final int KEYCODE_DPAD_UP = 19, KEYCODE_DPAD_DOWN = 20;
    static final int KEYCODE_DPAD_LEFT = 21, KEYCODE_DPAD_RIGHT = 22, KEYCODE_DPAD_CENTER = 23;
    static final int KEYCODE_BUTTON_A = 96, KEYCODE_BUTTON_B = 97, KEYCODE_BUTTON_C = 98;
    static final int KEYCODE_BUTTON_X = 99, KEYCODE_BUTTON_Y = 100, KEYCODE_BUTTON_Z = 101;
    static final int KEYCODE_BUTTON_L1 = 102, KEYCODE_BUTTON_R1 = 103;
    static final int KEYCODE_BUTTON_L2 = 104, KEYCODE_BUTTON_R2 = 105;
    static final int KEYCODE_BUTTON_THUMBL = 106, KEYCODE_BUTTON_THUMBR = 107;
    static final int KEYCODE_BUTTON_START = 108, KEYCODE_BUTTON_SELECT = 109;

    static final float DEAD_ZONE = 0.5f;
    /** Analog triggers past this count as L2/R2 presses (pads that report them only as axes). */
    static final float TRIGGER_THRESHOLD = 0.6f;

    interface Sink {
        void button(int slot, int button, boolean down);
        /** A slot no longer has any controller (disconnect or swap into a free slot). */
        void detached(int slot);
        /** A controller produced input on this slot (used to hide that player's touch controls). */
        void used(int slot);
    }

    private static final class Controller {
        final String key;
        final boolean builtIn;
        int slot = -1;
        final Set<Integer> devices = new HashSet<>();
        final Set<Integer> heldKeys = new HashSet<>();
        final Map<Integer, Integer> deviceAxes = new HashMap<>();  // device -> direction bits
        final Map<Integer, Integer> deviceTriggers = new HashMap<>(); // device -> L/R bits
        int emitted;          // abstract buttons currently reported down on 'slot'
        long swapSince = -1;  // when L+R became held together
        Controller(String key, boolean builtIn) { this.key = key; this.builtIn = builtIn; }
    }

    private final Sink sink;
    private final Controller[] slots = new Controller[SLOTS];
    private final Map<String, Controller> controllers = new HashMap<>();
    private final Map<Integer, Controller> byDevice = new HashMap<>();
    private final Map<String, Integer> lastSlot = new HashMap<>();
    private boolean inMatch;

    PadManager(Sink sink) { this.sink = sink; }

    void setInMatch(boolean value) { inMatch = value; }

    int slotOf(String key) {
        Controller c = controllers.get(key);
        return c == null ? -1 : c.slot;
    }

    static boolean isHandledKey(int keyCode) {
        return keyCode == KEYCODE_BACK || button(keyCode) >= -1;
    }

    /** Abstract button for a key code; -1 = consumed but unused; -2 = not a pad key. */
    static int button(int keyCode) {
        switch (keyCode) {
            case KEYCODE_DPAD_UP: return UP;
            case KEYCODE_DPAD_DOWN: return DOWN;
            case KEYCODE_DPAD_LEFT: return LEFT;
            case KEYCODE_DPAD_RIGHT: return RIGHT;
            case KEYCODE_BUTTON_A: case KEYCODE_BUTTON_C: case KEYCODE_DPAD_CENTER: return A;
            case KEYCODE_BUTTON_B: return B;
            case KEYCODE_BUTTON_X: case KEYCODE_BUTTON_Z: return X;
            case KEYCODE_BUTTON_Y: return Y;
            case KEYCODE_BUTTON_L1: case KEYCODE_BUTTON_L2: return L;
            case KEYCODE_BUTTON_R1: case KEYCODE_BUTTON_R2: return R;
            case KEYCODE_BUTTON_START: return START;
            case KEYCODE_BUTTON_SELECT: case KEYCODE_BACK: return SELECT;
            case KEYCODE_BUTTON_THUMBL: case KEYCODE_BUTTON_THUMBR: return -1;
            default: return -2;
        }
    }

    /** @return false when the key is not a gamepad key (caller lets Android/SDL have it). */
    boolean key(String key, boolean builtIn, int deviceId, int keyCode, boolean down, long now) {
        int b = button(keyCode);
        if (b == -2) return false;
        Controller c = controller(key, builtIn, deviceId);
        if (down) c.heldKeys.add(keyCode); else c.heldKeys.remove(keyCode);
        if (down && b != -1) claim(c);
        update(c, now);
        return true;
    }

    /** Hat, left stick and analog triggers for one device, axes in [-1, 1] / [0, 1]. */
    void motion(String key, boolean builtIn, int deviceId, float hatX, float hatY,
                float stickX, float stickY, float leftTrigger, float rightTrigger, long now) {
        Controller c = controller(key, builtIn, deviceId);
        int dirs = 0;
        if (hatX < -DEAD_ZONE || stickX < -DEAD_ZONE) dirs |= 1 << LEFT;
        if (hatX > DEAD_ZONE || stickX > DEAD_ZONE) dirs |= 1 << RIGHT;
        if (hatY < -DEAD_ZONE || stickY < -DEAD_ZONE) dirs |= 1 << UP;
        if (hatY > DEAD_ZONE || stickY > DEAD_ZONE) dirs |= 1 << DOWN;
        int triggers = 0;
        if (leftTrigger > TRIGGER_THRESHOLD) triggers |= 1 << L;
        if (rightTrigger > TRIGGER_THRESHOLD) triggers |= 1 << R;
        int before = c.deviceAxes.getOrDefault(deviceId, 0) | c.deviceTriggers.getOrDefault(deviceId, 0);
        c.deviceAxes.put(deviceId, dirs);
        c.deviceTriggers.put(deviceId, triggers);
        // Resting sticks report constantly; only movement past the dead zone claims a player.
        if (((dirs | triggers) & ~before) != 0) claim(c);
        update(c, now);
    }

    /** Re-check the L+R hold; call about SWAP_HOLD_MS after {@link #swapPending()}. */
    void tick(long now) {
        for (Controller c : new ArrayList<>(controllers.values())) update(c, now);
    }

    boolean swapPending() {
        for (Controller c : controllers.values()) if (c.swapSince >= 0) return true;
        return false;
    }

    void deviceRemoved(int deviceId) {
        Controller c = byDevice.remove(deviceId);
        if (c == null) return;
        c.devices.remove(deviceId);
        c.deviceAxes.remove(deviceId);
        c.deviceTriggers.remove(deviceId);
        if (!c.devices.isEmpty()) {
            update(c, -1);
            return;
        }
        controllers.remove(c.key);
        if (c.slot >= 0) {
            lastSlot.put(c.key, c.slot);
            int slot = c.slot;
            unseat(c);
            sink.detached(slot);
        }
    }

    /** Lifecycle: focus lost / paused. Releases everything; buttons must be pressed again. */
    void releaseAll() {
        for (Controller c : controllers.values()) {
            c.heldKeys.clear();
            c.deviceAxes.clear();
            c.deviceTriggers.clear();
            c.swapSince = -1;
            emit(c, 0);
        }
    }

    private Controller controller(String key, boolean builtIn, int deviceId) {
        Controller c = controllers.get(key);
        if (c == null) {
            c = new Controller(key, builtIn);
            controllers.put(key, c);
        }
        if (c.devices.add(deviceId)) byDevice.put(deviceId, c);
        return c;
    }

    private int lowestFree() {
        for (int i = 0; i < SLOTS; i++) if (slots[i] == null) return i;
        return -1;
    }

    private void claim(Controller c) {
        if (c.slot >= 0) return;
        Integer previous = lastSlot.get(c.key);
        if (previous != null && slots[previous] == null) {
            seat(c, previous);
            return;
        }
        int free = lowestFree();
        if (!c.builtIn && !inMatch) {
            for (int i = 0; i < SLOTS; i++) {
                Controller other = slots[i];
                if (other != null && other.builtIn && (free < 0 || i < free)) {
                    // The built-in controls give their number to the external pad.
                    unseat(other);
                    seat(c, i);
                    // With no free slot the built-in controls sit out until one opens.
                    if (free >= 0) seat(other, free);
                    return;
                }
            }
        }
        if (free >= 0) seat(c, free);
    }

    private void seat(Controller c, int slot) {
        slots[slot] = c;
        c.slot = slot;
        c.emitted = 0;
    }

    /** Releases everything the controller reported and frees its slot. */
    private void unseat(Controller c) {
        emit(c, 0);
        slots[c.slot] = null;
        c.slot = -1;
    }

    private void becomeFirst(Controller c) {
        if (c.slot <= 0) return;
        int from = c.slot;
        Controller other = slots[0];
        unseat(c);
        if (other != null) unseat(other);
        seat(c, 0);
        if (other != null) seat(other, from);
        else sink.detached(from);
    }

    private int held(Controller c) {
        int bits = 0;
        for (int keyCode : c.heldKeys) {
            int b = button(keyCode);
            if (b >= 0) bits |= 1 << b;
        }
        for (int v : c.deviceAxes.values()) bits |= v;
        for (int v : c.deviceTriggers.values()) bits |= v;
        return bits;
    }

    private void update(Controller c, long now) {
        int bits = held(c);
        final int swap = (1 << L) | (1 << R);
        if ((bits & swap) == swap && !inMatch && c.slot > 0) {
            if (c.swapSince < 0) c.swapSince = now;
            else if (now >= 0 && now - c.swapSince >= SWAP_HOLD_MS) {
                c.swapSince = -1;
                becomeFirst(c); // Now Player 1, so holding on cannot swap again.
            }
        } else if ((bits & swap) != swap) {
            c.swapSince = -1;
        }
        if (c.slot >= 0) emit(c, bits);
    }

    private void emit(Controller c, int bits) {
        if (c.slot < 0) {
            c.emitted = 0;
            return;
        }
        int released = c.emitted & ~bits, pressed = bits & ~c.emitted;
        c.emitted = bits;
        for (int b = 0; b < BUTTONS; b++)
            if ((released & (1 << b)) != 0) sink.button(c.slot, b, false);
        for (int b = 0; b < BUTTONS; b++)
            if ((pressed & (1 << b)) != 0) sink.button(c.slot, b, true);
        if (pressed != 0) sink.used(c.slot);
    }

    /** For tests and diagnostics: which controller key owns each slot (null = free). */
    List<String> owners() {
        List<String> out = new ArrayList<>();
        for (Controller c : slots) out.add(c == null ? null : c.key);
        return out;
    }
}
