from twisted.internet.protocol import DatagramProtocol
from twisted.internet import reactor
from packet import Packet, State, NegotiatePacket, PacketType, AckPacket, FinishPacket
import time, sys

frames = {}

def get_us():
    return int(time.perf_counter() * 1e6)


class VideoClient(DatagramProtocol):
    def __init__(self):
        self.state = State.INITIAL
        self.previous_packet_time = 0

    def startProtocol(self):
        packet = NegotiatePacket(1920, 1080, 30)
        self.sendPacket(packet)

    def finishConnection(self):
        packet = FinishPacket()
        self.sendPacket(packet)

    def sendPacket(self, packet):
        self.transport.write(packet.encode(), ("127.0.0.1", 9999))

    def datagramReceived(self, data, addr):
        packet = Packet.decode(data)
        print(packet)
        if packet.type == PacketType.FRAME:
            if packet.frame_idx not in frames:
                frames[packet.frame_idx] = {}

            frames[packet.frame_idx][packet.sequence_idx] = packet.frame_data

            if len(frames[packet.frame_idx]) == packet.sequence_total:
                now = get_us()
                interarrival_time = now - self.previous_packet_time - packet.grace_period
                self.previous_packet_time = now
                whole_frame = b""
                for i in range(packet.sequence_total):
                    whole_frame += frames[packet.frame_idx][i]
                print(f"received frame {packet.frame_idx}", file=sys.stderr)
                sys.stdout.buffer.write(whole_frame)
                frames.pop(packet.frame_idx)
                res = AckPacket(packet.frame_idx, interarrival_time)
                self.sendPacket(res)

        elif packet.type == PacketType.RESPONSE:
            self.previous_packet_time = get_us()

        elif packet.type == PacketType.FINISH:
            reactor.stop()


f = open("out.264", "wb")
vc = VideoClient()
reactor.listenUDP(2601, vc)
reactor.addSystemEventTrigger('before', 'shutdown', vc.finishConnection)
reactor.run()
