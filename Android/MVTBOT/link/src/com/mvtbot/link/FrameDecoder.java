package com.mvtbot.link;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;

/** Bounded BLE notification stream decoder for MiniBalan (LINK-002 / APP-REL-001). */
public final class FrameDecoder {
    public interface Listener {
        void onFrame(String frame);
        void onRejected(String reason);
    }

    private static final byte[] HEADER = { 'C', 'M', 'D', '|' };
    // The extra byte records the exact first byte causing an overlength rejection.
    private final byte[] pending = new byte[ProtocolValidation.MAX_FRAME_BYTES + 1];
    private final Listener listener;
    private int length;
    private int headerMatch;
    private boolean receiving;
    private long acceptedCount;
    private long rejectedCount;
    private long callbackFailureCount;
    private byte[] lastRejectedBytes = new byte[0];

    public FrameDecoder(Listener listener) {
        this.listener = listener;
    }

    public synchronized void feed(byte[] data) {
        feed(data, 0, data == null ? 0 : data.length);
    }

    /** Invalid caller arguments are rejected without throwing on the BLE callback thread. */
    public synchronized void feed(byte[] data, int offset, int count) {
        if (data == null || offset < 0 || count < 0 || offset > data.length - count) {
            reset();
            reject("invalid_input_range", 0);
            return;
        }
        for (int i = offset; i < offset + count; ++i) consume(data[i]);
    }

    /** Clear session state on disconnect; lifetime diagnostic counters/sample are retained. */
    public synchronized void reset() {
        length = 0;
        headerMatch = 0;
        receiving = false;
    }

    public synchronized long getAcceptedCount() { return acceptedCount; }
    public synchronized long getRejectedCount() { return rejectedCount; }
    public synchronized long getCallbackFailureCount() { return callbackFailureCount; }

    /** Exact bytes through the rejection point, bounded at 129 bytes; never UTF-8 repaired. */
    public synchronized byte[] getLastRejectedBytes() {
        return Arrays.copyOf(lastRejectedBytes, lastRejectedBytes.length);
    }

    private void consume(byte value) {
        headerMatch = value == HEADER[headerMatch] ? headerMatch + 1
                : value == HEADER[0] ? 1 : 0;
        if (!receiving) {
            if (headerMatch == HEADER.length) beginFrame();
            return;
        }

        pending[length++] = value;
        if (headerMatch == HEADER.length) {
            // A fresh header unambiguously replaces an unterminated corrupt frame.
            int rejectedLength = length - HEADER.length;
            byte[] rejected = Arrays.copyOf(pending, rejectedLength);
            beginFrame();
            reject("new_header_before_terminator", rejected);
            return;
        }

        int unsigned = value & 0xff;
        if (unsigned < 0x20 || unsigned > 0x7e) {
            int rejectedLength = length;
            reset();
            reject("non_ascii_or_control_byte", rejectedLength);
        } else if (length > ProtocolValidation.MAX_FRAME_BYTES) {
            int rejectedLength = length;
            receiving = false;
            length = 0;
            // Retain an incomplete header suffix for recovery at the size boundary.
            reject("frame_too_long", rejectedLength);
        } else if (value == '$') {
            int frameLength = length;
            String frame = new String(pending, 0, frameLength, StandardCharsets.US_ASCII);
            reset();
            if (ProtocolValidation.validTelemetry(frame)) {
                ++acceptedCount;
                if (listener != null) {
                    try {
                        listener.onFrame(frame);
                    } catch (RuntimeException callbackFailure) {
                        ++callbackFailureCount;
                    }
                }
            } else {
                reject("invalid_telemetry_fields", frameLength);
            }
        }
    }

    private void beginFrame() {
        System.arraycopy(HEADER, 0, pending, 0, HEADER.length);
        length = HEADER.length;
        headerMatch = 0;
        receiving = true;
    }

    private void reject(String reason, int sampleLength) {
        reject(reason, Arrays.copyOf(pending, sampleLength));
    }

    private void reject(String reason, byte[] sample) {
        ++rejectedCount;
        lastRejectedBytes = sample;
        if (listener != null) {
            try {
                listener.onRejected(reason);
            } catch (RuntimeException callbackFailure) {
                ++callbackFailureCount;
            }
        }
    }
}
