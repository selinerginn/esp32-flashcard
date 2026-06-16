// ═══════════════════════════════════════════════════════════
//  ESP32 Flashcard
//  Kütüphaneler: Adafruit SSD1306, Adafruit GFX, ArduinoJson, WiFi, HTTPClient
// ═══════════════════════════════════════════════════════════

#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include "esp_eap_client.h"
#include "config.h"

// ── Ekran ────────────────────────────────────────────────
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ── Wi-Fi / API ──────────────────────────────────────────

struct WiFiNetwork {
  const char* ssid;
  const char* pass;
  const char* user; // NULL = WPA2-Personal, dolu = WPA2-Enterprise
};

// ── Butonlar ─────────────────────────────────────────────
const int btnMenu = 13;
const int btnOk   = 27;
const int btnDown = 26;
#define LONG_PRESS_MS 1000
#define WAKEUP_PIN    GPIO_NUM_13

// ── Sabitler ─────────────────────────────────────────────
#define MAX_DECKS  10
#define MAX_CARDS  50

// ── Veri yapıları ────────────────────────────────────────
struct Card {
  int    id;
  String question;
  String answer;
};

struct DeckMeta {
  int    id;
  String name;
  int    cardCount;
};

DeckMeta deckMeta[MAX_DECKS];
int      deckMetaCount = 0;
int      activeDeckIdx = -1;

Card cards[MAX_CARDS];
int  cardCount   = 0;
int  score[MAX_CARDS];
int  currentCard = 0;

DynamicJsonDocument syncDoc(10240);

// ── Durum makinesi ───────────────────────────────────────
enum AppState { STATE_QUESTION, STATE_ANSWER, STATE_MENU, STATE_DECKS, STATE_SETTINGS };
AppState appState = STATE_QUESTION;

// ── Menü ─────────────────────────────────────────────────
const char* menuItems[]     = { "Decks", "Sync", "Stats", "Settings", "Reset Scores" };
const char* settingsItems[] = { "Shuffle", "Filter", "Brightness" };
const int MENU_COUNT     = 5;
const int SETTINGS_COUNT = 3;
int menuIndex     = 0;
int settingsIndex = 0;
int deckSelectIndex = 0;

// ── Ayarlar ──────────────────────────────────────────────
bool shuffleOn  = false;
int  filterMode = 0;
int  brightness = 1;

// ── Filtre ───────────────────────────────────────────────
int  filteredCards[MAX_CARDS];
int  filteredCount  = 0;
bool filterFallback = false;

void buildFilter() {
  filteredCount  = 0;
  filterFallback = false;
  for (int i = 0; i < cardCount; i++) {
    if      (filterMode == 0)                  filteredCards[filteredCount++] = i;
    else if (filterMode == 1 && score[i] <= 0) filteredCards[filteredCount++] = i;
    else if (filterMode == 2 && score[i] == 0) filteredCards[filteredCount++] = i;
  }
  if (filteredCount == 0 && filterMode != 0) {
    filterFallback = true;
    for (int i = 0; i < cardCount; i++) filteredCards[filteredCount++] = i;
  }
}

// ── Türkçe → ASCII ───────────────────────────────────────
String turkishToAscii(String s) {
  s.replace("\xC5\x9F","s"); s.replace("\xC5\x9E","S");
  s.replace("\xC4\x9F","g"); s.replace("\xC4\x9E","G");
  s.replace("\xC3\xBC","u"); s.replace("\xC3\x9C","U");
  s.replace("\xC3\xB6","o"); s.replace("\xC3\x96","O");
  s.replace("\xC3\xA7","c"); s.replace("\xC3\x87","C");
  s.replace("\xC4\xB1","i"); s.replace("\xC4\xB0","I");
  return s;
}

// ════════════════════════════════════════════════════════
//  NVS
// ════════════════════════════════════════════════════════
Preferences prefs;

void saveDeckMeta() {
  prefs.begin("fc_meta", false);
  prefs.putInt("count", deckMetaCount);
  for (int i = 0; i < deckMetaCount; i++) {
    prefs.putInt(   ("mid" + String(i)).c_str(), deckMeta[i].id);
    prefs.putString(("mn"  + String(i)).c_str(), deckMeta[i].name);
    prefs.putInt(   ("mc"  + String(i)).c_str(), deckMeta[i].cardCount);
  }
  prefs.end();
}

