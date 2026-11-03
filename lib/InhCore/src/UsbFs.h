#pragma once
#include "Files.h"
#include "Reader.h"
#include <algorithm>
#include <array>
#include <cstring>

namespace inh::usb {
constexpr size_t Payload = 512, Header = 20, MaxPacket = Header + Payload + 4;
enum Op : uint8_t {
  Hello = 1,
  Info,
  List,
  PutBegin,
  PutData,
  PutEnd,
  GetBegin,
  GetData,
  Delete,
  Wipe,
  SleepGet,
  SleepSet,
  Cancel,
  Bye,
  Ping
};
enum Status : uint16_t {
  OK = 0,
  BadPacket,
  BadCRC,
  BadSession,
  BadSequence,
  BadPath,
  Busy,
  IO,
  Invalid,
  NoSpace,
  Confirmation,
  FileCRC
};
inline uint16_t u16(const uint8_t *p) { return p[0] | uint16_t(p[1]) << 8; }
inline uint32_t u32(const uint8_t *p) {
  return u16(p) | uint32_t(u16(p + 2)) << 16;
}
inline void put16(uint8_t *p, uint16_t v) {
  p[0] = v;
  p[1] = v >> 8;
}
inline void put32(uint8_t *p, uint32_t v) {
  put16(p, v);
  put16(p + 2, v >> 16);
}
struct Packet {
  uint8_t op = 0;
  uint32_t seq = 0, block = 0, session = 0;
  uint16_t size = 0, status = 0;
  std::array<uint8_t, Payload> data{};
};
inline size_t pack(const Packet &p, uint8_t *out) {
  out[0] = 'I';
  out[1] = 'F';
  out[2] = 1;
  out[3] = p.op;
  put32(out + 4, p.seq);
  put32(out + 8, p.block);
  put32(out + 12, p.session);
  put16(out + 16, p.size);
  put16(out + 18, p.status);
  memcpy(out + 20, p.data.data(), p.size);
  put32(out + 20 + p.size, crc32(out, 20 + p.size) ^ 0xffffffffu);
  return 24 + p.size;
}
inline Status unpack(const uint8_t *b, size_t n, Packet &p) {
  if (n < 24 || n > MaxPacket || b[0] != 'I' || b[1] != 'F' || b[2] != 1)
    return BadPacket;
  p.op = b[3];
  p.seq = u32(b + 4);
  p.block = u32(b + 8);
  p.session = u32(b + 12);
  p.size = u16(b + 16);
  p.status = u16(b + 18);
  if (p.size > Payload || n != size_t(24) + p.size)
    return BadPacket;
  if ((crc32(b, n - 4) ^ 0xffffffffu) != u32(b + n - 4))
    return BadCRC;
  memcpy(p.data.data(), b + 20, p.size);
  return OK;
}
// SLIP: a fresh delimiter also recovers from ROM boot/debug text and overflow.
struct Decoder {
  std::array<uint8_t, MaxPacket> bytes{};
  size_t size = 0;
  bool escape = false, discard = true;
  template <class F> void feed(uint8_t c, F receive) {
    if (c == 0xc0) {
      if (!discard && !escape && size)
        receive(bytes.data(), size);
      size = 0;
      escape = false;
      discard = false;
      return;
    }
    if (discard)
      return;
    if (escape) {
      escape = false;
      if (c == 0xdc)
        c = 0xc0;
      else if (c == 0xdd)
        c = 0xdb;
      else {
        discard = true;
        return;
      }
    } else if (c == 0xdb) {
      escape = true;
      return;
    }
    if (size == bytes.size()) {
      discard = true;
      return;
    }
    bytes[size++] = c;
  }
};
template <class F> void slip(const uint8_t *b, size_t n, F emit) {
  emit(0xc0);
  for (size_t i = 0; i < n; ++i) {
    if (b[i] == 0xc0) {
      emit(0xdb);
      emit(0xdc);
    } else if (b[i] == 0xdb) {
      emit(0xdb);
      emit(0xdd);
    } else
      emit(b[i]);
  }
  emit(0xc0);
}
struct Backend {
  virtual ~Backend() = default;
  virtual Status info(Packet &r) = 0; // UTF-8 JSON, <=512 bytes
  virtual Status list(uint32_t index,
                      Packet &r) = 0; // one record or empty at EOF
  virtual Status begin(const char *path, uint32_t size) = 0;
  virtual Status write(const uint8_t *b, size_t n) = 0;
  virtual Status commit(const char *path, uint32_t crc) = 0;
  virtual Status open(const char *path, uint32_t &size, uint32_t &crc) = 0;
  virtual int read(uint32_t offset, uint8_t *out, size_t n) = 0;
  virtual Status remove(const char *path) = 0;
  virtual Status sleep(const char *path,
                       Packet &r) = 0; // nullptr=get, ""=built-in
  virtual void cancel() = 0;
};
class Server {
  Backend &fs;
  uint32_t seed, session = 0, next = 0, lastCRC = 0, touched = 0;
  uint32_t total = 0, offset = 0, expectedCRC = 0, runningCRC = 0xffffffffu;
  bool uploading = false, downloading = false, cached = false;
  char path[38]{};
  std::array<uint8_t, MaxPacket> response{};
  size_t responseSize = 0;
  bool name(const Packet &p, size_t start = 0) {
    if (p.size < start || p.size - start > 37 || p.size == start)
      return false;
    for (size_t i = start; i < p.size; ++i)
      if (!p.data[i])
        return false;
    memcpy(path, p.data.data() + start, p.size - start);
    path[p.size - start] = 0;
    return userPath(path);
  }
  bool idle() const { return !uploading && !downloading; }

public:
  bool connected = false, closed = false;
  explicit Server(Backend &b, uint32_t nonce)
      : fs(b), seed(nonce ? nonce : 1) {}
  void reset(uint32_t nonce) {
    abort();
    seed = nonce ? nonce : 1;
    session = next = 0;
    cached = connected = closed = false;
  }
  void abort() {
    fs.cancel();
    uploading = downloading = false;
    offset = 0;
  }
  void tick(uint32_t now) {
    if (!idle() && uint32_t(now - touched) > 15000)
      abort();
    if (connected && uint32_t(now - touched) > 60000) {
      abort();
      connected = false;
      closed = true;
    }
  }
  template <class Send>
  void receive(const uint8_t *raw, size_t n, uint32_t now, Send send) {
    Packet q, r;
    Status decoded = unpack(raw, n, q);
    if (decoded == BadPacket)
      return;
    r.op = q.op | 0x80;
    r.seq = q.seq;
    r.block = q.block;
    r.session = q.session;
    auto reply = [&](Status status, bool remember) {
      r.status = status;
      std::array<uint8_t, MaxPacket> tmp{};
      size_t size = pack(r, tmp.data());
      if (remember) {
        response = tmp;
        responseSize = size;
        cached = true;
        lastCRC = u32(raw + n - 4);
      }
      send(tmp.data(), size);
    };
    if (decoded != OK) {
      reply(decoded, false);
      return;
    }
    if (cached && q.seq == u32(response.data() + 4) &&
        q.session == (q.op == Hello ? 0 : u32(response.data() + 12)) &&
        u32(raw + n - 4) == lastCRC) {
      touched = now;
      send(response.data(), responseSize);
      return;
    }
    if (q.op == Hello) {
      if (q.session || q.size != 7 || memcmp(q.data.data(), "INHFS/1", 7)) {
        reply(Invalid, false);
        return;
      }
      abort();
      closed = false;
      connected = true;
      touched = now;
      session = ++seed;
      if (!session)
        session = ++seed;
      r.session = session;
      next = q.seq + 1;
      memcpy(r.data.data(), "INHFS/1", 7);
      r.size = 7;
      reply(OK, true);
      return;
    }
    if (!connected || q.session != session) {
      reply(BadSession, false);
      return;
    }
    if (q.seq != next) {
      reply(BadSequence, false);
      return;
    }
    touched = now;
    ++next;
    Status status = OK;
    if (q.status || (q.op & 0x80))
      status = BadPacket;
    else if (q.op == Info && !q.size)
      status = fs.info(r);
    else if (q.op == List && !q.size)
      status = fs.list(q.block, r);
    else if (q.op == Cancel && !q.size)
      abort();
    else if (q.op == Ping && !q.size) {
    } else if (q.op == Bye && !q.size) {
      abort();
      closed = true;
      connected = false;
    } else if (q.op == SleepGet && !q.size)
      status = fs.sleep(nullptr, r);
    else if (q.op == PutData) {
      if (!uploading || q.block != offset || !q.size || q.size > total - offset)
        status = BadSequence;
      else {
        status = fs.write(q.data.data(), q.size);
        if (status == OK) {
          runningCRC = crc32(q.data.data(), q.size, runningCRC);
          offset += q.size;
        } else
          abort();
      }
    } else if (q.op == PutEnd && !q.size) {
      if (!uploading || offset != total)
        status = BadSequence;
      else if ((runningCRC ^ 0xffffffffu) != expectedCRC) {
        status = FileCRC;
        abort();
      } else {
        status = fs.commit(path, expectedCRC);
        abort();
      }
    } else if (q.op == GetData && !q.size) {
      if (!downloading || q.block != offset)
        status = BadSequence;
      else {
        size_t count = std::min<size_t>(Payload, total - offset);
        int got = fs.read(offset, r.data.data(), count);
        if (got < 0 || size_t(got) != count) {
          status = IO;
          abort();
        } else {
          r.size = count;
          offset += count;
        }
      }
    } else if (!idle())
      status = Busy;
    else if (q.op == PutBegin) {
      if (q.size < 9 || !name(q, 8))
        status = BadPath;
      else {
        total = u32(q.data.data());
        expectedCRC = u32(q.data.data() + 4);
        bool image = suffix(path, ".ginh");
        uint32_t limit =
            strcmp(path, "/book.txt") == 0 ? 3 * 1024 * 1024 : 32768;
        if (!total || total > limit || (image && total != 3833))
          status = Invalid;
        else {
          status = fs.begin(path, total);
          uploading = status == OK;
          offset = 0;
          runningCRC = 0xffffffffu;
        }
      }
    } else if (q.op == GetBegin) {
      if (!name(q))
        status = BadPath;
      else {
        status = fs.open(path, total, expectedCRC);
        if (status == OK) {
          downloading = true;
          offset = 0;
          put32(r.data.data(), total);
          put32(r.data.data() + 4, expectedCRC);
          r.size = 8;
        }
      }
    } else if (q.op == Delete) {
      if (!name(q))
        status = BadPath;
      else
        status = fs.remove(path);
    } else if (q.op == Wipe) {
      if (q.size != 17 || memcmp(q.data.data(), "DELETE USER FILES", 17))
        status = Confirmation;
      else
        status = fs.remove("*");
    } else if (q.op == SleepSet) {
      if (!q.size)
        status = fs.sleep("", r);
      else if (!name(q) || !suffix(path, ".ginh"))
        status = BadPath;
      else
        status = fs.sleep(path, r);
    } else
      status = Invalid;
    reply(status, true);
  }
};
} // namespace inh::usb
