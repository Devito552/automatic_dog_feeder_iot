#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <EEPROM.h>

// Configurações do sistema
#define VERSION "4.1"
#define EEPROM_SIZE 512
#define CONFIG_ADDRESS 0

// Configurações do Display OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
#define SCREEN_ADDRESS 0x3C
#define SDA_PIN 12
#define SCL_PIN 14

// Configurações do Motor de Passo NEMA 17 com driver A4988
#define STEP_PIN 5     // Pino STEP - controla os passos
#define DIR_PIN 4      // Pino DIR - controla direção
#define ENABLE_PIN 16  // Pino ENABLE - liga/desliga o driver (LOW = ligado, HIGH = desligado)


// Configurações do motor
const int STEPS_PER_MOTOR_REVOLUTION = 200; // NEMA 17 padrão: 200 passos por revolução (1.8° por passo)
const int MICROSTEPS = 1; // Full step (sem microstepping)
const int TOTAL_STEPS_PER_REVOLUTION = STEPS_PER_MOTOR_REVOLUTION * MICROSTEPS; // 200 passos full step
const int STEPS_PER_AUGER_ROTATION = TOTAL_STEPS_PER_REVOLUTION;
const int STEPS_PER_CHUNK = 50; // Dividir em pedaços menores para evitar timeout

// Configurações WiFi
const char* ssid = "INTERNET WAY DEVITO";
const char* password = "devito3452";

// Objetos
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
ESP8266WebServer server(80);
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", -3 * 3600, 60000);

// Variáveis globais
struct FeedingTime {
  int hour;
  int minute;
  int grams;  // Mudança: agora em gramas
  bool active;
};

struct SystemConfig {
  FeedingTime feedingTimes[4];
  int totalFeedings;
  long totalRotations;
  char ssid[32];
  char password[64];
  int motorSpeed;
  bool configured;
  
  // Novas configurações para sistema de gramas
  float gramsPerRotation;      // Gramas dispensadas por rotação (calibração)
  int dailyGramsTotal;         // Total de gramas por dia
  int periodsPerDay;           // Quantos períodos dividir (2=manhã/noite, 3=manhã/tarde/noite)
  bool autoDistribute;         // Se deve distribuir automaticamente
  
  uint16_t checksum;
};

SystemConfig config;

// Variáveis de controle
int totalFeedings = 0;
long totalRotations = 0;
long totalGramsDispensed = 0;  // Nova variável para total em gramas
bool manualFeedRequested = false;
int manualRotations = 3;
int manualGrams = 50;  // Nova variável para alimentação manual em gramas
bool calibrationMode = false;  // Modo de calibração
float calibrationGrams = 0;    // Gramas para calibração
unsigned long lastDisplayUpdate = 0;
int displayMode = 0;
bool isStepperMoving = false;

// Variáveis para controle não-bloqueante do motor
int pendingRotations = 0; // Mantido para compatibilidade com displays
int currentRotation = 0;  // Mantido para compatibilidade com displays
int stepsRemaining = 0;
bool feedingInProgress = false;
unsigned long lastStepTime = 0;
const unsigned long STEP_DELAY_US = 2000; // Delay entre passos em microssegundos (2ms para full step)
bool motorEnabled = false; // Estado do driver do motor
bool motorDirection = true; // true = horário, false = anti-horário

// Novas variáveis para controle preciso por passos
int totalStepsRequested = 0; // Total de passos solicitados para a alimentação
int stepsCompleted = 0;      // Passos já executados
float gramsRequested = 0;    // Gramas solicitados (valor exato)

// Variáveis para sistema anti-travamento
float gramsDispensedCurrent = 0; // Gramas dispensadas na alimentação atual
float lastReverseGrams = 0; // Última posição onde fez reversão
const float REVERSE_INTERVAL_GRAMS = 5.0; // A cada 5g faz reversão
const int REVERSE_STEPS = 20; // Passos para reverter (ajustado para full step)
const int FINAL_REVERSE_STEPS = 100; // Passos para reversão final (ajustado para full step)
bool needsReverse = false; // Flag para indicar que precisa fazer reversão

// Variáveis para reconexão WiFi
unsigned long lastWiFiCheck = 0;
const unsigned long WIFI_CHECK_INTERVAL = 30000; // Verifica WiFi a cada 30s
int wifiReconnectAttempts = 0;

// Configurações padrão WiFi (fallback)
const char* defaultSSID = "INTERNET WAY DEVITO";
const char* defaultPassword = "devito3452";

// Função de log estruturado
void logEvent(const char* event, const char* details = "") {
  Serial.print("[");
  Serial.print(millis());
  Serial.print("] ");
  Serial.print(event);
  if (strlen(details) > 0) {
    Serial.print(": ");
    Serial.print(details);
  }
  Serial.println();
}

void setup() {
  Serial.begin(74880);
  logEvent("SYSTEM_START", "Alimentador Pet v" VERSION);
  
  // Inicializa EEPROM
  EEPROM.begin(EEPROM_SIZE);
  loadConfig();
  
  // Inicializa I2C e Display
  Wire.begin(SDA_PIN, SCL_PIN);
  if(!display.begin(SSD1306_SWITCHCAPVCC, SCREEN_ADDRESS)) {
    logEvent("ERROR", "Falha no display OLED");
    for(;;);
  }

  // Inicializa pinos do motor A4988
  pinMode(STEP_PIN, OUTPUT);
  pinMode(DIR_PIN, OUTPUT);
  pinMode(ENABLE_PIN, OUTPUT);
  digitalWrite(STEP_PIN, LOW);
  digitalWrite(DIR_PIN, LOW);
  disableMotor(); // Inicia com motor desligado
  delay(1000);

  // Tela inicial
  displayStartup();

  // Conecta WiFi
  connectWiFi();

  // Inicializa NTP
  timeClient.begin();

  // Configura servidor web
  setupWebServer();
  
  // Configuração de microstepping (informativo)
  setMicrostepping(1); // Full step

  displayReady();
  logEvent("SYSTEM_READY", "Sistema inicializado com sucesso");
}

void loop() {
  // Processa requisições web PRIMEIRO
  server.handleClient();
  yield(); // Permite que o ESP8266 processe tarefas internas
  
  // Verifica e reconecta WiFi se necessário
  checkWiFiConnection();
  
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
    if (calibrationMode) {
      startFeeding(calibrationGrams);
      calibrationMode = false;
    } else {
      startFeeding(manualGrams);  // Agora usa gramas
    }
    manualFeedRequested = false;
  }

  // Atualiza display
  if (millis() - lastDisplayUpdate > 2000) {
    updateDisplay();
    lastDisplayUpdate = millis();
  }

  // Salva configuração periodicamente (a cada 10 minutos)
  static unsigned long lastSave = 0;
  if (millis() - lastSave > 600000) {
    saveConfig();
    lastSave = millis();
  }

  delay(10); // Delay reduzido para melhor responsividade
}

// Função para verificar e reconectar WiFi
void checkWiFiConnection() {
  if (millis() - lastWiFiCheck > WIFI_CHECK_INTERVAL) {
    lastWiFiCheck = millis();
    
    if (WiFi.status() != WL_CONNECTED) {
      logEvent("WIFI_DISCONNECTED", "Tentando reconectar...");
      wifiReconnectAttempts++;
      
      WiFi.begin(config.ssid, config.password);
      
      // Aguarda até 10 segundos para conectar
      int attempts = 0;
      while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        attempts++;
        yield();
      }
      
      if (WiFi.status() == WL_CONNECTED) {
        logEvent("WIFI_RECONNECTED", WiFi.localIP().toString().c_str());
        wifiReconnectAttempts = 0;
      } else {
        logEvent("WIFI_FAILED", String("Tentativa " + String(wifiReconnectAttempts)).c_str());
      }
    }
  }
}

// Funções para controle do motor
void enableMotor() {
  digitalWrite(ENABLE_PIN, LOW);  // A4988 ativa com nível baixo
  motorEnabled = true;
  logEvent("MOTOR_ENABLED", "Driver A4988 ligado");
  delay(100); // Pequeno delay para estabilizar
}

void disableMotor() {
  digitalWrite(ENABLE_PIN, HIGH); // A4988 desativa com nível alto
  motorEnabled = false;
  logEvent("MOTOR_DISABLED", "Driver A4988 desligado");
}

// Função para executar um único passo
void stepMotor() {
  digitalWrite(STEP_PIN, HIGH);
  delayMicroseconds(10); // Pulso mais longo para full step
  digitalWrite(STEP_PIN, LOW);
  delayMicroseconds(10);
}

// Função para definir direção do motor
void setMotorDirection(bool clockwise) {
  motorDirection = clockwise;
  digitalWrite(DIR_PIN, clockwise ? HIGH : LOW);
  delayMicroseconds(5); // Delay para estabilizar sinal de direção
}

// Função para executar múltiplos passos
void stepMotorMultiple(int steps, bool clockwise) {
  if (steps <= 0) return;
  
  setMotorDirection(clockwise);
  unsigned long stepDelay = calculateStepDelay();
  
  for (int i = 0; i < steps; i++) {
    stepMotor();
    delayMicroseconds(stepDelay);
    
    // Permite processamento de outras tarefas a cada alguns passos
    if (i % 25 == 0) { // Reduzido para full step
      server.handleClient();
      yield();
    }
  }
}

void processStepperMotor() {
  if (!feedingInProgress) return;
  
  // Verifica se precisa fazer reversão para evitar travamento
  if (needsReverse) {
    performAntiJamReverse();
    needsReverse = false;
    return;
  }
  
  if (stepsRemaining > 0) {
    unsigned long stepDelay = calculateStepDelay();
    if (micros() - lastStepTime >= stepDelay) {
      // Executa apenas alguns passos por vez
      int stepsToTake = min(stepsRemaining, STEPS_PER_CHUNK);
      
      // Executa os passos usando controle direto A4988
      setMotorDirection(true); // Direção horária para alimentação
      for (int i = 0; i < stepsToTake; i++) {
        stepMotor();
        delayMicroseconds(stepDelay);
      }
      
      stepsRemaining -= stepsToTake;
      stepsCompleted += stepsToTake; // NOVO: Conta passos executados
      lastStepTime = micros();
      
      // NOVO: Atualiza gramas dispensadas baseado nos passos reais
      gramsDispensedCurrent = calculateGramsForSteps(stepsCompleted);
      
      // Atualiza rotação atual para compatibilidade com displays
      currentRotation = stepsCompleted / TOTAL_STEPS_PER_REVOLUTION;
      
      // Verifica se precisa fazer reversão anti-travamento
      if (gramsDispensedCurrent - lastReverseGrams >= REVERSE_INTERVAL_GRAMS) {
        needsReverse = true;
        lastReverseGrams = gramsDispensedCurrent;
      }
      
      // Processa requisições web durante o movimento
      server.handleClient();
      yield();
    }
  } else {
    // NOVO: Alimentação concluída quando todos os passos foram executados
    finishFeeding(); // Simplificado - não precisa mais do controle de rotações
  }
}

