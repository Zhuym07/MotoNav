package com.example.motonav;

import java.io.ByteArrayOutputStream;

/** Downsamples the phone's 1200x1200 raster map to the CYD's internal-RAM map cache. */
public final class CydRasterPacket {
    private static final int SIZE = 220;
    private static final int HEADER = 16;

    private CydRasterPacket() {}

    public static byte[] downsample(byte[] input) {
        if (input == null) return null;
        // CYD understands the extended building classes (4/5). Do not run
        // the legacy downgrade here or the roof/outline pixels are lost.
        byte[] source = input;
        if (source.length < HEADER) return null;
        int frame = u16(source, 0);
        int x0 = i32(source, 2);
        int y0 = i32(source, 6);
        int width = u16(source, 10);
        int height = u16(source, 12);
        int flags = u16(source, 14);
        boolean palette = (flags & 0x8000) != 0;
        if (width == 0 || height == 0) return null;
        int start = palette ? 48 : HEADER;
        if (source.length < start) return null;

        byte[] pixels = new byte[SIZE * SIZE];
        int pos = start;
        int index = 0;
        int total = width * height;
        while (index < total) {
            if (pos >= source.length) return null;
            int runByte = source[pos++] & 0xff;
            int color = runByte & 15;
            int run = (runByte >>> 4) + 1;
            if ((runByte >>> 4) == 15) {
                if (pos >= source.length) return null;
                run = 16 + (source[pos++] & 0xff);
            }
            if (run > total - index) return null;
            int end = index + run;
            for (int src = index; src < end; src++) {
                int targetX = ((src % width) * SIZE) / width;
                int targetY = ((src / width) * SIZE) / height;
                pixels[targetY * SIZE + targetX] = (byte) color;
            }
            index = end;
        }
        if (index != total || pos != source.length) return null;

        ByteArrayOutputStream out = new ByteArrayOutputStream(HEADER + SIZE * SIZE / 2);
        write16(out, frame);
        write32(out, x0);
        write32(out, y0);
        write16(out, SIZE);
        write16(out, SIZE);
        int mm = Math.min(0x3fff, ((flags & 0x3fff) * Math.max(width, height) + SIZE / 2) / SIZE);
        write16(out, (flags & 0xc000) | mm);
        if (palette) out.write(source, 16, 32);
        for (int i = 0; i < pixels.length;) {
            int color = pixels[i] & 15;
            int run = 1;
            while (i + run < pixels.length && run < 271 && (pixels[i + run] & 15) == color) run++;
            if (run <= 15) out.write(((run - 1) << 4) | color);
            else {
                out.write(0xf0 | color);
                out.write(run - 16);
            }
            i += run;
        }
        return out.toByteArray();
    }

    private static int u16(byte[] b, int p) { return (b[p] & 255) | ((b[p + 1] & 255) << 8); }
    private static int i32(byte[] b, int p) { return u16(b, p) | (u16(b, p + 2) << 16); }
    private static void write16(ByteArrayOutputStream out, int n) { out.write(n); out.write(n >>> 8); }
    private static void write32(ByteArrayOutputStream out, int n) {
        write16(out, n);
        write16(out, n >>> 16);
    }
}
