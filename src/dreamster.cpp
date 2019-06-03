#include "dreamster.h"

// global for use in ISR
enum DreamsterSonarStates {
  DREAMSTER_SONAR_BEGIN_TRIGGER = 'T',
  DREAMSTER_SONAR_WAITING_ECHO = 'W',
  DREAMSTER_SONAR_MEASURING_PING = 'M',
  DREAMSTER_SONAR_COOLDOWN = 'C',
  DREAMSTER_SONAR_STANDBY = 'S',
};
struct DreamsterSonar {
  unsigned int distance = 0;
  char state = DREAMSTER_SONAR_STANDBY;
  unsigned int ticks = 0;
  unsigned int cooldown_ticks = 0;
  int trigger_pin;
  int echo_pin;
};
enum DreamsterSonars {
  A = 0, 
  B = 1, 
  C = 2,
  NUM_SONARS = 3,
};
DreamsterSonar sonar[NUM_SONARS];
int next_sonar = A;

extern char debug_state;
extern unsigned int debug_ticks;

void timer_callback()
{
  bool all_sonars_in_standby = true;
  // sonars state machine
  for (int i = 0; i < NUM_SONARS; i++) {
    switch (sonar[i].state) {
      case DREAMSTER_SONAR_BEGIN_TRIGGER:
        // trigger ping and move to next state
        digitalWrite(sonar[i].trigger_pin, HIGH);
        delayMicroseconds(5); // needs 10us in HIGH, and digitalWrite takes 5us
        digitalWrite(sonar[i].trigger_pin, LOW);
        sonar[i].state = DREAMSTER_SONAR_WAITING_ECHO;
      break;
      case DREAMSTER_SONAR_WAITING_ECHO:
        // awaiting start of ping reflection (echo)
        sonar[i].ticks++;
        if (digitalRead(sonar[i].echo_pin) == HIGH) {
          // measure first tick and move to next state
          sonar[i].ticks = 1;
          sonar[i].state = DREAMSTER_SONAR_MEASURING_PING;
        }
        // timeout, shouldn't take longer than 400us (8 cycles of a 40KHz burst),
        // plus some overhead
        if (sonar[i].ticks > 16) { // 464us
          sonar[i].ticks = 1;
          sonar[i].state = DREAMSTER_SONAR_MEASURING_PING;
        }
      break;
      case DREAMSTER_SONAR_MEASURING_PING:
        // count ticks until the echo pin is low
        if (digitalRead(sonar[i].echo_pin) == HIGH) {
          sonar[i].ticks++;
        } else {
          // when pin is low, count each tick as 1 cm, and move to next state
          sonar[i].distance = sonar[i].ticks / 2;
          sonar[i].cooldown_ticks = (sonar[i].cooldown_ticks + (sonar[i].ticks * 8)) / 2;
          sonar[i].ticks = 0;
          sonar[i].state = DREAMSTER_SONAR_COOLDOWN;
        }
        // timeout
        if (sonar[i].ticks > 800) { // 23ms
          sonar[i].distance = 0;
          sonar[i].cooldown_ticks = (sonar[i].cooldown_ticks + (sonar[i].ticks * 8)) / 2;
          sonar[i].ticks = 0;
          sonar[i].state = DREAMSTER_SONAR_COOLDOWN;
        }
        break;
      case DREAMSTER_SONAR_COOLDOWN:
        // wait for a while before triggering next ping
        sonar[i].ticks++;
        if (sonar[i].ticks > sonar[i].cooldown_ticks) { // 380 ticks = 11ms
          sonar[i].ticks = 0;
          sonar[i].state = DREAMSTER_SONAR_STANDBY;
        }
      break;
      case DREAMSTER_SONAR_STANDBY:
        // wait
      break;
    }
    if (sonar[i].state != DREAMSTER_SONAR_STANDBY) {
      all_sonars_in_standby = false;
    }
  }
  
  debug_state = sonar[next_sonar].state;
  debug_ticks = next_sonar;
  
  // if all sonars are on standby, proceed to ping the next sonar
  if (all_sonars_in_standby) {
    sonar[next_sonar].state = DREAMSTER_SONAR_BEGIN_TRIGGER;
    next_sonar++;
    if (next_sonar == NUM_SONARS) next_sonar = A;
  }
  
}

