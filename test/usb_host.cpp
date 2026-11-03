// Native harness: executes the SAME framing/session/transfer engine as
// firmware. Host filesystem adapter is a test double; it is not the ESP32
// LittleFS driver.
#include "Book.h"
#include "Ginh.h"
#include "UsbFs.h"
#include <cassert>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <poll.h>
#include <sstream>
#include <unistd.h>
using namespace inh::usb;
namespace fs = std::filesystem;
struct Host final : Backend {
  fs::path root;
  std::fstream file;
  std::string photo;
  Host(const char *r) : root(r) {
    fs::create_directories(root);
    cancel();
  }
  fs::path path(const char *p) { return root / std::string(p).substr(1); }
  Status string(Packet &r, const std::string &s) {
    if (s.size() > Payload)
      return IO;
    memcpy(r.data.data(), s.data(), s.size());
    r.size = s.size();
    return OK;
  }
  Status info(Packet &r) override {
    return string(r, "{\"protocol\":\"INHFS/"
                     "1\",\"version\":\"host-test\",\"book_toc\":1,\"free\":"
                     "4000000,\"total\":5111808}");
  }
  Status list(uint32_t i, Packet &r) override {
    for (auto &e : fs::directory_iterator(root)) {
      std::string name = "/" + e.path().filename().string();
      if (!inh::userPath(name))
        continue;
      if (i--)
        continue;
      return string(r, "{\"name\":\"" + name + "\",\"size\":" +
                           std::to_string(e.file_size()) + ",\"type\":\"" +
                           (inh::suffix(name, ".inh")    ? "app"
                            : inh::suffix(name, ".ginh") ? "photo"
                                                         : "book") +
                           "\"}");
    }
    return OK;
  }
  Status begin(const char *, uint32_t) override {
    cancel();
    file.open(root / "usb-upload.tmp",
              std::ios::binary | std::ios::out | std::ios::trunc);
    return file ? OK : IO;
  }
  Status write(const uint8_t *b, size_t n) override {
    file.write((const char *)b, n);
    return file ? OK : IO;
  }
  Status commit(const char *p, uint32_t expected) override {
    file.close();
    std::ifstream f(root / "usb-upload.tmp", std::ios::binary);
    uint8_t buffer[512];
    uint32_t crc = 0xffffffffu;
    inh::SourceValidator v;
    const bool image = inh::suffix(p, ".ginh");
    while (f.read((char *)buffer, sizeof buffer) || f.gcount()) {
      size_t n = f.gcount();
      crc = inh::crc32(buffer, n, crc);
      if (!image)
        for (size_t i = 0; i < n; i++)
          if (!v.feed(buffer[i]))
            return Invalid;
    }
    if ((crc ^ 0xffffffffu) != expected || (!image && !v.complete()))
      return Invalid;
    if (image) {
      f.clear();
      f.seekg(0);
      std::array<uint8_t, 3833> b{};
      inh::Image im;
      f.read((char *)b.data(), b.size());
      if (f.gcount() != 3833 || !inh::decodeGinh(b.data(), b.size(), im))
        return Invalid;
    }
    if (!strcmp(p, "/book.txt")) {
      struct BookSource : inh::TextSource {
        std::ifstream &f;
        uint32_t length;
        BookSource(std::ifstream &in) : f(in) {
          f.clear();
          f.seekg(0, std::ios::end);
          length = f.tellg();
        }
        uint32_t size() const override { return length; }
        int byte(uint32_t at) override {
          f.clear();
          f.seekg(at);
          return f.get();
        }
      } source(f);
      inh::BookLayout layout;
      if (!inh::parseBook(source, layout))
        return Invalid;
      fs::remove(root / "book.idx");
      fs::remove(root / "book.idx.tmp");
    }
    fs::rename(root / "usb-upload.tmp", path(p));
    return OK;
  }
  Status open(const char *p, uint32_t &size, uint32_t &crc) override {
    cancel();
    file.open(path(p), std::ios::binary | std::ios::in);
    if (!file)
      return IO;
    file.seekg(0, std::ios::end);
    size = file.tellg();
    file.seekg(0);
    uint8_t buffer[512];
    crc = 0xffffffffu;
    while (file.read((char *)buffer, sizeof buffer) || file.gcount())
      crc = inh::crc32(buffer, file.gcount(), crc);
    crc ^= 0xffffffffu;
    file.clear();
    file.seekg(0);
    return OK;
  }
  int read(uint32_t offset, uint8_t *b, size_t n) override {
    file.clear();
    file.seekg(offset);
    file.read((char *)b, n);
    return file.gcount();
  }
  Status remove(const char *p) override {
    if (!strcmp(p, "*")) {
      for (auto &e : fs::directory_iterator(root))
        if (inh::userPath("/" + e.path().filename().string()))
          fs::remove(e.path());
      fs::remove(root / "book.idx");
      fs::remove(root / "book.idx.tmp");
      photo.clear();
      return OK;
    }
    if (!inh::userPath(p))
      return BadPath;
    if (!fs::remove(path(p)))
      return IO;
    if (!strcmp(p, "/book.txt")) {
      fs::remove(root / "book.idx");
      fs::remove(root / "book.idx.tmp");
    }
    if (photo == p)
      photo.clear();
    return OK;
  }
  Status sleep(const char *p, Packet &r) override {
    if (p) {
      if (*p) {
        std::ifstream f(path(p), std::ios::binary);
        std::array<uint8_t, 3833> b{};
        inh::Image im;
        f.read((char *)b.data(), b.size());
        if (f.gcount() != 3833 || !inh::decodeGinh(b.data(), b.size(), im))
          return Invalid;
      }
      photo = p;
    }
    return string(r, photo);
  }
  void cancel() override {
    file.close();
    file.clear();
    fs::remove(root / "usb-upload.tmp");
  }
};
int main(int argc, char **argv) {
  if (argc != 2 && argc != 3)
    return 1;
  Host host(argv[1]);
  Server server(host, 112233);
  if (argc == 3) {
    std::array<uint8_t, MaxPacket> bytes{};
    Packet q;
    auto request = [&](uint32_t now) {
      size_t size = pack(q, bytes.data());
      server.receive(bytes.data(), size, now, [](const uint8_t *, size_t) {});
    };
    q.op = Hello;
    q.seq = 1;
    q.size = 7;
    memcpy(q.data.data(), "INHFS/1", 7);
    request(0);
    assert(server.connected);
    q = Packet{};
    q.op = PutBegin;
    q.seq = 2;
    q.session = 112234;
    q.size = 14;
    put32(q.data.data(), 10);
    put32(q.data.data() + 4, 123);
    memcpy(q.data.data() + 8, "/a.inh", 6);
    request(1);
    assert(fs::exists(host.root / "usb-upload.tmp"));
    server.tick(15002);
    assert(!fs::exists(host.root / "usb-upload.tmp"));
    assert(server.connected && !server.closed);
    server.tick(60002);
    assert(!server.connected && server.closed);
    return 0;
  }
  Decoder decoder;
  auto start = std::chrono::steady_clock::now();
  auto now = [&]() {
    return uint32_t(std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::steady_clock::now() - start)
                        .count());
  };
  auto send = [](const uint8_t *b, size_t n) {
    slip(b, n, [](uint8_t c) { std::cout.put(c); });
    std::cout.flush();
  };
  for (;;) {
    pollfd fd{STDIN_FILENO, POLLIN, 0};
    int ready = poll(&fd, 1, 20);
    if (ready > 0) {
      uint8_t data[1024];
      ssize_t n = ::read(STDIN_FILENO, data, sizeof data);
      if (n <= 0)
        break;
      for (ssize_t i = 0; i < n; ++i)
        decoder.feed(data[i], [&](const uint8_t *b, size_t n) {
          server.receive(b, n, now(), send);
        });
    }
    server.tick(now());
  }
}
