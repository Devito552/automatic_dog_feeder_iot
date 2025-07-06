#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Stepper.h>
#include <NTPClient.h>
#include <WiFiUdp.h>

// Configurações do Display OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define SDA_PIN 12
#define SCL_PIN 14

// Configurações do Motor de Passo 28BYJ-48 com driver ULN2003
#define IN1_PIN 5
#define IN2_PIN 4
#define IN3_PIN 0
#define IN4_PIN 2

// Configurações do motor
const int STEPS_PER_MOTOR_REVOLUTION = 2048;
const int STEPS_PER_AUGER_ROTATION = STEPS_PER_MOTOR_REVOLUTION;
const int STEPS_PER_CHUNK = 64; // Dividir em pedaços menores para evitar timeout

// Configurações WiFi
const char* ssid = "INTERNET ";
const char* password = "";

// Objetos
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
Stepper stepperMotor(STEPS_PER_MOTOR_REVOLUTION, IN1_PIN, IN3_PIN, IN2_PIN, IN4_PIN);
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000);

// Variáveis globais
struct FeedingTime {
  int hour;
  int minute;
  int rotations;
  bool active;
};

FeedingTime feedingTimes[4] = {
  {6, 0, 3, true},
  {12, 0, 3, true},
  {18, 0, 3, true},
  {22, 0, 2, false}
};

int totalFeedings = 0;
long totalRotations = 0;
bool manualFeedRequested = false;
int manualRotations = 3;
unsigned long lastDisplayUpdate = 0;
int displayMode = 0;
bool isStepperMoving = false;

// Variáveis para controle não-bloqueante do motor
int pendingRotations = 0;
int currentRotation = 0;
int stepsRemaining = 0;
bool feedingInProgress = false;
unsigned long lastStepTime = 0;
const unsigned long STEP_DELAY = 3; // Delay entre passos em ms

void setup() {
  Serial.begin(74880);
  
  // Inicializa I2C e Display
  Wire.begin(SDA_PIN, SCL_PIN);
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    Serial.println("Falha no display OLED");
    for(;;);
  }

  // Inicializa motor de passo
  stepperMotor.setSpeed(12);
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
  // Processa requisições web PRIMEIRO
  server.handleClient();
  yield(); // Permite que o ESP8266 processe tarefas internas
  
  // Atualiza tempo
  timeClient.update();
  
  // Processa motor de passo de forma não-bloqueante
  processStepperMotor();
  yield();

  // Verifica horários de alimentação programados
  checkFeedingTimes();
  yield();

  // Processa alimentação manual solicitada
  if (manualFeedRequested && !feedingInProgress) {
    startFeeding(manualRotations);
    manualFeedRequested = false;
  }

  // Atualiza display
  if (millis() - lastDisplayUpdate > 2000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }

  delay(10); // Delay reduzido para melhor responsividade
}

void processStepperMotor() {
  if (!feedingInProgress) return;
  
  if (stepsRemaining > 0) {
    if (millis() - lastStepTime >= STEP_DELAY) {
      // Executa apenas alguns passos por vez
      int stepsToTake = min(stepsRemaining, STEPS_PER_CHUNK);
      stepperMotor.step(stepsToTake);
      stepsRemaining -= stepsToTake;
      lastStepTime = millis();
      
      // Processa requisições web durante o movimento
      server.handleClient();
      yield();
    }
  } else {
    // Rotação atual concluída
    currentRotation++;
    totalRotations++;
    
    if (currentRotation >= pendingRotations) {
      // Todas as rotações concluídas
      finishFeeding();
    } else {
      // Inicia próxima rotação
      stepsRemaining = STEPS_PER_AUGER_ROTATION;
      delay(200); // Pequena pausa entre rotações
    }
  }
}

