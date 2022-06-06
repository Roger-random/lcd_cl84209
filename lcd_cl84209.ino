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

// GPIO2 = D4 and also built-in LED on Wemos D1 Mini
#define LED_PIN 2

// GPIO13 = D7 on a Wemos D1 Mini
#define KNOB_BUTTON_PIN 13

// milliseconds to wait before acknowledging button down state
unsigned long knob_button_wait = 500;
// Wait until this time before acknowledging button down state
unsigned long knob_button_next;

// Button down toggles display mode
typedef enum display_mode {
  character_set = 0,
  all_on,
  segment_knob
} display_mode;

display_mode current_mode = character_set;

// Handset LCD is one space offset from base station LCD.
// Toggle this every time we cycle back through beginning of display_mode.
bool is_handset;

#include <Encoder.h>
Encoder segmentKnob(12,14); // GPIO12 = D6 on a Wemos D1 Mini, GPIO14 = D5
long segmentKnobPosition;
long segmentKnobOffset = 0;
long segmentNumber=0;

// Starting point for the next subset of characters to be printed.
uint8_t characterSetStart;

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

    // If in handset mode, print an extra space.
    if (is_handset)
    {
      Wire.write(0x20); // <Space>
    }

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

// Check for button down and cycle to the next mode if so. Only responds once
// every knob_button_wait milliseconds to handle both debouncing and also held
// down button will continue cycling every knob_button_wait.
void checkButton()
{
  if (0 == digitalRead(KNOB_BUTTON_PIN) &&
      millis() > knob_button_next)
  {
    knob_button_next = millis() + knob_button_wait;
    switch(current_mode)
    {
      case character_set:
        Serial.println("Switching from character set mode to all on mode.");
        current_mode = all_on;
        break;
      case all_on:
        Serial.println("Switching from all on mode to segment knob mode.");
        current_mode = segment_knob;
        break;
      case segment_knob:
        Serial.println("Switching from segment knob mode to character set mode.");
        current_mode = character_set;
        is_handset = !is_handset;
        break;
      default:
        Serial.println("Oh no, fell out of switch()! Reverting to character set mode.");
        current_mode = character_set;
        break;
    }
  }
}

// Pause for a bit of LED heartbeat because either we have time to kill or
// screen is static and we want to show we are not frozen.
void led_heartbeat()
{
  // LED heartbeat
  digitalWrite(LED_PIN, LOW);
  delay(10);
  digitalWrite(LED_PIN, HIGH);
  delay(100);
  digitalWrite(LED_PIN, LOW);
  delay(10);
  digitalWrite(LED_PIN, HIGH);
  delay(1000);
}

// Set up knob button. (Knob quadrature encoder was done in Encoder() constructor)
void setup() {
  is_handset = false;

  pinMode(KNOB_BUTTON_PIN, INPUT);
  knob_button_next = millis();

  segmentKnobPosition = segmentKnob.read()/4;
  segmentKnobOffset = -segmentKnobPosition;

  pinMode(LED_PIN, OUTPUT);

  Serial.begin(115200);
  Wire.begin();
}

void loop() {
  uint8_t segments[SEGMENTS_BYTE_COUNT]={0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0};
  long newPos = segmentKnob.read()/4;

  checkButton();
  writeConfig();

  switch(current_mode)
  {
    case character_set:
      writeCharacterSetLines(characterSetStart);
      writeSegmentsPattern();

      if (characterSetStart++ >= 0x0F)
      {
        characterSetStart = 0;
      }
      led_heartbeat();
      break;
    case all_on:
      for (uint8_t line = 0; line < 2; line++)
      {
        Wire.beginTransmission(0x3E);
        writeLineStart(line);

        // If in handset mode, print an extra space.
        if (is_handset)
        {
          Wire.write(0x20); // <Space>
        }

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
      led_heartbeat();
      break;
    case segment_knob:
      if (newPos != segmentKnobPosition)
      {
        segmentKnobPosition = newPos;

        if (segmentKnobPosition + segmentKnobOffset > 127)
        {
          // Segment max reached but knob kept moving up. Adjust offset so
          // the first turn back will immediately start moving down.
          segmentKnobOffset = 127-segmentKnobPosition;
        }
        else if (segmentKnobPosition + segmentKnobOffset < 0)
        {
          // Segment min reached but knob kept moving down. Adjust offset so
          // the first turn back will immediately start moving up.
          segmentKnobOffset = -segmentKnobPosition;
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

        // If in handset mode, print an extra space.
        if (is_handset)
        {
          Wire.write(0x20); // <Space>
        }

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
      break;
    default:
      Serial.println("Oh no, fell out of switch()! Reverting to character set mode.");
      current_mode = character_set;
      break;
  }
}
