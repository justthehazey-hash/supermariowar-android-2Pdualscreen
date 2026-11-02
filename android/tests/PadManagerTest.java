package org.supermariowar.app;

import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;

/** Host-side tests for controller -> player assignment: run ./test-touch.sh. */
public final class PadManagerTest {
    private static int checks;
    private static void check(boolean condition, String message) {
        checks++;
        if (!condition) throw new AssertionError(message);
    }

    private static final class Recorder implements PadManager.Sink {
        final List<String> events = new ArrayList<>();
        final int[] held = new int[PadManager.SLOTS];
        public void button(int slot, int button, boolean down) {
            check(((held[slot] & (1 << button)) != 0) != down, "Unbalanced button " + button + " on P" + (slot + 1));
            held[slot] = down ? held[slot] | (1 << button) : held[slot] & ~(1 << button);
            events.add("P" + (slot + 1) + ":" + button + (down ? "+" : "-"));
        }
        public void detached(int slot) { events.add("P" + (slot + 1) + ":detach"); }
        public void used(int slot) {}
        void expect(String... expected) {
            check(events.equals(Arrays.asList(expected)), "Expected " + Arrays.toString(expected) + ", got " + events);
            events.clear();
        }
    }

    private static final int A = PadManager.KEYCODE_BUTTON_A;
    private static final String BI = PadManager.BUILT_IN;

    private static void press(PadManager m, String key, boolean builtIn, int device, int code, long t) {
        m.key(key, builtIn, device, code, true, t);
        m.key(key, builtIn, device, code, false, t);
    }

    public static void main(String[] args) {
        testFirstPressOrder();
        testBuiltInYieldsToExternal();
        testBuiltInSitsOutWhenFull();
        testNoReshuffleDuringMatch();
        testReconnectKeepsSlot();
        testSwapToPlayerOne();
        testSticksAndDpad();
        testBuiltInMultipleDevices();
        testTriggersAndExtras();
        testReleaseAll();
        System.out.println("Gamepad assignment: " + checks + " checks passed");
    }

