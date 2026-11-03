// Included after reader, VM and Wi-Fi handlers: only this adapter touches
// Arduino.
#include "UsbFs.h"
class UsbBackend final : public inh::usb::Backend {
  File transfer;
  static constexpr const char *temp = "/usb-upload.tmp";
  inh::usb::Status json(inh::usb::Packet &r, const char *s) {
    size_t n = strlen(s);
    if (n > r.data.size())
      return inh::usb::IO;
    memcpy(r.data.data(), s, n);
    r.size = n;
    return inh::usb::OK;
  }
  bool digest(File &f, uint32_t &crc, bool utf8 = false) {
    if (!f || !f.seek(0))
      return false;
    uint8_t chunk[512];
    inh::SourceValidator validator;
    crc = 0xffffffffu;
    size_t remaining = f.size();
    while (remaining) {
      size_t n = std::min<size_t>(sizeof chunk, remaining);
      if (f.read(chunk, n) != n)
        return false;
      crc = inh::crc32(chunk, n, crc);
      if (utf8)
        for (size_t i = 0; i < n; ++i)
          if (!validator.feed(chunk[i]))
            return false;
      remaining -= n;
      yield();
    }
    crc ^= 0xffffffffu;
    return (!utf8 || validator.complete()) && f.seek(0);
  }

public:
  inh::usb::Status info(inh::usb::Packet &r) override {
    char b[256];
    snprintf(b, sizeof b,
             "{\"protocol\":\"INHFS/1\",\"version\":\"" INH_VERSION
             "\",\"total\":%u,\"free\":%u,\"book_toc\":1,\"block_size\":512,"
             "\"max_app\":32768,"
             "\"max_book\":3145728,\"image_bytes\":3833,\"apps\":%s}",
             unsigned(LittleFS.totalBytes()),
             unsigned(LittleFS.totalBytes() - LittleFS.usedBytes()),
             INH_APPS ? "true" : "false");
    return json(r, b);
  }
  inh::usb::Status list(uint32_t index, inh::usb::Packet &r) override {
    File root = LittleFS.open("/");
    if (!root)
      return inh::usb::IO;
    uint32_t i = 0;
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      String name = f.name();
      if (!name.startsWith("/"))
        name = "/" + name;
      if (f.isDirectory() || !inh::userPath(name.c_str()))
        continue;
      if (i++ != index)
        continue;
      char b[160];
      snprintf(b, sizeof b, "{\"name\":\"%s\",\"size\":%u,\"type\":\"%s\"}",
               name.c_str(), unsigned(f.size()),
               name.endsWith(".inh")    ? "app"
               : name.endsWith(".ginh") ? "photo"
                                        : "book");
      return json(r, b);
    }
    return inh::usb::OK;
  }
  inh::usb::Status begin(const char *path, uint32_t size) override {
    if (!INH_APPS && inh::suffix(path, ".inh"))
      return inh::usb::Invalid;
    cancel();
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < size + 8192)
      return inh::usb::NoSpace;
    transfer = LittleFS.open(temp, "w");
    return transfer ? inh::usb::OK : inh::usb::IO;
  }
  inh::usb::Status write(const uint8_t *b, size_t n) override {
    return transfer && transfer.write(b, n) == n ? inh::usb::OK : inh::usb::IO;
  }
  inh::usb::Status commit(const char *path, uint32_t expected) override {
    transfer.flush();
    transfer.close();
    File f = LittleFS.open(temp, "r");
    uint32_t crc = 0;
    const bool image = inh::suffix(path, ".ginh");
    if (!digest(f, crc, !image) || crc != expected)
      return inh::usb::Invalid;
    if (image && (f.size() != inh::GinhBytes ||
                  f.read(encodedPhoto.data(), encodedPhoto.size()) !=
                      encodedPhoto.size() ||
                  !inh::decodeGinh(encodedPhoto.data(), encodedPhoto.size(),
                                   photoPixels)))
      return inh::usb::Invalid;
    if (!strcmp(path, "/book.txt")) {
      Source raw;
      raw.reset(&f);
      inh::BookLayout layout;
      if (!inh::parseBook(raw, layout))
        return inh::usb::Invalid;
    }
    f.close();
    // Invalidate before the atomic replacement: after a power cut even an old
    // same-size index cannot be used with a new book. Old book is not removed.
    if (!strcmp(path, "/book.txt")) {
      book.close();
      indexFile.close();
      building.close();
      for (const char *idx : {"/book.idx", "/book.idx.tmp"})
        if (LittleFS.exists(idx) && !LittleFS.remove(idx))
          return inh::usb::IO;
    }
    // ESP-IDF LittleFS VFS rename maps to lfs_rename: atomic replacement.
    if (!LittleFS.rename(temp, path))
      return inh::usb::IO;
    if (!strcmp(path, "/book.txt")) {
      page = pageCount = 0;
      prefs.putUInt("page", 0);
    }
    return inh::usb::OK;
  }
  inh::usb::Status open(const char *path, uint32_t &size,
                        uint32_t &crc) override {
    cancel();
    transfer = LittleFS.open(path, "r");
    if (!transfer)
      return inh::usb::IO;
    size = transfer.size();
    if (!digest(transfer, crc)) {
      cancel();
      return inh::usb::IO;
    }
    return inh::usb::OK;
  }
  int read(uint32_t offset, uint8_t *out, size_t n) override {
    if (!transfer || !transfer.seek(offset))
      return -1;
    return transfer.read(out, n);
  }
  inh::usb::Status remove(const char *path) override {
    return removeUser(path) ? inh::usb::OK : inh::usb::IO;
  }
  inh::usb::Status sleep(const char *path, inh::usb::Packet &r) override {
    if (path) {
      if (!loadPhoto(path))
        return inh::usb::Invalid;
      sleepPhoto = path;
      prefs.putString("sleepPhoto", sleepPhoto);
    }
    return json(r, sleepPhoto.c_str());
  }
  void cancel() override {
    transfer.close();
    LittleFS.remove(temp);
  }
};
UsbBackend usbBackend;
inh::usb::Server usbServer(usbBackend, 1);
inh::usb::Decoder usbDecoder;
void exitUsb() {
  usbServer.abort();
  usbServer.connected = false;
  usbActive = false;
  usbConnected = false;
  prefs.putBool("usbMode", false);
  activeAt = millis();
  view = Home;
  selection = 0;
  render();
}
void enterUsb() {
  vm.stop();
  stopNetwork();
  indexing = false;
  building.close();
  book.close();
  indexFile.close();
  std::vector<String>().swap(apps);
  // Persist once per entry: CP2102 DTR/RTS may reset the board on port open.
  prefs.putBool("usbMode", true);
  usbServer.reset(esp_random());
  usbDecoder = inh::usb::Decoder();
  usbConnected = false;
  xSemaphoreTake(serialMutex, portMAX_DELAY);
  usbActive = true;
  Serial.flush();
  for (unsigned i = 0; i < 2048 && Serial.available(); ++i)
    Serial.read();
  xSemaphoreGive(serialMutex);
  view = Usb;
  render();
}
void usbTick() {
  auto send = [](const uint8_t *b, size_t n) {
    uint8_t framed[2 * inh::usb::MaxPacket + 2];
    size_t count = 0;
    inh::usb::slip(b, n, [&](uint8_t c) { framed[count++] = c; });
    xSemaphoreTake(serialMutex, portMAX_DELAY);
    Serial.write(framed, count);
    Serial.flush();
    xSemaphoreGive(serialMutex);
  };
  unsigned budget = 2048;
  while (budget-- && Serial.available()) {
    usbDecoder.feed(uint8_t(Serial.read()), [&](const uint8_t *b, size_t n) {
      usbServer.receive(b, n, millis(), send);
    });
    if (usbServer.closed)
      break;
  }
  usbServer.tick(millis());
  if (usbServer.closed) {
    exitUsb();
    return;
  }
  int previousBattery = battery.percent;
  sampleBattery();
  if (usbConnected != usbServer.connected ||
      previousBattery != battery.percent) {
    usbConnected = usbServer.connected;
    render();
  }
}
