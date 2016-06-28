/*
 7-17-2011
 Spark Fun Electronics 2011
 Nathan Seidle
 
 This code is public domain but you buy me a beer if you use this and we meet someday (Beerware license).
 
 This is the firmware for BigTime, the wrist watch kit. It is based on an ATmega328 running with internal
 8MHz clock and external 32kHz crystal for keeping the time (aka RTC). The code and system have been tweaked
 to lower the power consumption of the ATmeg328 as much as possible. The watch currently uses about 
 1.2uA in idle (non-display) mode and about 13mA when displaying the time. With a 200mAh regular 
 CR2032 battery you should get 2-3 years of use!
 
 To compile and load this code onto your watch, select "Arduino Pro or Pro Mini 3.3V/8MHz w/ ATmega328" from
 the Boards menu. 
 
 If you're looking to save power in your own project, be sure to read section 9.10 of the ATmega328 
 datasheet to turn off all the bits of hardware you don't need.
 
 BigTime requires the Pro 8MHz bootloader with a few modifications:
 Internal 8MHz
 Clock div 8 cleared
 Brown out detect disabled
 BOOTRST set
 BOOSZ = 1024 
 This is to save power and open up the XTAL pins for use with a 38.786kHz external osc.
 
 So the fuse bits I get using AVR studio:
 HIGH 0xDA
 LOW 0xE2
 Extended 0xFF  
 
 3,600 seconds in an hour
 1 time check per hour, 2 seconds at 13mA
 3,598 seconds @ 1.2uA
 (3598 / 3600) * 0.0012mA + (2 / 3600) * 13mA = 0.0084mA used per hour
 
 200mAh / 0.0084mA = 23,809hr = 992 days = 2.7 years
 
 We can't use the standard Arduino delay() or delaymicrosecond() because we shut down timer0 to save power
 
 We turn off Brown out detect because it alone uses ~16uA.
 
 7-2-2011: Currently at 1.13uA
 
 7-4-2011: Let's wake up every 8 seconds instead of every 1 to save even more power
 Since we don't display seconds, this should be fine
 We are now at ~1.05uA on average
 
 7-17-2011: 1.09uA, portable with coincell power
 Jumps to 1.47uA every 8 seconds with system clock lowered to 1MHz
 Jumps to 1.37uA every 8 seconds with system clock at 8MHz
 Let's keep the internal clock at 8MHz since it doesn't seem to help to lower the internal clock.
 
 8-11-2011: Adding display color so that production can more easily know what code is on the 
 pre-programmed ATmega. 
 
 8-19-2011: Now we can print things like "red, gren, blue, yelo".
 
 7-12-2012: Added TV-B-Gone off codes. TV-B-Gone by Mitch Altman and Limor Fried. This project allows BigTime to
 turn off TVs by soldering an IR LED to Arduino pin 3 and GND. I updated the enclosure design files as well to
 allow the 5MM LED to fit inside the circumference of the enclosure.
 
 */

#include <avr/sleep.h> //Needed for sleep_mode
#include <avr/power.h> //Needed for powering down perihperals such as the ADC/TWI and Timers
#include <Time.h>
#include <SevSeg.h>
#include <Bounce2.h>


#define DISP_OFF       0
#define DISP_TIME      1
#define DISP_TIME_WAIT 2
#define DISP_DATE      3
#define DISP_DATE_WAIT 4
#define DISP_SECS      5
#define DISP_SECS_WAIT 6

#define SET_HOUR       7
#define SET_MINUTE     8
#define SET_MONTH      9
#define SET_DAY       10
#define SET_YEAR      11
#define SET_12HR      12


//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Pin definitions
#define BTN_DISP 2
#define BTN_SET  3
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=


//Set the 12hourMode to false for military/world time. Set it to true for American 12 hour time.
int TwelveHourMode = true;

//Set this variable to change how long the time is shown on the watch face. In milliseconds so 1677 = 1.677 seconds
int show_time_length = 2000;

byte state = DISP_OFF;

time_t tick;

SevSeg myDisplay;

// Instantiate Bounce objects
Bounce debDisp = Bounce(); 
Bounce debSet = Bounce(); 

//The very important 32.686kHz interrupt handler
SIGNAL(TIMER2_OVF_vect){
  tick += 8; //We sleep for 8 seconds instead of 1 to save more power
  //tick++; //Use this if we are waking up every second
  setTime(tick);
}

extern volatile unsigned long timer0_millis;

//The interrupt occurs when you push the button
SIGNAL(INT0_vect){
  //When you hit the button, we will need to display the time
  if(state == DISP_OFF) {
    state = DISP_TIME;

    // since tick hasn't happened yet, update clock
    setTime(tick + TCNT2 / 32); // 8 secs every 256 counter increments
    uint8_t oldSREG = SREG;
    cli();
    timer0_millis += 31.25 * (TCNT2 % 32); // add leftover 32ths of a second to millis for full synchronization
    SREG = oldSREG;
  }
}

