package com.mvtbot.link;

import java.nio.charset.StandardCharsets;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.Random;

/** Dependency-free JVM regression suite; run with assertions or without -ea. */
public final class ProtocolTest {
    private static int assertions;
    private static final String PID = "CMD|8|-4.0|140|30|5|450|80|500|$";
    private static final String SPEED = "CMD|4|-2147483648|2147483647|$";
    private static final String DISTANCE = "CMD|5|100|$";
    private static final String WAVE = "CMD|6|-0.1|2.3|$";
    private static final String VOLTAGE = "CMD|7|11800|$";

    public static void main(String[] args) {
        testTelemetryValidation();
        testCommandValidation();
        testFragmentation();
        testNoiseAndRecovery();
        testInvalidBytesAndValues();
        testLengthBoundary();
        testResetAndDiagnostics();
        testInvalidArgumentsAndCallbacks();
        testBoundedRandomStreams();
        System.out.println("ProtocolTest: PASS (" + assertions + " assertions)");
    }

    private static void testTelemetryValidation() {
        for (String good : new String[] { PID, SPEED, DISTANCE, WAVE, VOLTAGE,
                "CMD|4|+0|-0$", "CMD|6|.2|-1.e-2|$", "CMD|8|1e1|0|0|0|0|0|0$" }) {
            check(ProtocolValidation.validTelemetry(good), "valid telemetry: " + good);
        }
        for (String bad : new String[] { null, "", "CMD|", "CMD|$", "CMD||$",
                "CMD|4|1|$", "CMD|4|1|2|3|$", "CMD|5||$", "CMD|5|1||$",
                "CMD|5|2147483648|$", "CMD|5|-2147483649|$", "CMD|5|1.0|$",
                "CMD|5| 1|$", "CMD|5|1 |$", "CMD|5|\uFF11|$", "CMD|5|1\u0000|$",
                "CMD|6|NaN|0|$", "CMD|6|Infinity|0|$", "CMD|6|-Infinity|0|$",
                "CMD|6|1e39|0|$", "CMD|6|0x1.0p0|0|$", "CMD|6|1f|0|$",
                "CMD|6|1e|0|$", "CMD|6|--1|0|$", "CMD|6|1\n|0|$",
                "CMD|7|1|2|$", "CMD|8|0|1|2|3|4|5|$", "CMD|8|0|1|2|3|4|5|6|7|$",
                "CMD|8|0|1|2|3|4|5|2147483648|$", "CMD|8|NaN|1|2|3|4|5|6|$",
                "CMD|3|0|0|$", "CMD|04|1|2|$", "CMD|4|1|2|$junk", "CMD|4|1|2|$$" }) {
            check(!ProtocolValidation.validTelemetry(bad), "reject telemetry: " + bad);
        }
    }

    private static void testCommandValidation() {
        for (String good : new String[] { "CMD|1|-4.0|$", "CMD|2|1|140|450|$",
                "CMD|2|2|30|80|$", "CMD|2|3|5|500|$", "CMD|3|-2147483648|2147483647|$",
                "CMD|3|+0|-0$", "CMD|4|1|$", "CMD|5|0|$", "CMD|6|1|$", "CMD|7|$",
                "CMD|7$", "CMD|8|1|$", "CMD|8|2|$", "CMD|8|$", "CMD|8$" }) {
            check(ProtocolValidation.validCommand(good), "valid command: " + good);
        }
        for (String bad : new String[] { null, "", "CMD|1|NaN|$", "CMD|1|1e40|$",
                "CMD|2|0|1|1|$", "CMD|2|4|1|1|$", "CMD|2|1|1|$", "CMD|2|1|1|2|3|$",
                "CMD|3|1|$", "CMD|3|1|0|0|$", "CMD|3|1|\u00000|$", "CMD|3|1x|0|$",
                "CMD|3|2147483648|0|$", "CMD|3|0|-2147483649|$", "CMD|3|1.0|0|$",
                "CMD|4|2|$", "CMD|5|-1|$", "CMD|6|1|1|$", "CMD|7|1|$", "CMD|7||$",
                "CMD|8|0|$", "CMD|8|3|$", "CMD|8||$", "CMD|8|1|2|$", "CMD|9|$" }) {
            check(!ProtocolValidation.validCommand(bad), "reject command: " + bad);
        }
        check(Arrays.equals(new int[] { Integer.MIN_VALUE, Integer.MAX_VALUE },
                ProtocolValidation.motion("CMD|3|-2147483648|2147483647|$")), "motion exact values");
        check(Arrays.equals(new int[] { 0, 0 }, ProtocolValidation.motion("CMD|3|+0|-0$")), "signed zero");
        check(ProtocolValidation.motion("CMD|3|0\u0000|0|$") == null, "motion NUL invalid");
        check(ProtocolValidation.motion("CMD|4|1|2|$") == null, "telemetry is not motion");
        check(ProtocolValidation.motion("CMD|3|1|2147483648|$") == null, "motion overflow invalid");
    }