void startFeeding(int grams) {
  if (feedingInProgress) return; // Evita sobreposição
  
  // NOVO: Cálculo preciso por passos
  gramsRequested = grams;
  totalStepsRequested = calculateStepsForGrams(grams);
  float actualGrams = calculateGramsForSteps(totalStepsRequested);
  
  // Mantém compatibilidade para displays (calcula rotações equivalentes)
  pendingRotations = (totalStepsRequested + TOTAL_STEPS_PER_REVOLUTION - 1) / TOTAL_STEPS_PER_REVOLUTION;
  
  logEvent("FEEDING_START", String("Iniciando " + String(grams) + "g (precisão: " + String(actualGrams, 2) + "g, " + String(totalStepsRequested) + " passos)").c_str());
  
  // Liga o motor antes de iniciar
  enableMotor();
  
  // Inicializa variáveis anti-travamento
  gramsDispensedCurrent = 0;
  lastReverseGrams = 0;
  needsReverse = false;
  
  // NOVO: Controle por passos
  stepsCompleted = 0;
  currentRotation = 0;
  stepsRemaining = totalStepsRequested; // Agora usa o total exato de passos
  feedingInProgress = true;
  isStepperMoving = true;
  
  // Atualiza display
  updateFeedingDisplay();
}

void finishFeeding() {
  feedingInProgress = false;
  isStepperMoving = false;
  
  // Executa reversão final para evitar ração caindo
  logEvent("FINAL_REVERSE", "Reversão final para parar gotejamento");
  stepMotorMultiple(FINAL_REVERSE_STEPS, false); // false = anti-horário
  delay(200); // Pausa para garantir que pare completamente
  
  // Desliga o motor após completar a alimentação
  disableMotor();
  
  totalFeedings++;
  // NOVO: Usa o cálculo preciso por passos
  float gramsDispensed = calculateGramsForSteps(stepsCompleted);
  totalGramsDispensed += gramsDispensed;
  
  // Atualiza contador de rotações para estatísticas
  totalRotations += (stepsCompleted + TOTAL_STEPS_PER_REVOLUTION - 1) / TOTAL_STEPS_PER_REVOLUTION;
  
  saveConfig(); // Salva estatísticas
  
  logEvent("FEEDING_COMPLETE", String("Dispensado: " + String(gramsDispensed, 2) + "g (solicitado: " + String(gramsRequested, 0) + "g). Total: " + String(totalFeedings) + " alimentações").c_str());
  
  // Mostra confirmação no display
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20,2);
  display.print("CONCLUIDO!");
  
  display.setCursor(5,15);
  display.print("Solicitado: ");
  display.print(gramsRequested, 0);
  display.print("g");
  
  display.setCursor(5,25);
  display.print("Dispensado: ");
  display.print(gramsDispensed, 1);
  display.print("g");
  
  // Mostra precisão
  float accuracy = (gramsDispensed / gramsRequested) * 100;
  display.setCursor(5,35);
  display.print("Precisao: ");
  display.print(accuracy, 0);
  display.print("%");
  
  // Desenha um smile
  display.drawCircle(64, 50, 8, SSD1306_WHITE);
  display.drawPixel(60, 47, SSD1306_WHITE);
  display.drawPixel(68, 47, SSD1306_WHITE);
  display.drawLine(59, 52, 69, 52, SSD1306_WHITE);
  
  display.display();
  
  delay(3000);
  
  // Limpa variáveis de controle
  pendingRotations = 0;
  totalStepsRequested = 0;
  stepsCompleted = 0;
  gramsRequested = 0;
}

void updateFeedingDisplay() {
  if (!feedingInProgress) return;
  
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(20,2);
  display.print("ALIMENTANDO");
  
  // NOVO: Mostra valores precisos
  display.setCursor(20,18);
  display.print("Meta: ");
  display.print(gramsRequested, 1);
  display.print("g");
  
  display.setCursor(10,28);
  display.print("Atual: ");
  display.print(gramsDispensedCurrent, 1);
  display.print("g");
  
  display.setCursor(5,38);
  display.print("Progresso: ");
  float progressPercent = (stepsCompleted * 100.0) / totalStepsRequested;
  display.print(progressPercent, 0);
  display.print("%");
  
  // Barra de progresso
  int progressBar = (int)((progressPercent * 120) / 100);
  progressBar = min(120, max(0, progressBar)); // Limita entre 0 e 120
  
  display.drawRect(4, 48, 120, 8, SSD1306_WHITE);
  display.fillRect(4, 48, progressBar, 8, SSD1306_WHITE);
  
  display.setCursor(32,58);
  if (needsReverse) {
    display.print("Anti-travamento...");
  } else {
    display.print("Dispensando...");
  }
  display.display();
}

void displayStartup() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(8,2);
  display.print("ALIMENTADOR PET v" VERSION);
  display.setCursor(16,18);
  display.print("Rosca Sem Fim");
  display.setCursor(18,30);
  display.print("Inicializando...");
  display.setCursor(20,48);
  display.print("Carregando config...");
  display.display();
  delay(2000);
}

void connectWiFi() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(8,2);
  display.print("Conectando WiFi...");
  display.display();

  WiFi.begin(config.ssid, config.password);
  logEvent("WIFI_CONNECTING", config.ssid);

  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;

    display.clearDisplay();
    display.setCursor(8,2);
    display.print("Conectando WiFi...");
    display.setCursor(8,18);
    display.print("SSID: ");
    display.print(config.ssid);
    display.setCursor(8,28);
    display.print("Tentativa: ");
    display.print(attempts);
    display.print("/20");

    // Barra de progresso
    int progress = (attempts * 120) / 20;
    display.drawRect(4, 38, 120, 8, SSD1306_WHITE);
    display.fillRect(4, 38, progress, 8, SSD1306_WHITE);

    display.display();
  }

  if (WiFi.status() == WL_CONNECTED) {
    logEvent("WIFI_CONNECTED", WiFi.localIP().toString().c_str());
    wifiReconnectAttempts = 0;
  } else {
    logEvent("WIFI_FAILED", "Falha na conexão inicial");
  }
}

