// Gameboy peripheral control for ATtiny85
// Drives a motor through a P-channel MOSFET
// When ATtiny85 output is HIGH, motor is OFF

#define F_CPU 8000000L

#include <avr/io.h>
#include <avr/interrupt.h>

#define CLOCK     PB2         // Which pin is connected to Main Serial Clock line
#define DATA      PB1         // Which pin is connected to Main Serial In line (i.e. data entering peripheral from game boy)
#define MOTOR     PB0         // Which pin will switch a MOSFET to control a vibrator motor
#define TIMEOUT   420UL       // How many counter overflows must elapse before giving up on a partial read
uint8_t dataByte = 0;         // Data received so far from GB
uint8_t bitPosition = 7;      // Bits are transmitted MSB first
uint8_t dataBit = 0;          // What's on the data line coming from the GB
volatile bool newBit = false; // Set in interrupt, use during loop because...reasons
bool receiving = false;       // Whether a byte is being received or not
bool rumbling = false;        // Whether the rumble is active or not
volatile uint32_t timer = 0;  // Increments 4200ish times a second as a crude substitute for millis()
uint32_t receiveStart = 0;    // The value of timeout when signal receive began
uint8_t powerValue = 0;       // How intense the motor should be vibrating, between 0 and 255
uint16_t timerAdd = 0;        // How many counter overflows the motor should rumble for
uint32_t rumbleStart = 0;     // The value of timeout when rumble began
uint32_t rumbleEnd = 0;       // The value of timeout when rumble should end

// Interrupt vector sequence triggered by rising edge on INT0 (PB2)
ISR(INT0_vect) {
  newBit = true;
}

ISR(TIMER0_OVF_vect) {
  timer++;
}

// Call this to prepare the controller to receive another data byte
void resetValues() {
  bitPosition = 7;
  dataByte = 0;
  dataBit = 0;
  newBit = false;
  receiving = false;
}

// Convert each data byte into some action
void parseCommand(uint8_t command) {
  uint8_t commandPower = (command & 0xF0) >> 4;
  uint8_t commandTime = (command & 0x0F);

  switch (commandPower) {
    case 0b0001:
    powerValue = 63;  // 25% power
    break;
    case 0b0011:
    powerValue = 127;  // 50% power
    break;
    case 0b0111:
    powerValue = 191;  // 75% power
    break;
    case 0b1111:
    powerValue = 255; // 100% power
    break;
    default:
    return;
    break;
  }

  // the relationship between the time command and the number of frames is a little weird
  // Also I've tuned these values based on my own device's timing, no calibrating of chips has occurred
  switch (commandTime) {
    case 0b0001:
    timerAdd = 70;  // 1 frame, 17 ms
    break;
    case 0b0010:
    timerAdd = 210;  // 3 frames, 50 ms
    break;
    case 0b0011:
    timerAdd = 280;  // 4 frames, 67 ms
    break;
    case 0b0100:
    timerAdd = 560;  // 8 frames, 133 ms
    break;
    case 0b0101:
    timerAdd = 4480;  // 64 frames, 1067 ms
    break;
    case 0b0110:
    timerAdd = 6720;  // 96 frames, 1600 ms
    break;
    default:
    return;
    break;
  }

  // If a rumble isn't currently occurring, start a rumble with the given intensity and duration
  // If a rumble is currently occurring but this one would be longer, update intensity and duration, otherwise ignore
  
  if (!rumbling) {
    rumbling = true;
    TCCR0A = 3 << WGM00 | 3 << COM0A0;
    rumbleStart = timer;
    rumbleEnd = rumbleStart + timerAdd;
    OCR0A = powerValue;
  } else if ((timer + timerAdd) > rumbleEnd) {
    rumbleEnd = timer + timerAdd;
    OCR0A = powerValue;
  }
}

void setup() {
  // Set Clock, Data-in as inputs (pull-ups shouldn't be necessary, they're pulled up on the GB end)
  // Set a pin as output to drive motor
  // Optional: Set Data-out as output to send data back to console
  DDRB = 1 << MOTOR;
  PORTB | = 1 << MOTOR;

  // Set INT0 (PB2, CLOCK pin) to trigger an interrupt on rising edge
  GIMSK = 1 << INT0;         // Enables INT0 interrupt
  MCUCR |= 0b11 << ISC00;     // INT0 interrupt only on rising edge

  // Set Timer/Counter0 Overflow interrupt
  TIMSK |= 1 << TOIE0;

  // Configure PWM for motor control pin
  //TCCR0A = 3 << WGM00 | 3 << COM0A0;   // Fast PWM mode 3 | clear OC0A on Compare Match
  TCCR0A = 3 << WGM00;                 // Only enable COM0A0 when duty cycle is greater than zero (because sometimes it glitches up and it drives me crazy)
  TCCR0B = 2 << CS00;                  // 1:8 clock prescaler (1 MHz effective ticks, 4-ish kHz counter)
  OCR0A = 0;                           // Duty cycle 0 initially
  
  // Prepare global values for receiving data
  resetValues();

  // Enable interrupts
  sei();
}

void loop() {
  // If a new bit interrupt flag has arisen:
  if (newBit) {
    newBit = false;
    dataBit = ((PINB & (1 << DATA)) >> DATA) & 1;
    
    if (!receiving) {
      receiving = true;
      receiveStart = timer;
    }
    
    dataByte |= ((dataBit & 0x01) << bitPosition);
    if (bitPosition == 0) {
      parseCommand(dataByte);
      resetValues();
      } else {
      bitPosition--;
    }
  }

  // Reset signal receiver if too much time has elapsed since last clock edge interrupt
  if (receiving && ((timer - receiveStart) > TIMEOUT || (timer < receiveStart))) {
    resetValues();
  }
  
  // Turn the rumble off if too much time has elapsed
  // This will misbehave briefly if rumbleEnd overflows but timer has yet to overflow, but, meh
  if (rumbling && (timer > rumbleEnd)) {
    OCR0A = 0;
    rumbling = false;
    TCCR0A = 3 << WGM00;
    PORTB |= 1 << MOTOR;
  }
}
