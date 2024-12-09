#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <PubSubClient.h>
#include <FS.h>
#include <LittleFS.h>
#include <ArduinoJson.h>
#include <Tone32.hpp>

// owlcms parameters
// referee = 0 is used when all three referees are wired to the same device
// if each referee has their own device, set referee to 1, 2 or 3
const int referee = 0;
const char *platform = "A";

// pins for refs 1 2 and 3 (1 good, 1 bad, 2 good, etc.)
int decisionPins[] = {14, 27, 26, 25, 33, 32};
int ledPins[] = {15, 5, 19};
int buzzerPins[] = {4, 18, 21};

// number of beeps when referee wakeup is received
// set to 0 to disable beeps.
const int nbBeeps = 3;
const note_t cfgBeepNote = NOTE_F;
const int cfgBeepOctave = 7; // F7
const int cfgBeepMilliseconds = 100;
const int cfgSilenceMilliseconds = 50; // time between beeps
const int cfgLedDuration = 20000;      // led stays for this maximum time;

// referee summon parameters
const int nbSummonBeeps = 1;
const note_t cfgSummonNote = NOTE_F;
const int cfgSummonOctave = 7; // F7
const int cfgSummonBeepMilliseconds = 3000;
const int cfgSummonSilenceMilliseconds = 0;
const int cfgSummonLedDuration = cfgSummonBeepMilliseconds;

// ====== END CONFIG SECTION ======================================================

#define ELEMENTCOUNT(x) (sizeof(x) / sizeof(x[0]))
// networking values
String macAddress;
char mac[50];
char clientId[50] = "RefereeHubClient";

// owlcms values
char fop[20];
int ref13Number = 0;
// 6 pins where we have to detect transitions. initial state unknown.
int prevDecisionPinState[] = {-1, -1, -1, -1, -1, -1};

// for each referee, a Tone generator, and control for a LED
Tone32 tones[3] = {Tone32(buzzerPins[0], 0), Tone32(buzzerPins[1], 1), Tone32(buzzerPins[2], 2)};
int beepingIterations[] = {0, 0, 0};
int ledStartedMillis[] = {0, 0, 0};
int ledDuration[] = {0, 0, 0};
note_t beepNote;
int beepOctave;
int silenceMilliseconds;
int beepMilliseconds;

const char *ap_ssid = "RefereeHub_Configuration_AP";
const char *ap_password = "123456789";

String connect_ssid;
String connect_password;

AsyncWebServer server(80);
WiFiClient espClient;
PubSubClient mqttClient(espClient);

const int ledPin = 2; // Pin dla diody

bool isConnecting = false;

struct WiFiNetwork
{
  String ssid;
  String password;
};

WiFiNetwork savedNetworks[5]; // Maksymalnie 5 zapisanych sieci
const char *networksFile = "/networks.json";

// Plik z konfiguracją
const char *configFile = "/config.json";

struct MQTTConfig
{
  IPAddress server;
  int port;
  String user;
  String password;
};

// Dane konfiguracyjne
MQTTConfig mqttConfig;

// const char index_html[] PROGMEM = R"rawliteral(
//)rawliteral";

// Funkcja do zapisu zapisanych sieci do pliku
void saveNetworksToFile()
{
  JsonDocument doc;
  JsonArray array = doc["networks"].to<JsonArray>();

  for (const auto &network : savedNetworks)
  {
    if (!network.ssid.isEmpty())
    {
      JsonObject obj = array.add<JsonObject>();
      obj["ssid"] = network.ssid;
      obj["password"] = network.password;
    }
  }

  File file = LittleFS.open(networksFile, FILE_WRITE);
  if (file)
  {
    serializeJson(doc, file);
    file.close();
    Serial.println("Zapisano sieci Wi-Fi.");
  }
  else
  {
    Serial.println("Błąd zapisu do pliku sieci Wi-Fi.");
  }
}

