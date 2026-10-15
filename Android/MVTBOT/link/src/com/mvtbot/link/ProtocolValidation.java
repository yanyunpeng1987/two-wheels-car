package com.mvtbot.link;

import java.util.regex.Pattern;

/** Strict validation for the unchanged MiniBalan ASCII protocol (LINK-002). */
public final class ProtocolValidation {
    public static final int MAX_FRAME_BYTES = 128;
    private static final Pattern INTEGER = Pattern.compile("[+-]?[0-9]+");
    private static final Pattern DECIMAL = Pattern.compile(
            "[+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?");

    private ProtocolValidation() { }

    /** Validate only MCU-to-phone telemetry; do not normalize or repair fields. */
    public static boolean validTelemetry(String frame) {
        String[] fields = fields(frame);
        if (fields == null) return false;
        switch (fields[0]) {
            case "4": return fields.length == 3 && integers(fields, 1);
            case "5":
            case "7": return fields.length == 2 && integers(fields, 1);
            case "6": return fields.length == 3 && finiteFloat(fields[1])
                    && finiteFloat(fields[2]);
            case "8": return fields.length == 8 && finiteFloat(fields[1])
                    && integers(fields, 2);
            default: return false;
        }
    }

    /** Validate the phone-to-MCU forms emitted by the MiniBalan pages. */
    public static boolean validCommand(String frame) {
        String[] fields = fields(frame);
        if (fields == null) return false;
        switch (fields[0]) {
            case "1": return fields.length == 2 && finiteFloat(fields[1]);
            case "2": return fields.length == 4 && oneOf(fields[1], 1, 3)
                    && integers(fields, 2);
            case "3": return fields.length == 3 && integers(fields, 1);
            case "4":
            case "5":
            case "6": return fields.length == 2 && oneOf(fields[1], 0, 1);
            case "7": return fields.length == 1;
            case "8": return fields.length == 1
                    || fields.length == 2 && oneOf(fields[1], 1, 2);
            default: return false;
        }
    }

    /** Return the two original CMD3 int32 values, or null for any invalid frame. */
    public static int[] motion(String frame) {
        String[] fields = fields(frame);
        if (fields == null || fields.length != 3 || !"3".equals(fields[0])) {
            return null;
        }
        Integer forward = integer(fields[1]);
        Integer turn = integer(fields[2]);
        return forward == null || turn == null ? null : new int[] { forward, turn };
    }

    private static String[] fields(String frame) {
        if (frame == null || frame.length() < 6 || frame.length() > MAX_FRAME_BYTES
                || !frame.startsWith("CMD|") || frame.charAt(frame.length() - 1) != '$') {
            return null;
        }
        for (int i = 0; i < frame.length(); ++i) {
            char c = frame.charAt(i);
            if (c < 0x20 || c > 0x7e) return null;
        }
        int end = frame.length() - 1;
        // Existing firmware emits a trailing '|'; accept its historical optional form too.
        if (frame.charAt(end - 1) == '|') --end;
        String[] fields = frame.substring(4, end).split("\\|", -1);
        for (String field : fields) {
            if (field.length() == 0) return null;
        }
        return fields;
    }

    private static boolean integers(String[] fields, int from) {
        for (int i = from; i < fields.length; ++i) {
            if (integer(fields[i]) == null) return false;
        }
        return true;
    }

    private static boolean oneOf(String field, int minimum, int maximum) {
        Integer value = integer(field);
        return value != null && value >= minimum && value <= maximum;
    }

    private static Integer integer(String value) {
        if (!INTEGER.matcher(value).matches()) return null;
        try {
            return Integer.valueOf(value);
        } catch (NumberFormatException invalid) {
            return null;
        }
    }

    private static boolean finiteFloat(String value) {
        if (!DECIMAL.matcher(value).matches()) return false;
        try {
            float parsed = Float.parseFloat(value);
            return !Float.isNaN(parsed) && !Float.isInfinite(parsed);
        } catch (NumberFormatException invalid) {
            return false;
        }
    }
}