Dreamster::Dreamster(void)
{
}

void Dreamster::scan(int &a, int &b, int &c)
{
  scan_a(a);
  scan_b(b);
  scan_c(c);
}

void Dreamster::scan_a(int &a)
{
  a = sonar[A].distance;
}

void Dreamster::scan_b(int &b)
{
  b = sonar[B].distance;
}

void Dreamster::scan_c(int &c)
{
  c = sonar[C].distance;
}

void Dreamster::read_ir(int &left, int &right)
{
  left = analogRead(ir_l_);
  right = analogRead(ir_r_);
}

void Dreamster::show(uint8_t red, uint8_t green, uint8_t blue)
{
  analogWrite(led_r_, red);
  analogWrite(led_g_, green);
  analogWrite(led_b_, blue);
}

void Dreamster::move(int left, int right)
{
  rightMotor->setSpeed(right);
  leftMotor->setSpeed(left);
}

void Dreamster::update()
{
  // Update motor speed ramps
  leftMotor->update();
  rightMotor->update();
}

void Dreamster::setup()
{
  pinMode(us_trigger_a_, OUTPUT);
  pinMode(us_echo_a_, INPUT);
  pinMode(us_trigger_b_, OUTPUT);
  pinMode(us_echo_b_, INPUT);
  pinMode(us_trigger_c_, OUTPUT);
  pinMode(us_echo_c_, INPUT);
  
  sonar[A].trigger_pin = us_trigger_a_;
  sonar[A].echo_pin = us_echo_a_;
  sonar[B].trigger_pin = us_trigger_b_;
  sonar[B].echo_pin = us_echo_b_;
  sonar[C].trigger_pin = us_trigger_c_;
  sonar[C].echo_pin = us_echo_c_;

  pinMode(ir_l_, INPUT);
  pinMode(ir_r_, INPUT);

  pinMode(led_r_, OUTPUT);
  pinMode(led_g_, OUTPUT);
  pinMode(led_b_, OUTPUT);

  leftMotor = new Motor(LEFT_MOTOR_PIN, 1);
  rightMotor = new Motor(RIGHT_MOTOR_PIN, -1);

  Serial.begin(9600);
  
  // set timer 3 to interrupt every 58us
  TIMSK3 &= ~(1<<OCIE3A);        // Disable Timer3 interrupt
  TCCR3A = 0;                    // Set Timer3 prescaler to 8, CTC mode.
  TCCR3B = (1<<CS31 | 1<<WGM32); // Set Timer3 prescaler to 8, CTC mode.
  OCR3A = 57;                    // Set interrupt every 58us (115)
  TIMSK3 |= (1<<OCIE3A);         // Enable Timer3 interrupt.
}
ISR(TIMER3_COMPA_vect) {
  timer_callback();
}

/**
 * Sleep for msec milliseconds. The robot keeps updating it's status.
 * @param msec Time in milliseconds to sleep.
 */
void Dreamster::sleep(unsigned long msec)
{
  unsigned long start = millis();
  while ((millis() - start) < msec)
  {
    update();
  }
}

Dreamster::Motor::Motor(int pin, int dir)
{
  servo.attach(pin);
  servo.write(90);
  currentSpeed = 0;
  targetSpeed = 0;
  this->dir = dir;
}

void Dreamster::Motor::setSpeed(int speed)
{
  if (speed < (-clampSpeed)) speed = -clampSpeed;
  if (speed > clampSpeed) speed = clampSpeed;

  targetSpeed = speed;
}

int Dreamster::Motor::getSpeed()
{
  return currentSpeed;
}

void Dreamster::Motor::update()
{
  if (currentSpeed < targetSpeed)
    currentSpeed += step;
  else
    if (currentSpeed > targetSpeed)
      currentSpeed -= step;

  servo.write(90 + dir * currentSpeed);
}