void loadDeckMeta() {
  prefs.begin("fc_meta", true);
  deckMetaCount = prefs.getInt("count", 0);
  for (int i = 0; i < deckMetaCount; i++) {
    deckMeta[i].id        = prefs.getInt(   ("mid" + String(i)).c_str(), 0);
    deckMeta[i].name      = prefs.getString(("mn"  + String(i)).c_str(), "");
    deckMeta[i].cardCount = prefs.getInt(   ("mc"  + String(i)).c_str(), 0);
  }
  prefs.end();
}

// prefs.clear() ile eski kartları tamamen sil
void saveCardsForDeck(int deckId) {
  String ns = "dk" + String(deckId);
  prefs.begin(ns.c_str(), false);
  prefs.clear();
  prefs.putInt("cnt", cardCount);
  for (int i = 0; i < cardCount; i++) {
    prefs.putInt(   ("id" + String(i)).c_str(), cards[i].id);
    prefs.putString(("q"  + String(i)).c_str(), cards[i].question);
    prefs.putString(("a"  + String(i)).c_str(), cards[i].answer);
  }
  prefs.end();
}

// -1 = hiç kaydedilmemiş, 0 = boş deck (geçerli)
bool loadCardsForDeck(int deckId) {
  String ns = "dk" + String(deckId);
  prefs.begin(ns.c_str(), true);
  cardCount = prefs.getInt("cnt", -1);
  if (cardCount < 0) { prefs.end(); return false; }
  if (cardCount == 0) { prefs.end(); return true; }
  for (int i = 0; i < cardCount; i++) {
    cards[i].id       = prefs.getInt(   ("id" + String(i)).c_str(), i);
    cards[i].question = prefs.getString(("q"  + String(i)).c_str(), "");
    cards[i].answer   = prefs.getString(("a"  + String(i)).c_str(), "");
  }
  prefs.end();
  return true;
}

void saveScores() {
  if (activeDeckIdx < 0) return;
  String ns = "sc" + String(deckMeta[activeDeckIdx].id);
  prefs.begin(ns.c_str(), false);
  for (int i = 0; i < cardCount; i++)
    prefs.putInt(("s" + String(cards[i].id)).c_str(), score[i]);
  prefs.end();
}

void loadScores() {
  if (activeDeckIdx < 0) return;
  String ns = "sc" + String(deckMeta[activeDeckIdx].id);
  prefs.begin(ns.c_str(), true);
  for (int i = 0; i < cardCount; i++)
    score[i] = prefs.getInt(("s" + String(cards[i].id)).c_str(), 0);
  prefs.end();
}

void resetScores() {
  if (activeDeckIdx < 0) return;
  String ns = "sc" + String(deckMeta[activeDeckIdx].id);
  prefs.begin(ns.c_str(), false);
  for (int i = 0; i < cardCount; i++) {
    score[i] = 0;
    prefs.putInt(("s" + String(cards[i].id)).c_str(), 0);
  }
  prefs.end();
}

void saveSettings() {
  prefs.begin("fc_set", false);
  prefs.putBool("shuffle",    shuffleOn);
  prefs.putInt ("filter",     filterMode);
  prefs.putInt ("brightness", brightness);
  prefs.putInt ("activeDeck", activeDeckIdx);
  prefs.end();
}

void loadSettings() {
  prefs.begin("fc_set", true);
  shuffleOn     = prefs.getBool("shuffle",    false);
  filterMode    = prefs.getInt ("filter",     0);
  brightness    = prefs.getInt ("brightness", 1);
  activeDeckIdx = prefs.getInt ("activeDeck", -1);
  prefs.end();
}

void applyBrightness() {
  uint8_t val = (brightness == 0) ? 10 : (brightness == 1) ? 100 : 255;
  display.ssd1306_command(0x81);
  display.ssd1306_command(val);
}

