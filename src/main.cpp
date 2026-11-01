#include "Glagol.h"
#include "Reader.h"
#include "Orientation.h"
#include <Adafruit_GFX.h>
#include <Arduino.h>
#include <DNSServer.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <U8g2_for_Adafruit_GFX.h>
#include <WebServer.h>
#include <WiFi.h>
#include <cmath>
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
volatile bool displayBusy = false, displayFault = false;
volatile uint32_t sent = 0, shown = 0;
uint32_t activeAt = 0, appWake = 0;
bool fsReady = false, wifiOn = false;
unsigned selection = 0, settingSelection = 0;
bool flipped = true, pendingFlipped = true;
int appInput = 0;
enum View { Home, Reading, Apps, Settings, Upload, Application, Error, Format };
View view = Home;
std::string message;
File book, indexFile, building;
uint32_t page = 0, pageCount = 0, buildOffset = 0;
bool indexing = false;
unsigned cols = 40, rows = 8;
std::vector<String> apps;
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
    if (displayFault)
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
void listApps() {
  apps.clear();
  File root = LittleFS.open("/");
  for (File f = root.openNextFile(); f; f = root.openNextFile()) {
    String n = f.name();
    if (n.endsWith(".inh") && apps.size() < 16)
      apps.push_back(n);
  }
  selection = 0;
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
  if (view == Reading)
    drawReading();
  else {
    text(4, 13, "inhOS " INH_VERSION);
    canvas.drawLine(3, 17, 246, 17, 1);
    if (view == Home) {
      const char *choices[] = {"Читать", "Приложения", "Загрузить", "Настройки",
                               "Сон"};
      for (int i = 0; i < 5; i++) {
        if (!INH_APPS && i == 1)
          continue;
        text(4, 31 + i * 15,
             std::string(i == selection ? "> " : "  ") + choices[i]);
      }
    } else if (view == Settings) {
      text(4, 38, std::string(settingSelection == 0 ? "> " : "  ") + "Разворот 180: " + (flipped ? "вкл." : "выкл."));
      text(4, 58, std::string(settingSelection == 1 ? "> " : "  ") + "Сброс позиции книги");
      text(4, 84, "1 клик: выбор; 2: изменить");
      text(4, 104, "3 клика: назад");
    } else if (view == Apps) {
      if (apps.empty())
        text(4, 40, "Нет .inh приложений");
      for (unsigned j = selection / 5 * 5;
           j < apps.size() && j < selection / 5 * 5 + 5; j++)
        text(4, 32 + (j % 5) * 15,
             std::string(j == selection ? "> " : "  ") + apps[j].c_str());
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
  web.on("/", [] { web.sendHeader("Cache-Control", "no-store"); web.send_P(200, "text/html; charset=utf-8", portal); });
  web.on("/info", HTTP_GET, [] {
    String json = "{\"version\":\"" INH_VERSION "\",\"edition\":\"";
    json += INH_APPS ? "Apps" : "Reader";
    json += "\",\"apps\":";
    json += INH_APPS ? "true" : "false";
    json += ",\"max_app_bytes\":32768,\"max_book_bytes\":3145728}";
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
    bool app = n.endsWith(".inh");
    if (app && !INH_APPS) {
      web.send(409, "text/plain; charset=utf-8", "Установлена Reader. Для приложений .inh нужна сборка Apps.");
      return;
    }
    if (n.length() > 36 || n.indexOf("..") >= 0 ||
        (!app && n != "book.txt")) {
      web.send(400, "text/plain", "Неверное имя");
      return;
    }
    for (char c : n)
      if (!isalnum((unsigned char)c) && c != '_' && c != '.' && c != '-') {
        web.send(400);
        return;
      }
    expected = web.arg("size").toInt();
    if (!expected || expected > (app ? 32768 : 3 * 1024 * 1024) ||
        LittleFS.totalBytes() - LittleFS.usedBytes() < expected + 8192) {
      web.send(413, "text/plain", "Нет места или файл слишком велик");
      return;
    }
    incoming.close();
    incoming = LittleFS.open("/upload.tmp", "w");
    receiving = bool(incoming);
    received = 0;
    destination = app ? "/" + n : "/book.txt";
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
      String s = f.readString();
      f.close();
      try {
        auto p = inh::compile(s.c_str());
      } catch (const std::exception &e) {
        web.send(400, "text/plain; charset=utf-8", e.what());
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
  web.onNotFound([] {
    web.sendHeader("Location", "/");
    web.send(302);
  });
  web.begin();
  wifiOn = true;
}
void stopNetwork() {
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
    String html = a[0].str().c_str();
    web.on("/", [html] { web.send(200, "text/html; charset=utf-8", html); });
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
  savePage();
  stopNetwork();
  canvas.fillScreen(0);
  text(62, 60, "inhOS");
  text(210, 119, "inh 1");
  frame();
  uint32_t start = millis();
  while (shown != sent && millis() - start < 10000)
    delay(10);
  if (displayBusy)
    return;
  Platform::prepareToSleep();
  esp_sleep_enable_ext0_wakeup(GPIO_NUM_0, 0);
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
      selection = (selection + 1) % 5;
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
        network();
        view = Upload;
      }
      if (selection == 3) {
        settingSelection = 0;
        view = Settings;
      }
      if (selection == 4) {
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
      File f = LittleFS.open("/" + apps[selection]);
      if (f.size() > 32768) {
        error("Приложение слишком велико");
        return;
      }
      String s = f.readString();
      f.close();
      try {
        vm.start(inh::compile(s.c_str()));
        appInput = 0;
        view = Application;
        appWake = millis();
        canvas.fillScreen(0);
      } catch (const std::exception &e) {
        error(e.what());
        return;
      }
    } else
      render();
  } else if (view == Settings) {
    if (g == inh::Gesture::Single) settingSelection = (settingSelection + 1) % 2;
    if (g == inh::Gesture::Double) {
      if (settingSelection == 0) {
        flipped = !flipped;
        prefs.putBool("flip", flipped);
      } else {
        page = 0;
        savePage();
        view = Home;
      }
    }
    if (g == inh::Gesture::Triple) view = Home;
    render();
  } else if (view == Error && g == inh::Gesture::Triple) {
    view = Home;
    render();
  }
}
void setup() {
  Serial.begin(115200);
  pinMode(0, INPUT_PULLUP);
  prefs.begin("inhOS", false);
  flipped = prefs.getBool("flip", true);
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
  } catch (const std::exception &e) {
    indexing = false;
    vm.stop();
    error(e.what());
  }
  delay(2);
}
