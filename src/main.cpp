#include "Files.h"
#include "Ginh.h"
#include "Glagol.h"
#include "Orientation.h"
#include "Reader.h"
#include "SleepGate.h"
#include "default_sleep.h"
#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WebServer.h>
#include <WiFi.h>
#include <atomic>
#include <cmath>
#include <driver/rtc_io.h>
#include <esp_heap_caps.h>
#include <esp_sleep.h>
#include <heltec-eink-modules.h>
#if INH_PANEL == 11
using Panel = EInkDisplay_WirelessPaperV1_1;
#else
using Panel = EInkDisplay_WirelessPaperV1_2;
#endif
class BoundedDisplay final : public Panel {
protected:
  void wait() override {
    uint32_t start = millis();
    while (digitalRead(7) == HIGH) {
      if (millis() - start > 15000)
        throw std::runtime_error("EPD BUSY timeout");
      vTaskDelay(1);
    }
  }
};
BoundedDisplay screen;
GFXcanvas1 canvas(250, 122);
U8G2_FOR_ADAFRUIT_GFX font;
Preferences prefs;
WebServer web(80);
DNSServer dns;
inh::Button button;
uint8_t pending[32 * 122];
SemaphoreHandle_t frameMutex;
TaskHandle_t displayTask;
std::atomic<bool> displayBusy{false}, displayFault{false};
std::atomic<uint32_t> sent{0}, shown{0};
uint32_t activeAt = 0, appWake = 0;
bool fsReady = false, wifiOn = false;
unsigned selection = 0, settingSelection = 0;
bool flipped = true, pendingFlipped = true;
int appInput = 0;
enum View {
  Home,
  Reading,
  Apps,
  Settings,
  Upload,
  Application,
  Error,
  Format,
  Photos,
  PhotoView,
  SleepPicker,
  FileManager,
  DeleteConfirm
};
View view = Home;
std::string message;
String sleepPhoto, deleteTarget;
View confirmReturn = FileManager;
unsigned confirmChoice = 0;
inh::Image photoPixels;
std::array<uint8_t, inh::GinhBytes> encodedPhoto;
bool routesInstalled = false;
String appHtml;
struct HeapSample {
  uint32_t free = 0, minimum = 0, largest = 0;
};
HeapSample compileBefore, compileAfter, runAfter;
HeapSample heapSample() {
  return {uint32_t(heap_caps_get_free_size(MALLOC_CAP_8BIT)),
          uint32_t(heap_caps_get_minimum_free_size(MALLOC_CAP_8BIT)),
          uint32_t(heap_caps_get_largest_free_block(MALLOC_CAP_8BIT))};
}
void logHeap(const char *phase, HeapSample h) {
  Serial.printf("HEAP %s free=%lu minimum_since_boot=%lu largest=%lu\n", phase,
                (unsigned long)h.free, (unsigned long)h.minimum,
                (unsigned long)h.largest);
}