void setup() {                
  //To reduce power, setup all pins as inputs with no pullups
  for(int x = 1 ; x < 18 ; x++){
    pinMode(x, INPUT);
    digitalWrite(x, LOW);
  }

  pinMode(BTN_DISP, INPUT); //This is the main button, tied to INT0
  digitalWrite(BTN_DISP, HIGH); //Enable internal pull up on button
  debDisp.attach(BTN_DISP);
  debDisp.interval(5); // interval in ms

  pinMode(BTN_SET, INPUT); //This is the setting button
  digitalWrite(BTN_SET, HIGH); //Enable internal pull up on button
  debSet.attach(BTN_SET);
  debSet.interval(5); // interval in ms

  int displayType = COMMON_CATHODE; //Your display is either common cathode or common anode

  int digit1 = 9; //Display pin 1
  int digit2 = 10; //Display pin 10
  int digit3 = A0; //Display pin 4
  int digit4 = A1; //Display pin 6

  int segA = 6; //Display pin 12
  int segB = 8; //Display pin 11
  int segC = 5; //Display pin 3
  int segD = 11; //Display pin 8
  int segE = 13; //Display pin 2
  int segF = 4; //Display pin 9
  int segG = 7; //Display pin 7
  int segDP = 12; //Display pin 5
   
  int numberOfDigits = 4; //Do you have a 1, 2 or 4 digit display?

  myDisplay.Begin(displayType, numberOfDigits, digit1, digit2, digit3, digit4, segA, segB, segC, segD, segE, segF, segG, segDP);
  
  myDisplay.SetBrightness(100); //Set the display to 100% brightness level

  //Power down various bits of hardware to lower power usage  
  set_sleep_mode(SLEEP_MODE_PWR_SAVE);
  sleep_enable();

  //Shut off ADC, TWI, SPI, Timer0, Timer1

  ADCSRA &= ~(1<<ADEN); //Disable ADC
  ACSR = (1<<ACD); //Disable the analog comparator
  DIDR0 = 0x3F; //Disable digital input buffers on all ADC0-ADC5 pins
  DIDR1 = (1<<AIN1D)|(1<<AIN0D); //Disable digital input buffer on AIN1/0

  power_twi_disable();
  power_spi_disable();
  //  power_usart0_disable(); //Needed for serial.print
  //power_timer0_disable(); //Needed for delay and millis()
  power_timer1_disable();
  //power_timer2_disable(); //Needed for asynchronous 32kHz operation

  //Setup TIMER2
  TCCR2A = 0x00;
  //TCCR2B = (1<<CS22)|(1<<CS20); //Set CLK/128 or overflow interrupt every 1s
  TCCR2B = (1<<CS22)|(1<<CS21)|(1<<CS20); //Set CLK/1024 or overflow interrupt every 8s
  ASSR = (1<<AS2); //Enable asynchronous operation
  TIMSK2 = (1<<TOIE2); //Enable the timer 2 interrupt

  //Setup external INT0 interrupt
  EICRA = (1<<ISC01); //Interrupt on falling edge
  EIMSK = (1<<INT0); //Enable INT0 interrupt

  //System clock futzing
  //CLKPR = (1<<CLKPCE); //Enable clock writing
  //CLKPR = (1<<CLKPS3); //Divid the system clock by 256

  Serial.begin(9600);  
  Serial.println("BigTime Testing:");

  setTime(8, 12, 55, 31, 12, 2015);
  tick = now();

  sei(); //Enable global interrupts
}

