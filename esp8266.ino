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
double totalGramsDispensed = 0;  // Nova variável para total em gramas
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
 
  float gramsDispensed = calculateGramsForSteps(stepsCompleted);
  totalGramsDispensed += gramsDispensed; // Agora soma corretamente
  
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
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Alimentador Pet</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; margin: 0; padding: 10px; background: #eef; }
    .container { max-width: 800px; margin: auto; background: #fff; padding: 20px; border-radius: 10px; }
    h1, h2 { text-align: center; }
    .section { margin: 20px 0; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(100px, 1fr)); gap: 10px; }
    .btn {
      display: block; padding: 10px; text-align: center;
      background: #4CAF50; color: white; text-decoration: none;
      border-radius: 8px; font-size: 14px;
    }
    .btn:hover { background: #45a049; }
    .warn { background: #f90; }
    .danger { background: #e33; }
    .info-box { background: #f0f0f0; padding: 10px; border-radius: 5px; }
    .schedule { padding: 5px 0; }
    .small { font-size: 0.9em; color: #555; text-align: center; margin-top: 15px; }
  </style>
  <script>setTimeout(()=>location.reload(), 5000);</script>
</head>
<body>
  <div class="container">
    <h1>🐕 Alimentador Pet</h1>

    <div class="section info-box">
      <p><b>⏰ Hora:</b> )rawliteral";
  html += timeClient.getFormattedTime();
  html += "<br><b>🍽️ Total alimentações:</b> " + String(totalFeedings);
  html += "<br><b>📦 Total Dispensado:</b> " + String(totalGramsDispensed, 1) + "g";  
  html += "<br><b>🎯 Meta diária:</b> " + String(config.dailyGramsTotal) + "g (" + String(config.periodsPerDay) + "x)";
  html += "<br><b>📏 Calibração:</b> " + String(config.gramsPerRotation, 1) + "g/rotação";
  html += "<br><b>📶 WiFi:</b> " + String(WiFi.status() == WL_CONNECTED ? "🟢 Conectado" : "🔴 Desconectado");
  html += "<br><b>⚙️ Motor:</b> ";

  if (feedingInProgress) {
    html += "🔄 Alimentando (" + String(currentRotation + 1) + "/" + String(pendingRotations) + ")";
  } else {
    html += motorEnabled ? "🟢 Ligado" : "⚫ Desligado";
  }

  html += R"rawliteral(</p>
    </div>

    <div class="section">
      <h2>🎮 Controles</h2>
      <div class="grid">)rawliteral";

  if (!feedingInProgress) {
    html += R"rawliteral(
        <a href="/feed1" class="btn">🥄 25g</a>
        <a href="/feed3" class="btn">🍽️ 50g</a>
        <a href="/feed5" class="btn">🍖 100g</a>
        <a href="/test" class="btn">🔧 Teste</a>
        <a href="/reverse" class="btn">↩️ Reverter</a>
        <a href="/calibrate" class="btn warn">⚖️ Calibrar</a>)rawliteral";
    if (motorEnabled)
      html += R"rawliteral(<a href="/motor_off" class="btn warn">⚡ Desligar</a>)rawliteral";
    else
      html += R"rawliteral(<a href="/motor_on" class="btn">⚡ Ligar</a>)rawliteral";
  } else {
    html += R"rawliteral(<a href="/stop" class="btn danger">🛑 Parar</a>)rawliteral";
  }

  html += R"rawliteral(
      </div>
    </div>

    <div class="section">
      <h2>🔧 Configurações</h2>
      <div class="grid">
        <a href="/status" class="btn">📊 Status</a>
        <a href="/config" class="btn">⚙️ Config</a>
        <a href="/schedule" class="btn">⏰ Horários</a>
        <a href="/redistribute" class="btn">🔄 Redistribuir</a>
        <a href="/reset" class="btn danger">🔄 Reiniciar</a>
      </div>
    </div>

    <div class="section">
      <h2>⏰ Horários</h2>)rawliteral";

  bool hasSchedules = false;
  for (int i = 0; i < 4; i++) {
    if (config.feedingTimes[i].active) {
      hasSchedules = true;
      html += "<div class='schedule'>🕐 <b>" + String(config.feedingTimes[i].hour) + ":";
      if (config.feedingTimes[i].minute < 10) html += "0";
      html += String(config.feedingTimes[i].minute) + "</b> - " + String(config.feedingTimes[i].grams) + "g</div>";
    }
  }
  if (!hasSchedules) {
    html += "<div class='schedule'><i>Nenhum horário configurado</i></div>";
  }

  html += "<div class='small'>💡 Atualiza a cada 5s | 🔁 WiFi tentativas: " + String(wifiReconnectAttempts) + " | ⚙️ Versão: " + String(VERSION) + "</div>";
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
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Configurações</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; margin: 0; padding: 10px; background: #eef; }
    .container { max-width: 800px; margin: auto; background: #fff; padding: 20px; border-radius: 10px; }
    h1, h2, h3 { text-align: center; margin: 10px 0; }
    .info-box, .form-box, .stat-box { background: #f0f0f0; padding: 15px; border-radius: 8px; margin: 15px 0; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(200px, 1fr)); gap: 10px; }
    .btn {
      display: inline-block; background: #4CAF50; color: white;
      padding: 10px 15px; border-radius: 8px; text-decoration: none;
      font-weight: bold; text-align: center;
    }
    .btn:hover { background: #45a049; }
    .form-row { margin: 10px 0; }
    label { display: block; margin-bottom: 5px; font-weight: 600; }
    input, select {
      width: 100%; padding: 8px; border-radius: 5px;
      border: 1px solid #ccc; font-size: 14px;
    }
    .schedule-item {
      padding: 10px; border-radius: 6px; background: #fff;
      border-left: 4px solid #4CAF50;
    }
    .inactive { border-left-color: #aaa; color: #666; }
    .center { text-align: center; }
    .small { font-size: 0.9em; color: #555; text-align: center; margin-top: 15px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>⚙️ Configurações</h1>

    <div class="info-box grid">
      <div><h3>📱 Versão</h3><p><b>)rawliteral" + String(VERSION) + R"rawliteral(</b></p></div>
      <div><h3>📶 WiFi</h3><p><b>)rawliteral" + String(config.ssid) + R"rawliteral(</b></p></div>
      <div><h3>⚡ Motor</h3><p><b>)rawliteral" + String(config.motorSpeed) + R"rawliteral( RPM</b></p></div>
    </div>

    <div class="stat-box">
      <h2>📊 Estatísticas</h2>
      <p><b>🍽️ Total Alimentações:</b> )rawliteral" + String(totalFeedings) + R"rawliteral(</p>
      <p><b>📦 Total Dispensado:</b> )rawliteral" + String(totalGramsDispensed, 1) + R"rawliteral(g</p>
      <p><b>🕐 Tempo Online:</b> )rawliteral" + String(millis() / 60000) + R"rawliteral( minutos</p>
      <p><b>💾 Memória Livre:</b> )rawliteral" + String(ESP.getFreeHeap()) + R"rawliteral( bytes</p>
    </div>

    <div class="stat-box">
      <h2>⚖️ Calibração</h2>
      <p><b>📏 Gramas por Rotação:</b> )rawliteral" + String(config.gramsPerRotation, 1) + R"rawliteral(g</p>
      <p><b>🎯 Meta Diária:</b> )rawliteral" + String(config.dailyGramsTotal) + R"rawliteral(g</p>
      <p><b>🔄 Períodos:</b> )rawliteral" + String(config.periodsPerDay) + R"rawliteral(</p>
      <div class="center"><a href="/calibrate" class="btn">🔧 Recalibrar</a></div>
    </div>

    <div class="stat-box">
      <h2>🔧 Anti-Travamento</h2>
      <p><b>🔄 Reversão a cada:</b> )rawliteral" + String(REVERSE_INTERVAL_GRAMS) + R"rawliteral(g</p>
      <p><b>↩️ Passos:</b> )rawliteral" + String(REVERSE_STEPS) + R"rawliteral(</p>
      <p><b>🔚 Final:</b> )rawliteral" + String(FINAL_REVERSE_STEPS) + R"rawliteral(</p>
    </div>

    <div class="form-box">
      <h2>🎯 Meta Diária</h2>
      <form action="/set_daily" method="GET">
        <div class="form-row">
          <label>Total por dia (g)</label>
          <input type="number" name="total" value=")rawliteral" + String(config.dailyGramsTotal) + R"rawliteral(" min="50" max="1000">
        </div>
        <div class="form-row">
          <label>Períodos por dia</label>
          <select name="periods">)rawliteral";

  for (int i = 2; i <= 4; i++) {
    html += "<option value='" + String(i) + "'";
    if (config.periodsPerDay == i) html += " selected";
    html += ">" + String(i) + " períodos</option>";
  }

  html += R"rawliteral(
          </select>
        </div>
        <div class="center"><input type="submit" value="💾 Aplicar" class="btn"></div>
      </form>
    </div>

    <h2>⏰ Horários Configurados</h2>
    <div class="grid">)rawliteral";

  for (int i = 0; i < 4; i++) {
    bool active = config.feedingTimes[i].active;
    html += "<div class='schedule-item" + String(active ? "" : " inactive") + "'>";
    html += "<h3>Horário " + String(i + 1) + "</h3>";
    if (active) {
      html += "<p><b>🕐</b> " + String(config.feedingTimes[i].hour) + ":";
      if (config.feedingTimes[i].minute < 10) html += "0";
      html += String(config.feedingTimes[i].minute) + "</p>";
      html += "<p><b>⚖️</b> " + String(config.feedingTimes[i].grams) + "g</p>";
      html += "<p><b>✅ Ativo</b></p>";
    } else {
      html += "<p><b>❌ Desativado</b></p>";
    }
    html += "</div>";
  }

  html += R"rawliteral(
    </div>

    <div class="center">
      <a href="/schedule" class="btn">⏰ Editar Horários</a>
      <a href="/redistribute" class="btn">🔄 Redistribuir</a>
    </div>

    <div class="small"><a href="/" class="btn">← Voltar</a></div>
  </div>
</body>
</html>
)rawliteral";

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
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <title>Calibração</title>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; margin: 0; padding: 15px; background: #eef; }
    .container { max-width: 800px; margin: auto; background: #fff; padding: 20px; border-radius: 10px; }
    h1, h2, h3 { text-align: center; margin: 10px 0; }
    .section { margin: 20px 0; padding: 15px; background: #f5f5f5; border-radius: 8px; }
    .grid { display: grid; grid-template-columns: repeat(auto-fit, minmax(250px, 1fr)); gap: 10px; }
    .form { padding: 15px; background: #fff; border-radius: 8px; }
    .form label { display: block; margin-bottom: 5px; font-weight: bold; }
    .form input { width: 100%; padding: 8px; border: 1px solid #ccc; border-radius: 5px; }
    .btn {
      display: inline-block; background: #4CAF50; color: #fff; padding: 10px 15px;
      margin-top: 10px; text-align: center; border-radius: 6px; font-weight: bold; text-decoration: none;
    }
    .btn-secondary { background: #2196F3; }
    .warning { background: #fff3cd; padding: 10px; border-radius: 8px; text-align: center; font-weight: bold; }
    ul, ol { padding-left: 20px; line-height: 1.6; }
    .center { text-align: center; margin-top: 20px; }
  </style>
</head>
<body>
  <div class="container">
    <h1>⚖️ Calibração</h1>
    <div class="section center">
      <h2>📊 Atual</h2>
      <p><strong>)rawliteral" + String(config.gramsPerRotation, 1) + R"rawliteral(g</strong> por rotação</p>
    </div>

    <div class="section">
      <h2>🔧 Como Calibrar</h2>
      <ol>
        <li>Coloque um recipiente vazio sob a saída da ração.</li>
        <li>Configure o número de rotações e clique em "Dispensar".</li>
        <li>Pese a quantidade dispensada.</li>
        <li>Digite o peso medido e o número de rotações usadas.</li>
        <li>Salve para atualizar a calibração.</li>
      </ol>
    </div>
)rawliteral";

  if (feedingInProgress) {
    html += R"rawliteral(
    <div class="warning">
      ⚠️ Dispensando ração para calibração...<br>
      Aguarde e pese a ração dispensada.
    </div>
)rawliteral";
  } else {
    html += R"rawliteral(
    <div class="grid">
      <div class="form">
        <h3>🔄 1. Dispensar</h3>
        <form action="/set_calibration" method="GET">
          <label>Rotações:</label>
          <input type="number" name="rotations" value="3" min="1" max="10">
          <input type="submit" value="🚀 Dispensar" class="btn btn-secondary">
        </form>
        <p><em>Recomenda-se 3-5 rotações</em></p>
      </div>

      <div class="form">
        <h3>💾 2. Salvar</h3>
        <form action="/set_calibration" method="GET">
          <label>Peso medido (g):</label>
          <input type="number" name="grams" step="0.1" min="0.1" placeholder="Ex: 25.5">
          <label>Rotações usadas:</label>
          <input type="number" name="rotations" value="3" min="1">
          <input type="submit" value="💾 Salvar" class="btn">
        </form>
        <p><em>Use o valor exato da balança</em></p>
      </div>
    </div>
)rawliteral";
  }

  html += R"rawliteral(
    <div class="section">
      <h2>💡 Dicas</h2>
      <ul>
        <li>Use balança digital com precisão de 0.1g.</li>
        <li>Repita a calibração 2-3 vezes e tire a média.</li>
        <li>Evite umidade na ração.</li>
        <li>Certifique-se de que a rosca está limpa.</li>
      </ul>
    </div>

    <div class="center">
      <a href="/" class="btn">← Voltar</a>
    </div>
  </div>
</body>
</html>
)rawliteral";

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
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Reiniciar Sistema</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; background: #f0f0f0; padding: 20px; margin: 0; }
    .container { max-width: 600px; margin: auto; background: white; padding: 20px; border-radius: 10px; }
    h1 { text-align: center; font-size: 1.8em; margin-bottom: 20px; }
    h2 { font-size: 1.3em; margin: 10px 0; }
    .warning-box, .option-card, .stats-info {
      background: #f9f9f9; padding: 15px; border-radius: 10px; margin: 15px 0;
      border-left: 4px solid #ccc;
    }
    .btn {
      display: block; width: 100%; text-align: center; padding: 12px; margin: 10px 0;
      border-radius: 5px; color: white; text-decoration: none; font-weight: bold;
      border: none; cursor: pointer;
    }
    .btn-warning { background: #ff9800; }
    .btn-danger { background: #f44336; }
    .btn:hover { opacity: 0.9; }
    .back-link {
      display: inline-block; margin-top: 25px; text-decoration: none; color: #333;
      background: #e0e0e0; padding: 10px 20px; border-radius: 20px;
    }
    .back-link:hover { background: #d0ffd0; color: #4CAF50; }
  </style>
</head>
<body>
  <div class="container">
    <h1>🔄 Reinicialização</h1>
)rawliteral";

  if (feedingInProgress) {
    html += R"rawliteral(
    <div class="warning-box">
      <h2>⚠️ Sistema Ocupado</h2>
      <p>O motor está em funcionamento. Aguarde a alimentação terminar para continuar.</p>
    </div>
    )rawliteral";
  } else {
    html += R"rawliteral(
    <div class="warning-box">
      <h2>⚠️ Atenção</h2>
      <p>Escolha abaixo o tipo de reinicialização:</p>
    </div>

    <div class="option-card">
      <h2>📊 Resetar Estatísticas</h2>
      <div class="stats-info">
        <p><strong>Alimentações:</strong> )rawliteral" + String(totalFeedings) + R"rawliteral(</p>
        <p><strong>Gramas:</strong> )rawliteral" + String(totalGramsDispensed, 1) + R"rawliteral(g</p>
        <p><strong>Rotações:</strong> )rawliteral" + String(totalRotations) + R"rawliteral(</p>
      </div>
      <p><em>Essa ação mantém configurações e horários.</em></p>
      <a href="javascript:void(0)" onclick="resetStats()" class="btn btn-warning">📊 Resetar Estatísticas</a>
    </div>

    <div class="option-card">
      <h2>🔄 Reiniciar Sistema</h2>
      <p><em>Reinicia o ESP8266, mantendo configurações.</em></p>
      <a href="javascript:void(0)" onclick="restartSystem()" class="btn btn-danger">🔄 Reiniciar Sistema</a>
    </div>
    )rawliteral";
  }

  html += R"rawliteral(
    <a href="/" class="back-link">← Voltar ao Dashboard</a>
  </div>

  <script>
    function resetStats() {
      if(confirm('Deseja resetar as estatísticas?\n\nIsso irá zerar:\n- Total de alimentações\n- Total de gramas\n- Total de rotações\n\nAs configurações serão mantidas.')) {
        window.location.href = '/reset_stats';
      }
    }
    function restartSystem() {
      if(confirm('Deseja reiniciar o sistema agora?\n\nO dispositivo será reiniciado.')) {
        fetch('/reset_system').then(() => {
          alert('Reiniciando o sistema... Aguarde.');
          setTimeout(() => { window.location.href = '/'; }, 5000);
        });
      }
    }
  </script>
</body>
</html>
)rawliteral";

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
  String html = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
  <meta charset="UTF-8">
  <title>Horários</title>
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <style>
    body { font-family: sans-serif; background: #eef; margin: 0; padding: 15px; }
    .container { max-width: 450px; margin: auto; background: #fff; padding: 20px; border-radius: 10px; }
    h2 { margin-top: 0; text-align: center; }
    form { margin-top: 20px; }
    .row { margin-bottom: 15px; padding-bottom: 10px; border-bottom: 1px solid #ddd; }
    label { display: inline-block; width: 50px; margin-right: 5px; }
    input[type="number"] { width: 55px; padding: 5px; border-radius: 4px; border: 1px solid #ccc; }
    input[type="checkbox"] { margin-bottom: 5px; }
    .btn {
      display: inline-block; padding: 10px 18px; background: #4CAF50; color: #fff;
      border: none; border-radius: 5px; cursor: pointer; font-weight: bold;
    }
    .btn-cancel {
      background: #bbb; margin-left: 10px; text-decoration: none; color: #fff;
      padding: 10px 18px; border-radius: 5px;
    }
  </style>
</head>
<body>
  <div class="container">
    <h2>Configurar Horários</h2>
    <form action="/set_schedule" method="GET">
)rawliteral";

  for (int i = 0; i < 4; i++) {
    html += "<div class='row'>";
    html += "<input type='checkbox' name='active" + String(i) + "' value='1'";
    if (config.feedingTimes[i].active) html += " checked";
    html += "> Ativo<br>";

    html += "<label>Hora:</label>";
    html += "<input type='number' name='hour" + String(i) + "' min='0' max='23' value='" + String(config.feedingTimes[i].hour) + "'>";
    
    html += "<label>Min:</label>";
    html += "<input type='number' name='minute" + String(i) + "' min='0' max='59' value='" + String(config.feedingTimes[i].minute) + "'>";
    
    html += "<label>g:</label>";
    html += "<input type='number' name='grams" + String(i) + "' min='1' max='500' value='" + String(config.feedingTimes[i].grams) + "'>";
    html += "</div>";
  }

  html += R"rawliteral(
      <input type="submit" value="Salvar" class="btn">
      <a href="/" class="btn-cancel">Cancelar</a>
    </form>
  </div>
</body>
</html>
)rawliteral";

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