void displayReady() {
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(16,2);
  display.print("SISTEMA PRONTO!");
  
  display.setCursor(12,18);
  display.print("Motor: OK");
  display.setCursor(12,28);
  display.print("Drive: Desligado");

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(12,38);
    display.print("WiFi: Conectado");
    display.setCursor(0,48);
    display.print("IP: ");
    display.print(WiFi.localIP());
  } else {
    display.setCursor(8,38);
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
    if (config.feedingTimes[i].active &&
        config.feedingTimes[i].hour == currentHour &&
        config.feedingTimes[i].minute == currentMinute) {

      if (lastFedIndex != i || (millis() - lastFedMillis > 60 * 1000)) {
         startFeeding(config.feedingTimes[i].grams);  // Agora passa gramas
         lastFedIndex = i;
         lastFedMillis = millis();
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
  display.setCursor(20,2);
  display.print("STATUS");

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(0,18);
    display.print("Hora: ");
    display.print(timeClient.getFormattedTime());
  } else {
    display.setCursor(0,18);
    display.print("Hora: --:--:--");
  }

  display.setCursor(0,28);
  display.print("Alimentacoes: ");
  display.print(totalFeedings);

  display.setCursor(0,38);
  display.print("Motor: ");
  if (feedingInProgress) {
    display.print("Girando");
  } else if (motorEnabled) {
    display.print("Ligado");
  } else {
    display.print("Desligado");
  }

  if (WiFi.status() == WL_CONNECTED) {
    int nextHour = getNextFeedingHour();
    if (nextHour != -1) {
      display.setCursor(0,48);
      display.print("Prox:");
      display.print(nextHour);
      display.print("h");
    }
  }
}

void displaySchedule() {
  display.setCursor(16,2);
  display.print("HORARIOS");
  display.setCursor(4,18);
  display.print("Alimentacao:");

  int yPos = 28;
  for (int i = 0; i < 4; i++) {
    if (config.feedingTimes[i].active) {
      display.setCursor(8,yPos);
      if (config.feedingTimes[i].hour < 10) display.print("0");
      display.print(config.feedingTimes[i].hour);
      display.print(":");
      if (config.feedingTimes[i].minute < 10) display.print("0");
      display.print(config.feedingTimes[i].minute);
      display.print(" - ");
      display.print(config.feedingTimes[i].grams);
      display.print("g");
      yPos += 10;
    }
  }
}

void displayWiFiInfo() {
  display.setCursor(18,2);
  display.print("CONEXAO");

  if (WiFi.status() == WL_CONNECTED) {
    display.setCursor(0,18);
    display.print("SSID: ");
    String ssidStr = WiFi.SSID();
    if (ssidStr.length() > 15) {
      ssidStr = ssidStr.substring(0, 15);
    }
    display.print(ssidStr);
    
    display.setCursor(0,28);
    display.print("IP: ");
    display.print(WiFi.localIP());
    
    display.setCursor(0,38);
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
    display.setCursor(8,28);
    display.print("WiFi: Desconectado");
    display.setCursor(8,38);
    display.print("Tentando reconectar...");
  }
}

void displayStats() {
  display.setCursor(8,2);
  display.print("ESTATISTICAS");
  
  display.setCursor(0,18);
  display.print("Total alim: ");
  display.print(totalFeedings);
  
  display.setCursor(0,28);
  display.print("Total: ");
  display.print(totalGramsDispensed, 0);
  display.print("g");
  
  display.setCursor(0,38);
  display.print("Hoje: ");
  display.print(getTodayGrams(), 0);
  display.print("g");
  
  display.setCursor(0,48);
  display.print("Cal: ");
  display.print(config.gramsPerRotation, 1);
  display.print("g/rot");
  
  display.setCursor(0,58);
  display.print("Meta: ");
  display.print(config.dailyGramsTotal);
  display.print("g/dia");
}

// Função para calcular gramas dispensadas hoje (simplificada)
float getTodayGrams() {
  // Para uma implementação completa, seria necessário armazenar data das alimentações
  // Por enquanto, retorna uma estimativa baseada nos horários configurados
  float todayGrams = 0;
  for (int i = 0; i < 4; i++) {
    if (config.feedingTimes[i].active) {
      todayGrams += config.feedingTimes[i].grams;
    }
  }
  return todayGrams;
}

int getNextFeedingHour() {
  int currentHour = timeClient.getHours();
  int currentMinute = timeClient.getMinutes();

  for (int i = 0; i < 4; i++) {
    if (config.feedingTimes[i].active) {
        if (config.feedingTimes[i].hour > currentHour || 
           (config.feedingTimes[i].hour == currentHour && config.feedingTimes[i].minute > currentMinute)) {
            return config.feedingTimes[i].hour;
        }
    }
  }

  for (int i = 0; i < 4; i++) {
    if (config.feedingTimes[i].active) {
      return config.feedingTimes[i].hour;
    }
  }

  return -1;
}

void setupWebServer() {
  server.on("/", handleRoot);
  server.on("/feed", handleFeed);
  server.on("/feed1", []() { 
    if (!feedingInProgress) {
      manualGrams = 25; 
      manualFeedRequested = true; 
      server.send(200, "text/plain", "Alimentacao agendada: 25g");
    } else {
      server.send(423, "text/plain", "Motor ocupado, tente novamente");
    }
  });
  server.on("/feed3", []() { 
    if (!feedingInProgress) {
      manualGrams = 50; 
      manualFeedRequested = true; 
      server.send(200, "text/plain", "Alimentacao agendada: 50g");
    } else {
      server.send(423, "text/plain", "Motor ocupado, tente novamente");
    }
  });
  server.on("/feed5", []() { 
    if (!feedingInProgress) {
      manualGrams = 100; 
      manualFeedRequested = true; 
      server.send(200, "text/plain", "Alimentacao agendada: 100g");
    } else {
      server.send(423, "text/plain", "Motor ocupado, tente novamente");
    }
  });
  server.on("/status", handleStatus);
  server.on("/test", handleTest);
  server.on("/reverse", handleReverse);
  server.on("/stop", handleStop);
  server.on("/config", handleConfig);
  server.on("/motor_on", handleMotorOn);
  server.on("/motor_off", handleMotorOff);
  server.on("/reset", handleReset);
  server.on("/reset_stats", handleResetStats);
  server.on("/reset_system", handleResetSystem);
  server.on("/confirm_reset", handleConfirmReset);
  server.on("/calibrate", handleCalibrate);
  server.on("/set_calibration", handleSetCalibration);
  server.on("/set_daily", handleSetDaily);
  server.on("/redistribute", handleRedistribute);
  server.on("/schedule", handleSchedule);
  server.on("/set_schedule", handleSetSchedule);

  server.begin();
  Serial.println("Servidor web iniciado");
}

void handleRoot() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Alimentador Pet - Dashboard</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "* { box-sizing: border-box; }";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 900px; margin: 0 auto; background: white; padding: 30px; border-radius: 20px; box-shadow: 0 15px 40px rgba(0,0,0,0.15); }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 20px; font-size: 2.5em; font-weight: 300; }";
  html += "h2 { color: #555; margin: 30px 0 15px 0; font-size: 1.5em; font-weight: 500; border-bottom: 2px solid #eee; padding-bottom: 10px; }";
  html += ".stats { background: linear-gradient(145deg, #f8f9fa, #e9ecef); padding: 25px; border-radius: 15px; margin: 20px 0; box-shadow: 0 5px 15px rgba(0,0,0,0.08); }";
  html += ".stats p { margin: 12px 0; font-size: 1.1em; line-height: 1.6; }";
  html += ".status-busy { color: #ff9800; font-weight: bold; background: #fff3e0; padding: 5px 10px; border-radius: 20px; }";
  html += ".controls-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 15px; margin: 20px 0; }";
  html += ".btn { background: linear-gradient(135deg, #4CAF50, #45a049); color: white; padding: 14px 20px; margin: 5px; border: none; cursor: pointer; border-radius: 25px; text-decoration: none; display: inline-block; font-size: 14px; font-weight: 600; text-align: center; transition: all 0.3s ease; box-shadow: 0 6px 20px rgba(76,175,80,0.3); }";
  html += ".btn:hover { transform: translateY(-2px); box-shadow: 0 8px 25px rgba(76,175,80,0.4); background: linear-gradient(135deg, #45a049, #4CAF50); }";
  html += ".btn-small { background: linear-gradient(135deg, #2196F3, #1976D2); box-shadow: 0 6px 20px rgba(33,150,243,0.3); padding: 12px 18px; font-size: 13px; }";
  html += ".btn-small:hover { box-shadow: 0 8px 25px rgba(33,150,243,0.4); background: linear-gradient(135deg, #1976D2, #2196F3); }";
  html += ".btn-warn { background: linear-gradient(135deg, #ff9800, #f57c00); box-shadow: 0 6px 20px rgba(255,152,0,0.3); }";
  html += ".btn-warn:hover { box-shadow: 0 8px 25px rgba(255,152,0,0.4); background: linear-gradient(135deg, #f57c00, #ff9800); }";
  html += ".btn-danger { background: linear-gradient(135deg, #f44336, #d32f2f); box-shadow: 0 6px 20px rgba(244,67,54,0.3); }";
  html += ".btn-danger:hover { box-shadow: 0 8px 25px rgba(244,67,54,0.4); background: linear-gradient(135deg, #d32f2f, #f44336); }";
  html += ".schedule-list { background: linear-gradient(135deg, #e8f5e8, #c8e6c9); padding: 20px; border-radius: 15px; margin: 20px 0; border-left: 5px solid #4CAF50; }";
  html += ".schedule-list ul { list-style: none; padding: 0; margin: 0; }";
  html += ".schedule-list li { background: white; margin: 8px 0; padding: 12px 15px; border-radius: 8px; box-shadow: 0 2px 5px rgba(0,0,0,0.1); }";
  html += ".footer-info { margin-top: 30px; padding: 20px; background: #f5f5f5; border-radius: 10px; text-align: center; font-size: 0.9em; color: #666; }";
  html += ".progress-indicator { display: inline-block; background: #4CAF50; color: white; padding: 3px 8px; border-radius: 12px; font-size: 0.8em; margin-left: 10px; }";
  html += "@media (max-width: 768px) { .container { padding: 20px; margin: 10px; border-radius: 15px; } .controls-grid { grid-template-columns: 1fr 1fr; gap: 12px; } h1 { font-size: 2em; } .btn { padding: 12px 16px; font-size: 13px; } }";
  html += "</style>";
  html += "<script>setTimeout(function(){location.reload();}, 5000);</script>";
  html += "</head><body><div class='container'>";
  html += "<h1>🐕 Alimentador Pet Dashboard</h1>";
  html += "<div class='stats'>";
  html += "<p><strong>⏰ Hora atual:</strong> " + timeClient.getFormattedTime() + "</p>";
  html += "<p><strong>🍽️ Total de alimentações:</strong> " + String(totalFeedings) + "</p>";
  html += "<p><strong>📦 Total dispensado:</strong> " + String(totalGramsDispensed, 1) + "g</p>";
  html += "<p><strong>🎯 Meta diária:</strong> " + String(config.dailyGramsTotal) + "g (" + String(config.periodsPerDay) + " refeições)</p>";
  html += "<p><strong>📏 Calibração:</strong> " + String(config.gramsPerRotation, 1) + "g por rotação</p>";
  html += "<p><strong>📶 WiFi:</strong> " + String(WiFi.status() == WL_CONNECTED ? "🟢 Conectado" : "🔴 Desconectado") + "</p>";
  html += "<p><strong>⚙️ Status do Motor:</strong> ";
  if (feedingInProgress) {
    html += "<span class='status-busy'>🔄 Alimentando";
    html += "<span class='progress-indicator'>" + String(currentRotation + 1) + "/" + String(pendingRotations) + "</span></span>";
  } else if (motorEnabled) {
    html += "🟢 Ligado";
  } else {
    html += "⚫ Desligado";
  }
  html += "</p>";
  html += "</div>";
  html += "<h2>🎮 Controles de Alimentação</h2>";
  
  if (feedingInProgress) {
    html += "<div style='text-align: center; padding: 20px;'>";
    html += "<p class='status-busy'>⚠️ Motor em funcionamento - Dispensando ração...</p>";
    html += "<a href='/stop' class='btn btn-danger'>🛑 Parar Alimentação</a>";
    html += "</div>";
  } else {
    html += "<div class='controls-grid'>";
    html += "<a href='/feed1' class='btn btn-small'>🥄 25g</a>";
    html += "<a href='/feed3' class='btn btn-small'>🍽️ 50g</a>";
    html += "<a href='/feed5' class='btn btn-small'>🍖 100g</a>";
    html += "<a href='/test' class='btn btn-small'>🔧 Teste</a>";
    html += "<a href='/reverse' class='btn btn-small'>↩️ Reverter</a>";
    html += "<a href='/calibrate' class='btn btn-warn'>⚖️ Calibrar</a>";
    
    if (motorEnabled) {
      html += "<a href='/motor_off' class='btn btn-warn'>⚡ Desligar</a>";
    } else {
      html += "<a href='/motor_on' class='btn btn-small'>⚡ Ligar</a>";
    }
    html += "</div>";
  }
  
  html += "<h2>🔧 Configurações e Informações</h2>";
  html += "<div class='controls-grid'>";
  html += "<a href='/status' class='btn btn-small'>📊 Status JSON</a>";
  html += "<a href='/config' class='btn btn-small'>⚙️ Configurações</a>";
  html += "<a href='/schedule' class='btn btn-small'>⏰ Horários</a>";
  html += "<a href='/redistribute' class='btn btn-small'>🔄 Redistribuir</a>";
  html += "<a href='/reset' class='btn btn-danger'>🔄 Reiniciar</a>";
  html += "</div>";
  
  html += "<div class='schedule-list'>";
  html += "<h2>⏰ Horários Programados</h2>";
  if (true) { // Check if there are active schedules
    bool hasSchedules = false;
    html += "<ul>";
    for (int i = 0; i < 4; i++) {
      if (config.feedingTimes[i].active) {
        hasSchedules = true;
        html += "<li><strong>🕐 " + String(config.feedingTimes[i].hour) + ":";
        if (config.feedingTimes[i].minute < 10) html += "0";
        html += String(config.feedingTimes[i].minute) + "</strong> - ";
        html += String(config.feedingTimes[i].grams) + "g</li>";
      }
    }
    if (!hasSchedules) {
      html += "<li style='text-align: center; font-style: italic; color: #666;'>Nenhum horário configurado</li>";
    }
    html += "</ul>";
  }
  html += "</div>";
  
  html += "<div class='footer-info'>";
  html += "<p>💡 <strong>Página atualiza automaticamente a cada 5 segundos</strong></p>";
  html += "<p>📊 Reconexões WiFi: " + String(wifiReconnectAttempts) + " | 🔧 Versão: " + String(VERSION) + "</p>";
  html += "</div>";
  html += "</div></body></html>";

  server.send(200, "text/html", html);
}

void handleFeed() {
  if (!feedingInProgress) {
    manualGrams = 50;
    manualFeedRequested = true;
    server.send(200, "text/plain", "Alimentacao agendada: 50g");
  } else {
    server.send(423, "text/plain", "Motor ocupado, tente novamente");
  }
}

void handleTest() {
  if (!feedingInProgress) {
    manualGrams = 10;  // Teste com pouca quantidade
    manualFeedRequested = true;
    server.send(200, "text/plain", "Teste agendado: 10g");
  } else {
    server.send(423, "text/plain", "Motor ocupado, tente novamente");
  }
}