// ════════════════════════════════════════════════════════
//  EKRAN YARDIMCILARI
// ════════════════════════════════════════════════════════
void drawTopBar(const char* title) {
  display.fillRect(0, 0, 128, 15, WHITE);
  display.drawLine(0, 15, 127, 15, BLACK);
  display.setTextColor(BLACK);
  display.setCursor(2, 4);
  display.print(title);
  display.setTextColor(WHITE);
}

void showStatus(const char* line1, const char* line2 = "") {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);
  display.setCursor(0, 20);
  display.println(line1);
  if (line2 && line2[0]) display.println(line2);
  display.display();
}

// ════════════════════════════════════════════════════════
//  WI-FI
// ════════════════════════════════════════════════════════
bool tryConnect(int timeoutMs) {
  unsigned long start = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - start < (unsigned long)timeoutMs)
    delay(200);
  return WiFi.status() == WL_CONNECTED;
}

bool connectWiFi() {
  for (int i = 0; i < NETWORK_COUNT; i++) {
    WiFi.disconnect(true);
    WiFi.mode(WIFI_STA);
    delay(300);

    showStatus("Connecting...", networks[i].ssid);

    if (networks[i].user == NULL) {
      WiFi.begin(networks[i].ssid, networks[i].pass);
    } else {
      esp_eap_client_set_identity((uint8_t*)networks[i].user, strlen(networks[i].user));
      esp_eap_client_set_username((uint8_t*)networks[i].user, strlen(networks[i].user));
      esp_eap_client_set_password((uint8_t*)networks[i].pass, strlen(networks[i].pass));
      esp_wifi_sta_enterprise_enable();
      WiFi.begin(networks[i].ssid);
    }

    if (tryConnect(10000)) {
      showStatus("WiFi OK!", WiFi.localIP().toString().c_str());
      delay(500);
      return true;
    }
  }

  showStatus("WiFi FAILED!", "No network found");
  delay(1500);
  return false;
}

// ════════════════════════════════════════════════════════
//  SYNC
// ════════════════════════════════════════════════════════
bool syncAll() {
  Serial.println("[SYNC] heap before: " + String(ESP.getFreeHeap()));
  showStatus("Syncing...");

  HTTPClient http;
  http.begin(String(SERVER) + "/all");
  http.setTimeout(15000);
  int code = http.GET();
  Serial.println("[SYNC] HTTP: " + String(code));

  if (code != 200) {
    showStatus("Sync failed!", ("HTTP: " + String(code)).c_str());
    http.end(); delay(2000); return false;
  }

  syncDoc.clear();
  WiFiClient* stream = http.getStreamPtr();
  DeserializationError err = deserializeJson(syncDoc, *stream);
  http.end();

  Serial.println("[SYNC] heap after parse: " + String(ESP.getFreeHeap()));

  if (err) {
    Serial.println("[SYNC] JSON err: " + String(err.c_str()));
    showStatus("JSON Error!", err.c_str());
    delay(1500); return false;
  }

  int newCount = 0, updCount = 0, delCount = 0;
  bool serverHasDeck[MAX_DECKS];
  memset(serverHasDeck, false, sizeof(serverHasDeck));

  for (JsonObject deck : syncDoc["decks"].as<JsonArray>()) {
    int    did   = deck["id"];
    String dname = turkishToAscii(deck["name"].as<String>());
    Serial.println("[SYNC] deck id=" + String(did) + " " + dname);

    cardCount = 0;
    for (JsonObject obj : deck["cards"].as<JsonArray>()) {
      if (cardCount >= MAX_CARDS) break;
      cards[cardCount].id       = obj["id"];
      cards[cardCount].question = turkishToAscii(obj["question"].as<String>());
      cards[cardCount].answer   = turkishToAscii(obj["answer"].as<String>());
      cardCount++;
    }
    Serial.println("[SYNC]  cards: " + String(cardCount));
    saveCardsForDeck(did);

    int existIdx = -1;
    for (int i = 0; i < deckMetaCount; i++)
      if (deckMeta[i].id == did) { existIdx = i; break; }

    if (existIdx >= 0) {
      deckMeta[existIdx].name      = dname;
      deckMeta[existIdx].cardCount = cardCount;
      serverHasDeck[existIdx] = true;
      updCount++;
    } else if (deckMetaCount < MAX_DECKS) {
      String ns = "sc" + String(did);
      prefs.begin(ns.c_str(), false);
      for (int i = 0; i < cardCount; i++)
        prefs.putInt(("s" + String(cards[i].id)).c_str(), 0);
      prefs.end();
      serverHasDeck[deckMetaCount] = true;
      deckMeta[deckMetaCount].id        = did;
      deckMeta[deckMetaCount].name      = dname;
      deckMeta[deckMetaCount].cardCount = cardCount;
      deckMetaCount++;
      newCount++;
    }
  }

  DeckMeta newMeta[MAX_DECKS];
  int newMetaCount = 0;
  int remapIdx[MAX_DECKS];
  memset(remapIdx, -1, sizeof(remapIdx));
  for (int i = 0; i < deckMetaCount; i++) {
    if (serverHasDeck[i]) { remapIdx[i] = newMetaCount; newMeta[newMetaCount++] = deckMeta[i]; }
    else delCount++;
  }
  for (int i = 0; i < newMetaCount; i++) deckMeta[i] = newMeta[i];
  deckMetaCount = newMetaCount;

  if (activeDeckIdx >= 0) {
    if (remapIdx[activeDeckIdx] == -1) activeDeckIdx = deckMetaCount > 0 ? 0 : -1;
    else activeDeckIdx = remapIdx[activeDeckIdx];
  }

  saveDeckMeta();
  syncDoc.clear();

  Serial.println("[SYNC] done new=" + String(newCount) + " upd=" + String(updCount) + " del=" + String(delCount));
  char msg[32]; snprintf(msg, 32, "%d new, %d upd", newCount, updCount);
  showStatus("Sync done!", msg);
  delay(1200);
  return deckMetaCount > 0;
}