    private static void testFragmentation() {
        // The existing firmware's default PID response exceeds a 20-byte BLE payload.
        String pid32 = PID;
        check(bytes(pid32).length == 32, "32-byte PID fixture");
        for (String frame : new String[] { pid32, PID, SPEED, WAVE, DISTANCE, VOLTAGE }) {
            byte[] input = bytes(frame);
            for (int split = 0; split <= input.length; ++split) {
                Capture c = new Capture();
                c.decoder.feed(input, 0, split);
                c.decoder.feed(input, split, input.length - split);
                equalFrames(c, frame);
                check(c.reasons.isEmpty(), "valid fragmentation rejected");
            }
            Capture c = new Capture();
            for (byte value : input) c.decoder.feed(new byte[] { value });
            equalFrames(c, frame);
        }
        Capture c = new Capture();
        c.decoder.feed(bytes(SPEED + DISTANCE + WAVE + VOLTAGE + PID));
        equalFrames(c, SPEED, DISTANCE, WAVE, VOLTAGE, PID);
    }

    private static void testNoiseAndRecovery() {
        Capture c = new Capture();
        c.decoder.feed(bytes("random$CCCMDCMC" + SPEED + "\r\n" + PID));
        equalFrames(c, SPEED, PID);
        c = new Capture();
        c.decoder.feed(bytes("CMD|4|1|" + WAVE + DISTANCE));
        equalFrames(c, WAVE, DISTANCE);
        check(c.reasons.equals(Arrays.asList("new_header_before_terminator")), "nested header recovery");
        c = new Capture();
        c.decoder.feed(bytes("CMD|4|1|2|"));
        c.decoder.feed(bytes("CM"));
        c.decoder.feed(bytes("D|5|100|$"));
        equalFrames(c, DISTANCE);
        c = new Capture();
        c.decoder.feed(bytes("CMD|4|1|x|$" + SPEED));
        equalFrames(c, SPEED);
        check(c.decoder.getRejectedCount() == 1, "bad numeric frame rejected once");
    }

    private static void testInvalidBytesAndValues() {
        for (String bad : new String[] { "CMD|4|1|-1\u0000|$", "CMD|4|1|-1\u0000$",
                "CMD|4|2147483648|1|$", "CMD|4|1|2|3|$", "CMD|6|NaN|1|$",
                "CMD|6|Infinity|1|$", "CMD|6|1e99|1|$", "CMD|8|1|2|3|4|5|6|$" }) {
            byte[] input = bytes(bad + DISTANCE);
            for (int split = 0; split <= input.length; ++split) {
                Capture c = new Capture();
                c.decoder.feed(input, 0, split);
                c.decoder.feed(input, split, input.length - split);
                equalFrames(c, DISTANCE);
                check(c.decoder.getRejectedCount() > 0, "bad frame must be diagnosed");
            }
        }
        for (byte invalid : new byte[] { 0, 1, 9, 10, 13, 0x7f, (byte) 0x80, (byte) 0xc0, (byte) 0xff }) {
            Capture c = new Capture();
            c.decoder.feed(bytes("CMD|4|1|"));
            c.decoder.feed(new byte[] { invalid });
            c.decoder.feed(bytes("2|$" + SPEED));
            equalFrames(c, SPEED);
            byte[] sample = c.decoder.getLastRejectedBytes();
            check(sample[sample.length - 1] == invalid, "preserve exact rejected byte");
        }
    }