void handleReverse() {
  if (!feedingInProgress) {
    logEvent("REVERSE_START", "Iniciando rotação reversa");
    enableMotor(); // Liga o motor para a operação
    isStepperMoving = true;
    
    // Executa rotação reversa em pedaços pequenos com anti-travamento
    int stepsToReverse = STEPS_PER_AUGER_ROTATION;
    int stepsReversed = 0;
    
    while (stepsToReverse > 0) {
      int chunk = min(stepsToReverse, STEPS_PER_CHUNK);
      stepMotorMultiple(chunk, false); // false = anti-horário
      stepsToReverse -= chunk;
      stepsReversed += chunk;
      
      // A cada reversão equivalente a ~20g, faz pequena reversão anti-travamento
      if (stepsReversed >= (REVERSE_INTERVAL_GRAMS / config.gramsPerRotation) * STEPS_PER_AUGER_ROTATION) {
        // Pequena pausa e movimento de liberação
        delay(50);
        stepMotorMultiple(REVERSE_STEPS/2, true); // Movimento pequeno para frente
        delay(50);
        stepMotorMultiple(REVERSE_STEPS/2, false); // Volta
        stepsReversed = 0; // Reset contador
      }
      
      server.handleClient();
      yield();
      delay(2);
    }
    
    // Reversão final para garantir que pare completamente
    delay(100);
    stepMotorMultiple(FINAL_REVERSE_STEPS/2, false); // Reversão final proporcional
    delay(100);
    
    isStepperMoving = false;
    disableMotor(); // Desliga o motor após a operação
    logEvent("REVERSE_COMPLETE", "Rotação reversa concluída");
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
    disableMotor(); // Desliga o motor quando para
    logEvent("FEEDING_STOPPED", "Alimentação interrompida pelo usuário");
    server.send(200, "text/plain", "Alimentacao interrompida");
  } else {
    server.send(200, "text/plain", "Nenhuma alimentacao em andamento");
  }
}

void handleStatus() {
  String json = "{";
  json += "\"version\":\"" VERSION "\",";
  json += "\"time\":\"" + timeClient.getFormattedTime() + "\",";
  json += "\"feedings\":" + String(totalFeedings) + ",";
  json += "\"rotations\":" + String(totalRotations) + ",";
  json += "\"total_grams_dispensed\":" + String(totalGramsDispensed, 1) + ",";
  json += "\"daily_grams_target\":" + String(config.dailyGramsTotal) + ",";
  json += "\"grams_per_rotation\":" + String(config.gramsPerRotation, 1) + ",";
  json += "\"periods_per_day\":" + String(config.periodsPerDay) + ",";
  json += "\"today_grams\":" + String(getTodayGrams(), 1) + ",";
  json += "\"current_grams_dispensed\":" + String(gramsDispensedCurrent, 1) + ",";
  json += "\"anti_jam_interval\":" + String(REVERSE_INTERVAL_GRAMS, 1) + ",";
  json += "\"anti_jam_steps\":" + String(REVERSE_STEPS) + ",";
  json += "\"final_reverse_steps\":" + String(FINAL_REVERSE_STEPS) + ",";
  json += "\"wifi_connected\":" + String(WiFi.status() == WL_CONNECTED ? "true" : "false") + ",";
  json += "\"wifi_reconnects\":" + String(wifiReconnectAttempts) + ",";
  json += "\"uptime\":" + String(millis()/1000) + ",";
  json += "\"free_heap\":" + String(ESP.getFreeHeap()) + ",";
  json += "\"motor_state\":\"" + String(feedingInProgress ? "feeding" : (motorEnabled ? "enabled" : "disabled")) + "\",";
  json += "\"motor_enabled\":" + String(motorEnabled ? "true" : "false") + ",";
  json += "\"feeding_progress\":" + String(feedingInProgress ? currentRotation + 1 : 0) + ",";
  json += "\"total_rotations_pending\":" + String(pendingRotations) + ",";
  json += "\"next_feeding\":" + String(getNextFeedingHour()) + ",";
  json += "\"config_saved\":" + String(config.configured ? "true" : "false");
  json += "}";

  server.send(200, "application/json", json);
}

void handleConfig() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Configurações - Alimentador Pet</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "* { box-sizing: border-box; }";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 900px; margin: 0 auto; background: white; padding: 30px; border-radius: 20px; box-shadow: 0 15px 40px rgba(0,0,0,0.15); }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 20px; font-size: 2.5em; font-weight: 300; }";
  html += "h2 { color: #555; margin: 30px 0 15px 0; font-size: 1.5em; font-weight: 500; border-bottom: 2px solid #eee; padding-bottom: 10px; }";
  html += ".info-section { background: linear-gradient(145deg, #f8f9fa, #e9ecef); padding: 25px; border-radius: 15px; margin: 20px 0; box-shadow: 0 5px 15px rgba(0,0,0,0.08); }";
  html += ".info-section p { margin: 12px 0; font-size: 1.1em; line-height: 1.6; }";
  html += ".stats-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 20px 0; }";
  html += ".stat-card { background: linear-gradient(135deg, #e3f2fd, #bbdefb); padding: 20px; border-radius: 12px; border-left: 5px solid #2196F3; box-shadow: 0 5px 15px rgba(33,150,243,0.1); }";
  html += ".calibration-card { background: linear-gradient(135deg, #fff3e0, #ffe0b2); border-left-color: #ff9800; }";
  html += ".antijam-card { background: linear-gradient(135deg, #f3e5f5, #e1bee7); border-left-color: #9c27b0; }";
  html += ".form-section { background: linear-gradient(145deg, #e8f5e8, #c8e6c9); padding: 25px; border-radius: 15px; margin: 20px 0; border-left: 5px solid #4CAF50; }";
  html += ".form-row { display: flex; align-items: center; margin: 15px 0; flex-wrap: wrap; gap: 15px; }";
  html += ".form-row label { font-weight: 600; color: #555; min-width: 120px; }";
  html += "input, select { padding: 12px 15px; border: 2px solid #ddd; border-radius: 8px; font-size: 16px; transition: all 0.3s ease; background: white; }";
  html += "input:focus, select:focus { outline: none; border-color: #4CAF50; box-shadow: 0 0 0 3px rgba(76,175,80,0.1); }";
  html += ".btn { background: linear-gradient(135deg, #4CAF50, #45a049); color: white; padding: 14px 25px; margin: 10px 5px; border: none, cursor: pointer; border-radius: 25px; text-decoration: none; display: inline-block; font-size: 16px; font-weight: 600; transition: all 0.3s ease; box-shadow: 0 6px 20px rgba(76,175,80,0.3); }";
  html += ".btn:hover { transform: translateY(-2px); box-shadow: 0 8px 25px rgba(76,175,80,0.4); background: linear-gradient(135deg, #45a049, #4CAF50); }";
  html += ".schedule-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(280px, 1fr)); gap: 15px; margin: 20px 0; }";
  html += ".schedule-item { background: white; padding: 15px; border-radius: 10px; box-shadow: 0 3px 10px rgba(0,0,0,0.1); border-left: 4px solid #4CAF50; }";
  html += ".schedule-item.inactive { border-left-color: #ccc; opacity: 0.7; }";
  html += ".back-link { display: inline-block; margin-top: 25px; color: #666; text-decoration: none; font-weight: 500; padding: 10px 20px; border-radius: 25px; transition: all 0.3s ease; background: #f5f5f5; }";
  html += ".back-link:hover { color: #4CAF50; background: #e8f5e8; transform: translateX(-5px); }";
  html += ".system-info { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 15px; }";
  html += ".info-item { background: white; padding: 15px; border-radius: 8px; text-align: center; }";
  html += "@media (max-width: 768px) { .container { padding: 20px; margin: 10px; border-radius: 15px; } .stats-grid, .schedule-grid, .system-info { grid-template-columns: 1fr; } .form-row { flex-direction: column; align-items: stretch; } h1 { font-size: 2em; } }";
  html += "</style>";
  html += "</head><body><div class='container'>";
  html += "<h1>⚙️ Configurações do Sistema</h1>";
  
  html += "<div class='info-section'>";
  html += "<div class='system-info'>";
  html += "<div class='info-item'>";
  html += "<h3>📱 Versão</h3>";
  html += "<p><strong>" + String(VERSION) + "</strong></p>";
  html += "</div>";
  html += "<div class='info-item'>";
  html += "<h3>📶 WiFi</h3>";
  html += "<p><strong>" + String(config.ssid) + "</strong></p>";
  html += "</div>";
  html += "<div class='info-item'>";
  html += "<h3>⚡ Motor</h3>";
  html += "<p><strong>" + String(config.motorSpeed) + " RPM</strong></p>";
  html += "</div>";
  html += "</div>";
  html += "</div>";
  
  html += "<div class='stats-grid'>";
  html += "<div class='stat-card'>";
  html += "<h2>📊 Estatísticas de Uso</h2>";
  html += "<p><strong>🍽️ Total de Alimentações:</strong> " + String(totalFeedings) + "</p>";
  html += "<p><strong>📦 Total Dispensado:</strong> " + String(totalGramsDispensed, 1) + "g</p>";
  html += "<p><strong>🕐 Tempo Online:</strong> " + String(millis()/1000/60) + " minutos</p>";
  html += "<p><strong>💾 Memória Livre:</strong> " + String(ESP.getFreeHeap()) + " bytes</p>";
  html += "</div>";
  
  html += "<div class='stat-card calibration-card'>";
  html += "<h2>⚖️ Calibração Atual</h2>";
  html += "<p><strong>📏 Gramas por Rotação:</strong> " + String(config.gramsPerRotation, 1) + "g</p>";
  html += "<p><strong>🎯 Meta Diária:</strong> " + String(config.dailyGramsTotal) + "g</p>";
  html += "<p><strong>🔄 Períodos por Dia:</strong> " + String(config.periodsPerDay) + "</p>";
  html += "<a href='/calibrate' class='btn'>🔧 Recalibrar Sistema</a>";
  html += "</div>";
  
  html += "<div class='stat-card antijam-card'>";
  html += "<h2>🔧 Sistema Anti-Travamento</h2>";
  html += "<p><strong>🔄 Reversão a cada:</strong> " + String(REVERSE_INTERVAL_GRAMS, 0) + "g</p>";
  html += "<p><strong>↩️ Passos anti-travamento:</strong> " + String(REVERSE_STEPS) + "</p>";
  html += "<p><strong>🔚 Passos reversão final:</strong> " + String(FINAL_REVERSE_STEPS) + "</p>";
  html += "<p><em>Sistema automático para evitar travamento e gotejamento</em></p>";
  html += "</div>";
  html += "</div>";
  
  html += "<div class='form-section'>";
  html += "<h2>🎯 Configurar Meta Diária</h2>";
  html += "<form action='/set_daily' method='GET'>";
  html += "<div class='form-row'>";
  html += "<label>📊 Total por dia:</label>";
  html += "<input type='number' name='total' value='" + String(config.dailyGramsTotal) + "' min='50' max='1000'>";
  html += "<span>gramas</span>";
  html += "</div>";
  html += "<div class='form-row'>";
  html += "<label>🔄 Dividir em:</label>";
  html += "<select name='periods'>";
  html += "<option value='2'" + String(config.periodsPerDay == 2 ? " selected" : "") + ">2 períodos (manhã/noite)</option>";
  html += "<option value='3'" + String(config.periodsPerDay == 3 ? " selected" : "") + ">3 períodos (manhã/tarde/noite)</option>";
  html += "<option value='4'" + String(config.periodsPerDay == 4 ? " selected" : "") + ">4 períodos (6h/12h/17h/21h)</option>";
  html += "</select>";
  html += "</div>";
  html += "<input type='submit' value='💾 Aplicar Nova Meta' class='btn'>";
  html += "</form>";
  html += "</div>";
  
  html += "<h2>⏰ Horários Atualmente Configurados</h2>";
  html += "<div class='schedule-grid'>";
  for (int i = 0; i < 4; i++) {
    html += "<div class='schedule-item" + String(config.feedingTimes[i].active ? "" : " inactive") + "'>";
    html += "<h3>🍽️ Horário " + String(i+1) + "</h3>";
    if (config.feedingTimes[i].active) {
      html += "<p><strong>🕐 Hora:</strong> " + String(config.feedingTimes[i].hour) + ":";
      if (config.feedingTimes[i].minute < 10) html += "0";
      html += String(config.feedingTimes[i].minute) + "</p>";
      html += "<p><strong>⚖️ Quantidade:</strong> " + String(config.feedingTimes[i].grams) + "g</p>";
      html += "<p><strong>✅ Status:</strong> Ativo</p>";
    } else {
      html += "<p><strong>❌ Status:</strong> Desativado</p>";
    }
    html += "</div>";
  }
  html += "</div>";
  
  html += "<div style='text-align: center; margin-top: 30px;'>";
  html += "<a href='/schedule' class='btn'>⏰ Editar Horários</a>";
  html += "<a href='/redistribute' class='btn'>🔄 Redistribuir Automaticamente</a>";
  html += "</div>";
  
  html += "<a href='/' class='back-link'>← Voltar ao Dashboard</a>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleMotorOn() {
  if (!feedingInProgress) {
    enableMotor();
    server.send(200, "text/plain", "Motor ligado");
  } else {
    server.send(423, "text/plain", "Motor ocupado com alimentacao");
  }
}

