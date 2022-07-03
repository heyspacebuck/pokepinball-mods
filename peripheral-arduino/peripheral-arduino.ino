// Gameboy serial port sniffer for Arduinos

#define CLOCK     2  // Must support interrupts: https://www.arduino.cc/reference/en/language/functions/external-interrupts/attachinterrupt/
#define DATA      5
#define TIMEOUT   100UL // How many milliseconds must elapse before giving up on a partial read
uint8_t dataByte = 0;
uint8_t bitPosition = 7;
uint8_t dataBit = 0;
volatile bool newBit = false;
bool receiving = false;
uint32_t timeout = 0;

// Run this on any clock rising edge:
void dataTick() {
  newBit = true;
  //Serial.println("inter!");
}

void resetValues() {
  bitPosition = 7;
  dataByte = 0;
  dataBit = 0;
  newBit = false;
  receiving = false;
  timeout = 0;
}

void parseCommand(uint8_t command) {
  uint8_t commandPower = (command & 0xF0) >> 4;
  uint8_t commandTime = (command & 0x0F);

  uint16_t powerPercent, timeFrames;

  switch (commandPower) {
    case 0b0001:
      powerPercent = 25;
      break;
    case 0b0011:
      powerPercent = 50;
      break;
    case 0b0111:
      powerPercent = 75;
      break;
    case 0b1111:
      powerPercent = 100;
      break;
    default:
      powerPercent = 0;
      break;
  }

  // the relationship between the time command and the number of frames is a little weird
  switch (commandTime) {
    case 0b0001:
      timeFrames = 1;
      break;
    case 0b0010:
      timeFrames = 3;
      break;
    case 0b0011:
      timeFrames = 4;
      break;
    case 0b0100:
      timeFrames = 8;
      break;
    case 0b0101:
      timeFrames = 64;
      break;
    case 0b0110:
      timeFrames = 96;
      break;
    default:
      timeFrames = 0;
      break;
  }
  
  char msg[80];
  sprintf(msg, "Rumble at %d%% intensity for %d frames", powerPercent, timeFrames);
  Serial.println(msg);
}

void setup() {
  pinMode(CLOCK, INPUT);
  pinMode(DATA, INPUT);
  Serial.begin(115200);
  Serial.println("online!");
  attachInterrupt(digitalPinToInterrupt(CLOCK), dataTick, RISING);
  resetValues();
}

void loop() {
  if (newBit) {
    newBit = false;
    dataBit = digitalRead(DATA);
    
    if (!receiving) {
      receiving = true;
      timeout = millis();
    }
    
    dataByte |= ((dataBit & 0x01) << bitPosition);
    //Serial.println(dataByte);
    if (bitPosition == 0) {
      parseCommand(dataByte);
      resetValues();
    } else {
      bitPosition--; 
    }
  }

  if (receiving && (millis() - timeout > TIMEOUT)) {
    char msg[80];
    sprintf(msg, "Timed out! Only received %d bits: %0x", (7 - bitPosition), dataByte);
    Serial.println(msg);
    resetValues();
  }
}
