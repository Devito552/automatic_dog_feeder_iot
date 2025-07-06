#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Servo.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Configurações do Display OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define SDA_PIN 12
#define SCL_PIN 14

// Configurações do Servo Digital
#define SERVO_PIN 16  // D0

// Configurações WiFi
const char* ssid = "SEU_WIFI";
const char* password = "SUA_SENHA";

// Objetos
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Servo servoMotor;
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000); // UTC-3 (Brasil)

// Configurações da Rosca Sem Fim
const int SERVO_STOP = 90;      // Posição de parada (servo digital)
const int SERVO_FORWARD = 120;  // Rotação para frente (ajustar conforme servo)
const int SERVO_BACKWARD = 60;  // Rotação para trás (limpeza/emergência)
const int ROTATION_DELAY = 100; // Delay entre movimentos (ms)

// Variáveis globais
struct FeedingTime {
  int hour;
  int minute;
  int rotations;  // número de rotações da rosca
  bool active;
};

FeedingTime feedingTimes[4] = {
  {8, 0, 3, true},   // 8h00 - 3 rotações
  {12, 0, 3, true},  // 12h00 - 3 rotações  
  {18, 0, 3, true},  // 18h00 - 3 rotações
  {22, 0, 2, false}  // 22h00 - 2 rotações (desabilitado)
};

int totalFeedings = 0;
int totalRotations = 0;
int lastFeedHour = -1;
bool manualFeedRequested = false;
int manualRotations = 3;
unsigned long lastDisplayUpdate = 0;
int displayMode = 0; // 0=status, 1=horários, 2=wifi, 3=estatísticas
bool servoAttached = false;
int currentServoPosition = SERVO_STOP;

void setup() {
  Serial.begin(115200);
  
  // Inicializa I2C e Display
  Wire.begin(SDA_PIN, SCL_PIN);
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Falha no display OLED");
    for(;;);
  }
  
  // Inicializa servo motor
  servoMotor.attach(SERVO_PIN);
  servoMotor.write(SERVO_STOP);
  servoAttached = true;
  delay(1000);
  
  // Tela inicial
  displayStartup();
  
  // Conecta WiFi
  connectWiFi();
  
  // Inicializa NTP
  timeClient.begin();
  
  // Configura servidor web
  setupWebServer();
  
  displayReady();
}

void loop() {
  // Atualiza tempo
  timeClient.update();
  
  // Verifica horários de alimentação
  checkFeedingTimes();
  
  // Processa alimentação manual
  if (manualFeedRequested) {
    feedPet(manualRotations);
    manualFeedRequested = false;
  }
  
  // Atualiza display a cada 2 segundos
  if (millis() - lastDisplayUpdate > 2000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }
  
  // Processa requisições web
  server.handleClient();
  
  delay(100);
}

void displayStartup() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("ALIMENTADOR");
  display.println("PET v3.0");
  display.setTextSize(1);
  display.println("");
  display.println("Rosca Sem Fim");
  display.println("Servo Digital");
  display.println("Inicializando...");
  display.display();
  delay(2000);
}

void connectWiFi() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("Conectando WiFi...");
  display.display();
  
  WiFi.begin(ssid, password);
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
    
    display.clearDisplay();
    display.setCursor(0,0);
    display.println("Conectando WiFi...");
    display.print("Tentativa: ");
    display.print(attempts);
    display.println("/20");
    
    // Barra de progresso animada
    int progress = (attempts * 128) / 20;
    display.drawRect(0, 25, 128, 8, SSD1306_WHITE);
    display.fillRect(0, 25, progress, 8, SSD1306_WHITE);
    
    // Indicador de rosca girando
    display.setCursor(50, 40);
    display.print("Rosca: ");
    char spinner[] = {'|', '/', '-', '\\'};
    display.print(spinner[attempts % 4]);
    
    display.display();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi conectado!");
    Serial.print("IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("\nFalha na conexão WiFi");
  }
}

