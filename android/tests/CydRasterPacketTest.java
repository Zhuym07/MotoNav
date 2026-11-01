package com.example.motonav;

import java.io.ByteArrayOutputStream;
import java.util.Arrays;

public final class CydRasterPacketTest {
    private static int checks;

    public static void main(String[] args) {
        byte[] input = raster(17, -1234, 5678, 1200, 1200, 2000, 2);
        byte[] output = CydRasterPacket.downsample(input);
        check(output != null, "valid raster is converted");
        check(u16(output, 0) == 17, "frame is preserved");
        check(i32(output, 2) == -1234 && i32(output, 6) == 5678, "origin is preserved");
        check(u16(output, 10) == 220 && u16(output, 12) == 220, "raster is bounded to CYD dimensions");
        check(u16(output, 14) == 10909, "meters per pixel scales with raster dimensions");
        int decodedClass = decode(output, 16, 220 * 220);
        check(decodedClass == 2, "resampled pixels retain their class (decoded " + decodedClass + ")");

        byte[] building = raster(18, 10, 20, 1200, 1200, 2000 | AmapBuildings.FLAG, 5);
        byte[] buildingOutput = CydRasterPacket.downsample(building);
        check(buildingOutput != null, "building raster is converted");
        check((u16(buildingOutput, 14) & AmapBuildings.FLAG) != 0, "building flag is preserved");
        check(decode(buildingOutput, 16, 220 * 220) == 5, "roof class is preserved");

        byte[] malformed = Arrays.copyOf(input, input.length - 1);
        check(CydRasterPacket.downsample(malformed) == null, "truncated raster is rejected");
        System.out.println("CydRasterPacketTest: " + checks + " checks passed");
    }

    private static byte[] raster(int frame, int x, int y, int width, int height, int mm, int color) {
        ByteArrayOutputStream out = new ByteArrayOutputStream();
        write16(out, frame); write32(out, x); write32(out, y);
        write16(out, width); write16(out, height); write16(out, mm);
        int remaining = width * height;
        while (remaining > 0) {
            int run = Math.min(271, remaining);
            if (run <= 15) out.write(((run - 1) << 4) | color);
            else { out.write(0xf0 | color); out.write(run - 16); }
            remaining -= run;
        }
        return out.toByteArray();
    }

    private static int decode(byte[] b, int pos, int expected) {
        int count = 0, color = -1;
        while (pos < b.length) {
            int token = b[pos++] & 255;
            int run = (token >>> 4) + 1;
            if ((token >>> 4) == 15) run = 16 + (b[pos++] & 255);
            if (color < 0) color = token & 15;
            if ((token & 15) != color) return -1;
            count += run;
        }
        return count == expected ? color : -1;
    }

    private static int u16(byte[] b, int p) { return (b[p] & 255) | ((b[p + 1] & 255) << 8); }
    private static int i32(byte[] b, int p) { return u16(b, p) | (u16(b, p + 2) << 16); }
    private static void write16(ByteArrayOutputStream out, int n) { out.write(n); out.write(n >>> 8); }
    private static void write32(ByteArrayOutputStream out, int n) { write16(out, n); write16(out, n >>> 16); }
    private static void check(boolean condition, String name) {
        checks++;
        if (!condition) throw new AssertionError(name);
    }
}