File book, indexFile, building;
uint32_t page = 0, pageCount = 0, buildOffset = 0;
bool indexing = false;
unsigned cols = 40, rows = 8;
std::vector<String> apps;
void launchApp(const String &path);
void installRoutes();
void renderImage();
bool loadPhoto(const String &path);
void listFiles(const char *extension);
bool removeUser(const String &path);
struct Source : inh::TextSource {
  File *f;
  uint32_t size() const override { return f->size(); }
  int byte(uint32_t p) override {
    if (f->position() != p && !f->seek(p))
      return -1;
    return f->read();
  }
} source;
void render();
void openBook();
void stopNetwork();
void text(int x, int y, const std::string &s) {
  font.setCursor(x, y);
  font.print(s.c_str());
}
void frame() {
  xSemaphoreTake(frameMutex, portMAX_DELAY);
  memcpy(pending, canvas.getBuffer(), sizeof pending);
  pendingFlipped = flipped;
  ++sent;
  xSemaphoreGive(frameMutex);
  xTaskNotifyGive(displayTask);
}
void displayWorker(void *) {
  uint8_t local[32 * 122];
  for (;;) {
    ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    uint32_t version;
    bool localFlipped;
    xSemaphoreTake(frameMutex, portMAX_DELAY);
    memcpy(local, pending, sizeof local);
    localFlipped = pendingFlipped;
    version = sent;
    xSemaphoreGive(frameMutex);
    if (!inh::needsRefresh(version, shown, displayFault))
      continue;
    displayBusy = true;
    try {
      screen.clearMemory();
      for (int y = 0; y < 122; y++)
        for (int x = 0; x < 250; x++)
          if (local[y * 32 + x / 8] & (0x80 >> (x & 7))) {
            const auto point = inh::orientPixel(x, y, localFlipped);
            screen.drawPixel(point.x, point.y, BLACK);
          }
      Serial.printf("EPD update seq=%lu\n", (unsigned long)version);
      screen.update();
      shown = version;
    } catch (const std::exception &e) {
      Serial.println(e.what());
      displayFault = true;
    }
    displayBusy = false;
    vTaskDelay(pdMS_TO_TICKS(800));
  }
}
void error(const std::string &s) {
  message = s;
  view = Error;
  render();
}
bool loadPhoto(const String &path) {
  if (path.isEmpty()) {
    for (size_t i = 0; i < photoPixels.size(); i++)
      photoPixels[i] = pgm_read_byte(defaultSleepPixels + i);
    return true;
  }
  if (!inh::userPath(path.c_str()) || !path.endsWith(".ginh"))
    return false;
  File f = LittleFS.open(path, "r");
  if (!f || f.size() != inh::GinhBytes)
    return false;
  if (f.read(encodedPhoto.data(), encodedPhoto.size()) != encodedPhoto.size())
    return false;
  return inh::decodeGinh(encodedPhoto.data(), encodedPhoto.size(), photoPixels);
}
void renderImage() {
  canvas.fillScreen(0);
  for (unsigned y = 0; y < 122; y++)
    for (unsigned x = 0; x < 250; x++)
      if (inh::imageBlack(photoPixels, x, y))
        canvas.drawPixel(x, y, 1);
  frame();
}
void listFiles(const char *extension) {
  std::vector<String>().swap(apps);
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String n = f.name();
    if (!n.startsWith("/"))
      n = "/" + n;
    if (inh::userPath(n.c_str()) && (!extension || n.endsWith(extension)) &&
        apps.size() < 64)
      apps.push_back(n);
  }
  selection = 0;
}
void listApps() { listFiles(".inh"); }
void beginConfirm(const String &target, View back) {
  deleteTarget = target;
  confirmReturn = back;
  confirmChoice = 0;
  view = DeleteConfirm;
  render();
}
void savePage() { prefs.putUInt("page", page); }
void drawReading() {
  if (!book || !indexFile || !pageCount) {
    text(4, 40, "Книга не загружена");
    return;
  }
  indexFile.seek(sizeof(inh::IndexHeader) + page * 4);
  uint32_t at;
  if (indexFile.read((uint8_t *)&at, 4) != 4 || at > book.size())
    throw std::runtime_error("Повреждён индекс книги");
  source.f = &book;
  auto p = inh::paginate(source, at, cols, rows);
  int y = 13;
  for (auto &l : p.lines) {
    text(4, y, l);
    y += 12;
  }
  text(4, 119, std::to_string(page + 1) + " / " + std::to_string(pageCount));
}
void render() {
  canvas.fillScreen(0);
  font.setForegroundColor(1);
  font.setBackgroundColor(0);
  if (view == PhotoView) {
    renderImage();
    return;
  }
  if (view == Reading)
    drawReading();
  else {
    text(4, 13, "inhOS " INH_VERSION);
    canvas.drawLine(3, 17, 246, 17, 1);
    if (view == Home) {
      const char *choices[] = {"Читать",    "Приложения", "Фото",
                               "Загрузить", "Настройки",  "Сон"};
      for (int i = 0; i < 6; i++) {
        if (!INH_APPS && i == 1)
          continue;
        text(4, 30 + i * 15,
             std::string(i == selection ? "> " : "  ") + choices[i]);
      }
    } else if (view == Settings) {
      const std::string choices[] = {
          std::string("Разворот 180: ") + (flipped ? "вкл." : "выкл."),
          "Фото для сна", "Управление файлами", "Сброс позиции книги"};
      for (unsigned i = 0; i < 4; i++)
        text(4, 34 + i * 17,
             std::string(i == settingSelection ? "> " : "  ") + choices[i]);
      text(4, 116, "1: выбор; 2: открыть; 3: назад");
    } else if (view == Apps || view == Photos || view == FileManager ||
               view == SleepPicker) {
      size_t count =
          apps.size() + (view == FileManager || view == SleepPicker ? 1 : 0);
      if (!count)
        text(4, 40, "Файлов нет");
      for (size_t i = (selection / 5) * 5;
           i < count && i < (selection / 5) * 5 + 5; i++) {
        String name = view == SleepPicker
                          ? (i == 0 ? String("Встроенная") : apps[i - 1])
                          : (i < apps.size() ? apps[i] : String("Удалить всё"));
        text(4, 33 + (i % 5) * 16,
             std::string(i == selection ? "> " : "  ") + name.c_str());
      }
    } else if (view == DeleteConfirm) {
      text(4, 36,
           deleteTarget == "*" ? "Удалить ВСЕ файлы?" : "Удалить этот файл?");
      text(4, 55, deleteTarget.c_str());
      text(4, 80,
           confirmChoice ? "  Отмена     > Удалить" : "> Отмена       Удалить");
      text(4, 108, "1: выбор; 2: OK; 3: отмена");
    } else if (view == Upload) {
      text(4, 38, "Wi-Fi: inhOS");
      text(4, 56, "Пароль: inhreader");
      text(4, 74, "http://192.168.4.1");
      text(4, 98, "Удержание: выход");
    } else if (view == Format) {
      text(4, 35, "Хранилище не читается");
      text(4, 53, "2 клика: создать заново");
      text(4, 71, "ДАННЫЕ БУДУТ УДАЛЕНЫ");
    } else {
      std::string chunk = message;
      int y = 35;
      while (!chunk.empty() && y < 120) {
        size_t n = std::min(size_t(38), chunk.size());
        while (n < chunk.size() && n && (uint8_t(chunk[n]) & 0xc0) == 0x80)
          n--;
        text(4, y, chunk.substr(0, n));
        chunk.erase(0, n);
        y += 13;
      }
    }
  }
  frame();
}
void beginIndex() {
  indexing = true;
  indexFile.close();
  building = LittleFS.open("/book.idx.tmp", "w");
  if (!building) {
    indexing = false;
    error("Нет места для индекса");
    return;
  }
  inh::IndexHeader h;
  h.bookSize = book.size();
  building.write((uint8_t *)&h, sizeof h);
  buildOffset = pageCount = 0;
  message = "Разметка книги. Удержание: выход";
  view = Error;
  render();
}
void openBook() {
  book.close();
  indexFile.close();
  book = LittleFS.open("/book.txt", "r");
  if (!book) {
    error("Загрузите TXT или FB2 по Wi-Fi");
    return;
  }
  indexFile = LittleFS.open("/book.idx", "r");
  inh::IndexHeader h;
  if (indexFile && indexFile.read((uint8_t *)&h, sizeof h) == sizeof h &&
      h.magic == 0x49484e32 && h.version == 1 && h.bookSize == book.size() &&
      h.columns == cols && h.rows == rows && h.count && h.count <= 100000 &&
      indexFile.size() == sizeof h + h.count * 4) {
    pageCount = h.count;
    page = std::min(prefs.getUInt("page", 0), pageCount - 1);
    view = Reading;
    render();
  } else
    beginIndex();
}
void indexTick() {
  if (!indexing)
    return;
  source.f = &book;
  for (int i = 0; i < 3; i++) {
    if (building.write((uint8_t *)&buildOffset, 4) != 4) {
      indexing = false;
      building.close();
      error("Ошибка индекса: нет места");
      return;
    }
    pageCount++;
    auto p = inh::paginate(source, buildOffset, cols, rows);
    buildOffset = p.next;
    if (buildOffset >= book.size()) {
      inh::IndexHeader h;
      h.bookSize = book.size();
      h.count = pageCount;
      building.seek(0);
      building.write((uint8_t *)&h, sizeof h);
      building.close();
      if (!LittleFS.rename("/book.idx.tmp", "/book.idx")) {
        indexing = false;
        error("Не удалось сохранить индекс");
        return;
      }
      indexing = false;
      openBook();
      return;
    }
    if (pageCount >= 100000) {
      indexing = false;
      error("Слишком много страниц");
      return;
    }
  }
}
inh::Value host(const std::string &, const std::vector<inh::Value> &);
inh::VM vm(host);
void memoryError() {
  vm.stop();
  message.clear();
  view = Error;
  canvas.fillScreen(0);
  font.setCursor(4, 35);
  font.print("Недостаточно RAM. Приложение остановлено.");
  frame();
}
std::string appSource(File &f) {
  if (!f || f.size() > 32768)
    throw std::runtime_error("Файл приложения повреждён/слишком велик");
  std::string out(f.size(), '\0');
  if (f.read((uint8_t *)out.data(), out.size()) != out.size())
    throw std::runtime_error("Ошибка чтения приложения");
  return out;
}
inh::Program compileApp(const std::string &source) {
  compileBefore = heapSample();
  logHeap("compile-before", compileBefore);
  if (compileBefore.free < 32768 || compileBefore.largest < 8192)
    throw std::runtime_error("Недостаточно свободной RAM для компиляции");
  auto program = inh::compile(source);
  compileAfter = heapSample();
  logHeap("compile-after", compileAfter);
  return program;
}
bool removeUser(const String &path) {
  vm.stop();
  if (path == "*") {
    for (;;) {
      listFiles(nullptr);
      auto targets = std::move(apps);
      if (targets.empty())
        break;
      for (const auto &item : targets)
        if (!removeUser(item))
          return false;
    }
    LittleFS.remove("/upload.tmp");
    LittleFS.remove("/book.idx");
    LittleFS.remove("/book.idx.tmp");
    sleepPhoto = "";
    prefs.remove("sleepPhoto");
    page = 0;
    prefs.putUInt("page", 0);
    return true;
  }
  if (!inh::userPath(path.c_str()))
    return false;
  if (path == "/book.txt") {
    indexing = false;
    building.close();
    book.close();
    indexFile.close();
    LittleFS.remove("/book.idx");
    LittleFS.remove("/book.idx.tmp");
    page = pageCount = 0;
    prefs.putUInt("page", 0);
  }
  if (!LittleFS.exists(path))
    return false;
  if (!LittleFS.remove(path))
    return false;
  if (path == sleepPhoto) {
    sleepPhoto = "";
    prefs.remove("sleepPhoto");
  }
  return true;
}

