from twisted.internet.protocol import DatagramProtocol
from twisted.internet.task import LoopingCall
from twisted.internet import reactor
from packet import Packet, State, PacketType, FramePacket, ResponsePacket, AckPacket, FinishPacket
import numpy as np


clients = {}
MTU = 508 - 13
ACK_TIMEOUT = 300

class VideoServer(DatagramProtocol):
    def datagramReceived(self, data, addr):
        if addr not in clients:
            clients[addr] = {"state": State.INITIAL, "lastACK": self.frame_idx}

        self.handlePacket(data, addr)

    def startProtocol(self):
        self.frame_idx = 0
        self.loopObj = LoopingCall(self.sendVideoFrame)
        self.loopObj.start(1/30, now=False)

    def sendVideoFrame(self):
        try:
            frame_len = np.random.normal(15000, 1000, 1)
            frame     = np.random.bytes(frame_len)

            seq_total = int(frame_len / MTU)
            if frame_len % MTU != 0:
                seq_total += 1

            for seg_idx in range(seq_total):
                packet = FramePacket(self.frame_idx, seg_idx, seq_total, frame[seg_idx*MTU:(seg_idx+1)*MTU])
                for client in clients.keys():
                    if clients[client]["state"] == State.ESTABLISHED:
                        if clients[client]["lastACK"] + ACK_TIMEOUT < self.frame_idx:
                            clients[client]["state"] == State.TIMED_OUT
                        else:
                            self.sendPacket(packet, client)
            self.frame_idx += 1
        except Exception as err:
            print(err)

    def handlePacket(self, packet, client):
        packet = Packet.decode(packet)
        print(f"{self.logPrefix()}: received {packet} from {client}")

        if packet.type == PacketType.NEGOTIATION:
            if clients[client]["state"] not in [State.INITIAL, State.NEGOTIATION_FAILED]:
                clients[client]["state"] = State.INITIAL
                clients[client]["lastACK"] = self.frame_idx

            if packet.width == 1920 and packet.height == 1080:
                reply = ResponsePacket(True)
                clients[client]["state"] = State.ESTABLISHED
            else:
                reply = ResponsePacket(False)
                clients[client]["state"] = State.NEGOTIATION_FAILED

            self.sendPacket(reply, client)
        elif packet.type == PacketType.ACK:
            clients[client]["lastACK"] = packet.frame_idx
        elif packet.type == PacketType.FINISH:
            clients[client]["state"] = State.FINISHED
        else:
            return #dunno, mate. client fucked up.

    def sendPacket(self, packet, client):
        if packet.type == PacketType.FRAME and packet.sequence_idx+1 != packet.sequence_total:
            pass
        else:
            print(f"{self.logPrefix()}: answered to {client} with {packet}")
        self.transport.write(packet.encode(), client)


reactor.listenUDP(9999, VideoServer())
print("server listening on UDP port 9999...")
reactor.run()
