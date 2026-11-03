"""INHFS/1 framing. No pyserial dependency needed for protocol/host tests."""
import struct
import zlib

HEADER = struct.Struct('<2sBBIIIHH')
MAX_PAYLOAD = 512
HELLO, INFO, LIST, PUT_BEGIN, PUT_DATA, PUT_END, GET_BEGIN, GET_DATA, DELETE, WIPE, SLEEP_GET, SLEEP_SET, CANCEL, BYE, PING = range(1, 16)


def packet(op, seq, block=0, session=0, data=b'', status=0):
    if len(data) > MAX_PAYLOAD:
        raise ValueError('Пакет слишком велик')
    raw = HEADER.pack(b'IF', 1, op, seq, block, session, len(data), status) + data
    return raw + struct.pack('<I', zlib.crc32(raw))


def unpack(raw):
    if not 24 <= len(raw) <= 536:
        raise ValueError('Длина кадра')
    magic, version, op, seq, block, session, size, status = HEADER.unpack(raw[:20])
    if magic != b'IF' or version != 1 or size > 512 or len(raw) != 24 + size:
        raise ValueError('Заголовок кадра')
    if zlib.crc32(raw[:-4]) != struct.unpack('<I', raw[-4:])[0]:
        raise ValueError('CRC кадра')
    return op, seq, block, session, status, raw[20:-4]


def frame(raw):
    return b'\xc0' + raw.replace(b'\xdb', b'\xdb\xdd').replace(b'\xc0', b'\xdb\xdc') + b'\xc0'


class Decoder:
    def __init__(self):
        self.data = bytearray()
        self.escape = False
        self.discard = True

    def feed(self, data):
        result = []
        for c in data:
            if c == 0xc0:
                if not self.discard and not self.escape and self.data:
                    result.append(bytes(self.data))
                self.data.clear()
                self.escape = False
                self.discard = False
            elif not self.discard:
                if self.escape:
                    self.escape = False
                    if c == 0xdc:
                        c = 0xc0
                    elif c == 0xdd:
                        c = 0xdb
                    else:
                        self.discard = True
                        continue
                elif c == 0xdb:
                    self.escape = True
                    continue
                if len(self.data) == 536:
                    self.discard = True
                else:
                    self.data.append(c)
        return result
