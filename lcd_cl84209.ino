/*

ESP8266 Arduino sketch for controlling I2C LCD salvaged from an AT&T CL84209
cordless landline phone system. The base station and remote handset LCDs are
slightly different in their segmented LCD area, but respond to the same
commands at I2C address 0x3E. Sample code released under MIT license.

Copyright (c) 2022 Roger Cheng

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include <Wire.h>

#define I2C_ADDR 0x3E
#define CONFIG_SEQUENCE_LENGTH 8
#define LINES 3
#define LINE_SEQUENCE_LENGTH 3
#define CHAR_POSITIONS 15
#define SEGMENTS_BYTE_COUNT 16

// Comment out this line out to turn off LED heartbeat
//#define LED_HEARTBEAT

// Only one of the following should be active.
//#define CHARACTER_SET
//#define ALL_ON
#define SEGMENT_KNOB

#ifdef SEGMENT_KNOB
#include <Encoder.h>
Encoder segmentKnob(12,14); // GPIO12 = D6 on a Wemos D1 Mini, GPIO14 = D5
long segmentKnobPosition;
long segmentKnobOffset = 0;
long segmentNumber=0;
#endif

#ifdef CHARACTER_SET
// Starting point for the next subset of characters to be printed.
uint8_t characterSetStart;
#endif // CHARACTER_SET

// The CL84209 handset sends a particular sequence of commands once a second.
// (The very first time after power-up it skips the final 0x02, but it doesn't
// seem to hurt to send it all the time.) This is data copied out of I2C logic
// analyzer and played back, with no understanding of what they mean.
uint8_t configSequence[CONFIG_SEQUENCE_LENGTH] = { 0x39, 0x39, 0x1C, 0x70, 0x5F, 0x6C, 0x0C, 0x02 };
void writeConfig()
{
  for(uint8_t i = 0; i < CONFIG_SEQUENCE_LENGTH; i++)
  {
    Wire.beginTransmission(I2C_ADDR);
    Wire.write(0);
    Wire.write(configSequence[i]);
    Wire.endTransmission();
  }
}

// Every time the CL84209 handset updates the screen, it sends three I2C
// in sequence one for each line on screen. The first two lines are generic
// alphanumeric displays, the third line is a custom segmented display.
// Each line starts with a three-byte sequence, followed by:
// Line 0: 15 bytes of alphanumeric data
// Line 1: 15 bytes of alphanumeric data
// Line 2: 16 bytes of binary data toggling segments on/off.
uint8_t lineStartSequence[LINES][LINE_SEQUENCE_LENGTH] = {
  {0x80, 0x80, 0x40},
  {0x80, 0xC0, 0x40},
  {0x80, 0x40, 0x40}
};
void writeLineStart(uint8_t line)
{
  for(uint8_t i = 0; i < LINE_SEQUENCE_LENGTH; i++)
  {
    Wire.write(lineStartSequence[line][i]);
  }
}

// Helper function to write a particular digit as hexadecimal out to I2C
void writeDigitAsHex(uint8_t digit)
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

// Sets one bit in the segments array, indexing from least significant bit.
void setBit(uint8_t index, uint8_t* segments)
{
  uint8_t segmentByte = index/8;
  uint8_t segmentBit = index%8;
  uint8_t bitFlag = 0x01 << segmentBit;

  if (segmentByte > SEGMENTS_BYTE_COUNT)
  {
    Serial.print("ERROR: segmentSetBit() index requires more than SEGMENTS_BYTE_COUNT bytes.");
    return;
  }
  segments[segmentByte] |= bitFlag;
}

#ifdef CHARACTER_SET
// Write 16 characters from the character set, eight characters per line.
// 'setStart' selects one of 16 starting points.
void writeCharacterSetLines(uint8_t setStart)
{
  if (0x10 < setStart)
  {
    Serial.print("ERROR: Out of range starting point for character subset.");
    return;
  }
  for (uint8_t line = 0; line < 2; line++)
  {
    Wire.beginTransmission(0x3E);
    writeLineStart(line);

    // When dumping out the character set, we prefix each line with a hexadecimal
    // value of that line's starting character. If the starting character is 0xA0,
    // this will send " 0xA0 " to I2C.
    Wire.write(0x20); // <Space>
    Wire.write(0x30); // 0
    Wire.write(0x78); // x
    writeDigitAsHex(setStart);
    Wire.write(0x30 + line*8); // 0 for first line, 8 for second line.

    Wire.write(0x20);

    for(uint8_t i = 0; i < 8; i++)
    {
      Wire.write(setStart*16+line*8+i);
    }

    Wire.write(0x20);
    Wire.endTransmission();
  }
}

// Cycles segments on/off depending on their position and
// current value of 'characterSetStart'
void writeSegmentsPattern()
{
  uint8_t segments[SEGMENTS_BYTE_COUNT]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  uint8_t whichBit = 0x01 << (characterSetStart&0x07);

  for(uint8_t i = 0; i < SEGMENTS_BYTE_COUNT*8; i++)
  {
    if (i & whichBit)
    {
      setBit(i, segments);
    }
  }

  Serial.print(characterSetStart, HEX);
  Serial.print(" ");
  Serial.print(whichBit, BIN);
  Serial.print(" ");
  for(uint8_t i = 0; i < SEGMENTS_BYTE_COUNT; i++)
  {
    Serial.print(segments[i],BIN);
    Serial.print(" ");
  }
  Serial.println();

  Wire.beginTransmission(0x3E);
  writeLineStart(2);
  for(uint8_t i = 0; i < SEGMENTS_BYTE_COUNT; i++)
  {
    Wire.write(segments[i]);
  }
  Wire.endTransmission();
}
#endif // CHARACTER_SET

void setup() {
  Serial.begin(115200);
  Wire.begin();

#ifdef ALL_ON
  Serial.println("CL84209 LCD all on");
#endif // ALL_ON

#ifdef CHARACTER_SET
  Serial.println("CL84209 LCD character set");
  characterSetStart = 0;
#endif // CHARACTER_SET

#ifdef SEGMENT_KNOB
  Serial.println("CL84209 LCD segment selection by knob");
  segmentKnobPosition = segmentKnob.read()/4;
  Serial.print("Segment knob start: ");
  Serial.println(segmentKnobPosition);
  segmentKnobOffset = -segmentKnobPosition;
#endif

#ifdef LED_HEARTBEAT
  pinMode(2, OUTPUT);
#endif // LED_HEARTBEAT
}

void loop() {
  writeConfig();

#ifdef ALL_ON
  for (uint8_t line = 0; line < 2; line++)
  {
    Wire.beginTransmission(0x3E);
    writeLineStart(line);
    for(uint8_t i = 0; i < CHAR_POSITIONS; i++)
    {
      Wire.write(0x7F);
    }
    Wire.endTransmission();
  }
  Wire.beginTransmission(0x3E);
  writeLineStart(2);
  for(uint8_t i = 0; i < SEGMENTS_BYTE_COUNT; i++)
  {
    Wire.write(0xFF);
  }
  Wire.endTransmission();
#endif // ALL_ON

#ifdef CHARACTER_SET
  writeCharacterSetLines(characterSetStart);
  writeSegmentsPattern();

  if (characterSetStart++ >= 0x0F)
  {
    characterSetStart = 0;
  }
#endif

#ifdef SEGMENT_KNOB
  uint8_t segments[SEGMENTS_BYTE_COUNT]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};

  long newPos = segmentKnob.read()/4;
  if (newPos != segmentKnobPosition)
  {
    Serial.print("Segment knob: ");
    Serial.println(newPos);
    segmentKnobPosition = newPos;

    if (segmentKnobPosition + segmentKnobOffset > 127)
    {
      segmentKnobOffset = 127-segmentKnobPosition;
      Serial.print("Segment max 127 reached, offset adjusted to ");
      Serial.println(segmentKnobOffset);
    }
    else if (segmentKnobPosition + segmentKnobOffset < 0)
    {
      segmentKnobOffset = -segmentKnobPosition;
      Serial.print("Segment min 0 reached, offset adjusted to ");
      Serial.println(segmentKnobOffset);
    }
    segmentNumber = segmentKnobOffset+segmentKnobPosition;
  }

  setBit(segmentNumber, segments);

  for (uint8_t line = 0; line < 2; line++)
  {
    bool wroteNonZero = false;

    // Write out segment data, most of which will be zero so
    // represented by '0'. One byte will be nonzero, write as hex.
    Wire.beginTransmission(0x3E);
    writeLineStart(line);
    for(uint8_t i = 0; i < 8; i++)
    {
      uint8_t segmentDataByte = segments[i+(line*8)];
      if (0 == segmentDataByte)
      {
        Wire.write(0x30); // 0
      }
      else
      {
        Wire.write(0x28); // (
        writeDigitAsHex(segmentDataByte>>4);
        writeDigitAsHex(segmentDataByte&0x0F);
        Wire.write(0x29); // )
        wroteNonZero = true;
      }
    }

    // If this entire line was zero, we need three spaces of padding.
    if(!wroteNonZero)
    {
      Wire.write(0x20);  // space
      Wire.write(0x20);  // space
      Wire.write(0x20);  // space
    }

    if (0==line)
    {
      // Line 0: write segment number as hexadecimal
      Wire.write(0x20); // space
      Wire.write(0x78); // x
      writeDigitAsHex(segmentNumber>>4);
      writeDigitAsHex(segmentNumber&0x0F);
    }
    else
    {
      // Line 1: write segment number as decimal
      Wire.write(0x20); // space
      if (segmentNumber >= 100)
      {
        Wire.write(0x30 + (segmentNumber/100));
      }
      else
      {
        Wire.write(0x20); // space
      }

      if (segmentNumber >= 10)
      {
        Wire.write(0x30 + ((segmentNumber%100)/10));
      }
      else
      {
        Wire.write(0x20); // space
      }
      if (segmentNumber >= 1)
      {
        Wire.write(0x30 + (segmentNumber%10));
      }
      else
      {
        Wire.write(0x30); // 0
      }
    }

    Wire.endTransmission();
  }

  // Write out segment control data
  Wire.beginTransmission(0x3E);
  writeLineStart(2);
  for(uint8_t i = 0; i < SEGMENTS_BYTE_COUNT; i++)
  {
    Wire.write(segments[i]);
  }
  Wire.endTransmission();
#endif // SEGMENT_KNOB

#ifdef LED_HEARTBEAT
  // LED heartbeat
  digitalWrite(2, LOW);
  delay(10);
  digitalWrite(2, HIGH);
  delay(100);
  digitalWrite(2, LOW);
  delay(10);
  digitalWrite(2, HIGH);
  delay(1000);
#endif // LED_HEARTBEAT
}
