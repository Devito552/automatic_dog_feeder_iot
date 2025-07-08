# 🐕 Alimentador Pet Automático com ESP8266

Um sistema completo de alimentação automática para pets baseado em ESP8266, com interface web moderna, sistema de calibração por gramas e controle anti-travamento.

![Version](https://img.shields.io/badge/version-4.1-blue.svg)
![Platform](https://img.shields.io/badge/platform-ESP8266-orange.svg)
![License](https://img.shields.io/badge/license-MIT-green.svg)

## 📋 Índice

- [Características](#-características)
- [Hardware Necessário](#-hardware-necessário)
- [Esquema de Ligação](#-esquema-de-ligação)
- [Instalação](#-instalação)
- [Configuração](#-configuração)
- [Interface Web](#-interface-web)
- [Funcionalidades](#-funcionalidades)
- [API Endpoints](#-api-endpoints)
- [Troubleshooting](#-troubleshooting)
- [Contribuição](#-contribuição)
- [Licença](#-licença)

## 🌟 Características

### ✨ Principais Funcionalidades
- 🎯 **Sistema baseado em gramas** - Controle preciso da quantidade dispensada
- ⚖️ **Calibração automática** - Ajuste fino para diferentes tipos de ração
- 🕐 **Programação de horários** - Até 4 horários configuráveis por dia
- 📱 **Interface web moderna** - Controle total via navegador
- 🔄 **Sistema anti-travamento** - Reversão automática para evitar entupimentos
- 📊 **Estatísticas detalhadas** - Acompanhe o consumo do seu pet
- 📶 **Conectividade WiFi** - Controle remoto e monitoramento
- 🛡️ **Sistema robusto** - Reconexão automática e tratamento de erros

### 🔧 Características Técnicas
- **Display OLED** - Informações em tempo real
- **Motor de passo** - Precisão na dispensação
- **Armazenamento EEPROM** - Configurações persistem após reinicialização
- **Interface responsiva** - Funciona em desktop e mobile
- **Logs estruturados** - Monitoramento via Serial Monitor

## 🔌 Hardware Necessário

| Componente | Descrição | Quantidade |
|------------|-----------|------------|
| **ESP8266** | NodeMCU v3 ou Wemos D1 Mini | 1x |
| **Display OLED** | SSD1306 128x64 I2C | 1x |
| **Motor de Passo** | 28BYJ-48 5V | 1x |
| **Driver Motor** | ULN2003 | 1x |
| **Rosca sem fim** | Para dispensar ração | 1x |
| **Fonte 5V** | Para alimentar o motor | 1x |
| **Jumpers** | Para conexões | Vários |
| **Protoboard/PCB** | Para montagem | 1x |

## 🔗 Esquema de Ligação

### ESP8266 → Display OLED (SSD1306)
```
ESP8266    →    OLED
GPIO12     →    SDA
GPIO14     →    SCL
3.3V       →    VCC
GND        →    GND
```

### ESP8266 → Driver ULN2003 → Motor 28BYJ-48
```
ESP8266    →    ULN2003
GPIO5      →    IN1
GPIO4      →    IN2
GPIO0      →    IN3
GPIO2      →    IN4
GPIO16     →    ENABLE
5V         →    VCC
GND        →    GND
```

### Diagrama de Conexão
```
                ESP8266 NodeMCU
                ┌─────────────┐
     Display    │             │    Motor Driver
     OLED       │   GPIO12    │      ULN2003
   ┌──────┐     │   GPIO14    │    ┌─────────┐
   │ SDA  │◄────┤   3.3V      │    │   IN1   │◄─── GPIO5
   │ SCL  │◄────┤   GND       │    │   IN2   │◄─── GPIO4
   │ VCC  │◄────┤             │    │   IN3   │◄─── GPIO0
   │ GND  │◄────┤   GPIO16    ├────┤ ENABLE  │◄─── GPIO2
   └──────┘     │   5V        ├────┤   VCC   │
                │   GND       ├────┤   GND   │
                └─────────────┘    └─────────┘
                                        │
                                  Motor 28BYJ-48
                                   ┌─────────┐
                                   │ Stepper │
                                   │  Motor  │
                                   └─────────┘
```

## 📥 Instalação

### 1. Arduino IDE
```bash
# Instale as bibliotecas necessárias:
# - ESP8266WiFi
# - ESP8266WebServer
# - Adafruit GFX Library
# - Adafruit SSD1306
# - Stepper
# - NTPClient
# - EEPROM
```

### 2. Configuração do WiFi
```cpp
// Edite no código antes de fazer upload:
const char* ssid = "SEU_WIFI_AQUI";
const char* password = "SUA_SENHA_AQUI";
```

### 3. Upload do Código
1. Conecte o ESP8266 via USB
2. Selecione a placa correta em `Tools > Board > ESP8266 Boards`
3. Escolha a porta COM correta
4. Clique em Upload

### 4. Primeira Configuração
1. Abra o Serial Monitor (115200 baud)
2. Anote o IP exibido após conectar ao WiFi
3. Acesse o IP no navegador

## ⚙️ Configuração

### 📡 Configuração Inicial
1. **WiFi**: Configure suas credenciais no código
2. **Calibração**: Primeira execução requer calibração do sistema
3. **Horários**: Configure os horários de alimentação
4. **Meta Diária**: Defina a quantidade total de ração por dia

### 🎯 Processo de Calibração
1. Acesse `/calibrate` na interface web
2. Coloque um recipiente na saída
3. Dispense 3-5 rotações para teste
4. Pese a ração dispensada
5. Digite o peso medido e salve

## 📱 Interface Web

### 🏠 Dashboard Principal (`/`)
- **Status em tempo real** do sistema
- **Controles de alimentação** (25g, 50g, 100g)
- **Informações de calibração** e estatísticas
- **Status do motor** e conexão WiFi
- **Horários programados**

### ⚙️ Configurações (`/config`)
- **Estatísticas de uso** detalhadas
- **Informações de calibração**
- **Sistema anti-travamento**
- **Configuração de meta diária**
- **Visualização de horários**

### ⏰ Horários (`/schedule`)
- **Interface moderna** para configurar horários
- **Toggle switches** para ativar/desativar
- **Seleção de hora e minuto**
- **Configuração de gramas** por horário
- **Validação em tempo real**

### ⚖️ Calibração (`/calibrate`)
- **Processo step-by-step** de calibração
- **Dispensação para teste**
- **Cálculo automático** da calibração
- **Dicas para calibração precisa**

### 🔄 Reset (`/reset`)
- **Reset de estatísticas** apenas
- **Reinicialização completa** do sistema
- **Confirmações de segurança**
- **Preservação de configurações**

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

## 🔍 Troubleshooting

### ❌ Problemas Comuns

#### WiFi não conecta
```
Verificações:
✓ SSID e senha corretos no código
✓ Rede 2.4GHz (ESP8266 não suporta 5GHz)
✓ Sinal WiFi suficiente
✓ Reset do ESP8266
```

#### Motor não gira
```
Verificações:
✓ Conexões do ULN2003
✓ Alimentação 5V do motor
✓ Enable pin (GPIO16) conectado
✓ Código do motor correto
```

#### Display não funciona
```
Verificações:
✓ Endereço I2C correto (0x3C)
✓ Conexões SDA/SCL
✓ Alimentação 3.3V
✓ Biblioteca Adafruit_SSD1306
```

#### Calibração imprecisa
```
Soluções:
✓ Use balança precisa (0.1g)
✓ Ração seca e limpa
✓ Múltiplas calibrações
✓ Rosca sem fim limpa
```

### 🔧 Debug
```cpp
// Monitor Serial (115200 baud) mostra:
[timestamp] EVENT: details
[12345] SYSTEM_START: Alimentador Pet v4.1
[12678] WIFI_CONNECTED: 192.168.1.100
[15234] FEEDING_START: Iniciando 50g (5 rot)
[18456] FEEDING_COMPLETE: Dispensado: 50.0g
```

## 📈 Especificações

### Performance
- **Precisão**: ±0.5g (com calibração)
- **Velocidade**: ~10g por rotação (configurável)
- **Capacidade**: Limitada pelo recipiente
- **Autonomia**: Depende da fonte de energia

### Conectividade
- **WiFi**: 802.11 b/g/n 2.4GHz
- **Interface**: HTTP/HTML responsiva
- **Protocolos**: TCP/IP, HTTP, NTP
- **Reconexão**: Automática a cada 30s

### Armazenamento
- **EEPROM**: 512 bytes para configurações
- **Backup**: Configurações persistem sem energia
- **Reset**: Opções de reset seletivo
- **Integridade**: Checksum de validação

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

**Devito** - [GitHub](https://github.com/Devito552)

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

---

**⭐ Se este projeto foi útil, considere dar uma estrela no GitHub!**

*Alimentador Pet v4.1 - Tecnologia a serviço do bem-estar animal* 🐾