void startFeeding(int rotations) {
  if (feedingInProgress) return; // Evita sobreposição
  
  Serial.print("Iniciando alimentação - ");
  Serial.print(rotations);
  Serial.println(" rotações");
  
  pendingRotations = rotations;
  currentRotation = 0;
  stepsRemaining = STEPS_PER_AUGER_ROTATION;
  feedingInProgress = true;
  isStepperMoving = true;
  
  // Atualiza display
  updateFeedingDisplay();
}

void finishFeeding() {
  feedingInProgress = false;
  isStepperMoving = false;
  
  Serial.println("Alimentação concluída!");
  
  // Mostra confirmação no display
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(12,0);
  display.print("CONCLUIDO!");
  
  display.setTextSize(1);
  display.setCursor(8,22);
  display.print("Dispensado: ");
  display.print(pendingRotations);
  display.print(" porcoes");
  
  display.setCursor(20,35);
  display.print("Pet alimentado!");
  
  // Desenha um smile
  display.drawCircle(64, 50, 8, SSD1306_WHITE);
  display.drawPixel(60, 47, SSD1306_WHITE);
  display.drawPixel(68, 47, SSD1306_WHITE);
  display.drawLine(59, 52, 69, 52, SSD1306_WHITE);
  
  display.display();
  
  delay(3000);
  pendingRotations = 0;
}

void updateFeedingDisplay() {
  if (!feedingInProgress) return;
  
  display.clearDisplay();
  display.setTextSize(2);
  display.setCursor(4,0);
  display.print("ALIMENTANDO");
  
  display.setTextSize(1);
  display.setCursor(24,20);
  display.print("Rotacao: ");
  display.print(currentRotation + 1);
  display.print("/");
  display.print(pendingRotations);
  
  // Barra de progresso
  int totalProgress = ((currentRotation * STEPS_PER_AUGER_ROTATION) + 
                      (STEPS_PER_AUGER_ROTATION - stepsRemaining));
  int maxProgress = pendingRotations * STEPS_PER_AUGER_ROTATION;
  int progressBar = (totalProgress * 120) / maxProgress;
  
  display.drawRect(4, 35, 120, 10, SSD1306_WHITE);
  display.fillRect(4, 35, progressBar, 10, SSD1306_WHITE);
  
  display.setCursor(32,50);
  display.print("Dispensando...");
  display.display();
}

void displayStartup() {
  display.clearDisplay();
  display.setTextSize(2);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8,2);
  display.print("ALIMENTADOR");
  display.setCursor(24,22);
  display.print("PET v3.2");
  display.setTextSize(1);
  display.setCursor(16,44);
  display.print("Rosca Sem Fim");
  display.setCursor(18,54);
  display.print("Inicializando...");
  display.display();
  delay(2000);
}

void connectWiFi() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(8,0);
  display.print("Conectando WiFi...");
  display.display();

  WiFi.begin(ssid, password);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;

    display.clearDisplay();
    display.setCursor(8,0);
    display.print("Conectando WiFi...");
    display.setCursor(8,15);
    display.print("Tentativa: ");
    display.print(attempts);
    display.print("/20");

    // Barra de progresso
    int progress = (attempts * 120) / 20;
    display.drawRect(4, 30, 120, 10, SSD1306_WHITE);
    display.fillRect(4, 30, progress, 10, SSD1306_WHITE);

    // Indicador de progresso
    display.setCursor(32,45);
    display.print("Aguarde");
    char spinner[] = {'|', '/', '-', '\\'};
    display.print(" ");
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
  display.setCursor(16,0);
  display.print("SISTEMA PRONTO!");
  
  display.setCursor(12,15);
  display.print("Motor: OK");
  display.setCursor(12,25);
  display.print("Rosca: Parada");

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(12,35);
    display.print("WiFi: Conectado");
    display.setCursor(0,45);
    display.print("IP: ");
    display.print(WiFi.localIP());
  } else {
    display.setCursor(8,35);
    display.print("WiFi: Desconectado");
  }

  display.display();
  delay(3000);
}

