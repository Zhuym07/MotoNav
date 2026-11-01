package com.example.motonav;

/** Host-side checks for the CYD XPT2046 calibration mapping used by the firmware. */
public final class CydTouchMappingTest {
    private static int checks;

    public static void main(String[] args) {
        check(map(250, 250, false, false, false)[0] == 22, "default left edge");
        check(map(250, 250, false, false, false)[1] == 53, "default top edge");
        check(map(3850, 3850, false, false, false)[0] == 298, "default right edge");
        check(map(3850, 3850, false, false, false)[1] == 218, "default bottom edge");
        check(map(250, 3850, true, false, false)[0] == 298, "reverse X");
        check(map(250, 3850, false, true, false)[1] == 53, "reverse Y");
        int[] swapped = map(250, 3850, false, false, true);
        check(swapped[0] == 298 && swapped[1] == 53, "swapped axes");
        System.out.println("CydTouchMappingTest: " + checks + " checks passed");
    }

    private static int[] map(int rawX, int rawY, boolean reverseX, boolean reverseY, boolean swap) {
        if (swap) { int t = rawX; rawX = rawY; rawY = t; }
        int x = scale(rawX, 250, 3850, reverseX ? 298 : 22, reverseX ? 22 : 298);
        int y = scale(rawY, 250, 3850, reverseY ? 218 : 53, reverseY ? 53 : 218);
        return new int[]{x, y};
    }

    private static int scale(int value, int inMin, int inMax, int outMin, int outMax) {
        int result = (int)(((long)(value - inMin) * (outMax - outMin)) / (inMax - inMin) + outMin);
        return Math.max(Math.min(result, Math.max(outMin, outMax)), Math.min(outMin, outMax));
    }

    private static void check(boolean condition, String name) {
        checks++;
        if (!condition) throw new AssertionError(name);
    }
}
