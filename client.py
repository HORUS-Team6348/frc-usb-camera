from twisted.internet.protocol import DatagramProtocol
from twisted.internet import reactor
from datetime import datetime
from packet import Packet, State, NegotiatePacket, PacketType, AckPacket, FinishPacket, ChangePacket
import time
import sdl2.ext
import numpy as np
import av

frames = {}

def get_us():
    return int(time.perf_counter() * 1e6)


class VideoClient(DatagramProtocol):
    def __init__(self):
        self.state = State.INITIAL
        self.previous_packet_time = 0

    def startProtocol(self):
        packet = NegotiatePacket(1280, 720, 30)
        self.sendPacket(packet)

    def finishConnection(self):
        packet = FinishPacket()
        self.sendPacket(packet)

    def sendPacket(self, packet):
        self.transport.write(packet.encode(), ("192.168.43.165", 1188))

    def datagramReceived(self, data, addr):
        packet = Packet.decode(data)
        #print(packet)
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
                print(f"received frame {packet.frame_idx}")
                pkt = av.packet.Packet(whole_frame)
                raw_frame = h264_decoder.decode(pkt)[0].reformat(format="bgra").to_ndarray()
                raw_frame = np.rot90(raw_frame)
                raw_frame = np.flipud(raw_frame)
                raw_frame.shape = (1280, 720, 4)
                np.copyto(window_array, raw_frame)
                window.refresh()
                f.write(whole_frame)
                for event in sdl2.ext.get_events():
                    if event.type == sdl2.SDL_KEYDOWN:
                        if sdl2.SDL_GetKeyName(event.key.keysym.sym).lower() == b'a':
                            res = ChangePacket(0)
                            print("Changed camera to 0")
                            self.sendPacket(res)
                        elif sdl2.SDL_GetKeyName(event.key.keysym.sym).lower() == b'd':
                            res = ChangePacket(1)
                            print("Changed camera to 1")
                            self.sendPacket(res)
                frames.pop(packet.frame_idx)
                res = AckPacket(packet.frame_idx, interarrival_time)
                self.sendPacket(res)

        elif packet.type == PacketType.RESPONSE:
            self.previous_packet_time = get_us()

        elif packet.type == PacketType.FINISH:
            reactor.stop()

fn = datetime.now().isoformat().replace(":", "-")
f = open(f"videos/{fn}.264", "wb")
h264 = av.Codec("h264")
h264_decoder = av.CodecContext.create(h264)
sdl2.ext.init()
window = sdl2.ext.Window("frc-usb-camera", size=(1280, 720))
window.show()
window_surface = sdl2.SDL_GetWindowSurface(window.window)
window_array = sdl2.ext.pixels3d(window_surface.contents)
vc = VideoClient()
reactor.listenUDP(2601, vc)
reactor.addSystemEventTrigger('before', 'shutdown', vc.finishConnection)
reactor.run()
