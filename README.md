# 🐕 Alimentador Pet Automático ESP8266

Sistema de alimentação automática para pets utilizando ESP8266, motor de passo e rosca sem fim. Controle via web interface com horários programáveis e alimentação manual.

## ✨ Características

- **Alimentação Automática**: Horários programados para dispensar ração
- **Interface Web**: Controle remoto via navegador
- **Display OLED**: Monitoramento em tempo real
- **Motor de Passo**: Controle preciso da quantidade de ração
- **Conectividade WiFi**: Sincronização de horário via NTP
- **Controle Manual**: Alimentação sob demanda

## 🛠️ Componentes Necessários

### Hardware
- ESP8266 (NodeMCU ou similar)
- Motor de passo 28BYJ-48
- Driver ULN2003
- Display OLED 128x64 (SSD1306)
- Rosca sem fim para transporte da ração
- Fonte de alimentação 5V
- Jumpers e protoboard

### Software
- Arduino IDE
- Bibliotecas necessárias (listadas abaixo)

## 📚 Bibliotecas Necessárias

```cpp
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Stepper.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
```

## 🔌 Pinagem

| Componente | Pino ESP8266 | Função |
|------------|--------------|---------|
| Display OLED SDA | GPIO 12 | Dados I2C |
| Display OLED SCL | GPIO 14 | Clock I2C |
| Motor IN1 | GPIO 5 | Controle motor |
| Motor IN2 | GPIO 4 | Controle motor |
| Motor IN3 | GPIO 0 | Controle motor |
| Motor IN4 | GPIO 2 | Controle motor |

## ⚙️ Configuração

### 1. Configurações WiFi
```cpp
const char* ssid = "SEU_WIFI";
const char* password = "SUA_SENHA";
```

### 2. Horários de Alimentação
```cpp
FeedingTime feedingTimes[4] = {
  {6, 0, 3, true},   // 06:00 - 3 rotações - Ativo
  {12, 0, 3, true},  // 12:00 - 3 rotações - Ativo
  {18, 0, 3, true},  // 18:00 - 3 rotações - Ativo
  {22, 0, 2, false}  // 22:00 - 2 rotações - Inativo
};
```

### 3. Configurações do Motor
```cpp
const int STEPS_PER_MOTOR_REVOLUTION = 2048;  // Passos por revolução
const int STEPS_PER_CHUNK = 64;               // Passos por lote (evita timeout)
```

## 🚀 Como Usar

### Instalação
1. Clone este repositório
2. Instale as bibliotecas necessárias no Arduino IDE
3. Configure suas credenciais WiFi
4. Faça upload do código para o ESP8266
5. Monte o hardware conforme a pinagem

### Interface Web
Após conectar à rede WiFi, acesse o IP do ESP8266 no navegador:

- **Página Principal**: `http://IP_DO_ESP8266/`
- **Alimentação Manual**: 
  - `/feed1` - 1 rotação
  - `/feed3` - 3 rotações  
  - `/feed5` - 5 rotações
- **Controles**:
  - `/test` - Teste (1 rotação)
  - `/reverse` - Rotação reversa
  - `/stop` - Parar alimentação
  - `/status` - Status JSON

### Display OLED
O display mostra informações rotativas a cada 6 segundos:
- **Status**: Hora atual, total de alimentações, próxima alimentação
- **Horários**: Programação de alimentação
- **WiFi**: Informações de conexão e sinal
- **Estatísticas**: Dados de uso e memória

## 📱 Interface Web

A interface web inclui:
- ⏰ Hora atual sincronizada
- 🍽️ Contador de alimentações
- 🔄 Total de rotações executadas
- 📶 Status da conexão WiFi
- ⚙️ Estado do motor
- 🎮 Controles de alimentação
- 📊 Dados em formato JSON

## 🔧 Funcionalidades Técnicas

### Controle Não-Bloqueante
- Motor executa em pedaços pequenos para evitar timeout
- Interface web permanece responsiva durante alimentação
- Processamento assíncrono de tarefas

### Segurança
- Prevenção de sobreposição de alimentações
- Verificação de estado antes de executar comandos
- Tratamento de erros de conexão

### Monitoramento
- Estatísticas de uso em tempo real
- Monitoramento de memória livre
- Logs via Serial Monitor

## 🎯 Horários Programados

Por padrão, o sistema está configurado para:
- **06:00** - 3 porções (manhã)
- **12:00** - 3 porções (almoço)
- **18:00** - 3 porções (jantar)
- **22:00** - 2 porções (desabilitado)

## 🔄 Personalização

### Ajustar Quantidade de Ração
Modifique o número de rotações nos horários programados ou altere `STEPS_PER_MOTOR_REVOLUTION` para ajustar a quantidade por rotação.

### Adicionar Novos Horários
Expanda o array `feedingTimes` e ajuste o loop de verificação em `checkFeedingTimes()`.

### Velocidade do Motor
Ajuste a velocidade com `stepperMotor.setSpeed(12)` (RPM).

## 🐛 Troubleshooting

### Problemas Comuns
- **WiFi não conecta**: Verifique SSID e senha
- **Display não funciona**: Confirme conexões I2C e endereço (0x3C)
- **Motor não gira**: Verifique alimentação e conexões do driver
- **Interface web lenta**: Reduza `STEPS_PER_CHUNK` se necessário

### Debug
Use o Serial Monitor (74880 baud) para acompanhar logs do sistema.

## 📄 Licença

Este projeto está sob a licença MIT. Veja o arquivo LICENSE para mais detalhes.

## 🤝 Contribuições

Contribuições são bem-vindas! Sinta-se à vontade para:
- Reportar bugs
- Sugerir melhorias
- Enviar pull requests
- Compartilhar suas modificações

## 📞 Suporte

Para dúvidas ou problemas, abra uma issue no repositório.

---

⭐ **Gostou do projeto?** Dê uma estrela no repositório!

**Versão**: 3.2  
**Compatibilidade**: ESP8266, Arduino IDE
