# 🐾 Alimentador Pet Automático v4.1

Sistema auto- 📊 **Estatísticas Completas** - Histórico de alimentações e consumoát### 🆕 **Novidades da v4.1**co de alimentação para pets com controle via web, baseado em ESP8266 e motor NEMA 17 com driver A4988.

![Version](https://img.shields.io/badge/version-4.1-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP8266-orange.svg)
![Motor](https://img.shields.io/badge/motor-NEMA17-red.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

## 📋 Índice

- [Características](#-características)
- [Hardware Necessário](#-hardware-necessário)
- [Conexões Físicas](#-conexões-físicas)
- [Instalação](#-instalação)
- [Configuração](#-configuração)
- [Interface Web](#-interface-web)
- [Calibração](#-calibração)
- [Solução de Problemas](#-solução-de-problemas)
- [Especificações Técnicas](#-especificações-técnicas)
- [Contribuição](#-contribuição)
- [Licença](#-licença)Automático v4.1

Sistema automático de alimentação para pets com controle via web, baseado em ESP8266 e motor NEMA 17 com driver A4988.

![Version](https://img.shields.io/badge/version-4.1-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP8266-orange.svg)
![Motor](https://img.shields.io/badge/motor-NEMA17-red.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

## 📋 Índice

- [Características](#-características)
- [Hardware Necessário](#-hardware-necessário)
- [Conexões Físicas](#-conexões-físicas)
- [Instalação](#-instalação)
- [Configuração](#-configuração)
- [Interface Web](#-interface-web)
- [Calibração](#-calibração)
- [Solução de Problemas](#-solução-de-problemas)
- [Especificações Técnicas](#-especificações-técnicas)

## 🌟 Características

### ✨ **Principais Funcionalidades**
- 🎯 **Controle Preciso por Passos** - Dispensação exata de ração (frações de rotação)
- 📱 **Interface Web Responsiva** - Controle completo via navegador
- ⏰ **Programação de Horários** - Até 4 horários automáticos por dia
- 🔄 **Sistema Anti-Travamento** - Reversão automática para evitar entupimentos
- ⚖️ **Calibração por Gramas** - Configuração precisa da quantidade por rotação
- 📺 **Display OLED** - Informações em tempo real no dispositivo
- 📶 **Conexão WiFi** - Controle remoto e monitoramento
- � **Estatísticas Completas** - Histórico de alimentações e consumo
- 💾 **Backup de Configuração** - Dados salvos na EEPROM com checksum

### � **Novidades da v4.1**
- ✅ **Motor NEMA 17** - Maior precisão e torque
- ✅ **Driver A4988** - Controle direto STEP/DIR/ENABLE
- ✅ **Controle por Passos** - Frações de rotação (1/2, 1/4, 3/4, etc.)
- ✅ **Precisão Máxima** - Dispensação exata sem desperdício
- ✅ **Interface Melhorada** - Progresso visual em tempo real
- ✅ **Diagnósticos** - Testes de hardware integrados

## 🔌 Hardware Necessário

| Componente | Descrição | Quantidade |
|------------|-----------|------------|
| **ESP8266** | NodeMCU v3 ou Wemos D1 Mini | 1x |
| **Display OLED** | SSD1306 128x64 I2C | 1x |
| **Motor NEMA 17** | Motor de passo 200 passos/revolução | 1x |
| **Driver A4988** | Driver para motor NEMA 17 | 1x |
| **Rosca sem fim** | Para dispensar ração | 1x |
| **Fonte 12V** | Alimentação para motor (5V-12V) | 1x |
| **Resistores 10kΩ** | Pull-down para MS1,MS2,MS3 (opcional) | 3x |
| **Capacitor 100μF** | Filtragem da fonte (recomendado) | 1x |

## 🔌 Conexões Físicas

### **ESP8266 → A4988**
```
GPIO 5  → STEP    (Controle de passos)
GPIO 4  → DIR     (Controle de direção)
GPIO 16 → ENABLE  (Liga/desliga motor)
VCC     → VCC     (3.3V)
GND     → GND     (Terra)
```

### **A4988 → NEMA 17**
```
A+/A- → Bobina A do motor
B+/B- → Bobina B do motor
VDD   → 3.3V (lógica)
VMOT  → 12V (motor)
GND   → GND comum
```

### **Configuração A4988 (OBRIGATÓRIO!)**
Para **FULL STEP** (200 passos/revolução):
```
MS1 → GND (ou resistor 10kΩ → GND)
MS2 → GND (ou resistor 10kΩ → GND)
MS3 → GND (ou resistor 10kΩ → GND)
```

### **Display OLED I2C**
```
ESP8266 → OLED
GPIO 12 → SDA
GPIO 14 → SCL
VCC     → 3.3V
GND     → GND
```

### **Diagrama de Conexão**
```
    ┌─────────────────┐
    │     A4988       │
    │                 │
ESP │ GPIO5  ──→ STEP │
826 │ GPIO4  ──→ DIR  │
6   │ GPIO16 ──→ EN   │
    │                 │         ┌──────────┐
    │ MS1 ──→ GND     │ ←──────→ │ NEMA 17  │
    │ MS2 ──→ GND     │ 12V      │  Motor   │
    │ MS3 ──→ GND     │         └──────────┘
    └─────────────────┘
          ↑ 12V
    ┌─────────────┐
    │ Fonte 12V   │
    └─────────────┘
```

## 💻 Instalação

### **1. Bibliotecas Necessárias**
Instale via Arduino IDE Library Manager:
```
- ESP8266WiFi (incluída no core ESP8266)
- ESP8266WebServer (incluída no core ESP8266)
- Adafruit GFX Library
- Adafruit SSD1306
- NTPClient
- WiFiUdp (incluída no core ESP8266)
- EEPROM (incluída no core ESP8266)
```

### **2. Configuração WiFi**
Edite no código:
```cpp
const char* ssid = "SEU_WIFI";
const char* password = "SUA_SENHA";
```

### **3. Upload do Código**
1. Selecione a placa: **NodeMCU 1.0 (ESP-12E Module)**
2. Configure a velocidade: **115200 baud**
3. Faça o upload do arquivo `alimentador_pet.ino`

### **4. Primeira Configuração**
1. Conecte-se ao WiFi configurado
2. Acesse o IP do dispositivo no navegador
3. Execute a calibração inicial
4. Configure os horários de alimentação

## ⚙️ Configuração

### **Configurações Padrão**
- **Velocidade Motor:** 100 RPM
- **Calibração Inicial:** 15g/rotação
- **Gramas por dia:** 300g
- **Períodos por dia:** 3 (manhã, tarde, noite)
- **Sistema anti-travamento:** Ativo

### **Horários Padrão (3 períodos)**
- **07:00** - 100g
- **13:00** - 100g  
- **19:00** - 100g

## 🎮 Interface Web

### **Dashboard Principal**
- **Status do Sistema** - Estado atual e próxima alimentação
- **Controle Manual** - Botões para teste e alimentação imediata
- **Monitoramento** - Progresso em tempo real durante alimentação
- **Diagnósticos** - Testes de hardware integrados

### **Funcionalidades Disponíveis**
| Função | Descrição |
|--------|-----------|
| 🍽️ Alimentar | Dispensação manual (25g, 50g, 100g) |
| 🔧 Teste | Teste de funcionamento do motor |
| 🔌 Test ENABLE | Diagnóstico do pino ENABLE |
| ↩️ Reverter | Rotação reversa para desentupir |
| ⚖️ Calibrar | Calibração da quantidade por rotação |
| ⚡ Motor On/Off | Controle manual do motor |
| 📊 Estatísticas | Histórico de alimentações |
| ⏰ Programar | Configuração de horários automáticos |
| 🔄 Reset | Reset de configurações |

### **URLs de Acesso**
```
http://[IP_DO_ESP]/          - Dashboard principal
http://[IP_DO_ESP]/status    - Status em JSON
http://[IP_DO_ESP]/calibrate - Calibração
http://[IP_DO_ESP]/schedule  - Programação de horários
http://[IP_DO_ESP]/test_enable - Teste do pino ENABLE
```

## 🚀 Funcionalidades

### 🎮 Controles Manuais
- **Alimentação rápida**: 25g, 50g, 100g
- **Teste**: 10g para verificações
- **Reversão**: Limpeza manual da rosca
- **Controle do motor**: Liga/desliga manual

### ⏰ Alimentação Automática
- **Até 4 horários** configuráveis
- **Controle por gramas** preciso
- **Distribuição automática** da meta diária
- **Sistema de retry** em caso de falha

### 📊 Monitoramento
- **Tempo real**: Status no display OLED
- **Estatísticas web**: Total dispensado, alimentações
- **Logs detalhados**: Via Serial Monitor
- **Reconexão automática**: WiFi e sistema

### 🔧 Sistema Anti-Travamento
- **Reversão periódica**: A cada 5g dispensados
- **Reversão final**: Evita gotejamento
- **Detecção automática**: Sem intervenção manual
- **Configurável**: Parâmetros ajustáveis

## 🌐 API Endpoints

### Controles Básicos
```
GET /                    # Dashboard principal
GET /feed               # Alimentação 50g
GET /feed1              # Alimentação 25g
GET /feed3              # Alimentação 50g
GET /feed5              # Alimentação 100g
GET /test               # Teste 10g
GET /reverse            # Reversão manual
GET /stop               # Parar alimentação
```

### Configurações
```
GET /config             # Página de configurações
GET /calibrate          # Página de calibração
GET /schedule           # Configurar horários
GET /reset              # Opções de reset
```

### API de Dados
```
GET /status             # Status JSON completo
GET /set_calibration    # Salvar calibração
GET /set_daily          # Configurar meta diária
GET /set_schedule       # Salvar horários
GET /redistribute       # Redistribuir horários
```

### Controles do Sistema
```
GET /motor_on           # Ligar motor
GET /motor_off          # Desligar motor
GET /reset_stats        # Resetar estatísticas
GET /reset_system       # Reiniciar sistema
```

## ⚖️ Calibração

### **Processo de Calibração**
1. Acesse `/calibrate` na interface web
2. Coloque um recipiente sob o dispensador
3. Execute uma rotação completa (200 passos)
4. Pese a ração dispensada com precisão
5. Insira o valor medido na interface
6. Sistema calcula automaticamente a precisão por passo

### **Exemplo de Calibração**
```
1. Motor executa 200 passos (1 rotação completa)
2. Ração dispensada: 46g
3. Sistema salva: 46g/rotação = 0.23g/passo
4. Precisão final: ±0.23g por passo individual
```

### **Sistema de Precisão**
```
Calibração: 46g/rotação
Precisão por passo: 46g ÷ 200 = 0.23g/passo

Exemplo de solicitação: 25g
Cálculo: 25g ÷ 0.23g/passo = 109 passos
Resultado: 109 passos = 25.07g (erro de apenas 0.07g!)
```

## 🛠️ Solução de Problemas

### **Motor não funciona**
1. ✅ Verifique conexões STEP, DIR, ENABLE
2. ✅ **Confirme que MS1, MS2, MS3 estão conectados ao GND**
3. ✅ Teste o pino ENABLE via `/test_enable`
4. ✅ Verifique alimentação 12V do A4988
5. ✅ Confirme que o motor não está travado mecanicamente

### **Motor só funciona quando toco nos pinos MS**
🎯 **CAUSA:** Pinos MS1, MS2, MS3 estão flutuando!
✅ **SOLUÇÃO:** Conecte MS1, MS2, MS3 diretamente ao GND ou use resistores pull-down de 10kΩ

### **Imprecisão na dispensação**
1. ✅ Execute nova calibração com balança precisa (0.1g)
2. ✅ Verifique se a rosca não está entupida
3. ✅ Confirme que o motor não está perdendo passos
4. ✅ Ajuste a velocidade se houver perda de passos

### **Problemas de WiFi**
```
Verificações:
✓ SSID e senha corretos no código
✓ Rede 2.4GHz (ESP8266 não suporta 5GHz)
✓ Sinal WiFi suficiente no local
✓ Reset do ESP8266 se necessário
```

### **Display não funciona**
```
Verificações:
✓ Endereço I2C correto (0x3C)
✓ Conexões SDA (GPIO12) / SCL (GPIO14)
✓ Alimentação 3.3V estável
✓ Biblioteca Adafruit_SSD1306 instalada
```

### **Diagnósticos Integrados**
- **`/test_enable`** - Testa funcionamento do pino ENABLE
- **Monitor Serial (115200 baud)** - Logs detalhados do sistema
- **Interface web** - Status em tempo real de todos os componentes

## 📊 Especificações Técnicas

| Item | Especificação |
|------|---------------|
| **Microcontrolador** | ESP8266 NodeMCU |
| **Motor** | NEMA 17, 200 passos/revolução |
| **Driver** | A4988, Full Step |
| **Display** | OLED 128x64, I2C |
| **Precisão** | ±0.23g (com calibração 46g/rot) |
| **Velocidade** | 100 RPM (configurável) |
| **Alimentação Motor** | 5V-12V |
| **Alimentação Lógica** | 3.3V |
| **Conectividade** | WiFi 2.4GHz |
| **Armazenamento** | EEPROM 512 bytes |
| **Interface** | Web responsiva |
| **Controle** | Passos individuais |

### **Performance**
- **Precisão máxima**: ±1 passo = ±(gramas_por_rotação/200)
- **Frações de rotação**: 1/2, 1/4, 3/4, etc.
- **Velocidade dispensação**: Configurável (50-200 RPM)
- **Capacidade**: Limitada pelo recipiente de ração
- **Autonomia**: Contínua com alimentação externa

### **Conectividade**
- **WiFi**: 802.11 b/g/n 2.4GHz
- **Interface**: HTTP/HTML responsiva
- **Protocolos**: TCP/IP, HTTP, NTP
- **Reconexão**: Automática a cada 30s

### **Armazenamento**
- **EEPROM**: 512 bytes para configurações
- **Backup**: Configurações persistem sem energia
- **Reset**: Opções de reset seletivo
- **Integridade**: Checksum de validação

### **Comparação v4.0 → v4.1**
| Aspecto | v4.0 (28BYJ-48) | v4.1 (NEMA 17) |
|---------|-----------------|----------------|
| **Precisão** | ±2g | ±0.23g |
| **Torque** | Baixo | Alto |
| **Velocidade** | 12 RPM | 100 RPM |
| **Controle** | Rotações completas | Passos individuais |
| **Frações** | ❌ | ✅ |
| **Confiabilidade** | Boa | Excelente |

---

## 🤝 Contribuição

Contribuições são bem-vindas! Por favor:

1. **Fork** o repositório
2. Crie uma **branch** para sua feature (`git checkout -b feature/AmazingFeature`)
3. **Commit** suas mudanças (`git commit -m 'Add some AmazingFeature'`)
4. **Push** para a branch (`git push origin feature/AmazingFeature`)
5. Abra um **Pull Request**

### 📝 Diretrizes
- Mantenha o código limpo e comentado
- Teste todas as funcionalidades
- Atualize a documentação se necessário
- Siga o padrão de código existente

## 📋 TODO / Roadmap

- [ ] **App mobile** nativo
- [ ] **Notificações push** quando alimentar
- [ ] **Histórico detalhado** de alimentações
- [ ] **Múltiplos pets** com perfis diferentes
- [ ] **Sensor de nível** de ração
- [ ] **Integração IoT** (Alexa, Google Home)
- [ ] **Backup na nuvem** das configurações
- [ ] **API REST** completa

## 📄 Licença

Este projeto está licenciado sob a Licença MIT - veja o arquivo [LICENSE](LICENSE) para detalhes.

## 👨‍💻 Autor

**Guilherme Devito** - [Devito](https://github.com/Devito552)

## 🙏 Agradecimentos

- Comunidade Arduino/ESP8266
- Bibliotecas open source utilizadas
- Beta testers e colaboradores
- Pets que inspiraram o projeto! 🐕🐱

---

## 📞 Suporte

Se você encontrar problemas ou tiver sugestões:

1. **Issues**: Use as [GitHub Issues](https://github.com/Devito552/automatic_dog_feeder_iot/issues)
2. **Documentação**: Consulte este README
3. **Comunidade**: Participe das discussões

---

**⭐ Se este projeto foi útil, considere dar uma estrela no GitHub!**

**Desenvolvido com ❤️ para nossos amigos de quatro patas!** 🐕🐱

*Alimentador Pet v4.1 - Motor NEMA 17 + Driver A4988 - Controle Preciso por Passos*
