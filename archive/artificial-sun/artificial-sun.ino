#define IR_SEND_PIN 3
#include <IRremote.hpp>

unsigned long command = 0;
byte al, ar, cl, cr; // address & command byte and their inverse
unsigned int a, c; // the complete address and command byte
unsigned long total = 0L; // the 32 byte long to send to plug into the NEC protocol

unsigned long second = 1000;
unsigned long minute = second * 60;
unsigned long hour = minute * 60;

// how long to sleep the program for between the sunset (8 mins) and sunrise (16 mins) sequence
unsigned long delay_minutes = 7 * 60 + 30 - 8 - 16;

namespace table{
  byte on = 192;
  byte off= 64;

  // 16 stages
  byte brighter = 0;
  byte darker = 128;
  
  byte flash = 208;
  byte smooth = 232;
  byte fade = 240;
  byte strobe = 200;
  
  byte W = 224;
  
  byte R = 32;
  byte R_1 = 16;
  byte R_2 = 48;
  byte R_3 = 8;
  byte R_4 = 40;
  
  
  byte B = 96;
  byte B_1 = 80;
  byte B_2 = 112;
  byte B_3 = 72;
  byte B_4 = 104;
  
  
  byte G = 160;
  byte G_1 = 144;
  byte G_2 = 177;
  byte G_3 = 136;
  byte G_4 = 168;
}

void setup() {
  IrSender.begin();
  // 8-minute sunset sequence
  controlLED(table::on);
  controlLED(table::R_4);
  for (int i=0; i<4; i++){
    delay(second * 30);
    controlLED(table::darker);
  }
  controlLED(table::R_2);
  for (int i=0; i<4; i++){
    delay(second * 30);
    controlLED(table::darker);
  }
  controlLED(table::R_1);
  for (int i=0; i<4; i++){
    delay(second * 30);
    controlLED(table::darker);
  }
  controlLED(table::R);
  for (int i=0; i<4; i++){
    delay(second * 30);
    controlLED(table::darker);
  }
  controlLED(table::off);

  delay(delay_minutes * minute);

  // 16-minute sunrise sequence
  controlLED(table::on);
  // assume it's the darkest LED setting now
  controlLED(table::R);
  delay(minute * 4);
  // send again to ensure that LED is turned on
  controlLED(table::on);
  controlLED(table::R_1);
  delay(minute * 4);
  controlLED(table::R_2);
  for (int i=0; i<4; i++){
    delay(second * 30);
    controlLED(table::brighter);
  }
  controlLED(table::R_4);
  for (int i=0; i<4; i++){
    delay(second * 30);
    controlLED(table::brighter);
  }
  controlLED(table::B_3);
  for (int i=0; i<4; i++){
    delay(second * 30);
    controlLED(table::brighter);
  }
  controlLED(table::W);
  for (int i=0; i<4; i++){
    delay(second * 30);
    controlLED(table::brighter);
  }
  delay(hour);
  controlLED(table::off);
}

void loop() {

}

void controlLED (byte command){
  cl = command;
  doen(al, cl);
  total = combineint(c, a);
  IrSender.sendNEC(total, 32);
}

unsigned long combineint (unsigned int rechts, unsigned long links)
{
  /** makes a long from two integers*/
  unsigned long value2 = 0L;
  value2 = links << 16;
  value2 = value2 | (rechts);
  return value2;
}

unsigned int doen(byte ag, byte cg)
{
  /** invert address & command byte, and combine each into variables a & c */
  ar = ag ^ 0xFF;
  cr = cg ^ 0xFF;
  a = 256 * ag + ar;
  c = cg << 8 | cr;
}