void displayReady() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  display.println("SISTEMA PRONTO!");
  display.println("");
  display.println("Servo Digital: OK");
  display.println("Rosca: Parada");
  
  if (WiFi.status() == WL_CONNECTED) {
    display.println("WiFi: Conectado");
    display.print("IP: ");
    display.println(WiFi.localIP());
  } else {
    display.println("WiFi: Desconectado");
  }
  
  display.display();
  delay(3000);
}

void checkFeedingTimes() {
  if (WiFi.status() != WL_CONNECTED) return;
  
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();
  
  // Evita alimentação duplicada na mesma hora
  if (currentHour == lastFeedHour) return;
  
  for (int i = 0; i < 4; i++) {
    if (feedingTimes[i].active && 
        feedingTimes[i].hour == currentHour && 
        feedingTimes[i].minute == currentMinute) {
      
      feedPet(feedingTimes[i].rotations);
      lastFeedHour = currentHour;
      totalFeedings++;
      break;
    }
  }
}

void feedPet(int rotations) {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0,0);
  display.println("ALIMENTANDO");
  display.setTextSize(1);
  display.println("");
  display.print("Rotacoes: ");
  display.println(rotations);
  display.println("Rosca girando...");
  display.display();
  
  // Garante que servo está conectado
  if (!servoAttached) {
    servoMotor.attach(SERVO_PIN);
    servoAttached = true;
    delay(100);
  }
  
  Serial.print("Iniciando alimentação - ");
  Serial.print(rotations);
  Serial.println(" rotações");
  
  // Executa as rotações
  for (int rot = 1; rot <= rotations; rot++) {
    // Atualiza display com progresso
    display.clearDisplay();
    display.setTextSize(2);
    display.setCursor(0,0);
    display.println("ALIMENTANDO");
    display.setTextSize(1);
    display.println("");
    display.print("Rotacao: ");
    display.print(rot);
    display.print("/");
    display.println(rotations);
    
    // Barra de progresso
    int progress = (rot * 128) / rotations;
    display.drawRect(0, 35, 128, 8, SSD1306_WHITE);
    display.fillRect(0, 35, progress, 8, SSD1306_WHITE);
    
    // Indicador visual da rosca
    display.setCursor(0, 50);
    display.print("Rosca: ");
    char spinner[] = {'|', '/', '-', '\\'};
    for (int i = 0; i < 8; i++) {
      display.print(spinner[i % 4]);
    }
    
    display.display();
    
    // Gira a rosca (uma rotação completa)
    executeRotation();
    
    totalRotations++;
    delay(500); // Pausa entre rotações
  }
  
  // Para o servo
  servoMotor.write(SERVO_STOP);
  currentServoPosition = SERVO_STOP;
  delay(500);
  
  // Mostra confirmação
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(0,0);
  display.println("CONCLUIDO!");
  display.setTextSize(1);
  display.println("");
  display.print("Dispensado: ");
  display.print(rotations);
  display.println(" porcoes");
  display.println("Pet alimentado!");
  display.display();
  
  Serial.println("Alimentação concluída!");
  delay(2000);
}

void executeRotation() {
  // Uma rotação completa da rosca
  // Ajustar tempo conforme a velocidade desejada
  
  servoMotor.write(SERVO_FORWARD);
  currentServoPosition = SERVO_FORWARD;
  
  // Tempo para uma rotação completa (ajustar conforme necessário)
  delay(1000); // 1 segundo por rotação
  
  servoMotor.write(SERVO_STOP);
  currentServoPosition = SERVO_STOP;
  delay(200); // Pausa para estabilizar
}

void updateDisplay() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);
  
  switch(displayMode) {
    case 0: // Status
      displayStatus();
      break;
    case 1: // Horários
      displaySchedule();
      break;
    case 2: // WiFi Info
      displayWiFiInfo();
      break;
    case 3: // Estatísticas
      displayStats();
      break;
  }
  
  display.display();
  
  // Alterna modo a cada 6 segundos
  static unsigned long lastModeChange = 0;
  if (millis() - lastModeChange > 6000) {
    displayMode = (displayMode + 1) % 4;
    lastModeChange = millis();
  }
}