void loop() {
  if (state == DISP_OFF)
    sleep_mode(); //Stop everything and go to sleep. Wake up if the Timer2 buffer overflows or if you hit the button

  char tempString[10];
  time_t dtime = now();

  debDisp.update();
  debSet.update();

  switch(state)
  {
    case DISP_TIME:
    case DISP_TIME_WAIT:
      if (TwelveHourMode == true)
        sprintf(tempString, "%2d%02d", hourFormat12(dtime), minute(dtime));
      else
        sprintf(tempString, "%02d%02d", hour(dtime), minute(dtime));
      if (millis() % 1000 < 500)
        myDisplay.DisplayString(tempString, 0);
      else
        myDisplay.DisplayString(tempString, 2);
      break;
    case DISP_DATE:
    case DISP_DATE_WAIT:
      sprintf(tempString, "%2d%2d", month(dtime), day(dtime));
      myDisplay.DisplayString(tempString, 0);
      break;
    case DISP_SECS:
    case DISP_SECS_WAIT:
      sprintf(tempString, "  %02d", second(dtime));
      myDisplay.DisplayString(tempString, 2);
      break;

    case SET_HOUR:
      if (TwelveHourMode == true)
        sprintf(tempString, "%2d%02d", hourFormat12(dtime), minute(dtime));
      else
        sprintf(tempString, "%02d%02d", hour(dtime), minute(dtime));
      if (millis() % 1000 < 500)
        tempString[0] = tempString[1] = ' ';
      myDisplay.DisplayString(tempString, 2);
      break;
    case SET_MINUTE:
      if (TwelveHourMode == true)
        sprintf(tempString, "%2d%02d", hourFormat12(dtime), minute(dtime));
      else
        sprintf(tempString, "%02d%02d", hour(dtime), minute(dtime));
      if (millis() % 1000 < 500)
        tempString[2] = tempString[3] = ' ';
      myDisplay.DisplayString(tempString, 2);
      break;
    case SET_MONTH:
      sprintf(tempString, "%2d%2d", month(dtime), day(dtime));
      if (millis() % 1000 < 500)
        tempString[0] = tempString[1] = ' ';
      myDisplay.DisplayString(tempString, 0);
      break;
    case SET_DAY:
      sprintf(tempString, "%2d%2d", month(dtime), day(dtime));
      if (millis() % 1000 < 500)
        tempString[2] = tempString[3] = ' ';
      myDisplay.DisplayString(tempString, 0);
      break;
    case SET_YEAR:
      if (millis() % 1000 < 500)
        sprintf(tempString, "    ");
      else
        sprintf(tempString, "%04d  ", year(dtime));
      myDisplay.DisplayString(tempString, 0);
      break;
    case SET_12HR:
      if (millis() % 1000 < 500)
        sprintf(tempString, "    ");
      else if (TwelveHourMode == true)
        sprintf(tempString, "12hr");
      else
        sprintf(tempString, "24hr");
      myDisplay.DisplayString(tempString, 0);
      break;
  }

  // continue to display for a certain length of time
  static long startTime = millis();
  static long setBtnTime = millis();
  TimeElements tm;
  breakTime(dtime, tm);

  // button pressed
  if (debDisp.read() == LOW && millis() > startTime + 100) {
    switch (state)
    {
      case DISP_TIME_WAIT: state = DISP_DATE; break;
      case DISP_DATE_WAIT: state = DISP_SECS; break;
      case DISP_SECS_WAIT: state = DISP_TIME; break;
      case SET_HOUR:
        tm.Hour++;
        tm.Hour %= 24;
        dtime = tick = makeTime(tm);
        setTime(tick);
        break;
      case SET_MINUTE:
        tm.Minute++;
        tm.Minute %= 60;
        tm.Second = 0;
        dtime = tick = makeTime(tm);
        setTime(tick);
        break;
      case SET_MONTH:
        tm.Month++;
        if (tm.Month > 12) tm.Month = 1;
        if (tm.Day > monthLength(tm.Year, tm.Month)) tm.Day = monthLength(tm.Year, tm.Month);
        dtime = tick = makeTime(tm);
        setTime(tick);
        break;
      case SET_DAY:
        tm.Day++;
        if (tm.Day > monthLength(tm.Year, tm.Month)) tm.Day = 1;
        dtime = tick = makeTime(tm);
        setTime(tick);
        break;
      case SET_YEAR:
        if (millis() > startTime + 10000)
          tm.Year = 0;
        else
          tm.Year++;
        dtime = tick = makeTime(tm);
        setTime(tick);
        break;
      case SET_12HR:
        TwelveHourMode = !TwelveHourMode;
    }
    startTime = millis();
  }

  // button released
  if (debDisp.read() == HIGH && millis() > startTime + 100) {
    switch (state)
    {
      case DISP_TIME: state = DISP_TIME_WAIT; startTime = millis(); break;
      case DISP_DATE: state = DISP_DATE_WAIT; startTime = millis(); break;
      case DISP_SECS: state = DISP_SECS_WAIT; startTime = millis(); break;
    }
  }

  // set button pressed
  if (debSet.read() == LOW) {
    if (millis() > setBtnTime + 1000) { // repeat rate
      switch (state)
      {
        case DISP_TIME_WAIT: state = SET_HOUR;   break;
        case SET_HOUR:       state = SET_MINUTE; break;
        case SET_MINUTE:     state = SET_MONTH;  break;
        case SET_MONTH:      state = SET_DAY;    break;
        case SET_DAY:        state = SET_YEAR;   break;
        case SET_YEAR:       state = SET_12HR;   break;
        case SET_12HR:       state = SET_HOUR;   break;
      }
      startTime = setBtnTime = millis();
    }
  }

  // set button released
  if (debSet.read() == HIGH) {
    switch (state)
    {
      case SET_HOUR:
      case SET_MINUTE:
      case SET_MONTH:
      case SET_DAY:
      case SET_YEAR:
      case SET_12HR:
        setBtnTime = 0;
        break;
    }
  }

  if ((millis() - startTime) > show_time_length)
    state = DISP_OFF;
}


int monthLength(int tmYear, int month)
{
  switch (month)
  {
    case 1: case 3: case 5: case 7: case 8: case 10: case 12:
      return 31;
    case 4: case 6: case 9: case 11:
      return 30;
    case 2:
      TimeElements tm;
      tm.Year = tmYear;
      tm.Month = 3;
      tm.Day = 1;
      tm.Hour = tm.Minute = tm.Second = 0;
      breakTime(makeTime(tm) - SECS_PER_DAY, tm);
      return tm.Day;
  }
}

