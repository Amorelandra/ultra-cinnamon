/* ~~~~~~~ ~~~~ ~~ ~
*  ultra-cinnamon
*   ~~~~ ~~ ~
*  
*  Emily Rose <emily@contactvibe.com>
* 
*  This work is licensed under a Creative Commons Attribution Share-Alike 3.0 License.
*  http://creativecommons.org/licenses/by-sa/3.0/
*/

#include <LiquidCrystal.h>

char imageName[] = "ultra-cinnamon";
char imageVersion[] = "v0.1(a)-stardust";  // I enjoy a fancy version name
LiquidCrystal lcd(12, 11, 10, 5, 4, 3, 2); // LCD pin configuration straight from hacktronics

char* lcdScreen[4] = {"", "", "", ""};     // LCD screen strings
int backLight = 13;                        // backlight pin (usually always on, whatever)
unsigned long lastWrite = 0; // timestamp of last screen write


/**
* ~ Warning window; timeout period before reporting a body
*/
int warningWindow = 15; // allow this many seconds to pass before tripping
int warningCount = 0; // track number of seconds for warning


/**
 * ~ Reporting ~ modified from arduino Smoothing example
 */
const int numReadings = 25;     // how many readings to average for our results
int tolerance = 1;              // the variance allowed before alerting (in inches)
int readings[numReadings];      // array of readings from the ultrasonic module
int index = 0;                  // the index of the current reading
int total = 0;                  // the running total of readings
int average = 0;                // the average of these readings
boolean tripped = false;        // I've fallen and I can't get up! (alarm state)


/**
 * Device state system thingy
 * this device has the following states: 
 * ~ 0 device is starting up
 * ~ 1 idling
 * ~ 2 waiting on reservation
 * ~ 3 body detected, host says auth_ok
 * ~ 4 body detected, host says no_auth
 * ~ 5 body detected, waiting to complain (warningWindow)
 * These are triggered by the host system.
 */
const byte UNINITIALIZED = -1;
const byte STARTUP = 0;
const byte IDLE = 1; 
const byte SUMMON = 2;
const byte AUTHED = 3;
const byte BADTOUCH = 4; 
const byte WARNING = 5;
byte currentState = UNINITIALIZED; // default status
byte lastState = STARTUP;
byte newState = lastState;


/**
* ~ Indicators (LEDs)
*/
int wrongLight = 6;              // when things are bad! ;<
int rightLight = 9;              // when things are great! :)
int indicatorStep = 5;           // current indicator fade step
int indicatorValue = 0;          // LED PWM value; 0 - 255; used internally
int pulseRate = 30;              // indicator pulse rate (milliseconds)

boolean indicatorStatus = true;  // true = rightLight on, false = wrongLight on
boolean indicatorInvert = false; // true = alternate indicators

unsigned long lastPulse = 0;     // timestamp of last LED PWM manipulation


/**
 * ~ Ultrasound TODO: see if it will work with only 3 pins
 * (switching between digi-r/w mode with echo/trigger on the same pin)
 */
int sampleRate = 30;          // in milliseconds
int baselineDistance = 0;     // the baseline reading we got when we initialized
int triggerPin = 7;           // ultrasound trigger pin
int echoPin = 8;              // ultrasound echo pin
unsigned long duration = 0;   // most recent pulse duration from echoPin pin
unsigned long distance = 0;   // most recent calculated distance based on speed of sound (sea level, dry air @ 20°C)
unsigned long lastEcho = 0;   // timestamp of the last echo received


/**
 * ~ Host communication
 */
int incomingByte = 0;
String inputString = ""; // String object is worth the memory here.
boolean inputComplete = false;


/**
* Startup tasks 
* simply list the name of a function you wish to 
* execute as the device is booting up.
*/
typedef void (* stepList) (); // define stepList
stepList tests[] = { 

  baselineSample, indicatorTestUp, indicatorTestBlink, indicatorTestDown

};


/**
 * System timing
 * stamps record millis() values to time the entire device loop
 */
unsigned long lastStamp = 0;
unsigned long currentStamp = 0;


/**
* setup runs once every time the device powers up
*/
void setup()
{
  /**
   * LCD
   */
  pinMode(backLight, OUTPUT);
  digitalWrite(backLight, HIGH);
  lcd.begin(20, 4); // columns, rows.  use 16,2 for a 16x2 LCD, etc.
  lcd.clear(); // start with a blank screen

  /**
   * Indicators
   */
  pinMode(wrongLight, OUTPUT);
  pinMode(rightLight, OUTPUT);

  /**
   * Ultrasound
   */
  pinMode(triggerPin, OUTPUT);
  pinMode(echoPin, INPUT);
  digitalWrite(triggerPin, LOW);

  startup();
}