// ── Aktif deck yükle ─────────────────────────────────────
bool activateDeck(int idx) {
  if (idx < 0 || idx >= deckMetaCount) return false;
  activeDeckIdx = idx;
  if (!loadCardsForDeck(deckMeta[idx].id)) return false;
  loadScores();
  buildFilter();
  currentCard = pickNextCard();
  return true;
}

// ════════════════════════════════════════════════════════
//  KART SEÇİMİ
// ════════════════════════════════════════════════════════
int pickNextCard() {
  if (filteredCount == 0) return 0;
  if (filteredCount == 1) return filteredCards[0];

  if (shuffleOn) {
    int r, tries = 0;
    do { r = filteredCards[esp_random() % filteredCount]; tries++; }
    while (r == currentCard && tries < 10);
    return r;
  }

  int maxScore = score[filteredCards[0]];
  for (int i = 1; i < filteredCount; i++)
    if (score[filteredCards[i]] > maxScore) maxScore = score[filteredCards[i]];

  int weights[MAX_CARDS], total = 0;
  for (int i = 0; i < filteredCount; i++) {
    weights[i] = (filteredCards[i] == currentCard) ? 0 : (maxScore + 1) - score[filteredCards[i]];
    total += weights[i];
  }
  if (total == 0) return filteredCards[0];

  uint32_t r = esp_random() % (uint32_t)total;
  for (int i = 0; i < filteredCount; i++) {
    if (r < (uint32_t)weights[i]) return filteredCards[i];
    r -= weights[i];
  }
  return filteredCards[0];
}

// ════════════════════════════════════════════════════════
//  BUTONLAR & GÜÇ
// ════════════════════════════════════════════════════════
bool shortPressed(int pin) {
  if (digitalRead(pin) == HIGH) return false;
  unsigned long t = millis(); delay(50);
  if (digitalRead(pin) == HIGH) return false;
  while (digitalRead(pin) == LOW) if (millis() - t > LONG_PRESS_MS) return false;
  return true;
}

void powerOff() {
  saveScores(); saveSettings();
  display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
  display.setCursor(20, 25); display.println("Shutting down...");
  display.display(); delay(800);
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  while (digitalRead(btnMenu) == LOW); delay(100);
  esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, LOW);
  esp_deep_sleep_start();
}