void handleMotorOff() {
  if (!feedingInProgress) {
    disableMotor();
    server.send(200, "text/plain", "Motor desligado");
  } else {
    server.send(423, "text/plain", "Nao e possivel desligar durante alimentacao");
  }
}

void handleCalibrate() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Calibração - Alimentador Pet</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "* { box-sizing: border-box; }";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 30px; border-radius: 20px; box-shadow: 0 15px 40px rgba(0,0,0,0.15); }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 20px; font-size: 2.5em; font-weight: 300; }";
  html += "h2 { color: #555; margin: 30px 0 15px 0; font-size: 1.5em; font-weight: 500; border-bottom: 2px solid #eee; padding-bottom: 10px; }";
  html += ".current-calibration { background: linear-gradient(135deg, #fff3e0, #ffe0b2); padding: 20px; border-radius: 15px; margin: 20px 0; text-align: center; font-size: 1.2em; box-shadow: 0 5px 15px rgba(255,152,0,0.1); }";
  html += ".instructions { background: linear-gradient(135deg, #e3f2fd, #bbdefb); padding: 25px; border-radius: 15px; margin: 20px 0; border-left: 5px solid #2196F3; }";
  html += ".instructions ol { margin: 0; padding-left: 20px; line-height: 1.8; }";
  html += ".instructions li { margin: 8px 0; font-size: 1.1em; }";
  html += ".form-section { background: linear-gradient(145deg, #e8f5e8, #c8e6c9); padding: 25px; border-radius: 15px; margin: 20px 0; border-left: 5px solid #4CAF50; }";
  html += ".form-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 20px; margin: 20px 0; }";
  html += ".form-card { background: white; padding: 20px; border-radius: 12px; box-shadow: 0 5px 15px rgba(0,0,0,0.1); }";
  html += ".form-row { margin: 15px 0; }";
  html += ".form-row label { display: block; font-weight: 600; color: #555; margin-bottom: 8px; }";
  html += "input { padding: 12px 15px; border: 2px solid #ddd; border-radius: 8px; font-size: 16px; width: 100%; transition: all 0.3s ease; background: white; }";
  html += "input:focus { outline: none; border-color: #4CAF50; box-shadow: 0 0 0 3px rgba(76,175,80,0.1); }";
  html += ".btn { background: linear-gradient(135deg, #4CAF50, #45a049); color: white; padding: 14px 25px; margin: 10px 5px; border: none, cursor: pointer; border-radius: 25px; text-decoration: none; display: inline-block; font-size: 16px; font-weight: 600; transition: all 0.3s ease; box-shadow: 0 6px 20px rgba(76,175,80,0.3); width: 100%; text-align: center; }";
  html += ".btn:hover { transform: translateY(-2px); box-shadow: 0 8px 25px rgba(76,175,80,0.4); background: linear-gradient(135deg, #45a049, #4CAF50); }";
  html += ".btn-secondary { background: linear-gradient(135deg, #2196F3, #1976D2); box-shadow: 0 6px 20px rgba(33,150,243,0.3); }";
  html += ".btn-secondary:hover { background: linear-gradient(135deg, #1976D2, #2196F3); box-shadow: 0 8px 25px rgba(33,150,243,0.4); }";
  html += ".status-warning { background: linear-gradient(135deg, #fff3e0, #ffe0b2); color: #f57c00; padding: 15px; border-radius: 10px; text-align: center; margin: 20px 0; border: 1px solid #ffcc02; font-weight: bold; }";
  html += ".back-link { display: inline-block; margin-top: 25px; color: #666; text-decoration: none; font-weight: 500; padding: 10px 20px; border-radius: 25px; transition: all 0.3s ease; background: #f5f5f5; }";
  html += ".back-link:hover { color: #4CAF50; background: #e8f5e8; transform: translateX(-5px); }";
  html += ".divider { margin: 30px 0; border-top: 2px solid #eee; }";
  html += "@media (max-width: 768px) { .container { padding: 20px; margin: 10px; border-radius: 15px; } .form-grid { grid-template-columns: 1fr; } h1 { font-size: 2em; } }";
  html += "</style>";
  html += "</head><body><div class='container'>";
  html += "<h1>⚖️ Calibração do Sistema</h1>";
  
  html += "<div class='current-calibration'>";
  html += "<h2>📊 Calibração Atual</h2>";
  html += "<p style='font-size: 1.5em; margin: 0;'><strong>" + String(config.gramsPerRotation, 1) + "g</strong> por rotação</p>";
  html += "</div>";
  
  html += "<div class='instructions'>";
  html += "<h2>🔧 Como Calibrar o Sistema</h2>";
  html += "<ol>";
  html += "<li><strong>Preparação:</strong> Coloque um recipiente vazio na saída da rosca</li>";
  html += "<li><strong>Dispensar:</strong> Configure quantas rotações e clique em 'Dispensar para Calibrar'</li>";
  html += "<li><strong>Pesar:</strong> Aguarde o sistema dispensar e pese a ração no recipiente</li>";
  html += "<li><strong>Calcular:</strong> Digite o peso medido e o número de rotações utilizadas</li>";
  html += "<li><strong>Salvar:</strong> O sistema calculará automaticamente a nova calibração</li>";
  html += "</ol>";
  html += "</div>";
  
  if (feedingInProgress) {
    html += "<div class='status-warning'>";
    html += "⚠️ Sistema dispensando ração para calibração...";
    html += "<br><em>Aguarde a conclusão e pese a ração dispensada</em>";
    html += "</div>";
  } else {
    html += "<div class='form-grid'>";
    
    html += "<div class='form-card'>";
    html += "<h2>🔄 1. Dispensar para Teste</h2>";
    html += "<form action='/set_calibration' method='GET'>";
    html += "<div class='form-row'>";
    html += "<label>🔢 Número de rotações:</label>";
    html += "<input type='number' name='rotations' value='3' min='1' max='10'>";
    html += "</div>";
    html += "<input type='submit' value='🚀 Dispensar para Calibrar' class='btn btn-secondary'>";
    html += "</form>";
    html += "<p><em>Recomendado: 3-5 rotações para calibração precisa</em></p>";
    html += "</div>";
    
    html += "<div class='form-card'>";
    html += "<h2>💾 2. Salvar Nova Calibração</h2>";
    html += "<form action='/set_calibration' method='GET'>";
    html += "<div class='form-row'>";
    html += "<label>⚖️ Peso medido (gramas):</label>";
    html += "<input type='number' name='grams' step='0.1' min='0.1' placeholder='Ex: 25.5'>";
    html += "</div>";
    html += "<div class='form-row'>";
    html += "<label>🔢 Rotações dispensadas:</label>";
    html += "<input type='number' name='rotations' value='3' min='1'>";
    html += "</div>";
    html += "<input type='submit' value='💾 Salvar Nova Calibração' class='btn'>";
    html += "</form>";
    html += "<p><em>Digite exatamente o peso que você mediu na balança</em></p>";
    html += "</div>";
    
    html += "</div>";
  }
  
  html += "<div class='instructions' style='margin-top: 30px;'>";
  html += "<h2>💡 Dicas para Calibração Precisa</h2>";
  html += "<ul style='line-height: 1.6;'>";
  html += "<li><strong>Use uma balança precisa:</strong> Prefira balanças digitais com precisão de 0.1g</li>";
  html += "<li><strong>Teste múltiplas vezes:</strong> Faça 2-3 calibrações e use a média</li>";
  html += "<li><strong>Ração seca:</strong> Use ração com baixa umidade para resultados consistentes</li>";
  html += "<li><strong>Limpeza:</strong> Certifique-se que a rosca está limpa antes de calibrar</li>";
  html += "</ul>";
  html += "</div>";
  
  html += "<a href='/' class='back-link'>← Voltar ao Dashboard</a>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSetCalibration() {
  if (server.hasArg("grams") && server.hasArg("rotations")) {
    // Salvar calibração
    float grams = server.arg("grams").toFloat();
    int rotations = server.arg("rotations").toInt();
    
    if (grams > 0 && rotations > 0) {
      config.gramsPerRotation = grams / rotations;
      saveConfig();
      logEvent("CALIBRATION_SET", String("Nova calibração: " + String(config.gramsPerRotation, 1) + "g/rot").c_str());
      server.send(200, "text/plain", "Calibracao salva: " + String(config.gramsPerRotation, 1) + "g por rotacao");
    } else {
      server.send(400, "text/plain", "Valores invalidos");
    }
  } else if (server.hasArg("rotations")) {
    // Dispensar para calibrar
    int rotations = server.arg("rotations").toInt();
    if (!feedingInProgress && rotations > 0) {
      calibrationGrams = rotations * config.gramsPerRotation; // Usa calibração atual para estimativa
      calibrationMode = true;
      manualFeedRequested = true;
      server.send(200, "text/plain", "Dispensando " + String(rotations) + " rotacoes para calibracao");
    } else {
      server.send(423, "text/plain", "Nao e possivel dispensar agora");
    }
  } else {
    server.send(400, "text/plain", "Parametros faltando");
  }
}

