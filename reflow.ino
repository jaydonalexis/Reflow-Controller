#include "TimerOne.h"
#include <LiquidCrystal.h>

#define VREF 5030.0 //mV

#define TEMP A0
#define TC A1
#define BUTTON_PIN A2 //All digital pins occupied

#define DB7 13
#define DB6 12
#define DB5 11
#define DB4 10
#define DB3 9
#define DB2 8
#define DB1 7
#define DB0 6
#define DE  5
#define DRW 4
#define DRS 2

#define PWMPIN 3

#define SAMPLE_COUNT 10.0
//Gain as calculated from given feedback resistor - Can only measure up to ~400
#define GAIN 304.0

//Reflow States
#define IDLING      0
#define RAMP2SOAK   1
#define SOAK        2
#define RAMP2REFLOW 3
#define REFLOW      4
#define COOLING     5

unsigned int soak_temp = 0;
unsigned int soak_time = 0;
unsigned int reflow_temp = 0;
unsigned int reflow_time = 0;
unsigned int cool_temp = 0;

unsigned char reflow_state = IDLING;

unsigned long temp;
unsigned long tc;

unsigned long SB[(int)SAMPLE_COUNT];

//Time variables
//First value passed to serial by the arduino is a garbage value which must be ignored
bool time_b = false;
unsigned int timer = 0;
unsigned long t1 = 0;
unsigned long t2 = 0;
unsigned int ms = 0;
unsigned int sec = 0;

//PWM variables
unsigned char duty_cycle = 0;
unsigned char pwm_count = 0;

//String parsing variables
char comma_position;

//Serial Inputs
String s_command = "";

//String buffers for formatting print text. Different strings will be printed to the lcd and serial simultaneously
char serial_buff[128];
char lcd_buff[128];

LiquidCrystal lcd(DRS, DRW, DE, DB0, DB1, DB2, DB3, DB4, DB5, DB6, DB7);

void setup() {
  // Initialize serial port
  Serial.begin(9600);
  
  initialize_timer();

  pinMode(TEMP, INPUT);
  pinMode(TC, INPUT);
  pinMode(PWMPIN, OUTPUT);
  pinMode(BUTTON_PIN, INPUT_PULLUP);
  //0.01s = 10ms = 10000us
  Timer1.initialize(10000);
  Timer1.attachInterrupt(PWM_interrupt);

  lcd.begin(16, 2);
  lcd.print("Initializing");

  delay(1000);

  lcd.clear();
  lcd.begin(16, 2);
  lcd.print("IDLING");
  
}

void loop() {
  update_timer();
  
  sample_value(TEMP, &temp);
  sample_value(TC, &tc);

  state_machine();

  s_command = check_serial();
  if (s_command.length() > 0) {
    execute_command(s_command);
  }
}

//Print functions
void print_info() {
  // '~' is needed to throw away garbage values in the python script
  sprintf(serial_buff, "~%lu %lu %u %u %u\n", tc, temp, sec, timer, reflow_state);
  sprintf(lcd_buff, "%lu %lu %u %u", tc, temp, sec, timer);
  
  //Note that line 1 is actually the second row
  LCD_clear_line(1);
  lcd.setCursor(0, 1);

  lcd.print(lcd_buff);
  Serial.print(serial_buff);
}

void print_parameters() {
  sprintf(lcd_buff, "%u %u %u %u %u", soak_temp, soak_time, reflow_temp, reflow_time, cool_temp);
  //'~' is needed to throw away garbage values in the python script
  sprintf(serial_buff, "~%lu %lu %u %u %u\n", tc, temp, sec, timer, reflow_state);

  LCD_clear_line(1);
  lcd.setCursor(0, 1);

  lcd.print(lcd_buff);
  Serial.print(serial_buff);
}

//Sample Value function. Uses 10 samples with a delay of 1ms for a total of 10ms
void sample_value(int pin, unsigned long* var) {
  unsigned long sum = 0;

  //Collect sample of values
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    SB[i] = analogRead(pin);
    delay(1);
  }

  //Average sample of values
  for (int i = 0; i < SAMPLE_COUNT; i++) {
    sum += SB[i];
  }

  switch(pin) {
    case TEMP: *var = room_conversion(raw_conversion(sum / SAMPLE_COUNT));
               break;
    case TC:   *var = thermocouple_conversion(raw_conversion(sum / SAMPLE_COUNT));
               break;
    default:   *var = -1;
               break;
  }
}

//Conversion functions
unsigned long raw_conversion(unsigned long raw) {
  return (raw / 1023.0 * VREF);
}

// 10 mV/K
unsigned long room_conversion(unsigned long mV) {
  return ((mV - 2730.0) / 10.0);
}

unsigned long thermocouple_conversion(unsigned long mV) {     
  return (((mV * 1000.0) / (41.0 * GAIN)) + temp);
}                
  