    private static void testLengthBoundary() {
        String prefix = "CMD|6|";
        String suffix = "|0|$";
        String maximum = prefix + repeat('0', 128 - prefix.length() - suffix.length()) + suffix;
        check(maximum.length() == 128 && ProtocolValidation.validTelemetry(maximum), "128-byte valid frame");
        Capture c = new Capture();
        c.decoder.feed(bytes(maximum));
        equalFrames(c, maximum);
        c = new Capture();
        c.decoder.feed(bytes(prefix + "0" + maximum.substring(prefix.length()) + VOLTAGE));
        equalFrames(c, VOLTAGE);
        check(c.reasons.equals(Arrays.asList("frame_too_long")), "129-byte frame rejected");
        check(c.decoder.getLastRejectedBytes().length == 129, "overflow diagnostic bound");
        c = new Capture();
        c.decoder.feed(bytes("CMD|" + repeat('0', 123) + SPEED));
        equalFrames(c, SPEED);
        check(c.decoder.getRejectedCount() == 1, "header crossing capacity recovered");
        c = new Capture();
        c.decoder.feed(bytes("CMD|" + repeat('0', 20000) + PID));
        equalFrames(c, PID);
    }

    private static void testResetAndDiagnostics() {
        Capture c = new Capture();
        c.decoder.feed(bytes("CMD|4|1|"));
        c.decoder.reset();
        c.decoder.feed(bytes("2|$"));
        equalFrames(c);
        c.decoder.feed(bytes("CMD|4|1x|2|$"));
        byte[] original = c.decoder.getLastRejectedBytes();
        check(Arrays.equals(original, bytes("CMD|4|1x|2|$")), "retain raw complete bad frame");
        original[0] = 0;
        check(c.decoder.getLastRejectedBytes()[0] == 'C', "diagnostics defensive copy");
        c.decoder.reset();
        check(c.decoder.getRejectedCount() == 1, "reset retains diagnostics");
        c.decoder.feed(bytes(SPEED));
        equalFrames(c, SPEED);
        check(c.decoder.getAcceptedCount() == 1, "accepted count");
    }

    private static void testInvalidArgumentsAndCallbacks() {
        Capture c = new Capture();
        c.decoder.feed(null);
        c.decoder.feed(new byte[1], -1, 1);
        c.decoder.feed(new byte[1], 0, -1);
        c.decoder.feed(new byte[1], Integer.MAX_VALUE, 1);
        c.decoder.feed(new byte[1], 1, Integer.MAX_VALUE);
        c.decoder.feed(new byte[1], 1, 1);
        check(c.decoder.getRejectedCount() == 6, "invalid ranges diagnosed without throwing");
        c.decoder.feed(bytes(SPEED));
        equalFrames(c, SPEED);
        FrameDecoder throwing = new FrameDecoder(new FrameDecoder.Listener() {
            @Override public void onFrame(String frame) { throw new IllegalStateException("test listener"); }
            @Override public void onRejected(String reason) { throw new IllegalStateException("test listener"); }
        });
        throwing.feed(bytes(SPEED + "CMD|4|bad|0|$" + PID));
        check(throwing.getCallbackFailureCount() == 3, "callback runtime errors contained");
        check(throwing.getAcceptedCount() == 2, "processing continued after callback failure");
        new FrameDecoder(null).feed(bytes(SPEED));
    }

    private static void testBoundedRandomStreams() {
        Random random = new Random(0x05d);
        Capture c = new Capture();
        for (int i = 0; i < 1000; ++i) {
            byte[] noise = new byte[random.nextInt(513)];
            random.nextBytes(noise);
            c.decoder.feed(noise);
            c.decoder.feed(bytes(PID));
        }
        check(c.frames.size() == 1000, "resynchronization after random input");
        for (String frame : c.frames) {
            check(ProtocolValidation.validTelemetry(frame), "only validated frames delivered");
        }
    }

    private static byte[] bytes(String value) { return value.getBytes(StandardCharsets.US_ASCII); }
    private static String repeat(char value, int count) {
        char[] result = new char[count];
        Arrays.fill(result, value);
        return new String(result);
    }
    private static void equalFrames(Capture capture, String... expected) {
        check(capture.frames.equals(Arrays.asList(expected)), "frames expected "
                + Arrays.toString(expected) + " got " + capture.frames);
    }
    private static void check(boolean condition, String description) {
        ++assertions;
        if (!condition) throw new AssertionError(description);
    }
    private static final class Capture implements FrameDecoder.Listener {
        final List<String> frames = new ArrayList<String>();
        final List<String> reasons = new ArrayList<String>();
        final FrameDecoder decoder = new FrameDecoder(this);
        @Override public void onFrame(String frame) { frames.add(frame); }
        @Override public void onRejected(String reason) { reasons.add(reason); }
    }
}