    private static void testFirstPressOrder() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        for (int i = 0; i < 5; i++) press(m, "bt" + i, false, 10 + i, A, 0);
        r.expect("P1:4+", "P1:4-", "P2:4+", "P2:4-", "P3:4+", "P3:4-", "P4:4+", "P4:4-");
        check(m.slotOf("bt4") == -1, "A fifth controller is ignored");
        check(m.key("bt4", false, 14, A, true, 0), "...but its keys are still consumed");
        check(!m.key("bt0", false, 10, 62 /* SPACE */, true, 0), "Non-pad keys pass through");
        r.expect();
    }

    private static void testBuiltInYieldsToExternal() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        m.key(BI, true, 1, A, true, 0); // Portal button held while docking
        r.expect("P1:4+");
        press(m, "bt0", false, 10, A, 0);
        r.expect("P1:4-", "P1:4+", "P1:4-");
        check(m.owners().equals(Arrays.asList("bt0", BI, null, null)), "Built-in moves behind the pad: " + m.owners());
        m.key(BI, true, 1, A, false, 0);
        press(m, BI, true, 1, A, 0);
        r.expect("P2:4+", "P2:4-");
        press(m, "bt1", false, 11, A, 0);
        check(m.owners().equals(Arrays.asList("bt0", "bt1", BI, null)), "Second pad also outranks built-in: " + m.owners());
        r.events.clear();
    }

    private static void testBuiltInSitsOutWhenFull() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        press(m, BI, true, 1, A, 0);
        for (int i = 0; i < 4; i++) press(m, "bt" + i, false, 10 + i, A, 0);
        check(m.owners().equals(Arrays.asList("bt0", "bt1", "bt2", "bt3")), "Four pads fill the slots: " + m.owners());
        check(m.slotOf(BI) == -1, "Built-in sits out");
        r.events.clear();
        m.deviceRemoved(12);
        r.expect("P3:detach");
        press(m, BI, true, 1, A, 0);
        check(m.slotOf(BI) == 2, "Built-in takes the slot that opened");
    }

    private static void testNoReshuffleDuringMatch() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        press(m, BI, true, 1, A, 0);
        m.setInMatch(true);
        press(m, "bt0", false, 10, A, 0);
        check(m.owners().equals(Arrays.asList(BI, "bt0", null, null)), "No reshuffle mid-match: " + m.owners());
    }

    private static void testReconnectKeepsSlot() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        press(m, "bt0", false, 10, A, 0);
        press(m, "bt1", false, 11, A, 0);
        m.key("bt0", false, 10, PadManager.KEYCODE_DPAD_RIGHT, true, 0);
        r.events.clear();
        m.deviceRemoved(10); // controller fell asleep while holding right
        r.expect("P1:3-", "P1:detach");
        check(m.owners().equals(Arrays.asList(null, "bt1", null, null)), "Others keep their numbers: " + m.owners());
        press(m, "bt0", false, 20, A, 0); // reconnects with a new device id
        check(m.slotOf("bt0") == 0, "Reconnected controller is Player 1 again");
    }

    private static void testSwapToPlayerOne() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        press(m, "bt0", false, 10, A, 0);
        press(m, "bt1", false, 11, A, 0);
        press(m, "bt2", false, 12, A, 0);
        r.events.clear();
        m.key("bt2", false, 12, PadManager.KEYCODE_BUTTON_L1, true, 100);
        m.key("bt2", false, 12, PadManager.KEYCODE_BUTTON_R1, true, 150);
        check(m.swapPending(), "Hold started");
        m.tick(900);
        check(m.slotOf("bt2") == 2, "Not before a full second");
        m.tick(1150);
        check(m.owners().equals(Arrays.asList("bt2", "bt1", "bt0", null)), "Swapped with Player 1: " + m.owners());
        check(!m.swapPending(), "Swap fires once");
        m.tick(5000);
        check(m.slotOf("bt2") == 0, "Holding on does nothing more");
        m.key("bt2", false, 12, PadManager.KEYCODE_BUTTON_L1, false, 5000);
        m.key("bt2", false, 12, PadManager.KEYCODE_BUTTON_R1, false, 5000);
        check(r.held[0] == 0 && r.held[2] == 0, "No buttons stuck after the swap");

        // Swapping into an empty Player 1 frees the old slot.
        m.deviceRemoved(12);
        r.events.clear();
        m.key("bt1", false, 11, PadManager.KEYCODE_BUTTON_L1, true, 0);
        m.key("bt1", false, 11, PadManager.KEYCODE_BUTTON_R1, true, 0);
        m.tick(1000);
        check(m.slotOf("bt1") == 0, "Moved to free Player 1");
        check(r.events.contains("P2:detach"), "Old slot detached: " + r.events);
        m.releaseAll();

        // Not during a match.
        m.setInMatch(true);
        m.key("bt0", false, 10, PadManager.KEYCODE_BUTTON_L1, true, 0);
        m.key("bt0", false, 10, PadManager.KEYCODE_BUTTON_R1, true, 0);
        m.tick(2000);
        check(m.slotOf("bt0") == 2, "No swap mid-match");
    }

    private static void testSticksAndDpad() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        m.motion("bt0", false, 10, 0, 0, 0.1f, -0.2f, 0, 0, 0);
        check(m.slotOf("bt0") == -1, "A resting stick does not claim a player");
        m.motion("bt0", false, 10, 0, 0, 0.9f, 0, 0, 0, 0);
        r.expect("P1:3+");
        m.key("bt0", false, 10, PadManager.KEYCODE_DPAD_RIGHT, true, 0);
        r.expect(); // stick and d-pad share Right
        m.motion("bt0", false, 10, 0, 0, 0, 0, 0, 0, 0);
        r.expect();
        m.key("bt0", false, 10, PadManager.KEYCODE_DPAD_RIGHT, false, 0);
        r.expect("P1:3-");
        m.motion("bt0", false, 10, -1, 0, 0, 0, 0, 0, 0); // hat left
        m.motion("bt0", false, 10, 0, 1, 0, 0, 0, 0, 0);  // hat down
        r.expect("P1:2+", "P1:2-", "P1:1+");
        m.motion("bt0", false, 10, 0, 0, 0, -0.8f, 0, 0, 0); // stick up
        r.expect("P1:1-", "P1:0+");
    }

    private static void testBuiltInMultipleDevices() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        // Buttons on one internal device, sticks on another: still one player.
        m.key(BI, true, 1, A, true, 0);
        m.motion(BI, true, 2, 0, 0, -1, 0, 0, 0, 0);
        m.motion(BI, true, 1, 0, 0, 0, 0, 0, 0, 0); // device 1 has no stick; must not cancel device 2
        r.expect("P1:4+", "P1:2+");
        m.deviceRemoved(2);
        r.expect("P1:2-");
        check(m.slotOf(BI) == 0, "Still seated while another of its devices remains");
    }

    private static void testTriggersAndExtras() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        m.motion("bt0", false, 10, 0, 0, 0, 0, 0.9f, 0, 0);
        r.expect("P1:8+");
        m.key("bt0", false, 10, PadManager.KEYCODE_BUTTON_L2, true, 0);
        m.motion("bt0", false, 10, 0, 0, 0, 0, 0, 0, 0);
        r.expect(); // button L2 still holds L
        m.key("bt0", false, 10, PadManager.KEYCODE_BUTTON_L2, false, 0);
        r.expect("P1:8-");
        check(m.key("bt1", false, 11, PadManager.KEYCODE_BUTTON_THUMBL, true, 0), "Stick click consumed");
        check(m.slotOf("bt1") == -1, "...without claiming a player");
        m.key("bt0", false, 10, PadManager.KEYCODE_BACK, true, 0);
        r.expect("P1:11+"); // a pad's Back button acts as Select/back
    }

    private static void testReleaseAll() {
        Recorder r = new Recorder();
        PadManager m = new PadManager(r);
        m.key("bt0", false, 10, A, true, 0);
        m.motion("bt0", false, 10, 0, 0, 1, 0, 0, 0, 0);
        r.events.clear();
        m.releaseAll();
        check(r.held[0] == 0, "Focus loss releases everything");
        r.events.clear();
        m.key("bt0", false, 10, A, false, 0);
        r.expect(); // the late release produces nothing
        m.key("bt0", false, 10, A, true, 0);
        r.expect("P1:4+");
    }
}
