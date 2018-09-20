import zmq
import struct
import random
import binascii
import enum

CURRENT_REQUEST_VERSION = 1


class RequestType(enum.IntEnum):
    ReadMemory = 1,
    WriteMemory = 2,
    PadState = 3,
    TouchState = 4,
    MotionState = 5,
    CircleState = 6,
    SetResolution = 7,
    SetGame = 8,
    SetOverrideControls = 9,
    Pause = 10,
    Resume = 11,
    Restart = 12,
    SetSpeedLimit = 13,
    SetBackgroundColor = 14


CITRA_PORT = "45987"


def BIT(n):  # https://github.com/smealum/ctrulib/blob/master/libctru/include/3ds/types.h#L46
    return (1 << n)


class Key(enum.IntEnum):
    A = BIT(0),             # A
    B = BIT(1),             # B
    Select = BIT(2),        # Select
    Start = BIT(3),         # Start
    DRight = BIT(4),        # D-Pad Right
    DLeft = BIT(5),         # D-Pad Left
    DUp = BIT(6),           # D-Pad Up
    DDown = BIT(7),         # D-Pad Down
    R = BIT(8),             # R
    L = BIT(9),             # L
    X = BIT(10),            # X
    Y = BIT(11),            # Y
    CircleRight = BIT(28),  # Circle Pad Right
    CircleLeft = BIT(29),   # Circle Pad Left
    CircleUp = BIT(30),     # Circle Pad Up
    CircleDown = BIT(31)    # Circle Pad Down


class Citra:
    def __init__(self, address="127.0.0.1", port=CITRA_PORT):
        self.context = zmq.Context()
        self.socket = self.context.socket(zmq.REQ)
        self.socket.connect("tcp://" + address + ":" + port)

    def is_connected(self):
        return self.socket is not None

    def _generate_header(self, request_type, data_size):
        request_id = random.getrandbits(32)
        return (struct.pack("IIII", CURRENT_REQUEST_VERSION, request_id, request_type, data_size), request_id)

    def _read_and_validate_header(self, raw_reply, expected_id, expected_type):
        reply_version, reply_id, reply_type, reply_data_size = struct.unpack(
            "IIII", raw_reply[:4*4])
        if (CURRENT_REQUEST_VERSION == reply_version and
            expected_id == reply_id and
            expected_type == reply_type and
                reply_data_size == len(raw_reply[4*4:])):
            return raw_reply[4*4:]
        return None

    def read_memory(self, read_address, read_size):
        request_data = struct.pack("II", read_address, read_size)
        request, request_id = self._generate_header(
            RequestType.ReadMemory, len(request_data))
        request += request_data
        self.socket.send(request)

        raw_reply = self.socket.recv()
        return self._read_and_validate_header(
            raw_reply, request_id, RequestType.ReadMemory)

    def write_memory(self, write_address, write_contents):
        request_data = struct.pack("II", write_address, len(write_contents))
        request_data += write_contents
        request, request_id = self._generate_header(
            RequestType.WriteMemory, len(request_data))
        request += RequestType.WriteMemory
        self.socket.send(request)
        self.socket.recv()

    def set_pad_state(self, pad_state):
        request_data = struct.pack("III", 0, 0, pad_state)
        request, request_id = self._generate_header(
            RequestType.PadState, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def set_touch_state(self, x, y, valid):
        request_data = struct.pack("IIhh?", 0, 0, x, y, valid)
        request, request_id = self._generate_header(
            RequestType.TouchState, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def set_motion_state(self, x, y, z, roll, pitch, yaw):
        request_data = struct.pack("IIhhhhhh", 0, 0, x, y, z, roll, pitch, yaw)
        request, request_id = self._generate_header(
            RequestType.MotionState, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def set_circle_state(self, x, y):
        request_data = struct.pack("IIhh", 0, 0, x, y)
        request, request_id = self._generate_header(
            RequestType.CircleState, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def set_resolution(self, resolution):
        request_data = struct.pack("IIh", 0, 0, resolution)
        request, request_id = self._generate_header(
            RequestType.SetResolution, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def set_game(self, path):
        request_data = struct.pack("II", 0, 0)
        request_data += str.encode(path)
        request, request_id = self._generate_header(
            RequestType.SetGame, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def set_override_controls(self, pad, touch, motion, circle):
        request_data = struct.pack("II????", 0, 0, pad, touch, motion, circle)
        request, request_id = self._generate_header(
            RequestType.SetOverrideControls, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def pause(self):
        request_data = struct.pack("II", 0, 0)
        request, request_id = self._generate_header(
            RequestType.Pause, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def resume(self):
        request_data = struct.pack("II", 0, 0)
        request, request_id = self._generate_header(
            RequestType.Resume, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def restart(self):
        request_data = struct.pack("II", 0, 0)
        request, request_id = self._generate_header(
            RequestType.Restart, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def set_speed_limit(self, speed_limit):
        request_data = struct.pack("IIh", 0, 0, speed_limit)
        request, request_id = self._generate_header(
            RequestType.SetSpeedLimit, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()

    def set_background_color(self, r, g, b):
        request_data = struct.pack("IIfff", 0, 0, r, g, b)
        request, request_id = self._generate_header(
            RequestType.SetBackgroundColor, len(request_data))
        request += request_data
        self.socket.send(request)
        self.socket.recv()