void handleSetDaily() {
  if (server.hasArg("total") && server.hasArg("periods")) {
    int total = server.arg("total").toInt();
    int periods = server.arg("periods").toInt();
    
    if (total > 0 && periods >= 2 && periods <= 4) {
      config.dailyGramsTotal = total;
      config.periodsPerDay = periods;
      distributeFeedings();
      saveConfig();
      logEvent("DAILY_SET", String("Nova meta: " + String(total) + "g em " + String(periods) + " períodos").c_str());
      server.send(200, "text/plain", "Meta diaria configurada: " + String(total) + "g em " + String(periods) + " periodos");
    } else {
      server.send(400, "text/plain", "Valores invalidos");
    }
  } else {
    server.send(400, "text/plain", "Parametros faltando");
  }
}

void handleRedistribute() {
  if (!feedingInProgress) {
    distributeFeedings();
    saveConfig();
    logEvent("REDISTRIBUTE", "Horários redistribuídos automaticamente");
    server.send(200, "text/plain", "Horarios redistribuidos: " + String(config.dailyGramsTotal) + "g em " + String(config.periodsPerDay) + " periodos");
  } else {
    server.send(423, "text/plain", "Aguarde terminar a alimentacao atual");
  }
}

void handleReset() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Reiniciar Sistema - Alimentador Pet</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "* { box-sizing: border-box; }";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 600px; margin: 0 auto; background: white; padding: 30px; border-radius: 20px; box-shadow: 0 15px 40px rgba(0,0,0,0.15); }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 20px; font-size: 2.5em; font-weight: 300; }";
  html += ".warning-box { background: linear-gradient(135deg, #ffe6e6, #ffcccc); padding: 20px; border-radius: 15px; margin: 20px 0; border-left: 5px solid #ff9999; text-align: center; }";
  html += ".option-card { background: linear-gradient(145deg, #f8f9fa, #e9ecef); padding: 25px; border-radius: 15px; margin: 20px 0; box-shadow:  0 5px 15px rgba(0,0,0,0.08); }";
  html += ".btn { background: linear-gradient(135deg, #4CAF50, #45a049); color: white; padding: 16px 25px; margin: 10px 5px; border: none, cursor: pointer; border-radius: 25px; text-decoration: none, display: inline-block; font-size: 16px; font-weight: 600; transition: all 0.3s ease; box-shadow: 0 6px 20px rgba(76,175,80,0.3); text-align: center; width: 100%; }";
  html += ".btn:hover { transform: translateY(-2px); box-shadow: 0 8px 25px rgba(76,175,80,0.4); }";
  html += ".btn-danger { background: linear-gradient(135deg, #f44336, #d32f2f); box-shadow: 0 6px 20px rgba(244,67,54,0.3); }";
  html += ".btn-danger:hover { box-shadow: 0 8px 25px rgba(244,67,54,0.4); background: linear-gradient(135deg, #d32f2f, #f44336); }";
  html += ".btn-warning { background: linear-gradient(135deg, #ff9800, #f57c00); box-shadow: 0 6px 20px rgba(255,152,0,0.3); }";
  html += ".btn-warning:hover { box-shadow: 0 8px 25px rgba(255,152,0,0.4); background: linear-gradient(135deg, #f57c00, #ff9800); }";
  html += ".back-link { display: inline-block; margin-top: 25px; color: #666; text-decoration: none; font-weight: 500; padding: 10px 20px; border-radius: 25px; transition: all 0.3s ease; background: #f5f5f5; text-align: center; }";
  html += ".back-link:hover { color: #4CAF50; background: #e8f5e8; }";
  html += ".stats-info { background: linear-gradient(135deg, #e3f2fd, #bbdefb); padding: 15px; border-radius: 10px; margin: 15px 0; border-left: 5px solid #2196F3; }";
  html += "</style>";
  html += "</head><body><div class='container'>";
  html += "<h1>🔄 Opções de Reinicialização</h1>";
  
  if (feedingInProgress) {
    html += "<div class='warning-box'>";
    html += "<h2>⚠️ Sistema Ocupado</h2>";
    html += "<p>Não é possível reiniciar enquanto o motor está em funcionamento.</p>";
    html += "<p>Aguarde a conclusão da alimentação atual.</p>";
    html += "</div>";
  } else {
    html += "<div class='warning-box'>";
    html += "<h2>⚠️ Escolha uma Opção</h2>";
    html += "<p>Selecione o tipo de reinicialização que deseja realizar:</p>";
    html += "</div>";
    
    html += "<div class='option-card'>";
    html += "<h2>📊 Resetar Apenas Estatísticas</h2>";
    html += "<div class='stats-info'>";
    html += "<p><strong>📈 Total de alimentações:</strong> " + String(totalFeedings) + "</p>";
    html += "<p><strong>📦 Total dispensado:</strong> " + String(totalGramsDispensed, 1) + "g</p>";
    html += "<p><strong>🔄 Total de rotações:</strong> " + String(totalRotations) + "</p>";
    html += "</div>";
    html += "<p><em>Zera os contadores de alimentações, gramas dispensadas e rotações. Mantém todas as configurações, calibrações e horários.</em></p>";
    html += "<a href='javascript:void(0)' onclick='resetStats()' class='btn btn-warning'>📊 Resetar Estatísticas</a>";
    html += "</div>";
    
    html += "<div class='option-card'>";
    html += "<h2>🔄 Reiniciar Sistema Completo</h2>";
    html += "<p><em>Reinicia completamente o ESP8266. Mantém todas as configurações salvas na memória (EEPROM).</em></p>";
    html += "<a href='javascript:void(0)' onclick='restartSystem()' class='btn btn-danger'>🔄 Reiniciar Sistema</a>";
    html += "</div>";
  }
  
  html += "<a href='/' class='back-link'>← Voltar ao Dashboard</a>";
  html += "</div>";
  
  html += "<script>";
  html += "function resetStats() {";
  html += "  if(confirm('Tem certeza que deseja resetar todas as estatísticas?\\n\\nIsto irá zerar:\\n- Total de alimentações\\n- Total de gramas dispensadas\\n- Total de rotações\\n\\nAs configurações e horários serão mantidos.')) {";
  html += "    window.location.href = '/reset_stats';";
  html += "  }";
  html += "}";
  html += "function restartSystem() {";
  html += "  if(confirm('Tem certeza que deseja reiniciar o sistema completo?\\n\\nO ESP8266 será reiniciado e você precisará aguardar a reconexão.')) {";
  html += "    fetch('/reset_system').then(function() {";
  html += "      alert('Sistema reiniciando... Aguarde alguns segundos e atualize a página.');";
  html += "      setTimeout(function() { window.location.href = '/'; }, 5000);";
  html += "    });";
  html += "  }";
  html += "}";
  html += "</script>";
  html += "</body></html>";
  
  server.send(200, "text/html", html);
}

void handleResetStats() {
  if (!feedingInProgress) {
    // Reseta apenas as estatísticas
    totalFeedings = 0;
    totalRotations = 0;
    totalGramsDispensed = 0;
    
    // Atualiza as estatísticas na configuração e salva
    config.totalFeedings = totalFeedings;
    config.totalRotations = totalRotations;
    saveConfig();
    
    logEvent("STATS_RESET", "Estatísticas resetadas via web");
    
    String response = "Estatisticas resetadas com sucesso!\\n\\n";
    response += "- Total de alimentacoes: 0\\n";
    response += "- Total dispensado: 0g\\n";
    response += "- Total de rotacoes: 0\\n\\n";
    response += "Configuracoes e horarios mantidos.";
    
    server.send(200, "text/plain", response);
  } else {
    server.send(423, "text/plain", "Nao e possivel resetar durante alimentacao");
  }
}

