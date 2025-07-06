# 🚗 Alimentador Automático com ESP8266 + OLED + Motor NEMA 17

Este projeto implementa um alimentador automático utilizando um **ESP8266 com display OLED integrado**, um **motor de passo NEMA 17** e um **driver A4988**. Ideal para dosagem automatizada (como ração para pets ou aditivos automotivos), o sistema se conecta via Wi-Fi e exibe informações no display.

---

## 🖼️ Visão Geral

- 📶 Conexão à rede Wi-Fi
- 🖥️ Exibição de status no display OLED 0.96" (SSD1306, I2C)
- 🌀 Controle preciso de motor de passo NEMA 17 via driver A4988
- 🛠️ Estrutura preparada para expansão com controle remoto ou agendamentos

---

## ⚙️ Componentes Utilizados

| Componente            | Função                                 |
|-----------------------|----------------------------------------|
| ESP8266 + OLED V2.1   | Microcontrolador + display embutido    |
| Display OLED 0.96"    | Interface visual (128x64, I2C, 0x3C)   |
| Motor de passo NEMA 17| Atuador para liberação de alimento     |
| Driver A4988          | Controle do motor com STEP e DIR       |
| Fonte 12V             | Alimentação do motor                   |

---

## 🔌 Esquema de Conexão

| Função     | GPIO | ESP8266 Pino | Observação                   |
|------------|------|---------------|------------------------------|
| SDA (OLED) | 5    | D1            | I2C display - dados          |
| SCL (OLED) | 4    | D2            | I2C display - clock          |
| STEP       | 12   | D6            | Pulso do motor A4988         |
| DIR        | 14   | D5            | Direção do motor A4988       |

---

## 📲 Como Usar

### 1. Instale as bibliotecas na Arduino IDE:
- `Adafruit SSD1306`
- `Adafruit GFX`
- `ESP8266WiFi`

> Vá em `Sketch > Incluir Biblioteca > Gerenciar Bibliotecas`

---

### 2. Configure o Wi-Fi

No código `alimentador.ino`, edite:

```cpp
const char* ssid = "SEU_WIFI";
const char* password = "SUA_SENHA";

