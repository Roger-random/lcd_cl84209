#include <Wire.h>

uint8_t configSequence[8][2] = {
  {0x00, 0x39},
  {0x00, 0x39},
  {0x00, 0x1C},
  {0x00, 0x70},
  {0x00, 0x5F},
  {0x00, 0x6C},
  {0x00, 0x0C},
  {0x00, 0x02},
};

uint8_t characterIndex;

void lcdConfig()
{
  for(uint8_t i = 0; i < 8; i++)
  {
    Wire.beginTransmission(0x3E);
    Wire.write(configSequence[i][0]);
    Wire.write(configSequence[i][1]);
    Wire.endTransmission();
  }
}

void writeAsHex(uint8_t digit)
{
  if (digit <= 0x09)
  {
    Wire.write(0x30+digit);
  }
  else if (digit <= 0x0F)
  {
    Wire.write(0x41+(digit-10));
  }
}

void characterSetCycle()
{
  // Line 1
  Wire.beginTransmission(0x3E);
  Wire.write(0x80);
  Wire.write(0x80);
  Wire.write(0x40);

  Wire.write(0x20);
  Wire.write(0x30); // 0
  Wire.write(0x78); // x
  writeAsHex(characterIndex>>4);
  Wire.write(0x30); // 0

  Wire.write(0x20);
  for(uint8_t i = 0; i < 8; i++)
  {
    Wire.write(characterIndex+i);
  }
  Wire.write(0x20);
  Wire.endTransmission();

  // Line 2
  Wire.beginTransmission(0x3E);
  Wire.write(0x80);
  Wire.write(0xC0);
  Wire.write(0x40);

  Wire.write(0x20);
  Wire.write(0x30); // 0
  Wire.write(0x78); // x
  writeAsHex(characterIndex>>4);
  Wire.write(0x38); // 8

  Wire.write(0x20);
  for(uint8_t i = 8; i < 16; i++)
  {
    Wire.write(characterIndex+i);
  }
  Wire.write(0x20);
  Wire.endTransmission();

  // Prepare for next call
  if (characterIndex >= 0xF0)
  {
    characterIndex = 0;
  }
  else
  {
    characterIndex += 0x10;
  }
}

void setup() {
  pinMode(2, OUTPUT);
  Serial.begin(115200);
  Wire.begin();

  Serial.println("CL84209 LCD character set");
  characterIndex = 0;
}

void loop() {
  lcdConfig();

  characterSetCycle();

  Wire.beginTransmission(0x3E);
  Wire.write(0x80);
  Wire.write(0x40);
  Wire.write(0x40);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.write(0xFF);
  Wire.endTransmission();

digitalWrite(2, LOW);
  delay(10);
  digitalWrite(2, HIGH);
  delay(200);
  digitalWrite(2, LOW);
  delay(10);
  digitalWrite(2, HIGH);
  delay(1000);
}