void handleSchedule() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<title>Configurar Horários - Alimentador Pet</title>";
  html += "<meta charset='UTF-8'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<style>";
  html += "* { box-sizing: border-box; }";
  html += "body { font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; margin: 0; padding: 20px; background: linear-gradient(135deg, #667eea 0%, #764ba2 100%); min-height: 100vh; }";
  html += ".container { max-width: 900px; margin: 0 auto; background: white; padding: 30px; border-radius: 20px; box-shadow: 0 15px 40px rgba(0,0,0,0.15); }";
  html += "h1 { color: #333; text-align: center; margin-bottom: 20px; font-size: 2.5em; font-weight: 300; }";
  html += "h3 { color: #555; margin: 0 0 15px 0; font-size: 1.4em; font-weight: 500; }";
  html += ".daily-info { background: linear-gradient(135deg, #4CAF50, #45a049); color: white; padding: 20px; border-radius: 15px; margin-bottom: 30px; text-align: center; font-size: 1.2em; box-shadow: 0 5px 15px rgba(76,175,80,0.3); }";
  html += ".schedule-grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(380px, 1fr)); gap: 25px; margin-bottom: 30px; }";
  html += ".schedule-row { background: linear-gradient(145deg, #f8f9fa, #e9ecef); padding: 25px; border-radius: 20px; border: 2px solid #e9ecef; transition: all 0.4s ease; position: relative; box-shadow: 0 5px 15px rgba(0,0,0,0.08); }";
  html += ".schedule-row:hover { transform: translateY(-3px); box-shadow: 0 10px 25px rgba(0,0,0,0.12); }";
  html += ".schedule-row.active { border-color: #4CAF50; background: linear-gradient(145deg, #f1f8e9, #e8f5e8); box-shadow: 0 8px 20px rgba(76,175,80,0.2); }";
  html += ".schedule-row.inactive { opacity: 0.6; border-color: #dee2e6; background: linear-gradient(145deg, #f8f9fa, #e9ecef); }";
  html += ".schedule-header { display: flex; align-items: center; justify-content: space-between; margin-bottom: 20px; padding-bottom: 15px; border-bottom: 2px solid #eee; }";
  html += ".toggle-switch { position: relative; display: inline-block; width: 70px; height: 40px; }";
  html += ".toggle-switch input { opacity: 0; width: 0; height: 0; }";
  html += ".slider { position: absolute; cursor: pointer; top: 0; left: 0; right: 0; bottom: 0; background: linear-gradient(135deg, #ccc, #bbb); transition: 0.4s; border-radius: 40px; box-shadow: inset 0 2px 4px rgba(0,0,0,0.1); }";
  html += ".slider:before { position: absolute; content: ''; height: 32px; width: 32px; left: 4px; bottom: 4px; background: linear-gradient(135deg, #fff, #f0f0f0); transition: 0.4s; border-radius: 50%; box-shadow: 0 2px 6px rgba(0,0,0,0.2); }";
  html += "input:checked + .slider { background: linear-gradient(135deg, #4CAF50, #45a049); }";
  html += "input:checked + .slider:before { transform: translateX(30px); }";
  html += ".time-inputs { display: grid; grid-template-columns: repeat(auto-fit, minmax(120px, 1fr)); gap: 20px; align-items: end; }";
  html += ".input-group { display: flex; flex-direction: column; }";
  html += ".input-group label { font-weight: 600; color: #555; margin-bottom: 8px; font-size: 0.95em; }";
  html += "select, input[type='number'] { padding: 12px 15px; border: 2px solid #ddd; border-radius: 12px; font-size: 16px; transition: all 0.3s ease; background: white; box-shadow: 0 2px 5px rgba(0,0,0,0.05); }";
  html += "select:focus, input[type='number']:focus { outline: none; border-color: #4CAF50; box-shadow: 0 0 0 4px rgba(76,175,80,0.1), 0 2px 8px rgba(0,0,0,0.1); transform: translateY(-1px); }";
  html += ".grams-input { width: 100%; max-width: 120px; }";
  html += ".btn-container { text-align: center; margin-top: 40px; }";
  html += ".btn { background: linear-gradient(135deg, #4CAF50, #45a049); color: white; padding: 16px 35px; margin: 10px; border: none; cursor: pointer; border-radius: 30px; text-decoration: none; display: inline-block; font-size: 16px; font-weight: 600; transition: all 0.3s ease; box-shadow: 0 6px 20px rgba(76,175,80,0.3); }";
  html += ".btn:hover { transform: translateY(-3px); box-shadow: 0 10px 30px rgba(76,175,80,0.4); background: linear-gradient(135deg, #45a049, #4CAF50); }";
  html += ".btn-danger { background: linear-gradient(135deg, #f44336, #d32f2f); box-shadow: 0 6px 20px rgba(244,67,54,0.3); }";
  html += ".btn-danger:hover { box-shadow: 0 10px 30px rgba(244,67,54,0.4); background: linear-gradient(135deg, #d32f2f, #f44336); }";
  html += ".tips { background: linear-gradient(135deg, #e3f2fd, #bbdefb); padding: 25px; border-radius: 15px; margin-top: 30px; border-left: 5px solid #2196F3; box-shadow: 0 5px 15px rgba(33,150,243,0.1); }";
  html += ".tips h3 { color: #1976D2; margin-top: 0; font-size: 1.3em; }";
  html += ".tips ul { color: #555; line-height: 1.8; margin: 0; }";
  html += ".tips li { margin-bottom: 8px; }";
  html += ".back-link { display: inline-block; margin-top: 25px; color: #666; text-decoration: none; font-weight: 500; padding: 10px 20px; border-radius: 25px; transition: all 0.3s ease; background: #f5f5f5; }";
  html += ".back-link:hover { color: #4CAF50; background: #e8f5e8; transform: translateX(-5px); }";
  html += "@media (max-width: 768px) { .container { padding: 20px; margin: 10px; border-radius: 15px; } .schedule-grid { grid-template-columns: 1fr; gap: 20px; } .time-inputs { grid-template-columns: 1fr 1fr 1fr; gap: 15px; } h1 { font-size: 2em; } .btn { padding: 14px 25px; margin: 8px; } }";
  html += "</style>";
  html += "<script>";
  html += "function toggleSchedule(index) {";
  html += "  const row = document.getElementById('schedule-' + index);";
  html += "  const checkbox = document.getElementById('active' + index);";
  html += "  if (checkbox.checked) {";
  html += "    row.classList.add('active');";
  html += "    row.classList.remove('inactive');";
  html += "  } else {";
  html += "    row.classList.remove('active');";
  html += "    row.classList.add('inactive');";
  html += "  }";
  html += "}";
  html += "function updateTotalGrams() {";
  html += "  let total = 0;";
  html += "  for(let i = 0; i < 4; i++) {";
  html += "    const checkbox = document.getElementById('active' + i);";
  html += "    const gramsInput = document.querySelector('input[name=\"grams' + i + '\"]');";
  html += "    if(checkbox && checkbox.checked && gramsInput) {";
  html += "      total += parseInt(gramsInput.value) || 0;";
  html += "    }";
  html += "  }";
  html += "  const info = document.querySelector('.daily-info');";
  html += "  if(info) {";
  html += "    const currentTotal = " + String(config.dailyGramsTotal) + ";";
  html += "    const diff = total - currentTotal;";
  html += "    let statusText = '';";
  html += "    if(diff > 0) statusText = ' (+' + diff + 'g)';";
  html += "    else if(diff < 0) statusText = ' (' + diff + 'g)';";
  html += "    info.innerHTML = '<strong>📊 Meta Diária:</strong> " + String(config.dailyGramsTotal) + "g dividida em " + String(config.periodsPerDay) + " períodos<br><strong>🧮 Total Configurado:</strong> ' + total + 'g' + statusText;";
  html += "  }";
  html += "}";
  html += "document.addEventListener('DOMContentLoaded', function() {";
  html += "  const gramsInputs = document.querySelectorAll('input[type=\"number\"]');";
  html += "  const checkboxes = document.querySelectorAll('input[type=\"checkbox\"]');";
  html += "  gramsInputs.forEach(input => input.addEventListener('input', updateTotalGrams));";
  html += "  checkboxes.forEach(checkbox => checkbox.addEventListener('change', updateTotalGrams));";
  html += "  updateTotalGrams();";
  html += "});";
  html += "</script>";
  html += "</head><body><div class='container'>";
  html += "<h1>⏰ Configurar Horários de Alimentação</h1>";
  
  html += "<div class='daily-info'>";
  html += "<strong>📊 Meta Diária:</strong> " + String(config.dailyGramsTotal) + "g dividida em " + String(config.periodsPerDay) + " períodos";
  html += "</div>";
  
  html += "<form action='/set_schedule' method='GET'>";
  html += "<div class='schedule-grid'>";
  
  for (int i = 0; i < 4; i++) {
    html += "<div id='schedule-" + String(i) + "' class='schedule-row" + String(config.feedingTimes[i].active ? " active" : " inactive") + "'>";
    
    html += "<div class='schedule-header'>";
    html += "<h3>🍽️ Horário " + String(i + 1) + "</h3>";
    html += "<label class='toggle-switch'>";
    html += "<input type='checkbox' id='active" + String(i) + "' name='active" + String(i) + "' value='1'" + String(config.feedingTimes[i].active ? " checked" : "") + " onchange='toggleSchedule(" + String(i) + ")'>";
    html += "<span class='slider'></span>";
    html += "</label>";
    html += "</div>";
    
    html += "<div class='time-inputs'>";
    
    // Hora
    html += "<div class='input-group'>";
    html += "<label>🕐 Hora:</label>";
    html += "<select name='hour" + String(i) + "'>";
    for (int h = 0; h < 24; h++) {
      html += "<option value='" + String(h) + "'" + String(config.feedingTimes[i].hour == h ? " selected" : "") + ">";
      if (h < 10) html += "0";
      html += String(h) + ":00</option>";
    }
    html += "</select>";
    html += "</div>";
    
    // Minuto
    html += "<div class='input-group'>";
    html += "<label>⏱️ Minuto:</label>";
    html += "<select name='minute" + String(i) + "'>";
    for (int m = 0; m < 60; m += 5) {
      html += "<option value='" + String(m) + "'" + String(config.feedingTimes[i].minute == m ? " selected" : "") + ">";
      if (m < 10) html += "0";
      html += String(m) + "</option>";
    }
    html += "</select>";
    html += "</div>";
    
    // Gramas
    html += "<div class='input-group'>";
    html += "<label>⚖️ Gramas:</label>";
    html += "<input type='number' class='grams-input' name='grams" + String(i) + "' value='" + String(config.feedingTimes[i].grams) + "' min='1' max='500'>";
    html += "</div>";
    
    html += "</div>";
    html += "</div>";
  }
  
  html += "</div>"; // fecha schedule-grid
  
  html += "<div class='btn-container'>";
  html += "<input type='submit' value='💾 Salvar Horários' class='btn'>";
  html += "<a href='/' class='btn btn-danger'>❌ Cancelar</a>";
  html += "</div>";
  html += "</form>";
  
  html += "<div class='tips'>";
  html += "<h3>💡 Dicas e Instruções</h3>";
  html += "<ul>";
  html += "<li><strong>Ativar/Desativar:</strong> Use o interruptor para habilitar ou desabilitar cada horário</li>";
  html += "<li><strong>Precisão:</strong> Minutos são configurados de 5 em 5 para maior praticidade</li>";
  html += "<li><strong>Flexibilidade:</strong> Ajuste as gramas individualmente conforme a necessidade do seu pet</li>";
  html += "<li><strong>Distribuição Automática:</strong> Use 'Redistribuir' na página principal para dividir a meta diária automaticamente</li>";
  html += "<li><strong>Validação:</strong> O sistema verifica se os horários não se sobrepõem</li>";
  html += "</ul>";
  html += "</div>";
  
  html += "<a href='/' class='back-link'>← Voltar ao Menu Principal</a>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleSetSchedule() {
  if (!feedingInProgress) {
    bool hasChanges = false;
    
    // Processa cada horário
    for (int i = 0; i < 4; i++) {
      String activeParam = "active" + String(i);
      String hourParam = "hour" + String(i);
      String minuteParam = "minute" + String(i);
      String gramsParam = "grams" + String(i);
      
      // Verifica se está ativo
      bool isActive = server.hasArg(activeParam);
      
      if (server.hasArg(hourParam) && server.hasArg(minuteParam) && server.hasArg(gramsParam)) {
        int hour = server.arg(hourParam).toInt();
        int minute = server.arg(minuteParam).toInt();
        int grams = server.arg(gramsParam).toInt();
        
        // Valida os valores
        if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59 && grams > 0 && grams <= 500) {
          // Verifica se houve mudança
          if (config.feedingTimes[i].active != isActive ||
              config.feedingTimes[i].hour != hour ||
              config.feedingTimes[i].minute != minute ||
              config.feedingTimes[i].grams != grams) {
            hasChanges = true;
          }
          
          // Aplica as mudanças
          config.feedingTimes[i].active = isActive;
          config.feedingTimes[i].hour = hour;
          config.feedingTimes[i].minute = minute;
          config.feedingTimes[i].grams = grams;
        }
      }
    }
    
    if (hasChanges) {
      saveConfig();
      logEvent("SCHEDULE_UPDATED", "Horários de alimentação atualizados via web");
      
      String response = "Horarios salvos com sucesso!\\n\\n";
      for (int i = 0; i < 4; i++) {
        response += "Horario " + String(i + 1) + ": ";
        if (config.feedingTimes[i].active) {
          response += String(config.feedingTimes[i].hour) + ":";
          if (config.feedingTimes[i].minute < 10) response += "0";
          response += String(config.feedingTimes[i].minute) + " - " + String(config.feedingTimes[i].grams) + "g";
        } else {
          response += "Desativado";
        }
        response += "\\n";
      }
      
      server.send(200, "text/plain", response);
    } else {
      server.send(200, "text/plain", "Nenhuma alteracao detectada");
    }
  } else {
    server.send(423, "text/plain", "Aguarde terminar a alimentacao atual");
  }
}

