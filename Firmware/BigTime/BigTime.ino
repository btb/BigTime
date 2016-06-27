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


#define DISP_OFF       0
#define DISP_TIME      1
#define DISP_TIME_WAIT 2
#define DISP_DATE      3
#define DISP_DATE_WAIT 4
#define DISP_SECS      5
#define DISP_SECS_WAIT 6


//Set the 12hourMode to false for military/world time. Set it to true for American 12 hour time.
int TwelveHourMode = true;

//Set this variable to change how long the time is shown on the watch face. In milliseconds so 1677 = 1.677 seconds
int show_time_length = 2000;

byte state = DISP_OFF;

time_t tick;
time_t button_down_time;

SevSeg myDisplay;

//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=
//Pin definitions
int theButton = 2;
//-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=-=

//The very important 32.686kHz interrupt handler
SIGNAL(TIMER2_OVF_vect){
  tick += 8; //We sleep for 8 seconds instead of 1 to save more power
  //tick++; //Use this if we are waking up every second
  setTime(tick);
}

//The interrupt occurs when you push the button
SIGNAL(INT0_vect){
  //When you hit the button, we will need to display the time
  if(state == DISP_OFF) 
    state = DISP_TIME;
}

void setup() {                
  //To reduce power, setup all pins as inputs with no pullups
  for(int x = 1 ; x < 18 ; x++){
    pinMode(x, INPUT);
    digitalWrite(x, LOW);
  }

  pinMode(theButton, INPUT); //This is the main button, tied to INT0
  digitalWrite(theButton, HIGH); //Enable internal pull up on button

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
  TimeElements tm;
  breakTime(now(), tm);

  switch(state)
  {
    case DISP_TIME:
    case DISP_TIME_WAIT:
      if(TwelveHourMode == true) {
        while(tm.Hour > 12) tm.Hour -= 12;
        sprintf(tempString, "%2d%02d", tm.Hour, tm.Minute);
      }
      else {
        sprintf(tempString, "%02d%02d", tm.Hour, tm.Minute);
      }
      myDisplay.DisplayString(tempString, 2);
      break;
    case DISP_DATE:
    case DISP_DATE_WAIT:
      sprintf(tempString, "%2d%2d", tm.Month, tm.Day);
      myDisplay.DisplayString(tempString, 0);
      break;
    case DISP_SECS:
    case DISP_SECS_WAIT:
      sprintf(tempString, "  %02d", tm.Second);
      myDisplay.DisplayString(tempString, 2);
      break;
  }

  // continue to display for a certain length of time
  static boolean waiting = false;
  static long startTime = millis();

  // button pressed
  if (digitalRead(theButton) == LOW && millis() > startTime + 100) {
    switch (state)
    {
      case DISP_TIME_WAIT: state = DISP_DATE; break;
      case DISP_DATE_WAIT: state = DISP_SECS; break;
      case DISP_SECS_WAIT: state = DISP_TIME; break;
    }
    startTime = millis();
  }

  // button released
  if (digitalRead(theButton) == HIGH && millis() > startTime + 100) {
    switch (state)
    {
      case DISP_TIME: state = DISP_TIME_WAIT; startTime = millis(); break;
      case DISP_DATE: state = DISP_DATE_WAIT; startTime = millis(); break;
      case DISP_SECS: state = DISP_SECS_WAIT; startTime = millis(); break;
    }
  }

  if ((millis() - startTime) > show_time_length)
    state = DISP_OFF;
}


//This routine occurs when you hold the button down
//The colon blinks indicating we are in this mode
//Holding the button down will increase the time (accelerates)
//Releasing the button for more than 2 seconds will exit this mode
void setTime(void) {
  TimeElements tm;
  breakTime(now(), tm);

  char tempString[10];

  int idleMiliseconds = 0;
  //This is the timeout counter. Once we get to ~2 seconds of inactivity, the watch
  //will exit the setTime function and return to normal operation

  int buttonHold = 0; 
  //This counts the number of times you are holding the button down consecutively
  //Once we notice you're really holding the button down a lot, we will speed up the minute counter

  while(idleMiliseconds < 2000) {

    cli(); //We don't want the interrupt changing values at the same time we are!

    //Update the minutes and hours variables
    tm.Minute += tm.Second / 60; //Example: seconds = 2317, minutes = 58 + 38 = 96
    tm.Second %= 60; //seconds = 37
    tm.Hour += tm.Minute / 60; //12 + (96 / 60) = 13
    tm.Minute %= 60; //minutes = 36

    //Do we display 12 hour or 24 hour time?
    if(TwelveHourMode == true) {
      //In 12 hour mode, hours go from 12 to 1 to 12.
      while(tm.Hour > 12) tm.Hour -= 12;
    }
    else {
      //In 24 hour mode, hours go from 0 to 23 to 0.
      while(tm.Hour > 23) tm.Hour -= 24;
    }

    sei(); //Resume interrupts

    sprintf(tempString, "%2d%2d", tm.Hour, tm.Minute); //Combine the hours and minutes

    myDisplay.DisplayString(tempString, 15);

    //If you're still hitting the button, then increase the time and reset the idleMili timeout variable
    if(digitalRead(theButton) == LOW) {
      idleMiliseconds = 0;

      buttonHold++;
      if(buttonHold < 10) //10 = 2 seconds
        tm.Minute++; //Advance the minutes
      else {
        //Advance the minutes faster because you're holding the button for 10 seconds
        //Start advancing on the tens digit. Floor the single minute digit.
        tm.Minute /= 10; //minutes = 46 / 10 = 4
        tm.Minute *= 10; //minutes = 4 * 10 = 40
        tm.Minute += 10;  //minutes = 40 + 10 = 50
      }
    }
    else
      buttonHold = 0;

    idleMiliseconds += 200;
  }
  tick = makeTime(tm);
  setTime(tick);
}

//This is a not-so-accurate delay routine
//Calling fake_msdelay(100) will delay for about 100ms
//Assumes 8MHz clock
/*void fake_msdelay(int x){
 for( ; x > 0 ; x--)
 fake_usdelay(1000);
 }*/

//This is a not-so-accurate delay routine
//Calling fake_usdelay(100) will delay for about 100us
//Assumes 8MHz clock
/*void fake_usdelay(int x){
 for( ; x > 0 ; x--) {
 __asm__("nop\n\t"); 
 __asm__("nop\n\t"); 
 __asm__("nop\n\t"); 
 __asm__("nop\n\t"); 
 __asm__("nop\n\t"); 
 __asm__("nop\n\t"); 
 __asm__("nop\n\t"); 
 }
 }*/