/**
* Fanciful startup routine that
* initializes the device while 
* reporting to the screen
*/
void startup(){
  
  Serial.begin(9600); // this is here so we don't start accepting commands before we're ready

  int steps = sizeof(tests) / sizeof(stepList);
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.write("Initializing");
  lcd.setCursor(1 , 2);
  lcd.write("img:");
  lcd.write(imageName);
  lcd.setCursor(1, 3);
  lcd.write("[");
  lcd.write(imageVersion);
  lcd.write("]");
  
  for(int each = 0; each < steps; each++){

    tests[each](); // execute each of the steps in our startup tests list
    loadDot(each + 12, 0); // more than 8 steps will wrap the dots, oh well.
  }
  newState = IDLE;
}


void loop(){

  newStamp(); // cycle timestamps
  
  commandLoop(); // check & process host commands
  
  pushState(); // flip device states
  
  ultraSample(); // send ping, get response, calculate distance
  
  tripLoop(); // check to see if we have a body

  lcdLoop(); // write to the LCD screen if appropriate

  indicatorLoop(); // manipulate the LEDs (emotionally)

}

/**
 * Called in between each loop() iteration
 * Reads & stores data for handling. if no
 * data is available, it does nothing. NOTHING!
 */
void serialEvent(){

  while(Serial.available()){

    char in = (char) Serial.read();
    if(in == '\n'){

      inputComplete = true;
    }
    else{

      inputString += in;
    }
  }
}

/**
* commandLoop called every loop iteration
*/
void commandLoop(){
  
  if(inputComplete == true){
    
    handleInput(inputString);
    inputComplete = false;
  }
}

/**
 * Handle received serial data from host system
 * TODO: make this handle states defined in a
 * more dynamic fashion.
 */
void handleInput(String& input){

  if(input == "state::summon"){

    newState = SUMMON;
  }
  else if(input == "state::idle"){

    newState = IDLE;
  }
  else if(input == "state::badtouch"){

    newState = BADTOUCH;
  }
  else if(input == "state::warning"){

    newState = WARNING;
  }
  else if(input == "alert::disarm"){
    
    tripped = false;
    if(currentState == BADTOUCH){
      
      newState = AUTHED;
    }
  }
  Serial.println(input);
  
  input = "";
}

void pushState(){
  
  /**
  * Push the system into a new state when the LEDs are faded out
  */
  if((newState)&&(newState != currentState)&&(indicatorValue == 0)){
    
    systemStatus(newState);
  }
}

void systemStatus(byte stat){

  indicatorValue = 0;
  
  if(stat == IDLE){

    currentState = IDLE;
    Serial.println("Switched to state::IDLE.");
  }
  else if(stat == SUMMON){

    currentState = SUMMON;
    Serial.println("Switched to state::SUMMON.");
  }
  else if(stat == BADTOUCH){
    
    currentState = BADTOUCH;
    Serial.println("Switched to state::BADTOUCH.");
  }
  else if(stat == WARNING){
    
    currentState = WARNING;
    Serial.println("Switched to state::WARNING.");
  }
  else if(stat == AUTHED){
    
    currentSTate = AUTHED;
    Serial.println("Switched to state::AUTHED.");
  }
}

/**
 * LCD management
 */
void lcdLoop(){

  if(currentStamp - lastWrite < pulseRate) { return; }
  
  lastWrite = currentStamp;
  
  if((currentState == IDLE)&&(lastState != IDLE)){ // idle state - no body detected, no session

    indicatorInvert = false;
    indicatorStatus = true;
    indicatorStep = 5;
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("  SCAN YOUR TICKET  ");
    lastState = IDLE;
  }
  else if((currentState == WARNING) && (lastState != WARNING)){ // body detected, no session, warning period
    
    indicatorInvert = true;
    indicatorStep = 25;

    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("SCAN YOUR TICKET NOW");
    lastState = WARNING;
  }
  else if((currentState == BADTOUCH)&&(lastState != BADTOUCH)){ // body detected, no session, done fucking around

    indicatorInvert = false;
    indicatorStatus = false;
    indicatorStep = 25;
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print(" TICKET NOT SCANNED ");
    lcd.setCursor(0, 3);
    lcd.print("  STAFF ALERT SENT  ");
    lastState = BADTOUCH;
  }  
  else if((currentState == AUTHED)&&(lastState != AUTHED)){ // body detected, session detected, it's all good
    
    indicatorInvert = false;
    indicatorStatus = true;
    indicatorStep = 10;
    lcd.clear();
    lcd.setCursor(0, 1);
    lcd.print("        ENJOY       ");
  }
}