// Função para calcular delay entre passos baseado na velocidade configurada
unsigned long calculateStepDelay() {
  // Converte RPM para microssegundos entre passos
  // RPM -> RPS -> passos por segundo -> microssegundos por passo
  float stepsPerSecond = (config.motorSpeed / 60.0) * TOTAL_STEPS_PER_REVOLUTION;
  unsigned long delayUs = (unsigned long)(1000000.0 / stepsPerSecond);
  
  // Limita delay mínimo para garantir funcionamento do A4988 em full step
  if (delayUs < 1000) delayUs = 1000; // Mínimo 1ms para full step
  if (delayUs > 20000) delayUs = 20000; // Máximo 20ms
  
  return delayUs;
}

// Função para calcular checksum da configuração
uint16_t calculateChecksum(SystemConfig* cfg) {
  uint16_t checksum = 0;
  uint8_t* ptr = (uint8_t*)cfg;
  
  // Calcula checksum de todos os bytes exceto o próprio checksum
  for (int i = 0; i < sizeof(SystemConfig) - sizeof(cfg->checksum); i++) {
    checksum += ptr[i];
  }
  
  return checksum;
}

// Função para salvar configuração na EEPROM
void saveConfig() {
  config.checksum = calculateChecksum(&config);
  
  EEPROM.put(CONFIG_ADDRESS, config);
  EEPROM.commit();
  
  logEvent("CONFIG_SAVED", "Configuração salva na EEPROM");
}

// Função para carregar configuração da EEPROM
void loadConfig() {
  SystemConfig tempConfig;
  EEPROM.get(CONFIG_ADDRESS, tempConfig);
  
  // Verifica se a configuração é válida
  if (tempConfig.configured && tempConfig.checksum == calculateChecksum(&tempConfig)) {
    config = tempConfig;
    totalFeedings = config.totalFeedings;
    totalRotations = config.totalRotations;
    logEvent("CONFIG_LOADED", "Configuração carregada da EEPROM");
  } else {
    // Configuração padrão
    initDefaultConfig();
    saveConfig();
    logEvent("CONFIG_DEFAULT", "Configuração padrão criada");
  }
}

// Função para configuração padrão
void initDefaultConfig() {
  // Configuração padrão: 300g por dia dividido em 3 períodos
  config.dailyGramsTotal = 300;
  config.periodsPerDay = 3;
  config.autoDistribute = true;
  config.gramsPerRotation = 15.0; // Valor inicial para NEMA 17 - precisa calibrar
  
  // Distribui automaticamente os horários
  distributeFeedings();
  
  strcpy(config.ssid, defaultSSID);
  strcpy(config.password, defaultPassword);
  config.motorSpeed = 100; // RPM para NEMA 17 (mais rápido que 28BYJ-48)
  config.configured = true;
  config.totalFeedings = 0;
  config.totalRotations = 0;
}

// Função para distribuir automaticamente as alimentações
void distributeFeedings() {
  // Limpa todos os horários
  for (int i = 0; i < 4; i++) {
    config.feedingTimes[i].active = false;
  }
  
  int gramsPerMeal = config.dailyGramsTotal / config.periodsPerDay;
  
  if (config.periodsPerDay == 2) {
    // Manhã e Noite
    config.feedingTimes[0] = {7, 0, gramsPerMeal, true};
    config.feedingTimes[1] = {19, 0, gramsPerMeal, true};
  } else if (config.periodsPerDay == 3) {
    // Manhã, Tarde e Noite
    config.feedingTimes[0] = {7, 0, gramsPerMeal, true};
    config.feedingTimes[1] = {13, 0, gramsPerMeal, true};
    config.feedingTimes[2] = {19, 0, gramsPerMeal, true};
  } else if (config.periodsPerDay == 4) {
    // Manhã, Meio-dia, Tarde e Noite
    config.feedingTimes[0] = {6, 0, gramsPerMeal, true};
    config.feedingTimes[1] = {12, 0, gramsPerMeal, true};
    config.feedingTimes[2] = {17, 0, gramsPerMeal, true};
    config.feedingTimes[3] = {21, 0, gramsPerMeal, true};
  }
}

// Função para calcular quantos PASSOS são necessárias para dispensar X gramas (permite frações)
int calculateStepsForGrams(float grams) {
  if (config.gramsPerRotation <= 0) return TOTAL_STEPS_PER_REVOLUTION; // Proteção
  
  float stepsPerGram = TOTAL_STEPS_PER_REVOLUTION / config.gramsPerRotation;
  int steps = (int)round(grams * stepsPerGram);
  
  return steps > 0 ? steps : 1;
}

// Função para calcular quantos gramas serão dispensados com X passos
float calculateGramsForSteps(int steps) {
  if (config.gramsPerRotation <= 0) return 0;
  
  float gramsPerStep = config.gramsPerRotation / TOTAL_STEPS_PER_REVOLUTION;
  return steps * gramsPerStep;
}

// FUNÇÕES ANTIGAS (mantidas para compatibilidade)
// Função para calcular quantas rotações são necessárias para dispensar X gramas
int calculateRotationsForGrams(int grams) {
  if (config.gramsPerRotation <= 0) return 1; // Proteção
  int rotations = (int)ceil((float)grams / config.gramsPerRotation);
  return rotations > 0 ? rotations : 1;
}

// Função para calcular quantos gramas serão dispensados com X rotações
float calculateGramsForRotations(int rotations) {
  return rotations * config.gramsPerRotation;
}

// Função para executar reversão anti-travamento
void performAntiJamReverse() {
  logEvent("ANTI_JAM_REVERSE", String("Reversão em " + String(gramsDispensedCurrent, 1) + "g para evitar travamento").c_str());
  
  // Executa reversão pequena
  stepMotorMultiple(REVERSE_STEPS, false); // false = anti-horário
  delay(100); // Pequena pausa
  
  // Volta à posição original
  stepMotorMultiple(REVERSE_STEPS, true); // true = horário
  delay(100);
  
  // Processa requisições web
  server.handleClient();
  yield();
}

// Função para resetar o sistema completamente
void handleResetSystem() {
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Reset do Sistema</title>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;}";
  html += ".container{background:white;padding:20px;border-radius:8px;max-width:500px;margin:0 auto;}";
  html += ".warning{background:#ffe6e6;border:1px solid #ff9999;padding:15px;border-radius:5px;margin:15px 0;}";
  html += ".button{background:#dc3545;color:white;padding:10px 20px;border:none;border-radius:5px;text-decoration:none;display:inline-block;margin:5px;}";
  html += ".button:hover{background:#c82333;}";
  html += ".button.safe{background:#28a745;} .button.safe:hover{background:#218838;}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h2>⚠️ Reset do Sistema</h2>";
  html += "<div class='warning'>";
  html += "<strong>ATENÇÃO:</strong> Esta ação irá:<br>";
  html += "• Resetar todas as configurações para padrão<br>";
  html += "• Apagar estatísticas de alimentação<br>";
  html += "• Reiniciar o dispositivo<br>";
  html += "• Perder todas as calibrações<br>";
  html += "</div>";
  html += "<p>Tem certeza que deseja continuar?</p>";
  html += "<a href='/confirm_reset' class='button'>SIM - Resetar Tudo</a>";
  html += "<a href='/' class='button safe'>NÃO - Voltar</a>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

// Função para confirmar e executar o reset completo
void handleConfirmReset() {
  // Apaga toda a EEPROM
  for (int i = 0; i < EEPROM_SIZE; i++) {
    EEPROM.write(i, 0);
  }
  EEPROM.commit();
  
  String html = "<!DOCTYPE html><html><head><meta charset='UTF-8'>";
  html += "<title>Sistema Resetado</title>";
  html += "<style>body{font-family:Arial;margin:20px;background:#f0f0f0;text-align:center;}";
  html += ".container{background:white;padding:20px;border-radius:8px;max-width:400px;margin:0 auto;}";
  html += ".success{background:#e6ffe6;border:1px solid #99ff99;padding:15px;border-radius:5px;margin:15px 0;}";
  html += "</style></head><body>";
  html += "<div class='container'>";
  html += "<h2>✅ Sistema Resetado</h2>";
  html += "<div class='success'>";
  html += "O sistema foi resetado com sucesso!<br>";
  html += "O dispositivo será reiniciado em 3 segundos...";
  html += "</div>";
  html += "<script>setTimeout(function(){window.location.href='/';}, 3000);</script>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
  
  logEvent("SYSTEM_RESET", "Sistema resetado pelo usuário via web");
  delay(2000);
  ESP.restart(); // Reinicia o ESP8266
}

// Função para configurar microstepping via software (OPCIONAL)
// Use apenas se conectar MS1, MS2, MS3 aos pinos do ESP8266
void setMicrostepping(int mode) {
  /*
  // Descomente se conectar os pinos MS
  #ifdef MS1_PIN
    pinMode(MS1_PIN, OUTPUT);
    pinMode(MS2_PIN, OUTPUT);
    pinMode(MS3_PIN, OUTPUT);
    
    switch(mode) {
      case 1:  // Full step
        digitalWrite(MS1_PIN, LOW);
        digitalWrite(MS2_PIN, LOW);
        digitalWrite(MS3_PIN, LOW);
        break;
      case 2:  // Half step
        digitalWrite(MS1_PIN, HIGH);
        digitalWrite(MS2_PIN, LOW);
        digitalWrite(MS3_PIN, LOW);
        break;
      case 4:  // Quarter step
        digitalWrite(MS1_PIN, LOW);
        digitalWrite(MS2_PIN, HIGH);
        digitalWrite(MS3_PIN, LOW);
        break;
      case 8:  // Eighth step
        digitalWrite(MS1_PIN, HIGH);
        digitalWrite(MS2_PIN, HIGH);
        digitalWrite(MS3_PIN, LOW);
        break;
      case 16: // Sixteenth step
        digitalWrite(MS1_PIN, HIGH);
        digitalWrite(MS2_PIN, HIGH);
        digitalWrite(MS3_PIN, HIGH);
        break;
    }
    logEvent("MICROSTEPPING", String("Configurado para " + String(mode) + " step").c_str());
  #endif
  */
  
  // Por enquanto, apenas log informativo
  logEvent("MICROSTEPPING", "Conecte MS1,MS2,MS3 ao GND para full step");
}