// Funkcja do odczytu zapisanych sieci z pliku
void loadNetworksFromFile()
{
  if (!LittleFS.exists(networksFile))
  {
    Serial.println("Plik sieci Wi-Fi nie istnieje.");
    return;
  }

  File file = LittleFS.open(networksFile, FILE_READ);
  if (!file)
  {
    Serial.println("Nie udało się otworzyć pliku sieci Wi-Fi.");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    Serial.println("Błąd odczytu zapisanych sieci Wi-Fi.");
    file.close();
    return;
  }
  file.close();

  JsonArray array = doc["networks"];
  int i = 0;
  for (JsonObject obj : array)
  {
    if (i >= 5)
      break; // Maksymalnie 5 sieci
    savedNetworks[i].ssid = obj["ssid"].as<String>();
    savedNetworks[i].password = obj["password"].as<String>();
    i++;
  }

  Serial.println("Wczytano zapisane sieci Wi-Fi.");
}

// Funkcja do zapisu konfiguracji
void saveConfig()
{
  JsonDocument doc;
  JsonObject mqtt = doc["mqtt"].to<JsonObject>();
  mqtt["server"] = mqttConfig.server;
  mqtt["port"] = mqttConfig.port;
  mqtt["user"] = mqttConfig.user;
  mqtt["password"] = mqttConfig.password;

  File file = LittleFS.open(configFile, FILE_WRITE);
  if (file)
  {
    if (serializeJson(doc, file))
    {
      Serial.println("Konfiguracja zapisana pomyślnie.");
    }
    else
    {
      Serial.println("Błąd serializacji konfiguracji.");
    }
    file.close();
  }
  else
  {
    Serial.println("Błąd otwarcia pliku konfiguracyjnego do zapisu.");
  }
}

// Funkcja do odczytu konfiguracji
void loadConfig()
{
  File file = LittleFS.open(configFile, FILE_READ);
  if (!file)
  {
    Serial.println("Plik konfiguracyjny nie istnieje.");
    return;
  }

  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, file);
  if (error)
  {
    Serial.println("Błąd deserializacji pliku konfiguracyjnego.");
    file.close();
    return;
  }
  file.close();

  mqttConfig.server.fromString(doc["mqtt"]["server"].as<String>());
  mqttConfig.port = doc["mqtt"]["port"].as<int>();
  mqttConfig.user = doc["mqtt"]["user"].as<String>();
  mqttConfig.password = doc["mqtt"]["password"].as<String>();

  Serial.println("Konfiguracja załadowana pomyślnie.");
}

String scanNetworks()
{
  int n = WiFi.scanNetworks();
  String json = "[";
  for (int i = 0; i < n; ++i)
  {
    if (i)
      json += ",";
    json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
  }
  json += "]";
  WiFi.scanDelete();
  return json;
}

void connectToWiFi(String ssid, String password)
{
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), password.c_str());
  isConnecting = true;

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 10000)
  {
    delay(1000);
    Serial.println("Próba łączenia...");
  }

  isConnecting = false;

  if (WiFi.status() == WL_CONNECTED)
  {
    loadConfig();
    Serial.println("\nPołączono z siecią Wi-Fi!");
    Serial.print("Adres IP: ");
    Serial.println(WiFi.localIP());
  }
  else
  {
    Serial.println("Nie udało się połączyć z siecią Wi-Fi. Powrót do trybu AP.");
    WiFi.mode(WIFI_AP);
    WiFi.softAP(ap_ssid, ap_password);
    if (WiFi.getMode() == WIFI_AP)
    {
      digitalWrite(ledPin, millis() % 1000 < 500 ? HIGH : LOW);
    }
  }
}

void listFiles()
{
  Serial.println("Lista plików w LittleFS:");
  File root = LittleFS.open("/");
  if (!root || !root.isDirectory())
  {
    Serial.println("Brak katalogu głównego lub błąd.");
    return;
  }

  File file = root.openNextFile();
  while (file)
  {
    Serial.printf("Plik: %s, Rozmiar: %d bajtów\n", file.name(), file.size());
    file = root.openNextFile();
  }
}