void checkFeedingTimes() {
  if (WiFi.status() != WL_CONNECTED || feedingInProgress) return;

  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  static int lastFedIndex = -1;
  static unsigned long lastFedMillis = 0;

  for (int i = 0; i < 4; i++) {
    if (feedingTimes[i].active &&
        feedingTimes[i].hour == currentHour &&
        feedingTimes[i].minute == currentMinute) {

      if (lastFedIndex != i || (millis() - lastFedMillis > 60 * 1000)) {
         startFeeding(feedingTimes[i].rotations);
         lastFedIndex = i;
         lastFedMillis = millis();
         totalFeedings++;
         break;
      }
    }
  }
}

void updateDisplay() {
  // Se estiver alimentando, mostra progresso
  if (feedingInProgress) {
    updateFeedingDisplay();
    return;
  }
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0,0);

  switch(displayMode) {
    case 0:
      displayStatus();
      break;
    case 1:
      displaySchedule();
      break;
    case 2:
      displayWiFiInfo();
      break;
    case 3:
      displayStats();
      break;
  }

  display.display();

  static unsigned long lastModeChange = 0;
  if (millis() - lastModeChange > 6000) {
    displayMode = (displayMode + 1) % 4;
    lastModeChange = millis();
  }
}

void displayStatus() {
  display.setCursor(20,0);
  display.print("=== STATUS ===");

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(0,12);
    display.print("Hora: ");
    display.print(timeClient.getFormattedTime());
  } else {
    display.setCursor(0,12);
    display.print("Hora: --:--:--");
  }

  display.setCursor(0,24);
  display.print("Alimentacoes: ");
  display.print(totalFeedings);

  display.setCursor(0,36);
  display.print("Motor: ");
  if (feedingInProgress) {
    display.print("Girando");
  } else {
    display.print("Parado");
  }

  display.setCursor(0,48);
  display.print("WiFi: ");
  display.print(WiFi.status() == WL_CONNECTED ? "Conectado" : "OFF");

  if (WiFi.status() == WL_CONNECTED) {
    int nextHour = getNextFeedingHour();
    if (nextHour != -1) {
      display.setCursor(72,48);
      display.print("Prox:");
      display.print(nextHour);
      display.print("h");
    }
  }
}

void displaySchedule() {
  display.setCursor(16,0);
  display.print("=== HORARIOS ===");
  display.setCursor(4,12);
  display.print("Alimentacao automatica:");

  int yPos = 24;
  for (int i = 0; i < 4; i++) {
    if (feedingTimes[i].active) {
      display.setCursor(8,yPos);
      if (feedingTimes[i].hour < 10) display.print("0");
      display.print(feedingTimes[i].hour);
      display.print(":");
      if (feedingTimes[i].minute < 10) display.print("0");
      display.print(feedingTimes[i].minute);
      display.print(" - ");
      display.print(feedingTimes[i].rotations);
      display.print(" rot");
      yPos += 10;
    }
  }
}

void displayWiFiInfo() {
  display.setCursor(18,0);
  display.print("=== CONEXAO ===");

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(0,12);
    display.print("SSID: ");
    String ssidStr = WiFi.SSID();
    if (ssidStr.length() > 16) {
      ssidStr = ssidStr.substring(0, 16);
    }
    display.print(ssidStr);
    
    display.setCursor(0,24);
    display.print("IP: ");
    display.print(WiFi.localIP());
    
    display.setCursor(0,36);
    display.print("Sinal: ");
    display.print(WiFi.RSSI());
    display.print(" dBm");

    // Barra de sinal visual
    int signalBars = map(WiFi.RSSI(), -100, -50, 0, 5);
    display.setCursor(0,48);
    display.print("Forca: ");
    for (int i = 0; i < 5; i++) {
      if (i < signalBars) {
        display.print((char)219); // Caracter sólido
      } else {
        display.print("-");
      }
    }
  } else {
    display.setCursor(8,24);
    display.print("WiFi: Desconectado");
    display.setCursor(8,36);
    display.print("Tentando reconectar...");
  }
}