//State Machines
void state_machine() {
  switch(reflow_state) {
    //Prints data from sensors
    case IDLING: break;
    
    //Ramps to a defined temperature before transitioning to SOAK
    case RAMP2SOAK:
      if (tc >= soak_temp) {
        duty_cycle = 20;
        reflow_state = SOAK;
        start_timer();
        lcd.clear();
        lcd.print("SOAK");
      }
      break;

    //Holds at the SOAK_TEMP for the required SOAK_TIME
    case SOAK:
      if (timer == soak_time) {
        duty_cycle = 80;
        reflow_state = RAMP2REFLOW;
        reset_timer();
        lcd.clear();
        lcd.print("RAMP TO REFLOW");
      }
      break;

    //Ramps to a define temperature before transitioning to REFLOW
    case RAMP2REFLOW:
      if (tc >= reflow_temp) {
        duty_cycle = 20;
        reflow_state = REFLOW;
        start_timer();
        lcd.clear();
        lcd.print("REFLOW");
      }
      break;

    //Holds at the REFLOW_TEMP for the required REFLOW_TIME
    case REFLOW:
      if (timer == reflow_time) {
        duty_cycle = 0;
        reflow_state = COOLING;
        reset_timer();
        lcd.clear();
        lcd.print("COOLING");
      }
      break;

    //Holds cooling state until under the COOL_TEMP
    case COOLING:
      if (tc <= cool_temp) {
        duty_cycle = 0;
        reflow_state = IDLING;
        lcd.clear();
        lcd.print("IDLING");
      }
      break;
    default: break;
  }

  //Hold the button to change the display state of the lcd to parameter values
  if(digitalRead(BUTTON_PIN) == HIGH) {
    print_info();
  } 
  else {
    print_parameters();
  }
  
  return;
}

void start_reflow() {
  reflow_state = RAMP2SOAK;
  duty_cycle = 80;
  sec = 0;
  timer = 0;
  lcd.clear();
  lcd.print("RAMP TO SOAK");
}

void stop_reflow() {
  reflow_state = IDLING;
  time_b = false;
  duty_cycle = 0;
  lcd.clear();
  lcd.print("IDLING");
}

// Timing
void initialize_timer() {
  timer = 0;
  t1 = millis();
  t2 = millis();
}

void update_timer() {
  unsigned long delta_t;
  
  t2 = millis();
  delta_t = t2 - t1;
  
  ms += delta_t;
  if (ms >= 1000) {
    ms = ms % 1000;
    
    if (reflow_state != IDLING)
      sec++;
    
    if (time_b && reflow_state != IDLING) {
      timer++;
    }
  }
  
  t1 = t2;
}

void reset_timer() {
  timer = 0;
  time_b = false;
}

void start_timer() {
  reset_timer();
  time_b = true;
}

void stop_timer() {
  time_b = false;
}

void PWM_interrupt() {
  if (duty_cycle == 0) {
    pwm_count = 0;
    digitalWrite(PWMPIN, LOW);
  } 
  else if (pwm_count == duty_cycle) {
    digitalWrite(PWMPIN, LOW);
  } 
  else if (pwm_count >= 100) {
    pwm_count = 0;
    digitalWrite(PWMPIN, HIGH);
  }

   pwm_count++;
}

String check_serial() {
  String data = "";
  if (Serial.available() > 0) {
    data = Serial.readString();
  } 
  else {
    data = "";
  }
  
  return data;
}

void execute_command(String command) {
  //Passing a string of reflow parameters delimited by commas which should have a length greater than two (including string termination char)
  if (command.length() > 2) {
    int param_number = 0;
    //String should be five integer values delimited by commas to be separated and stored in the appropriate variables
    do {                                          
      comma_position = command.indexOf(',');        
      if (comma_position != -1) {
        switch(param_number) {                 
          case 0: soak_temp = command.substring(0, comma_position).toInt();
                  command = command.substring(comma_position+1, command.length());
                  break;
          case 1: soak_time = command.substring(0, comma_position).toInt();
                  command = command.substring(comma_position+1, command.length());
                  break;
          case 2: reflow_temp = command.substring(0, comma_position).toInt();
                  command = command.substring(comma_position+1, command.length());
                  break;
          case 3: reflow_time = command.substring(0, comma_position).toInt();
                  command = command.substring(comma_position+1, command.length());
                  break;                                
        }
      } 
      else {
        //Accounts for the last parameter in the string which will not have a comma after it
        if(command.length() > 2)
          cool_temp = command.toInt();
      }
      ++param_number;
    } while(comma_position >= 0);

    start_reflow();
    //Pass anything of length 2 or less to terminate the reflow process (including string termination char)
  } 
  else {
    stop_reflow();
  }
}

//Note that line 1 is actually line 2
void LCD_clear_line(int line) {
  lcd.setCursor(0, line);
  for (int n = 0; n < 16; n++) {
    lcd.print(" ");
  }
}