void mqttReconnect()
{
  long r = random(1000);
  sprintf(clientId, "owlcms-%ld", r);
  if (WiFi.status() != WL_CONNECTED)
  {
    connectToWiFi(connect_ssid, connect_password);
  }
  while (!mqttClient.connected())
  {
    Serial.print(macAddress);
    Serial.print(" connecting to MQTT server...");

    if (mqttClient.connect(clientId, mqttConfig.user.c_str(), mqttConfig.password.c_str()))
    {
      Serial.println(" connected");

      char requestTopic[50];
      sprintf(requestTopic, "owlcms/decisionRequest/%s/+", fop);
      mqttClient.subscribe(requestTopic);

      char ledTopic[50];
      sprintf(ledTopic, "owlcms/led/#", fop);
      mqttClient.subscribe(ledTopic);

      char summonTopic[50];
      sprintf(summonTopic, "owlcms/summon/#", fop);
      mqttClient.subscribe(summonTopic);
    }
    else
    {
      Serial.print("MQTT connection failed, rc=");
      Serial.print(mqttClient.state());
      Serial.println(" try again in 5 second");
      delay(5000);
    }
  }
}

void sendDecision(int ref02Number, const char *decision)
{
  if (referee > 0)
  {
    // software configuration
    ref02Number = referee - 1;
  }
  char topic[50];
  sprintf(topic, "owlcms/decision/%s", fop);
  char message[32];
  sprintf(message, "%i %s", ref02Number + 1, decision);

  mqttClient.publish(topic, message);
  Serial.print(topic);
  Serial.print(" ");
  Serial.print(message);
  Serial.println(" sent.");
}

void setupPins()
{
  for (int j = 0; j < ELEMENTCOUNT(decisionPins); j++)
  {
    pinMode(decisionPins[j], INPUT_PULLUP);
    prevDecisionPinState[j] = digitalRead(decisionPins[j]);
  }
  for (int j = 0; j < ELEMENTCOUNT(ledPins); j++)
  {
    pinMode(ledPins[j], OUTPUT);
  }
  for (int j = 0; j < ELEMENTCOUNT(buzzerPins); j++)
  {
    pinMode(buzzerPins[j], OUTPUT);
  }
}

void setupTones()
{
  for (int j = 0; j < ELEMENTCOUNT(tones); j++)
  {
    tones[j] = Tone32(buzzerPins[j], j);
  }
}

void buttonLoop()
{
  for (int j = 0; j < ELEMENTCOUNT(decisionPins); j++)
  {
    int state = digitalRead(decisionPins[j]);
    int prevState = prevDecisionPinState[j];
    if (state != prevState)
    {
      prevDecisionPinState[j] = state;
      if (state == LOW)
      {
        if (j % 2 == 0)
        {
          sendDecision(j / 2, "good");
        }
        else
        {
          sendDecision(j / 2, "bad");
        }
        return;
      }
    }
  }
}

void buzzerLoop()
{
  for (int j = 0; j < ELEMENTCOUNT(beepingIterations); j++)
  {
    if (beepingIterations[j] > 0 && !tones[j].isPlaying())
    {
      if (((beepingIterations[j] % 2) == 0))
      {
        Serial.print(millis());
        Serial.println(" sound on");
        tones[j].playNote(beepNote, beepOctave, beepMilliseconds);
      }
      else
      {
        Serial.print(millis());
        Serial.println(" sound off");
        tones[j].silence(silenceMilliseconds);
      }
    }
    tones[j].update(); // turn off sound if duration reached.
    if (!tones[j].isPlaying())
    {
      beepingIterations[j]--;
    }
  }
}

void ledLoop()
{
  for (int j = 0; j < ELEMENTCOUNT(ledStartedMillis); j++)
  {
    if (ledStartedMillis[j] > 0 && millis() - ledStartedMillis[j] >= ledDuration[j])
    {
      digitalWrite(ledPins[j], LOW);
      ledStartedMillis[j] = 0; // Resetowanie czasu
    }
  }
}

String decisionRequestTopic = String("owlcms/decisionRequest/") + fop;
String summonTopic = String("owlcms/summon/") + fop;
String ledTopic = String("owlcms/led/") + fop;

void changeReminderStatus(int ref02Number, boolean warn)
{
  Serial.print("reminder ");
  Serial.print(warn);
  Serial.print(" ");
  Serial.println(ref02Number + 1);
  if (warn)
  {
    digitalWrite(ledPins[ref02Number], HIGH);
    beepingIterations[ref02Number] = nbBeeps * 2;
    ledStartedMillis[ref02Number] = millis();
    ledDuration[ref02Number] = cfgLedDuration;
    beepNote = cfgBeepNote;
    beepOctave = cfgBeepOctave;
    silenceMilliseconds = cfgSilenceMilliseconds;
    beepMilliseconds = cfgBeepMilliseconds;
  }
  else
  {
    digitalWrite(ledPins[ref02Number], LOW);
    beepingIterations[ref02Number] = 0;
    tones[ref02Number].stopPlaying();
  }
}