/**
 * Indicator management
 */
void indicatorLoop(){

    if(currentStamp - lastPulse < pulseRate){ return; }
    
    lastPulse = currentStamp;
    
    if((indicatorInvert == true) && (indicatorValue == 0)){
      
      indicatorStatus = !indicatorStatus; // flip our indicators
    }

    if(indicatorStatus == true){

      pulseLED(rightLight);
    }
    else{
  
      pulseLED(wrongLight);
    }
}

/**
 * Each iteration will bring
 * the specified LED up/down
 * by the configured amount
 */
void pulseLED(int pin){

 
  indicatorValue += indicatorStep;

  if(indicatorValue >= 255){ 

    flipStep(); 
    indicatorValue = 255; 
  }
  else if(indicatorValue <=0){ 

    flipStep(); 
    indicatorValue = 0;
  }

  analogWrite(pin, indicatorValue);
}

/**
 * Reverse our indicatorStep so that we may fade in/out
 */
void flipStep(){
  
  indicatorStep = indicatorStep - (indicatorStep * 2); 
}

/**
 * Modified from the smoothing example; perfect & easy
 * Just loads up <numReadings> values into the array and averages them!
 * Returns the new average distance.
 */
int ultraSample(){
  
  if(currentStamp - lastEcho < sampleRate){ return 0; }
  
  lastEcho = currentStamp;
  
  total = total - readings[index];
  readings[index] = calculateDistance(ping());
  total = total + readings[index];
  index = index + 1;

  if(index >= numReadings){

    index = 0;
  }

  average = total / numReadings;
  return average;
}

/**
* Compare the average distance
* recorded with the baseline sample
* and decide if we need to tell the
* host that we have a body
*/
void tripLoop(){
  
  if((average > baselineDistance + tolerance) || (average < baselineDistance - tolerance)){
    
    tripped = true;
    Serial.println("alert::tripped");
  }
}

/**
 * Activates the actual ping sound on the 
 * triggerPin pin of our ultrasonic device
 * Modified from the PING))) code.
 */
void sendtriggerPin(){

  digitalWrite(triggerPin, LOW);
  delayMicroseconds(2);
  digitalWrite(triggerPin, HIGH);
  delayMicroseconds(5);
  digitalWrite(triggerPin, LOW);
}

/**
 * call sendtriggerPin() and return the raw duration
 * of the pulse received as a result.
 */
unsigned long ping(){

  sendtriggerPin();
  duration = pulseIn(echoPin, HIGH);
  return duration;
}

/**
 * Calculate the distance to nearest object
 * based on the speed of sound (round-trip).
 * calculation: duration / microseconds per inch / 2.
 */
unsigned long calculateDistance(unsigned long d){
  if(d != -1){ // apparently we can get no pulse which will return -1

      return d / 74 / 2;
  } 
  else{ // so we will just return it without calculating it, that'd be silly

    return -1;
  }
}
void newStamp(){ 
  
  lastStamp = currentStamp;
  currentStamp = millis(); 
}

/**
* ~~~~~~~ ~~  ~~~  ~~ ~~~~  ~~~~~~~
* ~~~~    STARTUP FUNCTIONS    ~~~~
*/

void indicatorTestDown(){


  for(int x = 255; x>=0; x-=10){ // fade indicators down

    analogWrite(rightLight, x);
    analogWrite(wrongLight, x);
    delay(10);
  } 

  analogWrite(rightLight, 0);
  analogWrite(wrongLight, 0);
}

void indicatorTestUp(){ // fade indicators up

  for(int x = 0; x<255; x+=10){

    analogWrite(rightLight, x);
    analogWrite(wrongLight, x);
    delay(10);
  }  
}

void indicatorTestBlink(){ // blink indicators (visual check for voltage issues)

  for(int x = 0; x<5; x++){

    analogWrite(rightLight, 0);
    analogWrite(wrongLight, 0);
    delay(30);
    analogWrite(rightLight, 255);
    analogWrite(wrongLight, 255);
    delay(30);
  }  
}

void baselineSample(){ // Fill up our samples array with a good baseline distance.

  for(int x = 0; x < numReadings; x++){ 
    
    ultraSample(); 
    delay(sampleRate);
    
    baselineDistance = average;
  }
}

void loadDot(int x, int y){ // put another dot on the loading screen~!

  lcd.setCursor(x, y);
  lcd.write(".");
  delay(1000);
}



