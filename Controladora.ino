// ------------------------ LIBRARIES ------------------------
#include "FTTech_SAMD51Clicks.h"
#include <Keypad.h> // Biblioteca do codigo
#include "FTTech_Components.h"
#include <Wire.h>
#include <LiquidCrystal_I2C.h>
#include "RTC_SAMD51.h"
#include "DateTime.h"
#include<SPIMemory.h>

// ------------------------ DEFINES ------------------------
#define DEBUG false
#define DEBUG_BAUDRATE 9600
#define ENCODER_NUMBER_POS 10
#define TRANSITION_PRESSURE 0.3
#define MOTOR_DIRECTION 1
#define MOTOR_PULSE_DELAY 1500
#define VALVE_TIME_SECTOR 0
#define CURRENT_VALVE_ADRESS 4096
#define TIME_LEFT_SECTOR 8192
#define PRESSURE_SENSOR_PIN A1

// ------------------------ KEYPAD DECLARATION ------------------------
const byte LINHAS = 4; // Linhas do teclado
const byte COLUNAS = 4; // Colunas do teclado

char TECLAS_MATRIZ[LINHAS][COLUNAS] = { // Matriz de caracteres (mapeamento do teclado)
  {'1', '2', '3', 'A'},
  {'4', '5', '6', 'B'},
  {'7', '8', '9', 'C'},
  {'*', '0', '#', 'D'}
};

byte PINOS_LINHAS[LINHAS] = {4,46,41,42}; // Pinos de conexao com as linhas do teclado
byte PINOS_COLUNAS[COLUNAS] = {12,6,0,1}; // Pinos de conexao com as colunas do teclado

Keypad myKeypad = Keypad(makeKeymap(TECLAS_MATRIZ), PINOS_LINHAS, PINOS_COLUNAS, LINHAS, COLUNAS); // Inicia teclado

// ------------------------ LCD DECLARATION ------------------------
LiquidCrystal_I2C lcd(0x27,16,2);

// ------------------------ STEPPER MOTOR DECLARATION ------------------------
const byte pinPUL = 5;
const byte pinDIR = 44;
const byte pinENA = 47;
const uint16_t microsteps = 1600;
FT_Stepper motor(pinPUL, pinDIR, pinENA, microsteps);

// ------------------------ ENCODER DECLARATION ------------------------
const byte pinA = 11;
const byte pinB = 51;
const byte pinZ = 10;
FT_Encoder encoder(pinA, pinB, pinZ);

// ENCODER VECTOR
/*
  Its a map of encoder's position on each valve
*/
static const int ENCODER_POSITION[ENCODER_NUMBER_POS] = {0,-400,-800,-1190,-1585,-1980,-2390,-2780,-3190,-3597};

// ------------------------ RTC DECLARATION ------------------------
RTC_SAMD51 rtc;

// ------------------------ SPI MEMORY DECLARATION ------------------------

SPIFlash flash(A6);
uint16_t timeLeft = 0;

// ------------------------ GLOBAL VARIABLES ------------------------
uint8_t valv = 1;
bool onZero = false;

// ------------------------ SETUP ------------------------
void setup() {

  systemBegin();

  systemSetup();

  if(needConfig(flash)){
    //Apago outras partes da memória
    flash.eraseSector(VALVE_TIME_SECTOR); // Tempos de válvulas
    flash.eraseSector(TIME_LEFT_SECTOR); // Tempos restantes de válvulas
    // Solicita que o usuário selecione o tempo de funcionamento de cada setor
    setTime();
  }
}

// ------------------------ LOOP ------------------------
void loop() {

  valv = flash.readByte(CURRENT_VALVE_ADRESS); // Recupera da memória o número da válvula atual

  // Bomba Ligada => Há pressão
  if(pression()){

    goToValv(valv); // Motor gira até a válvula

    lcd.clear();
    lcd.setCursor(1,0);
    lcd.print("Sistema Ligado");
    lcd.setCursor(3,1);
    lcd.print("Valvula: ");
    lcd.setCursor(12,1);
    lcd.print(valv);

    // Verifico se a válvula tem tempo restante ou está iniciando agora [Se == 255 então está iniciando agora]
    if(flash.readByte(TIME_LEFT_SECTOR + (2*(valv-1))) == 255){
      if(DEBUG) Serial.println("MEMORY GET TIME FUNCTION [ valv " + (String)valv + "]: " + (String)memoryGetTime(flash, valv, VALVE_TIME_SECTOR));
      keepTime(memoryGetTime(flash, valv, VALVE_TIME_SECTOR)); // Está iniciando agora, então pego o tempo que o usuário definiu
    }
    // Não está iniciando agora, então pego o tempo RESTANTE salvo na memória quando o sistema foi interrompido
    else{
      if(DEBUG) Serial.println("MEMORY GET TIME REMAINING [ valv " + (String)valv + "]: " + (String)memoryGetTime(flash, valv, TIME_LEFT_SECTOR));
      int remaining = memoryGetTime(flash, valv, TIME_LEFT_SECTOR);
      flash.eraseSector(TIME_LEFT_SECTOR); // Apago da memória o tempo restante
      keepTime(remaining);
    }

  // Se a bomba continua ligada, então, após o tempo na válvula, passo para a válvula seguinte
  if(pression()){
      if(valv < 5)
        valv++;
      else
        valv = 1;
    }
    // ATUALIZA A VÁLVULA A SER ACIONADA NA PRÓXIMA OPERAÇÃO
    flash.eraseSector(CURRENT_VALVE_ADRESS); // Apago da memória o número da válvula atual
    flash.writeByte(CURRENT_VALVE_ADRESS, valv); // Salvo na memória o número da próxima válvula a ser acionada
  }

  // Bomba Desligada => Não há pressão
  else{
    lcd.clear();
    lcd.setCursor(0,0);
    lcd.print("Bomba desligada!");

    while(true){ // Aguardo até a bomba ser ligada
      char readKeypad = myKeypad.getKey(); // Faço a leitura do teclado
      if(pression()) break; // Se há pressão então quebro o while
      switch(readKeypad){
        case 'C':     // ========== RESETAR O SISTEMA ==========
          lcd.clear();
          lcd.setCursor(0,0);
          lcd.print("Deseja resetar o");
          lcd.setCursor(0,1);
          lcd.print("sistema inteiro?");

          bool endWhile = false; // Variável auxiliar para sair do While
          while(true){
            char readKeypad = myKeypad.getKey();
            switch(readKeypad){
              case 'A':
                resetSystem();
                endWhile = true;
                if(pression()){
                  lcd.clear();
                  lcd.setCursor(0,0);
                  lcd.print("Sistema Ligado!!");
                  lcd.setCursor(3,1);
                  lcd.print("Valvula: ");
                  lcd.setCursor(12,1);
                  lcd.print(valv);
                }

                else{
                  lcd.clear();
                  lcd.setCursor(0,0);
                  lcd.print("Bomba desligada!");
                }
                break;

              case 'B':
                endWhile = true;
                lcd.clear();
                lcd.setCursor(0,0);
                lcd.print("Bomba desligada!");
                break;
            }
            if(endWhile) break;
          }
          break;
      }
    }
  }
}