void changeSummonStatus(int ref02Number, boolean warn)
{
  Serial.print("summon ");
  Serial.print(warn);
  Serial.print(" ");
  Serial.println(ref02Number + 1);
  if (warn)
  {
    digitalWrite(ledPins[ref02Number], HIGH);
    beepingIterations[ref02Number] = nbSummonBeeps * 2;
    ledStartedMillis[ref02Number] = millis();
    ledDuration[ref02Number] = cfgSummonLedDuration;
    beepNote = cfgSummonNote;
    beepOctave = cfgSummonOctave;
    silenceMilliseconds = cfgSummonSilenceMilliseconds;
    beepMilliseconds = cfgSummonBeepMilliseconds;
  }
  else
  {
    digitalWrite(ledPins[ref02Number], LOW);
    beepingIterations[ref02Number] = 0;
    tones[ref02Number].stopPlaying();
  }
}

void callback(char *topic, byte *message, unsigned int length)
{

  String stTopic = String(topic);
  Serial.print("Message arrived on topic: ");
  Serial.print(stTopic);
  Serial.print("; Message: ");

  String stMessage;
  // convert byte to char
  for (int i = 0; i < length; i++)
  {
    stMessage += (char)message[i];
  }
  Serial.println(stMessage);

  int refIndex = stTopic.lastIndexOf("/") + 1;
  String refString = stTopic.substring(refIndex);
  int ref13Number = refString.toInt();

  if (stTopic.startsWith(decisionRequestTopic))
  {
    changeReminderStatus(ref13Number - 1, stMessage.startsWith("on"));
  }
  else if (stTopic.startsWith(summonTopic))
  {
    if (ref13Number == 0)
    {
      // topic did not end with number, blink all devices
      for (int j = 0; j < ELEMENTCOUNT(ledPins); j++)
      {
        changeSummonStatus(j, stMessage.startsWith("on"));
      }
    }
    else
    {
      changeSummonStatus(ref13Number - 1, stMessage.startsWith("on"));
    }
  }
  else if (stTopic.startsWith(ledTopic))
  {
    if (ref13Number == 0)
    {
      // topic did not end with number, blink all devices
      for (int j = 0; j < ELEMENTCOUNT(ledPins); j++)
      {
        changeSummonStatus(j, stMessage.startsWith("on"));
      }
    }
    else
    {
      changeSummonStatus(ref13Number - 1, stMessage.startsWith("on"));
    }
  }
}

