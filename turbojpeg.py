from ctypes import *
import numpy as np

class TurboJPEG(object):
    def __init__(self):
        self.lib = cdll.LoadLibrary("/usr/lib/x86_64-linux-gnu/libturbojpeg.so")
        self.handle = self.lib.tjInitDecompress()

    def decode(self, img, width, height):
        src_array  = np.frombuffer(img, dtype=np.uint8)
        src_addr   = src_array.ctypes.data_as(POINTER(c_ubyte))

        dst_array = np.empty([width, height, 3], dtype=np.uint8)
        dst_addr  = dst_array.ctypes.data_as(POINTER(c_ubyte))

        status = self.lib.tjDecompressToYUV2(self.handle, src_addr, src_array.size, dst_addr, width, 4, height, 2048)

        if status != 0:
            raise IOError(self.lib.tjGetErrorStr().decode())
        else:
            return dst_array