void displayStats() {
  display.setCursor(8,0);
  display.print("=== ESTATISTICAS ===");
  
  display.setCursor(0,12);
  display.print("Total alim: ");
  display.print(totalFeedings);
  
  display.setCursor(0,24);
  display.print("Total rot: ");
  display.print(totalRotations);
  
  display.setCursor(0,36);
  display.print("Uptime: ");
  display.print(millis()/1000/60);
  display.print(" min");
  
  display.setCursor(0,48);
  display.print("RAM: ");
  display.print(ESP.getFreeHeap());
  display.print(" bytes");

  if (totalFeedings > 0) {
    display.setCursor(0,60);
    display.print("Media: ");
    display.print((float)totalRotations / totalFeedings, 1);
    display.print(" r/a");
  }
}

int getNextFeedingHour() {
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  for (int i = 0; i < 4; i++) {
    if (feedingTimes[i].active) {
        if (feedingTimes[i].hour > currentHour || 
           (feedingTimes[i].hour == currentHour && feedingTimes[i].minute > currentMinute)) {
            return feedingTimes[i].hour;
        }
    }
  }

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
  server.on("/feed1", []() { 
    if (!feedingInProgress) {
      manualRotations = 1; 
      manualFeedRequested = true; 
      server.send(200, "text/plain", "Alimentacao agendada: 1 rotacao");
    } else {
      server.send(423, "text/plain", "Motor ocupado, tente novamente");
    }
  });
  server.on("/feed3", []() { 
    if (!feedingInProgress) {
      manualRotations = 3; 
      manualFeedRequested = true; 
      server.send(200, "text/plain", "Alimentacao agendada: 3 rotacoes");
    } else {
      server.send(423, "text/plain", "Motor ocupado, tente novamente");
    }
  });
  server.on("/feed5", []() { 
    if (!feedingInProgress) {
      manualRotations = 5; 
      manualFeedRequested = true; 
      server.send(200, "text/plain", "Alimentacao agendada: 5 rotacoes");
    } else {
      server.send(423, "text/plain", "Motor ocupado, tente novamente");
    }
  });
  server.on("/status", handleStatus);
  server.on("/test", handleTest);
  server.on("/reverse", handleReverse);
  server.on("/stop", handleStop);
  server.on("/config", handleConfig);

  server.begin();
  Serial.println("Servidor web iniciado");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Alimentador Pet</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;} .container{max-width:600px;margin:0 auto;background:white;padding:20px;border-radius:10px;box-shadow:0 2px 10px rgba(0,0,0,0.1);} .btn{background:#4CAF50;color:white;padding:12px 24px;font-size:14px;margin:5px;border:none;cursor:pointer;border-radius:5px;text-decoration:none;display:inline-block;} .btn:hover{background:#45a049;} .btn-small{background:#2196F3;padding:8px 16px;font-size:12px;} .btn-warn{background:#ff9800;} .btn-danger{background:#f44336;} .stats{background:#f9f9f9;padding:15px;border-radius:5px;margin:10px 0;} .status-busy{color:#ff9800;font-weight:bold;}</style>";
  html += "<script>setTimeout(function(){location.reload();}, 5000);</script>"; // Auto-refresh
  html += "</head><body><div class='container'>";
  html += "<h1>🐕 Alimentador Pet</h1>";
  html += "<div class='stats'>";
  html += "<p><strong>⏰ Hora atual:</strong> " + timeClient.getFormattedTime() + "</p>";
  html += "<p><strong>🍽️ Total de alimentações:</strong> " + String(totalFeedings) + "</p>";
  html += "<p><strong>🔄 Total de rotações:</strong> " + String(totalRotations) + "</p>";
  html += "<p><strong>📶 WiFi:</strong> " + String(WiFi.status() == WL_CONNECTED ? "Conectado" : "Desconectado") + "</p>";
  html += "<p><strong>⚙️ Motor:</strong> ";
  if (feedingInProgress) {
    html += "<span class='status-busy'>Girando (" + String(currentRotation + 1) + "/" + String(pendingRotations) + ")</span>";
  } else {
    html += "Parado";
  }
  html += "</p>";
  html += "</div>";
  html += "<h2>🎮 Controles:</h2>";
  
  if (feedingInProgress) {
    html += "<p class='status-busy'>⚠️ Motor em funcionamento...</p>";
    html += "<a href='/stop' class='btn btn-danger'>🛑 Parar</a>";
  } else {
    html += "<a href='/feed1' class='btn btn-small'>1 Rotação</a>";
    html += "<a href='/feed3' class='btn'>3 Rotações</a>";
    html += "<a href='/feed5' class='btn'>5 Rotações</a><br>";
    html += "<a href='/test' class='btn btn-small'>🔧 Testar</a>";
    html += "<a href='/reverse' class='btn btn-warn'>↩️ Reverter</a>";
  }
  
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
  html += "<p><small>💡 Página atualiza automaticamente a cada 5 segundos</small></p>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleFeed() {
  if (!feedingInProgress) {
    manualRotations = 3;
    manualFeedRequested = true;
    server.send(200, "text/plain", "Alimentacao agendada: 3 rotacoes");
  } else {
    server.send(423, "text/plain", "Motor ocupado, tente novamente");
  }
}