void displayStatus() {
  display.println("=== STATUS ===");
  
  if (WiFi.status() == WL_CONNECTED) {
    display.print("Hora: ");
    display.println(timeClient.getFormattedTime());
  } else {
    display.println("Hora: --:--:--");
  }
  
  display.print("Alimentacoes: ");
  display.println(totalFeedings);
  
  display.print("Servo: ");
  if (currentServoPosition == SERVO_STOP) {
    display.println("Parado");
  } else if (currentServoPosition == SERVO_FORWARD) {
    display.println("Girando");
  } else {
    display.println("Ativo");
  }
  
  display.print("WiFi: ");
  display.println(WiFi.status() == WL_CONNECTED ? "OK" : "OFF");
  
  // Próxima alimentação
  if (WiFi.status() == WL_CONNECTED) {
    int nextHour = getNextFeedingHour();
    if (nextHour != -1) {
      display.print("Proxima: ");
      display.print(nextHour);
      display.println(":00");
    }
  }
}

void displaySchedule() {
  display.println("=== HORARIOS ===");
  display.println("Alimentacao automatica:");
  display.println("");
  
  for (int i = 0; i < 4; i++) {
    if (feedingTimes[i].active) {
      display.print(feedingTimes[i].hour);
      display.print(":");
      if (feedingTimes[i].minute < 10) display.print("0");
      display.print(feedingTimes[i].minute);
      display.print(" - ");
      display.print(feedingTimes[i].rotations);
      display.println(" rot");
    }
  }
}

void displayWiFiInfo() {
  display.println("=== CONEXAO ===");
  
  if (WiFi.status() == WL_CONNECTED) {
    display.print("SSID: ");
    display.println(WiFi.SSID());
    display.print("IP: ");
    display.println(WiFi.localIP());
    display.print("Sinal: ");
    display.print(WiFi.RSSI());
    display.println(" dBm");
    
    // Barra de sinal
    int signalBars = map(WiFi.RSSI(), -100, -50, 0, 4);
    display.print("Qualidade: ");
    for (int i = 0; i < 4; i++) {
      if (i < signalBars) {
        display.print("█");
      } else {
        display.print("░");
      }
    }
  } else {
    display.println("WiFi: Desconectado");
    display.println("Tentando reconectar...");
  }
}

void displayStats() {
  display.println("=== ESTATISTICAS ===");
  display.print("Total alimentacoes: ");
  display.println(totalFeedings);
  display.print("Total rotacoes: ");
  display.println(totalRotations);
  display.print("Uptime: ");
  display.print(millis()/1000/60);
  display.println(" min");
  display.print("Memoria livre: ");
  display.print(ESP.getFreeHeap());
  display.println(" bytes");
  
  if (totalFeedings > 0) {
    display.print("Media rot/alim: ");
    display.println(totalRotations / totalFeedings);
  }
}

