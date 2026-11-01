package org.supermariowar.app;

import org.libsdl.app.SDLActivity;
import java.io.File;
import android.content.BroadcastReceiver;
import android.content.Context;
import android.content.Intent;
import android.content.IntentFilter;
import android.os.Build;
import android.os.Bundle;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import android.content.pm.ActivityInfo;
import android.hardware.input.InputManager;
import android.view.InputDevice;
import android.view.InputEvent;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.ViewGroup;

public class GameActivity extends SDLActivity {
    private TouchControlsView touchControls;
    private InputManager inputManager;
    private final PadManager pads = new PadManager(new PadManager.Sink() {
        @Override public void button(int slot, int button, boolean down) {
            nativePadButton(slot, button, down);
        }
        @Override public void detached(int slot) {
            nativePadDetach(slot);
            if (touchControls != null) touchControls.setPadActive(slot, false);
        }
        @Override public void used(int slot) {
            if (touchControls != null) touchControls.setPadActive(slot, true);
        }
    });
    private boolean swapCheckQueued;
    private final Runnable swapCheck = new Runnable() {
        @Override public void run() {
            swapCheckQueued = false;
            pads.tick(SystemClock.uptimeMillis());
            queueSwapCheck();
        }
    };
    private final InputManager.InputDeviceListener padListener = new InputManager.InputDeviceListener() {
        @Override public void onInputDeviceAdded(int deviceId) {}
        @Override public void onInputDeviceChanged(int deviceId) {}
        @Override public void onInputDeviceRemoved(int deviceId) {
            pads.deviceRemoved(deviceId);
        }
    };
    private final Handler modeHandler = new Handler(Looper.getMainLooper());
    private boolean portraitMode;
    // Read on the UI thread at a low rate. TouchEvent handling must never call JNI
    // for every finger or every movement: on some devices that stalls the input
    // queue long enough to look like a one-second loss of controls.
    private volatile boolean gameplay;
    private volatile boolean dpadUpAllowed = true;
    private final Runnable modePoll = new Runnable() {
        @Override public void run() {
            if (isFinishing() || isDestroyed()) return;
            boolean requested = nativeIsTwoPlayerPortrait();
            gameplay = nativeIsGameplay();
            dpadUpAllowed = nativeAllowsDpadUp();
            pads.setInMatch(gameplay);
            // Never rotate the Activity while a match is running. A transient
            // native player-state update must not drop window focus for both
            // touch controllers.
            if (gameplay && portraitMode && !requested) requested = true;
            if (requested != portraitMode) {
                portraitMode = requested;
                setRequestedOrientation(requested ? ActivityInfo.SCREEN_ORIENTATION_PORTRAIT
                        : ActivityInfo.SCREEN_ORIENTATION_FULL_SENSOR);
                if (touchControls != null) touchControls.setTwoPlayerMode(requested);
            }
            if (touchControls != null) touchControls.refreshMenuState(gameplay, dpadUpAllowed);
            modeHandler.postDelayed(this, 250);
        }
    };

    private native boolean nativeIsTwoPlayerPortrait();
    private native boolean nativeIsGameplay();
    private native boolean nativeAllowsDpadUp();
    // Controller input, already assigned to a player (0-3). Called only when a
    // button or direction changes, never per motion sample.
    private static native void nativePadButton(int player, int button, boolean down);
    private static native void nativePadDetach(int player);

    boolean allowsDpadUp() {
        return dpadUpAllowed;
    }

    boolean isGameplay() {
        return gameplay;
    }

    @Override protected void onCreate(Bundle state) {
        super.onCreate(state);
        // SDL can return early if native library loading fails.
        if (mLayout != null) {
            touchControls = new TouchControlsView(this);
            mLayout.addView(touchControls, new ViewGroup.LayoutParams(
                    ViewGroup.LayoutParams.MATCH_PARENT, ViewGroup.LayoutParams.MATCH_PARENT));
            modeHandler.post(modePoll);
            inputManager = (InputManager) getSystemService(Context.INPUT_SERVICE);
            if (inputManager != null) inputManager.registerInputDeviceListener(padListener, modeHandler);
        }
    }

