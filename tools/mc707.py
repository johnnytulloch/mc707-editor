"""Shared MC-707 SysEx helpers for the discovery scripts in this directory.

Ported from the patterns in Locriana/mcpoker (public domain) per docs/BRIEF.md §3/§10.
Ground truth for the message formats and the documented reply-address bug lives in
docs/BRIEF.md §3 -- read that before changing anything here.
"""

import sys
import threading
import time

import mido

ROLAND_ID = 0x41
DEVICE_ID = 0x10  # arbitrary, matches the known-working mcpoker example in the brief
MODEL_ID = (0x00, 0x00, 0x00, 0x5D)  # unverified until identity.py confirms it (brief §4)

RQ1_CMD = 0x11
DT1_CMD = 0x12

EXPECTED_IDENTITY_MODEL = (0x00, 0x00, 0x00, 0x5D)

# The one logging chokepoint every send/receive must go through (brief §10).
def _log(direction, data):
    ts = time.strftime("%H:%M:%S")
    hexstr = " ".join(f"{b:02X}" for b in data)
    print(f"[{ts}] {direction}: F0 {hexstr} F7", file=sys.stderr)


def checksum(address_and_data):
    """Roland checksum: (128 - (sum(address+data) mod 128)) mod 128."""
    return (128 - (sum(address_and_data) % 128)) % 128


def parse_hex_bytes(s):
    """Accepts '30 00 00 00', '30,00,00,00', or '30000000' -> bytes."""
    cleaned = s.replace(" ", "").replace(",", "")
    if len(cleaned) % 2 != 0:
        raise ValueError(f"odd number of hex digits in {s!r}")
    return bytes.fromhex(cleaned)


def build_identity_request_data():
    """Universal Non-Realtime Identity Request: F0 7E 7F 06 01 F7."""
    return (0x7E, 0x7F, 0x06, 0x01)


def build_rq1_data(address, size):
    """address, size: 4-byte sequences. Returns the sysex payload (no F0/F7)."""
    address = tuple(address)
    size = tuple(size)
    body = (ROLAND_ID, DEVICE_ID) + MODEL_ID + (RQ1_CMD,) + address + size
    chk = checksum(address + size)
    return body + (chk,)


def build_dt1_data(address, data):
    """address: 4-byte sequence, data: N-byte payload to write."""
    address = tuple(address)
    data = tuple(data)
    body = (ROLAND_ID, DEVICE_ID) + MODEL_ID + (DT1_CMD,) + address + data
    chk = checksum(address + data)
    return body + (chk,)


def parse_identity_reply(data):
    """data: mido sysex .data tuple (no F0/F7). Returns a dict of parsed fields.

    Standard Universal Non-Realtime Identity Reply:
    7E <device id> 06 02 <manufacturer> <family 2B> <model 2B> <version 4B>
    Roland uses a single-byte manufacturer ID (0x41), handled below; falls back
    to the 3-byte extended form (00 xx xx) if that's what comes back instead.
    """
    if len(data) < 4 or data[0] != 0x7E or data[2] != 0x06 or data[3] != 0x02:
        raise ValueError(f"not an identity reply: {data!r}")

    device_id = data[1]
    rest = data[4:]
    if rest and rest[0] == 0x00:
        manufacturer = rest[0:3]
        rest = rest[3:]
    else:
        manufacturer = rest[0:1]
        rest = rest[1:]

    family = rest[0:2]
    model = rest[2:4]
    version = rest[4:8]

    return {
        "device_id": device_id,
        "manufacturer": manufacturer,
        "family": family,
        "model": model,
        "version": version,
        "family_and_model": family + model,
    }


def parse_dt1_message(data):
    """data: mido sysex .data tuple (no F0/F7) for a DT1 command or reply.

    Layout: 41 <dev> 00 00 00 5D 12 <address 4B> <payload NB> <checksum 1B>
    """
    if len(data) < 7 + 4 + 1:
        raise ValueError(f"too short to be a DT1 message: {data!r}")

    manufacturer = data[0]
    device_id = data[1]
    model_id = data[2:6]
    cmd = data[6]
    address = data[7:11]
    payload_and_checksum = data[11:]
    payload = payload_and_checksum[:-1]
    received_checksum = payload_and_checksum[-1]

    return {
        "manufacturer": manufacturer,
        "device_id": device_id,
        "model_id": model_id,
        "cmd": cmd,
        "address": address,
        "data": payload,
        "checksum": received_checksum,
    }


def find_ports(name_substr="MC-707"):
    """Returns (input_name, output_name) for the first port whose name contains name_substr."""
    in_names = mido.get_input_names()
    out_names = mido.get_output_names()

    in_match = [n for n in in_names if name_substr.lower() in n.lower()]
    out_match = [n for n in out_names if name_substr.lower() in n.lower()]

    if not in_match:
        raise RuntimeError(f"No MIDI input matching {name_substr!r}. Available: {in_names}")
    if not out_match:
        raise RuntimeError(f"No MIDI output matching {name_substr!r}. Available: {out_names}")

    return in_match[0], out_match[0]


def open_ports(name_substr="MC-707"):
    in_name, out_name = find_ports(name_substr)
    print(f"Opening input {in_name!r} / output {out_name!r}", file=sys.stderr)
    inport = mido.open_input(in_name)
    outport = mido.open_output(out_name)
    return inport, outport


def safe_close(*ports, timeout=1.0):
    """Closes MIDI ports without risking a hang.

    python-rtmidi's Windows (WinMM) backend can occasionally block forever in
    close() if called soon after a sysex receive. Each close runs in a daemon
    thread with a timeout; if it doesn't finish in time we just move on --
    the caller is expected to force-exit the process afterward (see the
    __main__ blocks in the scripts here) so the handle gets reclaimed by the OS
    either way.
    """
    for port in ports:
        t = threading.Thread(target=port.close, daemon=True)
        t.start()
        t.join(timeout)


def drain(inport):
    """Discard any stale buffered messages before a new request."""
    for _ in inport.iter_pending():
        pass


def send_and_wait(inport, outport, sysex_data, timeout=2.0):
    """Sends a sysex payload (no F0/F7) and waits up to timeout seconds for a sysex reply.

    Returns the reply's .data tuple (no F0/F7), or None on timeout.
    """
    drain(inport)
    _log("SEND", sysex_data)
    outport.send(mido.Message("sysex", data=sysex_data))

    deadline = time.time() + timeout
    while time.time() < deadline:
        msg = inport.poll()
        if msg is not None and msg.type == "sysex":
            _log("RECV", msg.data)
            return tuple(msg.data)
        time.sleep(0.01)

    print(f"[{time.strftime('%H:%M:%S')}] TIMEOUT waiting for reply", file=sys.stderr)
    return None
