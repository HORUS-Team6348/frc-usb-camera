from enum import Enum
import struct

class State(Enum):
    INITIAL            = 1
    NEGOTIATION_FAILED = 2
    ESTABLISHED        = 3
    FINISHED           = 4
    TIMED_OUT          = 5

class PacketType:
    NEGOTIATION = 1
    RESPONSE    = 2
    FRAME       = 3
    ACK         = 4
    FINISH      = 5
    CHANGE      = 6

class Packet:
    @staticmethod
    def decode(packet):
        packet_id = struct.unpack_from("<B", packet)[0]
        if packet_id == PacketType.NEGOTIATION:
            return NegotiatePacket.fromWirePacket(packet)
        elif packet_id == PacketType.RESPONSE:
            return ResponsePacket.fromWirePacket(packet)
        elif packet_id == PacketType.FRAME:
            return FramePacket.fromWirePacket(packet)
        elif packet_id == PacketType.ACK:
            return AckPacket.fromWirePacket(packet)
        elif packet_id == PacketType.FINISH:
            return FinishPacket.fromWirePacket(packet)
        elif packet_id == PacketType.CHANGE:
            return ChangePacket.fromWirePacket(packet)

class NegotiatePacket(Packet):
    def __init__(self, width, height, fps):
        self.type = PacketType.NEGOTIATION
        self.width = width
        self.height = height
        self.fps = fps

    @staticmethod
    def fromWirePacket(packet):
        type, width, height, fps = struct.unpack("!BHHH", packet)
        return NegotiatePacket(width, height, fps)

    def encode(self):
        return struct.pack("!BHHH", self.type, self.width, self.height, self.fps)

    def __repr__(self):
        return f"NegotiatePacket(width={self.width}, height={self.height}, fps={self.fps})"


class ResponsePacket(Packet):
    def __init__(self, success):
        self.type = PacketType.RESPONSE
        self.success = success


    @staticmethod
    def fromWirePacket(packet):
        success = struct.unpack("!BB", packet)[1]
        if success == 0:
            return ResponsePacket(False)
        else:
            return ResponsePacket(True)

    def encode(self):
        return struct.pack("!BB", self.type, self.success)

    def __repr__(self):
        return f"ResponsePacket(success={self.success})"




class FramePacket(Packet):
    def __init__(self, frame_idx, sequence_idx, sequence_total, frame_data, grace_period):
        self.type = PacketType.FRAME
        self.frame_idx = frame_idx
        self.sequence_idx = sequence_idx
        self.sequence_total = sequence_total
        self.grace_period = grace_period
        self.frame_data = frame_data


    @staticmethod
    def fromWirePacket(packet):
        type, frame_idx, sequence_idx, sequence_total, grace_period = struct.unpack_from("!BIIII", packet)
        frame_data = packet[17:]
        return FramePacket(frame_idx, sequence_idx, sequence_total, frame_data, grace_period)

    def encode(self):
        return struct.pack("!BIIII", self.type, self.frame_idx, self.sequence_idx, self.sequence_total, self.grace_period) + self.frame_data

    def __repr__(self):
        return f"FramePacket(frame_idx={self.frame_idx}, sequence_idx={self.sequence_idx}, sequence_total={self.sequence_total}, grace_period={self.grace_period}, data=len({len(self.frame_data)}))"


class AckPacket(Packet):
    def __init__(self, frame_idx, interarrival_time):
        self.type = PacketType.ACK
        self.frame_idx = frame_idx
        self.interarrival_time = interarrival_time

    @staticmethod
    def fromWirePacket(packet):
        type, frame_idx, interarrival_time = struct.unpack("!BIi", packet)
        return AckPacket(frame_idx, interarrival_time)

    def encode(self):
        return struct.pack("!BIi", self.type, self.frame_idx, self.interarrival_time)

    def __repr__(self):
        return f"AckPacket(frame_idx={self.frame_idx}, interarrival_time={self.interarrival_time})"

class FinishPacket(Packet):
    def __init__(self):
        self.type = PacketType.FINISH

    @staticmethod
    def fromWirePacket(packet):
        return FinishPacket()

    def encode(self):
        return struct.pack("!B", self.type)

    def __repr__(self):
        return f"FinishPacket()"

class ChangePacket(Packet):
    def __init__(self, camera_id):
        self.type = PacketType.CHANGE
        self.camera_id = camera_id

    @staticmethod
    def fromWirePacket(packet):
        type, camera_id = struct.unpack("!BB", packet)
        return ChangePacket(type, camera_id)

    def encode(self):
        return struct.pack("!BB", self.type, self.camera_id)

    def __repr__(self):
        return f"ChangePacket(camera_id={self})"
