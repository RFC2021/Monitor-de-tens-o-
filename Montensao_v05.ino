#include <RunningStatistics.h>
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <WiFiClient.h>

const char *ssid = "";
const char *password = "";

const char *smtpServer = "smtp2.br";  // Servidor SMTP sem autenticação
const int smtpPort = 25;                // Porta SMTP sem SSL
const char *senderEmail = "nao_responda_esp@.br";
const char *recipientEmail = "rafael.costa@.br";

ESP8266WebServer server(80);
WiFiClient client;  // Para conexão ao servidor SMTP

const char *hostName = "montensao";

#define ZMPT101B A0
#define ledPin D1

float slope = 3;  // Ajuste da leitura do sensor
float intercept = 0;
const float voltageThreshold = 50.0;  // Limite de tensão para o alarme

unsigned long printPeriod = 5000;
unsigned long previousMillis = 0;
unsigned long lastEmailTime = 0;
unsigned long emailInterval = 12 * 60 * 60000;  // Intervalo de 1 minuto para envio de e-mail

RunningStatistics inputStats;

void CalibrateVoltage() {
  for (int i = 0; i < 5000; ++i) {
    int rawValue = analogRead(ZMPT101B);
    inputStats.input(rawValue);
    delay(1);
  }
  intercept = 0;
}

void ReadVoltage() {
  int rawValue = analogRead(ZMPT101B);
  inputStats.input(rawValue);

  if ((millis() - previousMillis) >= printPeriod) {
    previousMillis = millis();
    float Volts_TRMS = inputStats.sigma() * slope + intercept;

    // Limita a tensão para o range de 0 a 300 para a barra (com um máximo de 300 V)
    int voltageLevel = (Volts_TRMS / 300.0) * 100; // Ajustando para um limite de 300 V
    if (voltageLevel > 100) voltageLevel = 100;  // Limita o valor máximo da barra
    if (voltageLevel < 0) voltageLevel = 0;      // Evita valores negativos

    String mensagem = "<!DOCTYPE html><html lang='pt-br'><head>";
    mensagem += "<meta charset='UTF-8'><meta name='viewport' content='width=device-width, initial-scale=1.0'>";
    mensagem += "<title>Monitor de Tensão</title>";
    
    // Link para Bootstrap e estilos customizados
    mensagem += "<link rel='stylesheet' href='https://maxcdn.bootstrapcdn.com/bootstrap/3.3.7/css/bootstrap.min.css'>";
    mensagem += "<style>";
    mensagem += "body { padding: 20px; background-color: #f4f4f4; font-family: Arial, sans-serif; }";
    mensagem += ".container { max-width: 600px; margin: 0 auto; }";
    mensagem += ".progress-bar { width: " + String(voltageLevel) + "%; background-color: #28a745; }";
    mensagem += ".progress { height: 30px; background-color: #e9ecef; border-radius: 5px; }";
    mensagem += "h1 { font-size: 24px; margin-bottom: 20px; text-align: center; color: #333; }";
    mensagem += ".alert { text-align: center; font-size: 18px; }";
    mensagem += "</style>";
    
    // Atualiza a página automaticamente a cada 5 segundos
    mensagem += "<meta http-equiv='refresh' content='5'>";
    mensagem += "</head><body>";

    // Conteúdo principal da página
    mensagem += "<div class='container'>";
    mensagem += "<h1>Monitor de Baixa de Isolamento</h1>";

    // Mostra a tensão do aterramento
    mensagem += "<div class='alert alert-info'>Tensão do Aterramento: <strong>" + String(Volts_TRMS, 2) + " VCA</strong></div>";

    // Barra de progresso para o nível de tensão
    mensagem += "<div class='progress'><div class='progress-bar' role='progressbar'></div></div>";

    // Mostra uma mensagem de status conforme o nível de tensão
    if (Volts_TRMS > 220.0) {  // Alerta crítico para tensões acima de 220 V
      mensagem += "<div class='alert alert-danger' role='alert'>Perigo: Alta tensão! (Acima de 220V)</div>";
    } else if (Volts_TRMS > voltageThreshold) {
      mensagem += "<div class='alert alert-warning' role='alert'>Atenção: Tensão elevada (Acima de " + String(voltageThreshold) + "V)</div>";
    } else {
      mensagem += "<div class='alert alert-success' role='alert'>Tensão dentro do limite seguro.</div>";
    }

    mensagem += "</div>"; // Fecha container
    mensagem += "</body></html>";

    // Envia a resposta ao cliente
    server.send(200, "text/html", mensagem);
  }
}


void sendEmail(String message) {
  if (!client.connect(smtpServer, smtpPort)) {
    Serial.println("Falha ao conectar ao servidor SMTP");
    return;
  }

  // Enviando comandos SMTP diretamente
  client.println("HELO esp8266");
  client.println("MAIL FROM: <" + String(senderEmail) + ">");
  client.println("RCPT TO: <" + String(recipientEmail) + ">");
  client.println("DATA");
  client.println("Subject: Alerta de Baixa de Isolamento - Bloco D1");
  client.println("");
  client.println(message);
  client.println(".");
  client.println("QUIT");

  Serial.println("E-mail enviado!");
}

void setup() {
  Serial.begin(9600);
  inputStats.setWindowSecs(100 / 60.0);  // Janela de amostragem para 60 Hz

  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  Serial.print("Conectando à rede Wi-Fi ");
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  WiFi.hostname(hostName);
  Serial.println("\nConectado!");
  Serial.print("Endereço IP: ");
  Serial.println(WiFi.localIP());

  if (!MDNS.begin(hostName)) {
    Serial.println("Erro ao configurar o mDNS");
  }

  server.on("/", ReadVoltage);
  server.begin();
  Serial.println("Servidor HTTP iniciado.");

  CalibrateVoltage();  // Calibração inicial

  pinMode(ledPin, OUTPUT);
}

void loop() {
  server.handleClient();
  float Volts_TRMS = inputStats.sigma() * slope + intercept;

  if (Volts_TRMS > voltageThreshold) {
    digitalWrite(ledPin, HIGH);  // Alarme
    unsigned long currentMillis = millis();
    if (currentMillis - lastEmailTime >= emailInterval) {
      sendEmail("Baixa de Isolamento detectada no Bloco D1! " + String(Volts_TRMS, 2) + " VCA");
      lastEmailTime = currentMillis;
    }
  } else {
    digitalWrite(ledPin, LOW);
  }

  // Calibração contínua a cada 5 segundos
  static unsigned long lastCalibrationMillis = 0;
  if (millis() - lastCalibrationMillis >= 5000) {
    lastCalibrationMillis = millis();
    CalibrateVoltage();
  }
}