    private static boolean hasGamepadSource(int sources) {
        return (sources & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD
                || (sources & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    /** Built-in handheld controls (e.g. AYN Odin 2 Portal) and Bluetooth/USB pads. */
    private static boolean fromGamepad(InputEvent event) {
        if (hasGamepadSource(event.getSource())) return true;
        // D-pad keys often arrive with SOURCE_DPAD|SOURCE_KEYBOARD; check the device itself.
        InputDevice device = event.getDevice();
        return device != null && !device.isVirtual() && hasGamepadSource(device.getSources());
    }

    /** Built-in controls rank after Bluetooth/USB pads; see PadManager. */
    private static boolean isBuiltIn(InputDevice device) {
        return device != null && Build.VERSION.SDK_INT >= 29 && !device.isExternal();
    }

    /** One key per physical controller: Android gives multi-part pads one descriptor. */
    private static String controllerKey(InputEvent event, boolean builtIn) {
        if (builtIn) return PadManager.BUILT_IN;
        InputDevice device = event.getDevice();
        String descriptor = device == null ? null : device.getDescriptor();
        return descriptor != null ? descriptor : "device:" + event.getDeviceId();
    }

    private void queueSwapCheck() {
        if (swapCheckQueued || !pads.swapPending()) return;
        swapCheckQueued = true;
        modeHandler.postDelayed(swapCheck, PadManager.SWAP_HOLD_MS + 20);
    }

    @Override public boolean dispatchKeyEvent(KeyEvent event) {
        int code = event.getKeyCode();
        if (touchControls != null && PadManager.isHandledKey(code) && fromGamepad(event)) {
            int action = event.getAction();
            if (action != KeyEvent.ACTION_DOWN && action != KeyEvent.ACTION_UP) return true;
            if (action == KeyEvent.ACTION_DOWN && event.getRepeatCount() > 0) return true;
            boolean builtIn = isBuiltIn(event.getDevice());
            if (pads.key(controllerKey(event, builtIn), builtIn, event.getDeviceId(), code,
                    action == KeyEvent.ACTION_DOWN, event.getEventTime())) {
                queueSwapCheck();
                return true;
            }
        }
        return super.dispatchKeyEvent(event);
    }

    private static float axis(MotionEvent event, int first, int second) {
        return Math.max(event.getAxisValue(first), event.getAxisValue(second));
    }

    @Override public boolean dispatchGenericMotionEvent(MotionEvent event) {
        if (touchControls != null && event.getActionMasked() == MotionEvent.ACTION_MOVE
                && (event.getSource() & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK) {
            boolean builtIn = isBuiltIn(event.getDevice());
            pads.motion(controllerKey(event, builtIn), builtIn, event.getDeviceId(),
                    event.getAxisValue(MotionEvent.AXIS_HAT_X), event.getAxisValue(MotionEvent.AXIS_HAT_Y),
                    event.getAxisValue(MotionEvent.AXIS_X), event.getAxisValue(MotionEvent.AXIS_Y),
                    axis(event, MotionEvent.AXIS_LTRIGGER, MotionEvent.AXIS_BRAKE),
                    axis(event, MotionEvent.AXIS_RTRIGGER, MotionEvent.AXIS_GAS),
                    event.getEventTime());
            queueSwapCheck();
            return true;
        }
        return super.dispatchGenericMotionEvent(event);
    }

    @Override protected void onPause() {
        pads.releaseAll();
        if (touchControls != null) touchControls.setInputActive(false);
        super.onPause();
    }

    @Override public void onWindowFocusChanged(boolean focused) {
        if (!focused) pads.releaseAll();
        if (touchControls != null) touchControls.setInputActive(focused);
        super.onWindowFocusChanged(focused);
    }

    @Override protected void onDestroy() {
        modeHandler.removeCallbacks(modePoll);
        modeHandler.removeCallbacks(swapCheck);
        if (inputManager != null) inputManager.unregisterInputDeviceListener(padListener);
        if (touchControls != null) touchControls.setInputActive(false);
        super.onDestroy();
    }

    // SDL2's USB permission receiver uses the older overload. Target API 35
    // requires an explicit export policy for its custom permission action.
    @Override public Intent registerReceiver(BroadcastReceiver receiver, IntentFilter filter) {
        if (Build.VERSION.SDK_INT >= 33) {
            return super.registerReceiver(receiver, filter, Context.RECEIVER_NOT_EXPORTED);
        }
        return super.registerReceiver(receiver, filter);
    }
    @Override protected String[] getLibraries() {
        return new String[] {"SDL2", "SDL2_image", "SDL2_mixer", "main"};
    }
    @Override protected String[] getArguments() {
        return new String[] {"--datadir", new File(getFilesDir(), "game/data").getAbsolutePath()};
    }
}