void checkWakeup() {
  if (esp_sleep_get_wakeup_cause() != ESP_SLEEP_WAKEUP_EXT0) return;
  unsigned long t = millis();
  while (digitalRead(btnMenu) == LOW) if (millis() - t >= LONG_PRESS_MS) return;
  esp_sleep_enable_ext0_wakeup(WAKEUP_PIN, LOW);
  esp_deep_sleep_start();
}

// ════════════════════════════════════════════════════════
//  KELIME BAZLI SATIR KESME
// ════════════════════════════════════════════════════════
int drawWrapped(const char* text, int startY, int maxY, int textSize = 1) {
  int charW = 6 * textSize, lineH = 8 * textSize + 2;
  int maxCh = 128 / charW;
  int len = strlen(text), i = 0, y = startY;

  while (i < len && y + lineH <= maxY) {
    int rem = len - i;
    if (rem <= maxCh) {
      display.setCursor(0, y); display.print(text + i); return y;
    }
    int breakAt = -1;
    for (int j = i + maxCh; j > i; j--) if (text[j] == ' ') { breakAt = j; break; }
    char buf[22];
    int chunk = (breakAt == -1) ? maxCh : breakAt - i;
    strncpy(buf, text + i, chunk); buf[chunk] = '\0';
    display.setCursor(0, y); display.print(buf);
    i += (breakAt == -1) ? maxCh : breakAt + 1;
    y += lineH;
  }
  return y;
}

// ════════════════════════════════════════════════════════
//  EKRAN ÇİZİM
// ════════════════════════════════════════════════════════
void drawQuestion() {
  display.clearDisplay(); display.setTextSize(1);
  display.fillRect(0, 0, 128, 15, WHITE);
  display.drawLine(0, 15, 127, 15, BLACK);
  display.setTextColor(BLACK); display.setCursor(2, 4);
  // DÜZELTİLDİ: index taşması koruması
  if (currentCard >= cardCount && cardCount > 0) currentCard = 0;
  String num = String(cardCount > 0 ? currentCard + 1 : 0) + "/" + String(cardCount);
  int maxTLen = 16 - num.length();
  String title = (activeDeckIdx >= 0) ? deckMeta[activeDeckIdx].name : "Flashcard";
  display.print(title.substring(0, maxTLen));
  display.setCursor(128 - num.length() * 6 - 2, 4); display.print(num);
  display.setTextColor(WHITE);
  if (cardCount > 0) drawWrapped(cards[currentCard].question.c_str(), 17, 50, 1);
  display.setCursor(0, 56);
  display.print("Sc:"); display.print(cardCount > 0 ? score[currentCard] : 0); display.print(" [OK]=Show");
  display.display();
}

void drawAnswer() {
  display.clearDisplay(); display.setTextSize(1);
  drawTopBar("ANSWER");
  int alen = cards[currentCard].answer.length();
  if (alen <= 10) { display.setTextSize(2); drawWrapped(cards[currentCard].answer.c_str(), 20, 50, 2); }
  else            { display.setTextSize(1); drawWrapped(cards[currentCard].answer.c_str(), 20, 50, 1); }
  display.setTextSize(1); display.setCursor(0, 56);
  display.print("DOWN=No  OK=Know");
  display.display();
}

void drawStats() {
  int known = 0, learning = 0, unseen = 0, hard = 0;
  for (int i = 0; i < cardCount; i++) {
    if      (score[i] >= 3) known++;
    else if (score[i] >= 1) learning++;
    else if (score[i] == 0) unseen++;
    else                    hard++;
  }
  display.clearDisplay(); display.setTextSize(1);
  String dn = (activeDeckIdx >= 0) ? deckMeta[activeDeckIdx].name.substring(0, 12) : "";
  drawTopBar(dn.c_str());
  display.setCursor(0, 17);
  display.print("Total:    "); display.println(cardCount);
  display.print("Known:    "); display.println(known);
  display.print("Learning: "); display.println(learning);
  display.print("Unseen:   "); display.println(unseen);
  display.print("Hard:     "); display.println(hard);
  display.display();
}

const char* filterLabel()     { return filterMode==0?"ALL":filterMode==1?"HARD":"NEW"; }
const char* brightnessLabel() { return brightness==0?"LOW":brightness==1?"MED":"HIGH"; }