void handleTest() {
  if (!feedingInProgress) {
    manualRotations = 1;
    manualFeedRequested = true;
    server.send(200, "text/plain", "Teste agendado: 1 rotacao");
  } else {
    server.send(423, "text/plain", "Motor ocupado, tente novamente");
  }
}

void handleReverse() {
  if (!feedingInProgress) {
    Serial.println("Iniciando rotação reversa...");
    isStepperMoving = true;
    
    // Executa rotação reversa em pedaços pequenos
    int stepsToReverse = STEPS_PER_AUGER_ROTATION;
    while (stepsToReverse > 0) {
      int chunk = min(stepsToReverse, STEPS_PER_CHUNK);
      stepperMotor.step(-chunk);
      stepsToReverse -= chunk;
      server.handleClient();
      yield();
      delay(2);
    }
    
    isStepperMoving = false;
    Serial.println("Rotação reversa concluída.");
    server.send(200, "text/plain", "Rotacao reversa concluida");
  } else {
    server.send(423, "text/plain", "Motor ocupado, tente novamente");
  }
}

void handleStop() {
  if (feedingInProgress) {
    feedingInProgress = false;
    isStepperMoving = false;
    pendingRotations = 0;
    stepsRemaining = 0;
    Serial.println("Alimentação interrompida pelo usuário");
    server.send(200, "text/plain", "Alimentacao interrompida");
  } else {
    server.send(200, "text/plain", "Nenhuma alimentacao em andamento");
  }
}

void handleStatus() {
  String json = "{";
  json += "\"time\":\"" + timeClient.getFormattedTime() + "\",";
  json += "\"feedings\":" + String(totalFeedings) + ",";
  json += "\"rotations\":" + String(totalRotations) + ",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"uptime\":" + String(millis()/1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"motor_state\":\"" + String(feedingInProgress ? "feeding" : "stopped") + "\",";
  json += "\"feeding_progress\":" + String(feedingInProgress ? currentRotation + 1 : 0) + ",";
  json += "\"total_rotations_pending\":" + String(pendingRotations) + ",";
  json += "\"next_feeding\":" + String(getNextFeedingHour());
  json += "}";

  server.send(200, "application/json", json);
}

void handleConfig() {
  server.send(200, "text/plain", "Configuracao: Ajuste os horarios de alimentacao e a velocidade do motor no codigo.");
}