void launchApp(const String &path) {
  if (!inh::userPath(path.c_str()) || !path.endsWith(".inh"))
    throw std::runtime_error("Неверное приложение");
  book.close();
  indexFile.close();
  vm.stop();
  File f = LittleFS.open(path, "r");
  auto source = appSource(f);
  f.close();
  std::vector<String>().swap(apps);
  auto program = compileApp(source);
  std::string().swap(source);
  vm.start(std::move(program));
  runAfter = heapSample();
  logHeap("run-start", runAfter);
  appInput = 0;
  view = Application;
  appWake = millis();
  canvas.fillScreen(0);
}
File incoming;
String destination;
size_t expected = 0, received = 0;
bool receiving = false;
#include "portal.h"
void network() {
  if (wifiOn)
    return;
  WiFi.mode(WIFI_AP);
  WiFi.softAP("inhOS", "inhreader");
  dns.start(53, "*", WiFi.softAPIP());
  installRoutes();
  web.begin();
  wifiOn = true;
}
void installRoutes() {
  if (routesInstalled)
    return;
  routesInstalled = true;
  web.on("/", [] {
    web.sendHeader("Cache-Control", "no-store");
    if (appHtml.length())
      web.send(200, "text/html; charset=utf-8", appHtml);
    else
      web.send_P(200, "text/html; charset=utf-8", portal);
  });
  web.on("/info", HTTP_GET, [] {
    String json = "{\"version\":\"" INH_VERSION "\",\"edition\":\"";
    json += INH_APPS ? "Apps" : "Reader";
    json += "\",\"apps\":";
    json += INH_APPS ? "true" : "false";
    json += ",\"max_app_bytes\":32768,\"max_book_bytes\":3145728,\"max_image_"
            "bytes\":3833}";
    web.sendHeader("Cache-Control", "no-store");
    web.send(200, "application/json; charset=utf-8", json);
  });
  web.on("/begin", HTTP_POST, [] {
    if (indexing || vm.running()) {
      web.send(409, "text/plain", "Устройство занято");
      return;
    }
    String n = web.arg("name");
    n.toLowerCase();
    bool app = n.endsWith(".inh"), image = n.endsWith(".ginh");
    if (app && !INH_APPS) {
      web.send(409, "text/plain; charset=utf-8",
               "Установлена Reader. Для приложений .inh нужна сборка Apps.");
      return;
    }
    if (n.length() > 36 || n.indexOf("..") >= 0 ||
        (!app && !image && n != "book.txt")) {
      web.send(400, "text/plain", "Неверное имя");
      return;
    }
    for (char c : n)
      if (!isalnum((unsigned char)c) && c != '_' && c != '.' && c != '-') {
        web.send(400);
        return;
      }
    vm.stop();
    incoming.close();
    receiving = false;
    LittleFS.remove("/upload.tmp");
    expected = web.arg("size").toInt();
    if (!expected ||
        expected > (app     ? 32768
                    : image ? inh::GinhBytes
                            : 3 * 1024 * 1024) ||
        (image && expected != inh::GinhBytes) ||
        LittleFS.totalBytes() - LittleFS.usedBytes() < expected + 8192) {
      web.send(413, "text/plain", "Нет места или файл слишком велик");
      return;
    }
    incoming.close();
    incoming = LittleFS.open("/upload.tmp", "w");
    receiving = bool(incoming);
    received = 0;
    destination = (app || image) ? "/" + n : "/book.txt";
    web.send(receiving ? 200 : 500, "text/plain", "OK");
  });
  web.on("/chunk", HTTP_POST, [] {
    String data = web.arg("data");
    if (!receiving || web.arg("offset").toInt() != received ||
        data.length() > 2800) {
      web.send(409);
      return;
    }
    const char *alphabet =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    uint32_t acc = 0;
    unsigned bits = 0;
    uint8_t decoded[2100];
    size_t count = 0;
    for (char c : data) {
      if (c == '=')
        break;
      const char *at = strchr(alphabet, c);
      if (!at) {
        web.send(400);
        return;
      }
      acc = (acc << 6) | (at - alphabet);
      bits += 6;
      if (bits >= 8) {
        bits -= 8;
        decoded[count++] = (acc >> bits) & 255;
      }
    }
    if (received + count > expected ||
        incoming.write(decoded, count) != count) {
      receiving = false;
      incoming.close();
      web.send(507);
      return;
    }
    received += count;
    web.send(200, "text/plain", "OK");
  });
  web.on("/finish", HTTP_POST, [] {
    if (!receiving || received != expected) {
      web.send(409);
      return;
    }
    incoming.close();
    receiving = false;
    if (destination.endsWith(".inh")) {
      File f = LittleFS.open("/upload.tmp");
      try {
        vm.stop();
        auto source = appSource(f);
        f.close();
        auto p = compileApp(source);
      } catch (const std::exception &e) {
        web.send(400, "text/plain; charset=utf-8", e.what());
        return;
      }
    } else if (destination.endsWith(".ginh")) {
      File f = LittleFS.open("/upload.tmp", "r");
      if (!f || f.size() != inh::GinhBytes ||
          f.read(encodedPhoto.data(), encodedPhoto.size()) !=
              encodedPhoto.size() ||
          !inh::decodeGinh(encodedPhoto.data(), encodedPhoto.size(),
                           photoPixels)) {
        web.send(400, "text/plain; charset=utf-8",
                 "Повреждён GINH: заголовок/CRC");
        return;
      }
    } else {
      book.close();
      indexFile.close();
    }
    if (destination == "/book.txt")
      LittleFS.remove("/book.idx");
    if (!LittleFS.rename("/upload.tmp", destination)) {
      web.send(500);
      return;
    }
    if (destination == "/book.txt") {
      LittleFS.remove("/book.idx");
      prefs.putUInt("page", 0);
    }
    web.send(200, "text/plain; charset=utf-8",
             "Готово. Удерживайте кнопку для выхода из загрузки.");
  });
  web.on("/download", HTTP_GET, [] {
    String n = web.arg("name");
    if (!inh::userPath(n.c_str())) {
      web.send(400);
      return;
    }
    File f = LittleFS.open(n, "r");
    if (!f) {
      web.send(404);
      return;
    }
    web.streamFile(f, "application/octet-stream");
  });
  web.on("/run", HTTP_POST, [] {
    if (!INH_APPS || receiving || indexing || vm.running()) {
      web.send(409);
      return;
    }
    try {
      launchApp(web.arg("name"));
      web.send(200, "text/plain", "OK");
    } catch (const std::exception &e) {
      vm.stop();
      web.send(400, "text/plain; charset=utf-8", e.what());
    }
  });
  web.on("/stop", HTTP_POST, [] {
    vm.stop();
    appHtml = String();
    view = Upload;
    render();
    web.send(200, "text/plain", "OK");
  });
  web.on("/files", HTTP_GET, [] {
    String json = "[";
    bool first = true;
    File root = LittleFS.open("/");
    for (File f = root.openNextFile(); f; f = root.openNextFile()) {
      String n = f.name();
      if (!n.startsWith("/"))
        n = "/" + n;
      if (!inh::userPath(n.c_str()))
        continue;
      if (!first)
        json += ",";
      first = false;
      json += "{\"name\":\"" + n + "\",\"size\":" + String((unsigned)f.size()) +
              "}";
    }
    json += "]";
    web.send(200, "application/json", json);
  });
  web.on("/delete", HTTP_POST, [] {
    if (web.arg("confirm") != "delete" || receiving || indexing ||
        vm.running()) {
      web.send(409, "text/plain; charset=utf-8",
               "Подтвердите удаление; остановите приложение/загрузку");
      return;
    }
    const String target = web.arg("name");
    if (target != "*" && !inh::userPath(target.c_str())) {
      web.send(400);
      return;
    }
    if (!removeUser(target)) {
      web.send(500, "text/plain; charset=utf-8", "Ошибка удаления");
      return;
    }
    web.send(200, "text/plain; charset=utf-8", "Удалено");
  });
  web.on("/diagnostics", HTTP_GET, [] {
    auto h = heapSample();
    String data = "{\"free\":" + String(h.free) +
                  ",\"minimum_since_boot\":" + String(h.minimum) +
                  ",\"largest\":" + String(h.largest) +
                  ",\"compile_before\":" + String(compileBefore.free) +
                  ",\"compile_after\":" + String(compileAfter.free) +
                  ",\"run_after\":" + String(runAfter.free) + "}";
    web.send(200, "application/json", data);
  });
  web.onNotFound([] {
    web.sendHeader("Location", "/");
    web.send(302);
  });
}
void stopNetwork() {
  appHtml = String();
  if (wifiOn) {
    web.stop();
    dns.stop();
    WiFi.disconnect(true);
    WiFi.mode(WIFI_OFF);
    wifiOn = false;
  }
  incoming.close();
  receiving = false;
}
inh::Value host(const std::string &n, const std::vector<inh::Value> &a) {
  auto count = [&](size_t k) {
    if (a.size() != k)
      throw std::runtime_error("неверное число аргументов");
  };
  auto integer = [&](size_t i) {
    double v = a.at(i).num();
    if (!std::isfinite(v) || v < -10000 || v > 10000)
      throw std::runtime_error("координата вне пределов");
    return int(v);
  };
  if (n == "кнопка") {
    count(0);
    int value = appInput;
    appInput = 0;
    return inh::Value(value);
  } else if (n == "очисти") {
    count(0);
    canvas.fillScreen(0);
  } else if (n == "пиксель") {
    count(2);
    canvas.drawPixel(integer(0), integer(1), 1);
  } else if (n == "линия") {
    count(4);
    canvas.drawLine(integer(0), integer(1), integer(2), integer(3), 1);
  } else if (n == "текст") {
    count(3);
    text(integer(0), integer(1), a[2].str());
  } else if (n == "яви") {
    count(0);
    frame();
  } else if (n == "жди") {
    count(1);
    int ms = integer(0);
    if (ms < 0)
      throw std::runtime_error("отрицательное время");
    appWake = millis() + ms;
  } else if (n == "время") {
    count(0);
    return inh::Value(double(millis()));
  } else if (n == "вайфай") {
    count(2);
    String ssid = a[0].str().c_str(), password = a[1].str().c_str();
    if (ssid.length() < 1 || ssid.length() > 32 || password.length() < 8 ||
        password.length() > 63)
      throw std::runtime_error("SSID 1..32, пароль 8..63");
    WiFi.mode(WIFI_AP);
    if (!WiFi.softAP(ssid.c_str(), password.c_str()))
      throw std::runtime_error("ошибка Wi-Fi");
    wifiOn = true;
  } else if (n == "сайт") {
    count(1);
    appHtml = a[0].str().c_str();
    installRoutes();
    web.begin();
  } else if (n == "адрес") {
    count(0);
    return inh::Value(std::string(WiFi.softAPIP().toString().c_str()));
  } else
    throw std::runtime_error("неизвестное слово: " + n);
  return inh::Value(0);
}
void sleepNow() {
  if (indexing || receiving || displayFault)
    return;
  // First drain all queued frames, then submit exactly one final image.
  uint32_t started = millis();
  while ((shown != sent || displayBusy) && millis() - started < 20000)
    delay(5);
  if (shown != sent || displayBusy)
    return;
  if (!loadPhoto(sleepPhoto)) {
    sleepPhoto = "";
    prefs.remove("sleepPhoto");
    loadPhoto("");
  }
  renderImage();
  const uint32_t finalFrame = sent;
  started = millis();
  while ((shown != finalFrame || displayBusy) && millis() - started < 20000)
    delay(5);
  if (shown != finalFrame || displayBusy || displayFault)
    return;
  savePage();
  vm.stop();
  stopNetwork();
  book.close();
  indexFile.close();
  // EXT0 is level-triggered: only arm after a stable released GPIO0.
  inh::SleepGate gate(finalFrame);
  while (!gate.ready(shown, displayBusy, digitalRead(0) == LOW, millis()))
    delay(5);
  esp_sleep_disable_wakeup_source(ESP_SLEEP_WAKEUP_ALL);
  Platform::prepareToSleep();
  // Platform driver powers peripherals down; GPIO0 must remain RTC
  // input/pull-up.
  esp_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_ON);
  rtc_gpio_pullup_en(GPIO_NUM_0);
  rtc_gpio_pulldown_dis(GPIO_NUM_0);
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
  if (digitalRead(0) == LOW)
    return;
  Serial.println("SLEEP ready; wake GPIO0 only");
  Serial.flush();
  esp_deep_sleep_start();
}
void gesture(inh::Gesture g) {
  if (g == inh::Gesture::None)
    return;
  activeAt = millis();
  if (g == inh::Gesture::Hold) {
    if (!fsReady) {
      view = Format;
      render();
      return;
    }
    const bool resumeReader = !INH_APPS && view == Upload;
    vm.stop();
    stopNetwork();
    if (indexing) {
      indexing = false;
      building.close();
    }
    if (resumeReader) {
      openBook();
      return;
    }
    view = INH_APPS ? Home : Upload;
    if (view == Upload)
      network();
    selection = 0;
    render();
    return;
  }
  if (view == Format) {
    if (g == inh::Gesture::Double) {
      if (LittleFS.format() && LittleFS.begin(false)) {
        fsReady = true;
        view = Home;
        render();
      } else
        error("Ошибка форматирования");
    }
    return;
  }
  if (!fsReady)
    return;
  if (view == Application) {
    appInput = g == inh::Gesture::Single   ? 1
               : g == inh::Gesture::Double ? 2
                                           : 3;
    return;
  }
  if (view == Reading) {
    if (g == inh::Gesture::Triple && page > 0)
      page--;
    else if (g == inh::Gesture::Single && page + 1 < pageCount)
      page++;
    else if (g == inh::Gesture::Double) {
      view = Home;
      selection = 0;
    }
    savePage();
    render();
  } else if (view == Home) {
    if (g == inh::Gesture::Single) {
      selection = (selection + 1) % 6;
      if (!INH_APPS && selection == 1)
        selection = 2;
    }
    if (g == inh::Gesture::Double) {
      if (selection == 0) {
        openBook();
        return;
      }
      if (selection == 1) {
        listApps();
        view = Apps;
      }
      if (selection == 2) {
        listFiles(".ginh");
        view = Photos;
      }
      if (selection == 3) {
        network();
        view = Upload;
      }
      if (selection == 4) {
        settingSelection = 0;
        view = Settings;
      }
      if (selection == 5) {
        sleepNow();
        return;
      }
    }
    render();
  } else if (view == Apps) {
    if (g == inh::Gesture::Single && !apps.empty())
      selection = (selection + 1) % apps.size();
    if (g == inh::Gesture::Triple) {
      view = Home;
      selection = 0;
    }
    if (g == inh::Gesture::Double && !apps.empty()) {
      try {
        launchApp(apps[selection]);
      } catch (const std::exception &e) {
        error(e.what());
        return;
      }
    } else
      render();
  } else if (view == Settings) {
    if (g == inh::Gesture::Single)
      settingSelection = (settingSelection + 1) % 4;
    if (g == inh::Gesture::Double) {
      if (settingSelection == 0) {
        flipped = !flipped;
        prefs.putBool("flip", flipped);
      } else if (settingSelection == 1) {
        listFiles(".ginh");
        view = SleepPicker;
      } else if (settingSelection == 2) {
        listFiles(nullptr);
        view = FileManager;
      } else {
        page = 0;
        savePage();
        view = Home;
      }
    }
    if (g == inh::Gesture::Triple)
      view = Home;
    render();
  } else if (view == Photos || view == FileManager || view == SleepPicker) {
    const size_t count = apps.size() + (view != Photos ? 1 : 0);
    if (g == inh::Gesture::Single && count)
      selection = (selection + 1) % count;
    if (g == inh::Gesture::Triple) {
      view = view == Photos ? Home : Settings;
      selection = 0;
    } else if (g == inh::Gesture::Double && count) {
      if (view == Photos) {
        if (!loadPhoto(apps[selection])) {
          error("Повреждён GINH");
          return;
        }
        view = PhotoView;
      } else if (view == SleepPicker) {
        String path = selection ? apps[selection - 1] : String("");
        if (!loadPhoto(path)) {
          error("Повреждён GINH");
          return;
        }
        sleepPhoto = path;
        prefs.putString("sleepPhoto", sleepPhoto);
        view = Settings;
      } else {
        beginConfirm(selection < apps.size() ? apps[selection] : String("*"),
                     FileManager);
        return;
      }
    }
    render();
  } else if (view == PhotoView) {
    if (g == inh::Gesture::Triple) {
      view = Photos;
      render();
    } else if (g == inh::Gesture::Double) {
      beginConfirm(apps[selection], Photos);
    } else if (g == inh::Gesture::Single && !apps.empty()) {
      selection = (selection + 1) % apps.size();
      if (!loadPhoto(apps[selection]))
        error("Повреждён GINH");
      else
        renderImage();
    }
  } else if (view == DeleteConfirm) {
    if (g == inh::Gesture::Single)
      confirmChoice ^= 1;
    if (g == inh::Gesture::Triple ||
        (g == inh::Gesture::Double && !confirmChoice)) {
      view = confirmReturn;
      render();
      return;
    }
    if (g == inh::Gesture::Double && confirmChoice) {
      if (!removeUser(deleteTarget)) {
        error("Ошибка удаления");
        return;
      }
      view = confirmReturn;
      listFiles(view == Photos ? ".ginh" : nullptr);
    }
    render();
  } else if (view == Error && g == inh::Gesture::Triple) {
    view = Home;
    render();
  }
}
void setup() {
  Serial.begin(115200);
  rtc_gpio_deinit(GPIO_NUM_0);
  pinMode(0, INPUT_PULLUP);
  prefs.begin("inhOS", false);
  flipped = prefs.getBool("flip", true);
  sleepPhoto = prefs.getString("sleepPhoto", "");
  source.f = &book;
  frameMutex = xSemaphoreCreateMutex();
  font.begin(canvas);
  font.setFont(u8g2_font_6x12_t_cyrillic);
  font.setFontMode(1);
  xTaskCreatePinnedToCore(displayWorker, "eink", 8192, nullptr, 1, &displayTask,
                          0);
  screen.landscape();
  button.reset(millis(), digitalRead(0) == LOW);
  fsReady = LittleFS.begin(false);
  activeAt = millis();
  if (!fsReady) {
    view = Format;
    render();
  } else if (!INH_APPS || esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_EXT0)
    openBook();
  else
    render();
}
void loop() {
  try {
    gesture(button.tick(millis(), digitalRead(0) == LOW));
    if (wifiOn) {
      dns.processNextRequest();
      web.handleClient();
    }
    indexTick();
    if (view == Application && int32_t(millis() - appWake) >= 0 &&
        vm.running()) {
      vm.tick(1);
      if (!vm.running() && !vm.error().empty())
        error(vm.error());
    }
    if (fsReady && !wifiOn && !indexing && view != Application &&
        millis() - activeAt > 120000)
      sleepNow();
  } catch (const std::bad_alloc &) {
    indexing = false;
    vm.stop();
    memoryError();
  } catch (const std::exception &e) {
    indexing = false;
    vm.stop();
    error(e.what());
  }
  delay(2);
}