int getNextFeedingHour() {
  int currentHour = timeClient.getHours();
  
  for (int i = 0; i < 4; i++) {
    if (feedingTimes[i].active && feedingTimes[i].hour > currentHour) {
      return feedingTimes[i].hour;
    }
  }
  
  // Se não encontrou hoje, retorna o primeiro de amanhã
  for (int i = 0; i < 4; i++) {
    if (feedingTimes[i].active) {
      return feedingTimes[i].hour;
    }
  }
  
  return -1;
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/feed", handleFeed);
  server.on("/feed1", []() { manualRotations = 1; manualFeedRequested = true; server.send(200, "text/plain", "Alimentacao: 1 rotacao"); });
  server.on("/feed3", []() { manualRotations = 3; manualFeedRequested = true; server.send(200, "text/plain", "Alimentacao: 3 rotacoes"); });
  server.on("/feed5", []() { manualRotations = 5; manualFeedRequested = true; server.send(200, "text/plain", "Alimentacao: 5 rotacoes"); });
  server.on("/status", handleStatus);
  server.on("/test", handleTest);
  server.on("/reverse", handleReverse);
  server.on("/config", handleConfig);
  
  server.begin();
  Serial.println("Servidor web iniciado");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Alimentador Pet - Rosca Sem Fim</title>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;} .container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);} .btn{background:#4CAF50;color:white;padding:12px 24px;font-size:14px;margin:5px;border:none;cursor:pointer;border-radius:5px;text-decoration:none;display:inline-block;} .btn:hover{background:#45a049;} .btn-small{background:#2196F3;padding:8px 16px;font-size:12px;} .btn-warn{background:#ff9800;} .stats{background:#f9f9f9;padding:15px;border-radius:5px;margin:10px 0;}</style>";
  html += "</head><body><div class='container'>";
  html += "<h1>🐕 Alimentador Pet - Rosca Sem Fim</h1>";
  html += "<div class='stats'>";
  html += "<p><strong>⏰ Hora atual:</strong> " + timeClient.getFormattedTime() + "</p>";
  html += "<p><strong>🍽️ Total de alimentações:</strong> " + String(totalFeedings) + "</p>";
  html += "<p><strong>🔄 Total de rotações:</strong> " + String(totalRotations) + "</p>";
  // Fix: Ensure String literal or cast to String
  html += "<p><strong>📶 WiFi:</strong> " + String(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado") + "</p>";
  html += "<p><strong>⚙️ Servo:</strong> " + String(currentServoPosition == SERVO_STOP ? "Parado" : "Ativo") + "</p>";
  html += "</div>";
  html += "<h2>🎮 Controles:</h2>";
  html += "<a href='/feed1' class='btn btn-small'>1 Rotação</a>";
  html += "<a href='/feed3' class='btn'>3 Rotações</a>";
  html += "<a href='/feed5' class='btn'>5 Rotações</a><br>";
  html += "<a href='/test' class='btn btn-small'>🔧 Testar</a>";
  html += "<a href='/reverse' class='btn btn-warn'>↩️ Reverter</a>";
  html += "<a href='/status' class='btn btn-small'>📊 JSON</a>";
  html += "<h2>⏰ Horários Programados:</h2>";
  html += "<ul>";
  for (int i = 0; i < 4; i++) {
    if (feedingTimes[i].active) {
      html += "<li><strong>" + String(feedingTimes[i].hour) + ":";
      if (feedingTimes[i].minute < 10) html += "0";
      html += String(feedingTimes[i].minute) + "</strong> - ";
      html += String(feedingTimes[i].rotations) + " rotações</li>";
    }
  }
  html += "</ul>";
  html += "<p><small>💡 Acesse de qualquer dispositivo na rede local!</small></p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleFeed() {
  manualRotations = 3;
  manualFeedRequested = true;
  server.send(200, "text/plain", "Alimentacao manual: 3 rotacoes");
}

void handleTest() {
  executeRotation();
  server.send(200, "text/plain", "Teste concluido: 1 rotacao");
}

void handleReverse() {
  // Rotação reversa para limpeza
  servoMotor.write(SERVO_BACKWARD);
  delay(1000);
  servoMotor.write(SERVO_STOP);
  server.send(200, "text/plain", "Rotacao reversa concluida");
}

void handleStatus() {
  String json = "{";
  json += "\"time\":\"" + timeClient.getFormattedTime() + "\",";
  json += "\"feedings\":" + String(totalFeedings) + ",";
  json += "\"rotations\":" + String(totalRotations) + ",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"uptime\":" + String(millis()/1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"servo_position\":" + String(currentServoPosition) + ",";
  json += "\"next_feeding\":" + String(getNextFeedingHour());
  json += "}";
  
  server.send(200, "application/json", json);
}

void handleConfig() {
  server.send(200, "text/plain", "Configuracao: Ajustar tempos de rotacao conforme necessario");
}