void ServerSetupEndpoint()
{
  server.on("/save_network", HTTP_POST, [](AsyncWebServerRequest *request)
            {
  if (!request->hasParam("body", true)) {
    request->send(400, "text/plain", "Brak danych.");
    return;
  }

  String body = request->getParam("body", true)->value();
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    request->send(400, "text/plain", "Błąd w danych JSON.");
    return;
  }

  String ssid = doc["ssid"].as<String>();
  String password = doc["password"].as<String>();

  if (ssid.isEmpty()) {
    request->send(400, "text/plain", "SSID nie może być pusty.");
    return;
  }

  // Sprawdź, czy sieć już istnieje
  for (auto &network : savedNetworks) {
    if (network.ssid == ssid) {
      network.password = password;
      saveNetworksToFile();
      request->send(200, "text/plain", "Zaktualizowano sieć.");
      return;
    }
  }

  // Dodaj nową sieć
  for (auto &network : savedNetworks) {
    if (network.ssid.isEmpty()) {
      network.ssid = ssid;
      network.password = password;
      saveNetworksToFile();
      request->send(200, "text/plain", "Dodano sieć.");
      return;
    }
  }

  request->send(400, "text/plain", "Brak miejsca na nowe sieci."); });

  server.on("/get_saved_networks", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  JsonDocument doc;
  JsonArray array = doc.to<JsonArray>();

  for (const auto &network : savedNetworks) {
    if (!network.ssid.isEmpty()) {
      JsonObject obj = array.add<JsonObject>();
      obj["ssid"] = network.ssid;
    }
  }

  String json;
  serializeJson(doc, json);
  request->send(200, "application/json", json); });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request)
            {
  File file = LittleFS.open("/index.html", "r");
  if (!file) {
    request->send(500, "text/plain", "Nie można załadować pliku.");
    return;
  }
  request->send(file, "text/html", false);
  file.close(); });

  server.on("/scan", HTTP_GET, [](AsyncWebServerRequest *request)
            { request->send(200, "application/json", scanNetworks()); });

  server.on("/connect", HTTP_POST, [](AsyncWebServerRequest *request)
            {
  if (!request->hasParam("body", true)) {
    request->send(400, "text/plain", "Brak danych.");
    return;
  }

  String body = request->getParam("body", true)->value();
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, body);

  if (error) {
    request->send(400, "text/plain", "Błąd w danych JSON.");
    return;
  }

  String ssid = doc["ssid"].as<String>();
  String password = doc["password"].as<String>();

  connectToWiFi(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 10) {
    delay(1000);
    attempts++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    String ip = WiFi.localIP().toString();
    request->send(200, "text/plain", "Połączono z siecią Wi-Fi. IP:"+ ip);
  } else {
    request->send(500, "text/plain", "Nie udało się połączyć z siecią.");
  } });

  server.on("/mqtt", HTTP_GET, [](AsyncWebServerRequest *request)
            {
    mqttConfig.server.fromString(request->getParam("server")->value());
    mqttConfig.port = request->getParam("port")->value().toInt();
    mqttConfig.user = request->getParam("user")->value();
    mqttConfig.password = request->getParam("password")->value();
    saveConfig();
    request->send(200, "text/plain", "Ustawienia MQTT zapisane.");
    mqttClient.setServer(mqttConfig.server.toString().c_str(), mqttConfig.port); });
}

void setup()
{
  Serial.begin(115200);
  pinMode(ledPin, OUTPUT);
  if (!LittleFS.begin())
  {
    Serial.println("Nie udało się zainicjować LittleFS");
    return;
  }

  Serial.println("LittleFS zainicjalizowany.");
  listFiles();

  WiFi.mode(WIFI_AP);
  WiFi.softAP(ap_ssid, ap_password);
  Serial.println("Uruchomiono tryb AP.");
  if (WiFi.getMode() == WIFI_AP)
  {
    digitalWrite(ledPin, millis() % 1000 < 500 ? HIGH : LOW);
  }

  server.serveStatic("/", LittleFS, "/").setDefaultFile("index.html").setCacheControl("max-age=600");
  ServerSetupEndpoint();
  server.begin();
  mqttClient.setKeepAlive(20);
  // wait for serial port to become available
  while (!Serial.available())
  {
    delay(50);
  }
  randomSeed(analogRead(0));
  connectToWiFi(connect_ssid, connect_password);
  macAddress = WiFi.macAddress();
  macAddress.toCharArray(clientId, macAddress.length() + 1);
  mqttConfig.server = WiFi.localIP();
  Serial.print("MQTT server: ");
  Serial.println(mqttConfig.server);
  mqttClient.setServer(mqttConfig.server, mqttConfig.port);
  mqttClient.setCallback(callback);
  setupPins();
  setupTones();

  strcpy(fop, platform);

  mqttReconnect();
}

void loop()
{
  if (isConnecting)
  {
    digitalWrite(ledPin, millis() % 500 < 250 ? HIGH : LOW);
    Serial.println("loop : Connecting");
  }
  else
  {
    digitalWrite(ledPin, WiFi.status() == WL_CONNECTED ? HIGH : LOW);
    Serial.println("loop : Conected");
  }

  if (!mqttClient.connected() && WiFi.status() == WL_CONNECTED)
  {
    if (mqttClient.connect(clientId, mqttConfig.user.c_str(), mqttConfig.password.c_str()))
    {
      Serial.println("Połączono z brokerem MQTT.");
    }
    else
    {
      Serial.print("Błąd połączenia z MQTT. Kod: ");
      Serial.println(mqttClient.state());
      delay(5000); // Czas na ponowienie próby
    }
  }
  mqttClient.loop();
}