void drawMenu() {
  display.clearDisplay(); display.setTextSize(1);
  drawTopBar("MENU");
  for (int i = 0; i < MENU_COUNT; i++) {
    int y = 16 + i * 9;
    if (i == menuIndex) { display.fillRect(0, y, 128, 9, WHITE); display.setTextColor(BLACK); }
    else display.setTextColor(WHITE);
    display.setCursor(4, y + 1); display.print(menuItems[i]);
    if (i == 0 && activeDeckIdx >= 0) {
      display.setCursor(50, y + 1);
      display.print(deckMeta[activeDeckIdx].name.substring(0, 9));
    }
  }
  display.display();
}

void drawSettings() {
  display.clearDisplay(); display.setTextSize(1);
  drawTopBar("SETTINGS");
  for (int i = 0; i < SETTINGS_COUNT; i++) {
    int y = 16 + i * 9;
    if (i == settingsIndex) { display.fillRect(0, y, 128, 9, WHITE); display.setTextColor(BLACK); }
    else display.setTextColor(WHITE);
    display.setCursor(4, y + 1); display.print(settingsItems[i]);
    display.setCursor(75, y + 1);
    if (i == 0) display.print(shuffleOn ? "ON" : "OFF");
    if (i == 1) { display.print(filterLabel()); if (filterFallback) display.print("!"); }
    if (i == 2) display.print(brightnessLabel());
  }
  display.display();
}

void drawDeckSelect() {
  display.clearDisplay(); display.setTextSize(1);
  drawTopBar("SELECT DECK");
  int visible = 5, startIdx = 0;
  if (deckSelectIndex >= visible) startIdx = deckSelectIndex - visible + 1;
  for (int i = 0; i < visible; i++) {
    int di = startIdx + i;
    if (di >= deckMetaCount) break;
    int y = 16 + i * 9;
    if (di == deckSelectIndex) { display.fillRect(0, y, 128, 9, WHITE); display.setTextColor(BLACK); }
    else display.setTextColor(WHITE);
    display.setCursor(4, y + 1);
    display.print(di == activeDeckIdx ? "*" : " ");
    display.print(deckMeta[di].name.substring(0, 13));
    display.setCursor(112, y + 1); display.print(deckMeta[di].cardCount);
  }
  display.display();
}

// ════════════════════════════════════════════════════════
//  SETUP
// ════════════════════════════════════════════════════════
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("[BOOT] heap: " + String(ESP.getFreeHeap()));

  pinMode(btnMenu, INPUT_PULLUP);
  pinMode(btnOk,   INPUT_PULLUP);
  pinMode(btnDown, INPUT_PULLUP);
  checkWakeup();

  Wire.begin(21, 22);
  display.begin(SSD1306_SWITCHCAPVCC, 0x3C);
  display.clearDisplay(); display.display();

  loadSettings();
  applyBrightness();
  loadDeckMeta();

  Serial.println("[BOOT] decks in NVS: " + String(deckMetaCount) + " activeDeck: " + String(activeDeckIdx));

  if (deckMetaCount > 0) {
    if (activeDeckIdx < 0 || activeDeckIdx >= deckMetaCount) activeDeckIdx = 0;
    if (activateDeck(activeDeckIdx)) { drawQuestion(); return; }
  }

  showStatus("No decks.", "Use Sync");
  delay(800);
  if (connectWiFi() && syncAll() && deckMetaCount > 0) {
    WiFi.disconnect(true);
    activeDeckIdx = 0;
    activateDeck(0);
    saveSettings();
    drawQuestion();
  } else {
    WiFi.disconnect(true);
    showStatus("No data!", "Use menu>Sync");
    appState = STATE_MENU;
    while (!shortPressed(btnMenu));
    drawMenu();
  }
}

// ════════════════════════════════════════════════════════
//  LOOP
// ════════════════════════════════════════════════════════
void loop() {
  if (digitalRead(btnMenu) == LOW) {
    unsigned long t = millis(); delay(50);
    while (digitalRead(btnMenu) == LOW) if (millis() - t >= LONG_PRESS_MS) powerOff();
    if (millis() - t < 50) return;
    switch (appState) {
      case STATE_SETTINGS:
      case STATE_DECKS:
        appState = STATE_MENU; drawMenu(); break;
      case STATE_MENU:
        if (cardCount > 0) { appState = STATE_QUESTION; drawQuestion(); }
        break;
      default:
        appState = STATE_MENU; menuIndex = 0; drawMenu(); break;
    }
    return;
  }

  if (appState == STATE_QUESTION) {
    if (shortPressed(btnOk)) { appState = STATE_ANSWER; drawAnswer(); }
  }

  else if (appState == STATE_ANSWER) {
    if (shortPressed(btnOk)) {
      score[currentCard]++; saveScores(); buildFilter();
      currentCard = pickNextCard(); appState = STATE_QUESTION; drawQuestion();
    }
    if (shortPressed(btnDown)) {
      score[currentCard]--; saveScores(); buildFilter();
      currentCard = pickNextCard(); appState = STATE_QUESTION; drawQuestion();
    }
  }

  else if (appState == STATE_MENU) {
    if (shortPressed(btnDown)) { menuIndex = (menuIndex + 1) % MENU_COUNT; drawMenu(); }
    if (shortPressed(btnOk)) {
      switch (menuIndex) {

        case 0: // Decks
          if (deckMetaCount == 0) { showStatus("No decks!", "Sync first"); delay(1500); drawMenu(); }
          else { deckSelectIndex = max(0, activeDeckIdx); appState = STATE_DECKS; drawDeckSelect(); }
          break;

        case 1: // Sync
          if (connectWiFi()) {
            bool ok = syncAll();
            WiFi.disconnect(true);
            if (ok) {
              if (activeDeckIdx < 0 || activeDeckIdx >= deckMetaCount) activeDeckIdx = 0;
              activateDeck(activeDeckIdx);
              saveSettings();
              appState = STATE_QUESTION; drawQuestion();
            } else drawMenu();
          } else drawMenu();
          break;

        case 2: // Stats
          drawStats();
          while (digitalRead(btnOk)==HIGH && digitalRead(btnDown)==HIGH && digitalRead(btnMenu)==HIGH);
          delay(200); drawMenu();
          break;

        case 3: // Settings
          settingsIndex = 0; appState = STATE_SETTINGS; drawSettings();
          break;

        case 4: // Reset Scores
          display.clearDisplay(); display.setTextSize(1); display.setTextColor(WHITE);
          drawTopBar("RESET?");
          display.setCursor(4, 20); display.print("Reset: ");
          display.println(activeDeckIdx >= 0 ? deckMeta[activeDeckIdx].name : "");
          display.setCursor(4, 44); display.println("DOWN=No  OK=Yes");
          display.display();
          while (true) {
            if (shortPressed(btnOk))   { resetScores(); loadScores(); buildFilter(); showStatus("Reset!"); delay(1000); break; }
            if (shortPressed(btnDown)) { break; }
          }
          drawMenu();
          break;
      }
    }
  }

  else if (appState == STATE_SETTINGS) {
    if (shortPressed(btnDown)) { settingsIndex = (settingsIndex + 1) % SETTINGS_COUNT; drawSettings(); }
    if (shortPressed(btnOk)) {
      switch (settingsIndex) {
        case 0: shuffleOn  = !shuffleOn; break;
        case 1: filterMode = (filterMode + 1) % 3; buildFilter(); currentCard = pickNextCard(); break;
        case 2: brightness = (brightness + 1) % 3; applyBrightness(); break;
      }
      saveSettings(); drawSettings();
    }
  }

  else if (appState == STATE_DECKS) {
    if (shortPressed(btnDown)) { deckSelectIndex = (deckSelectIndex + 1) % deckMetaCount; drawDeckSelect(); }
    if (shortPressed(btnOk)) {
      if (deckSelectIndex == activeDeckIdx) { appState = STATE_QUESTION; drawQuestion(); return; }
      activeDeckIdx = deckSelectIndex;
      if (activateDeck(activeDeckIdx)) { saveSettings(); appState = STATE_QUESTION; drawQuestion(); }
      else { showStatus("Load failed!"); delay(1500); drawDeckSelect(); }
    }
  }
}
