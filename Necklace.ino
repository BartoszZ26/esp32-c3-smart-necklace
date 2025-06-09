#include <Wire.h>
#include <Adafruit_SSD1306.h>
#include <BleKeyboard.h>
#include <BleMouse.h>
#include <BLEDevice.h>
#include <ESP32Time.h>
#include <map>

#define WAKEUP_PIN 4

// MPU6500 addresses
#define MPU6500_ADDR  0x68
#define PWR_MGMT_1    0x6B
#define GYRO_XOUT_H   0x43
#define GYRO_YOUT_H   0x45
#define GYRO_ZOUT_H   0x47

// SSD1306 I2C address
#define OLED_ADDR 0x3C

// I2C pins
#define I2C_SDA 8  // GPIO6 is SDA
#define I2C_SCL 9  // GPIO7 is SCL

BleMouse bleMouse;
BleKeyboard bleKeyboard;

ESP32Time rtc(0);

// OLED display settings and init function
  #define SCREEN_WIDTH 128
  #define SCREEN_HEIGHT 64
  #define OLED_RESET -1  // Reset pin not used
  Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

  bool initOLED() {
    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDR)) {
      return false;
    }
    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.display();
    return true;
  }


enum WatchDisplayModeEnum{
  HOUR_MINUTES_SECONDS,
  HOUR_MINUTES,
  HOUR_MINUTES_SECONDS_WEATHER,
  HOUR_MINUTES_WEATHER,
};

enum TimeSettingEnum{
  YEAR,
  MONTH,
  DAY,
  HOUR,
  MINUTE,
  SECOND
};

std::map<TimeSettingEnum, std::string> timeSettingMap = {
    {YEAR, "Year"},
    {MONTH, "Month"},
    {DAY, "Day"},
    {HOUR, "Hour"},
    {MINUTE, "Minute"},
    {SECOND, "Second"}
};

enum MENU_ENUM {
  MAIN_MENU,
  WATCH,
  STOPWATCH,
  TIMER,
  MEDIA_CONTROLLER,
  AIR_MOUSE,
  NOTES,
  SETTINGS,
  CONNECTIVITY_SETTINGS,
  WEATHER,
  PEDOMETER,
  PARTICLE_SIMULATOR,
  GAMES_FOLDER,
  SNAKE,
  PONG,
  DINO,
  BIRD,
  MAZE,
  AIR_MOUSE_SETTINGS
};

class TactSwitch {
  private:
    unsigned int pin;
    unsigned int timeOfLastPress;
    unsigned int timeOfPressStart;
    unsigned int firstPressDelay;    // Initial delay before first press is registered
    unsigned int heldPressDelay;     // Delay between next presses while held
    unsigned int numberOfPressesSinceStart; // resets to 0 once button released, used for first press recognition
    bool isDown;
    bool isPressed;
    bool isFirstPress;

  public:
    TactSwitch(unsigned int pinNumber, unsigned int firstPressDelay, unsigned int heldPressDelay) {
      pin = pinNumber;
      this->firstPressDelay = firstPressDelay;
      this->heldPressDelay = heldPressDelay;
      timeOfLastPress = 0;
      timeOfPressStart = 0;
      isDown = false;
      isPressed = false;
      numberOfPressesSinceStart = 0;
      pinMode(pin, INPUT_PULLUP);
    }

    void updateVars(unsigned int currentTime) {
      bool temporaryIsDown = !digitalRead(pin); // temporary variable for first press recognition
      
      if(temporaryIsDown){
        if(!isButtonDown()){ // Button was just pressed
          timeOfPressStart = currentTime;
        }
        isDown = true;
      }
      else{
        isDown = false;
      }

      if(!isDown){ // button is released
        numberOfPressesSinceStart = 0;
        isPressed = false;
        isFirstPress = false;
      }

      if(isDown){
        if(numberOfPressesSinceStart == 0){ // first press
          if( currentTime - timeOfPressStart > firstPressDelay){
            isFirstPress = true;
            isPressed = true;
            numberOfPressesSinceStart++;
            timeOfLastPress = currentTime;
          }
        }
        else if (numberOfPressesSinceStart > 0){
          if(currentTime - timeOfLastPress > heldPressDelay){
            isFirstPress = false;
            isPressed = true;
            numberOfPressesSinceStart++;
            timeOfLastPress = currentTime;
          }
          else{
            isPressed = false;
            isFirstPress = 0;
          }
        }
        else{
          isPressed = false;
        }
      }
    }

    bool isButtonDown() {
      return isDown;
    }

    bool isButtonPressed() {
      return isPressed;
    }

    bool isButtonFirstPressed(){
      return isFirstPress;
    }

    unsigned int getTimeSinceLastPress(unsigned int currentTime) {
      return currentTime - timeOfLastPress;
    }

    unsigned int getTimeHeld(unsigned int currentTime) {
      return isDown ? currentTime - timeOfPressStart : 0;
    }

    void setHeldPressDelay(unsigned int delay) {
      heldPressDelay = delay;
    }

    void setFirstPressDelay(unsigned int delay) {
      firstPressDelay = delay;
    }
};

class AirMouse {
  private:
      float gyroX_offset, gyroY_offset, gyroZ_offset, mouseXScale, mouseYScale;
      unsigned long lastUpdate;
      bool bluetoothEnabled, isSleepModeOn, isXInverted, isYInverted;
  public:
      AirMouse(float xMouseScale, float yMouseScale) {
          gyroX_offset = 0;
          gyroY_offset = 0;
          gyroZ_offset = 0;
          lastUpdate = 0;
          mouseXScale = xMouseScale;
          mouseYScale = yMouseScale;
      }

      void calibrate() {
          const int numSamples = 100;
          long gyroX_sum = 0, gyroY_sum = 0, gyroZ_sum = 0;
          int sampleCount = 0;
          unsigned long startTime = millis();


          while (sampleCount < numSamples) {
              if (millis() - startTime >= 10) {
                  startTime = millis();
                  int16_t gyroX, gyroY, gyroZ;

                  Wire.beginTransmission(MPU6500_ADDR);
                  Wire.write(GYRO_XOUT_H);
                  Wire.endTransmission(false);
                  Wire.requestFrom(MPU6500_ADDR, 6, true);
                  gyroX = (Wire.read() << 8) | Wire.read();
                  gyroY = (Wire.read() << 8) | Wire.read();
                  gyroZ = (Wire.read() << 8) | Wire.read();

                  gyroX_sum += gyroX;
                  gyroY_sum += gyroY;
                  gyroZ_sum += gyroZ;

                  sampleCount++;
              }
          }

          gyroX_offset = gyroX_sum / numSamples;
          gyroY_offset = gyroY_sum / numSamples;
          gyroZ_offset = gyroZ_sum / numSamples;
      }

      void begin() {
          Serial.begin(115200);
          Wire.begin();

          if (!bleMouse.isConnected()) {
            bleMouse.begin();
          }

          // Wake up MPU6500
          Wire.beginTransmission(MPU6500_ADDR);
          Wire.write(PWR_MGMT_1);
          Wire.write(0x00);
          Wire.endTransmission();

          Serial.println("Calibrating... Keep MPU6500 still.");
          calibrate();
          Serial.println("Calibration complete!");
      }

      void update() {
          if ( bleMouse.isConnected() && millis() - lastUpdate >= 10) {
              lastUpdate = millis();
              int16_t gyroX, gyroY, gyroZ;

              // Read gyroscope data
              Wire.beginTransmission(MPU6500_ADDR);
              Wire.write(GYRO_XOUT_H);
              Wire.endTransmission(false);
              Wire.requestFrom(MPU6500_ADDR, 6, true);
              gyroX = (Wire.read() << 8) | Wire.read();
              gyroY = (Wire.read() << 8) | Wire.read();
              gyroZ = (Wire.read() << 8) | Wire.read();

              // Apply calibration offsets
              float moveX = (gyroX - gyroX_offset) / 131.0;
              float moveY = (gyroY - gyroY_offset) / 131.0;
              float moveZ = (gyroZ - gyroZ_offset) / 131.0;

              // Scale movement for mouse control
              int mouseX = -1 * mouseXScale * moveZ;
              int mouseY = -1 * mouseYScale * moveX;

              if(isXInverted) mouseX*=-1;
              if(isYInverted) mouseY*=-1;

              // Move mouse if above threshold
              if (abs(mouseX) > 1 || abs(mouseY) > 1) {
                  bleMouse.move(mouseX, mouseY);
              }
          }
      }

      void draw(){
        display.setTextSize(1);
        display.setCursor(0, 0);
        display.println("Air Mouse");
        display.println(bleMouse.isConnected() ? "Connected" : "Disconnected");
        display.display();
      }

      void sleepMPU6500() {
          Wire.beginTransmission(MPU6500_ADDR);
          Wire.write(PWR_MGMT_1);
          Wire.write(0x40); // 0x40 puts the MPU6500 into sleep mode
          Wire.endTransmission();
          Serial.println("MPU6500 in sleep mode");
      }

      void wakeMPU6500() {
          Wire.beginTransmission(MPU6500_ADDR);
          Wire.write(PWR_MGMT_1);
          Wire.write(0x00); // 0x00 wakes the MPU6500 up
          Wire.endTransmission();
          Serial.println("MPU6500 awake");
      }

      friend class AirMouseSettings;

};

enum AIR_MOUSE_SETTINGS_ENUM{
  X,
  Y,
  INVERT_X,
  INVERT_Y,
  RESOLUTION_X,
  RESOLUTION_Y
};

class AirMouseSettings{
  private:
  AIR_MOUSE_SETTINGS_ENUM SELECTED_SETTING;
  unsigned int xResolutions[16] = {360, 480, 640, 720, 768, 900, 1080, 1366, 1440, 1600, 1920, 2048, 2160, 2560, 3840, 4096 };
  unsigned int yResolutions[16] = {360, 480, 640, 720, 768, 900, 1080, 1366, 1440, 1600, 1920, 2048, 2160, 2560, 3840, 4096 };
  unsigned int xResIndex = 10, yResIndex = 6;

  public:

  void draw(AirMouse& mouse){

    display.clearDisplay();
    display.setTextSize(1);
    display.setCursor(1,1);
    display.print("X sensitivity: " + String(mouse.mouseXScale));
    display.setCursor(1,12);
    display.print("Y sensitivity: " + String(mouse.mouseYScale));
    display.setCursor(1,23);
    display.print( mouse.isXInverted ? "invert X: yes" : "invert X: no" );
    display.setCursor(1,34);
    display.print( mouse.isYInverted ? "invert Y:  yes" : "invert Y: no" );
    display.setCursor(1,45);
    display.print("X resolution: " + String(xResolutions[xResIndex]) );
    display.setCursor(1,56);
    display.print("Y resolution: " + String(yResolutions[yResIndex]) );

    switch(SELECTED_SETTING){
        case X:
      display.drawRect(0,0,127,12, SSD1306_WHITE);
      break;
        case Y:
      display.drawRect(0,11,127,12, SSD1306_WHITE);
      break;
        case INVERT_X:
      display.drawRect(0,22,127,12, SSD1306_WHITE);
      break;
        case INVERT_Y:
      display.drawRect(0,33,127,12, SSD1306_WHITE);
      break;
        case RESOLUTION_X:
      display.drawRect(0,44,127,12, SSD1306_WHITE);
      break;
        case RESOLUTION_Y:
      display.drawRect(0,55,127,12, SSD1306_WHITE);
      break;
    }
  }
  void up(){
    if(SELECTED_SETTING == X) SELECTED_SETTING = RESOLUTION_Y;
    else SELECTED_SETTING = static_cast<AIR_MOUSE_SETTINGS_ENUM>(static_cast<int>(SELECTED_SETTING) - 1);
  }
  void down(){
    if(SELECTED_SETTING == RESOLUTION_Y) SELECTED_SETTING = X;
    else SELECTED_SETTING = static_cast<AIR_MOUSE_SETTINGS_ENUM>(static_cast<int>(SELECTED_SETTING) + 1);
  }
  void increase(AirMouse& mouse){
    if(SELECTED_SETTING == X) mouse.mouseXScale +=0.1;
    else if (SELECTED_SETTING == Y) mouse.mouseYScale +=0.1;
    else if (SELECTED_SETTING == INVERT_X) toggleXInvert(mouse);
    else if (SELECTED_SETTING == INVERT_Y) toggleYInvert(mouse);
    else if (SELECTED_SETTING == RESOLUTION_X) increaseResolutionX();
    else if (SELECTED_SETTING == RESOLUTION_Y) increaseResolutionY();
  }
  void decrease(AirMouse& mouse){
    if(SELECTED_SETTING == X) mouse.mouseXScale -=0.1;
    else if (SELECTED_SETTING == Y) mouse.mouseYScale -=0.1;
    else if (SELECTED_SETTING == INVERT_X) toggleXInvert(mouse);
    else if (SELECTED_SETTING == INVERT_Y) toggleYInvert(mouse);
    else if (SELECTED_SETTING == RESOLUTION_X) decreaseResolutionX();
    else if (SELECTED_SETTING == RESOLUTION_Y) decreaseResolutionY();
  }
  void toggleXInvert(AirMouse& mouse){
    mouse.isXInverted = !mouse.isXInverted;
  }
  void toggleYInvert(AirMouse& mouse){
    mouse.isYInverted = !mouse.isYInverted;
  }
  void increaseResolutionX(){
    if(xResIndex >= 15){
      xResIndex = 0;
    }
    else{
      xResIndex++;
    }
  }
  void decreaseResolutionX(){
    if(xResIndex > 0){
      xResIndex--;
    }
    else{
      xResIndex = 15;
    }
  }
  void decreaseResolutionY(){
    if(yResIndex > 0){
      yResIndex--;
    }
    else{
      yResIndex = 15;
    }
  }
  void increaseResolutionY(){
    if(yResIndex >= 15){
      yResIndex = 0;
    }
    else{
      yResIndex++;
    }
  }
  unsigned int getCurrentResolutionX(){
    return xResolutions[xResIndex];
  }
  unsigned int getCurrentResolutionY(){
    return yResolutions[yResIndex];
  }
};

//TO IMPLEMENT: RESOLUTION SETTINGS FOR AIR MOUSE SO THAT DURING CALIBRATION IT GOES TO THE MIDDLE

class MediaController{
  private:

  public: 
  void draw(){
    display.setCursor(0,0);
    display.println("Media controller");
    if(bleKeyboard.isConnected()){
      display.println("Connected");
    }
    else{
      display.println("Disconnected");
    }
  }
  void volumeUp(){
    bleKeyboard.write(KEY_MEDIA_VOLUME_UP);
  }
  void volumeDown(){
    bleKeyboard.write(KEY_MEDIA_VOLUME_DOWN);
  }
  void nextTrack(){
    bleKeyboard.write(KEY_MEDIA_NEXT_TRACK);
  }
  void previousTrack(){
    bleKeyboard.write(KEY_MEDIA_PREVIOUS_TRACK);
  }
  void playPause(){
    bleKeyboard.write(KEY_MEDIA_PLAY_PAUSE);
  }
  void mute(){
    bleKeyboard.write(KEY_MEDIA_MUTE);
  }
};

class Watch {
  private:
    bool isTimeSettingModeOn, is12HourTimeModeOn;
    TimeSettingEnum TimeSettingMode;

  public:
    Watch(){
      isTimeSettingModeOn = false;
      is12HourTimeModeOn = false;
      TimeSettingMode = HOUR;
    }
    void draw(){
      display.setCursor(0,0);
      display.setTextSize(2);
      if(is12HourTimeModeOn) {
        String temp = rtc.getTime("%r");
        temp.remove(8);
        temp+=rtc.getAmPm();
        display.println(temp);
      }
      else display.println(rtc.getTime("%T"));
      display.println(rtc.getTime("%F"));
      if(isTimeSettingModeOn){
        display.setCursor(0, 64-8);
        display.setTextSize(1);     
        display.print(timeSettingMap[TimeSettingMode].c_str());
      }
    }
    void setIsTimeSettingModeOn(bool foo){
      isTimeSettingModeOn = foo;
    }
    void toggleIsTimeSettingModeOn(){
      isTimeSettingModeOn = !isTimeSettingModeOn;
    }
    void toggleIs12HourTimeModeOn(){
      is12HourTimeModeOn = !is12HourTimeModeOn;
    }
    void moveTimeSettingModeRight(){
      if(TimeSettingMode == SECOND) TimeSettingMode = YEAR;
      else TimeSettingMode = static_cast<TimeSettingEnum>(static_cast<int>(TimeSettingMode) + 1);
    }
    void moveTimeSettingModeLeft(){
      if(TimeSettingMode == YEAR) TimeSettingMode = SECOND;
      else TimeSettingMode = static_cast<TimeSettingEnum>(static_cast<int>(TimeSettingMode) - 1);
    }
    void addTime(unsigned int val) {
        switch (TimeSettingMode) {
          case YEAR: rtc.setTime(rtc.getSecond(), rtc.getMinute(), rtc.getHour(true), rtc.getDay(), rtc.getMonth()+1, rtc.getYear() + val); break;
          case MONTH: rtc.setTime(rtc.getSecond(), rtc.getMinute(), rtc.getHour(true), rtc.getDay(), rtc.getMonth()+1 + val, rtc.getYear()); break;
          case DAY: rtc.setTime(rtc.getSecond(), rtc.getMinute(), rtc.getHour(true), rtc.getDay() + val, rtc.getMonth()+1, rtc.getYear()); break;
          case HOUR: rtc.setTime(rtc.getSecond(), rtc.getMinute(), rtc.getHour(true) + val, rtc.getDay(), rtc.getMonth()+1, rtc.getYear()); break;
          case MINUTE: rtc.setTime(rtc.getSecond(), rtc.getMinute() + val, rtc.getHour(true), rtc.getDay(), rtc.getMonth()+1, rtc.getYear()); break;
          case SECOND: rtc.setTime(rtc.getSecond() + val, rtc.getMinute(), rtc.getHour(true), rtc.getDay(), rtc.getMonth()+1, rtc.getYear()); break;
        }
    }

    void subtractTime(unsigned int val) {
        switch (TimeSettingMode) {
            case YEAR: rtc.setTime(rtc.getSecond(), rtc.getMinute(), rtc.getHour(true), rtc.getDay(), rtc.getMonth()+1, rtc.getYear() - val); break;
            case MONTH: rtc.setTime(rtc.getSecond(), rtc.getMinute(), rtc.getHour(true), rtc.getDay(), rtc.getMonth()+1 - val, rtc.getYear()); break;
            case DAY: rtc.setTime(rtc.getSecond(), rtc.getMinute(), rtc.getHour(true), rtc.getDay() - val, rtc.getMonth()+1, rtc.getYear()); break;
            case HOUR: rtc.setTime(rtc.getSecond(), rtc.getMinute(), rtc.getHour(true) - val, rtc.getDay(), rtc.getMonth()+1, rtc.getYear()); break;
            case MINUTE: rtc.setTime(rtc.getSecond(), rtc.getMinute() - val, rtc.getHour(true), rtc.getDay(), rtc.getMonth()+1, rtc.getYear()); break;
            case SECOND: rtc.setTime(rtc.getSecond() - val, rtc.getMinute(), rtc.getHour(true), rtc.getDay(), rtc.getMonth()+1, rtc.getYear()); break;
        }
    }
    bool getIsTimeSettingModeOn(){
      return isTimeSettingModeOn;
    }
};

class Stopwatch{
  private:
    unsigned int hours, minutes, seconds, milliseconds;
    String currentTimeString;
    bool isStopwatchOn;

  public:
    Stopwatch(){
      hours = 0;
      minutes = 0;
      seconds = 0;
      milliseconds = 0;
      currentTimeString = "00:00:00:000";
      isStopwatchOn = false;
    }
    void update(unsigned int timePassed){

      if( timePassed>0 && isStopwatchOn ){
        
        milliseconds += timePassed;
        if (milliseconds >= 1000) {
          milliseconds = 0;
          seconds++;
        }
        if (seconds >= 60) {
          seconds = 0;
          minutes++;
        }
        if (minutes >= 60) {
          minutes = 0;
          hours++;
        }
      }
    }
    void updateCurrentTimeString() {
      currentTimeString = "";

      if (hours < 10) currentTimeString += '0';
      currentTimeString += String(hours) + ':';

      // Ensure two digits for minutes
      if (minutes < 10) currentTimeString += '0';
      currentTimeString += String(minutes) + ':';

      // Ensure two digits for seconds
      if (seconds < 10) currentTimeString += '0';
      currentTimeString += String(seconds) + ':';

      // Ensure three digits for milliseconds
      if (milliseconds < 100) currentTimeString += '0';
      if (milliseconds < 10) currentTimeString += '0';
      currentTimeString += String(milliseconds);
    }
    void draw(){
      
      display.setCursor(0, 0);
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      
      String hourMinutesSecondsString = currentTimeString.substring(0, 8);
      String milisecondsString = currentTimeString.substring(9,12);
      
      display.println("Stopwatch");
      display.println(hourMinutesSecondsString);
      display.println(milisecondsString);
    }
    void toggleStopwatch(){
      isStopwatchOn = !isStopwatchOn;
    }
    void setHour(int h){
      hours = h;
    }
    void setMinutes(int m){
      minutes = m;
    }
    void setSeconds(int s){
      seconds = s;
    }
    void setMilliseconds(int m){
      milliseconds = m;
    }
    void resetStopwatch(){
      setHour(0);
      setMinutes(0);
      setSeconds(0);
      setMilliseconds(0);
    }
    String getCurrentTime(){
      return currentTimeString;
    }
    bool getIsStopwatchOn(){
      return isStopwatchOn;
    }
    
};

class Timer{
  private:
    unsigned long hours, minutes, seconds, milliseconds;
    String currentTimeString;
    bool isTimerOn, isTimerSettingModeOn;

  public:
    TimeSettingEnum TIMER_SETTING_ENUM;
    Timer(){
      hours = 0;
      minutes = 0;
      seconds = 0;
      milliseconds = 0;
      currentTimeString = "00:00:00:000";
      isTimerOn = false;
      isTimerSettingModeOn = false;
      TIMER_SETTING_ENUM = HOUR;
    }
    void update(unsigned int timePassed) {
        if ( isTimerOn ) {
            unsigned long totalMilliseconds = hours * 3600000UL + minutes * 60000UL + seconds * 1000UL + milliseconds;
            
            if (timePassed >= totalMilliseconds) {
                totalMilliseconds = 0;
                isTimerOn = false;
            } 
            else 
            {
                totalMilliseconds -= timePassed;
            }

            hours = totalMilliseconds / 3600000;
            totalMilliseconds %= 3600000;

            minutes = totalMilliseconds / 60000;
            totalMilliseconds %= 60000;

            seconds = totalMilliseconds / 1000;
            milliseconds = totalMilliseconds % 1000;
        }
    }

    void updateCurrentTimeString() {
      currentTimeString = "";

      if (hours < 10) currentTimeString += '0';
      currentTimeString += String(hours) + ':';

      // Ensure two digits for minutes
      if (minutes < 10) currentTimeString += '0';
      currentTimeString += String(minutes) + ':';

      // Ensure two digits for seconds
      if (seconds < 10) currentTimeString += '0';
      currentTimeString += String(seconds) + ':';

      // Ensure three digits for milliseconds
      if (milliseconds < 100) currentTimeString += '0';
      if (milliseconds < 10) currentTimeString += '0';
      currentTimeString += String(milliseconds);
    }
    void draw(){
      
      display.setCursor(0, 0);
      display.setTextSize(2);
      display.setTextColor(SSD1306_WHITE);
      
      String hourMinutesSecondsString = currentTimeString.substring(0, 8);
      String milisecondsString = currentTimeString.substring(9,12);
      
      display.println("Timer");
      display.println(hourMinutesSecondsString);
      display.println(milisecondsString);

      if(isTimerSettingModeOn){
        display.setCursor(0, 64-8);
        display.setTextSize(1);
          
        if(TIMER_SETTING_ENUM == HOUR){
          display.print("hours");
        }
        else if(TIMER_SETTING_ENUM == MINUTE){
          display.print("minutes");
        }
        else if(TIMER_SETTING_ENUM == SECOND){
          display.print("seconds");
        }
      }
    }
    void toggleTimer(){
      isTimerOn = !isTimerOn;
    }
    void toggleTimerSettingMode(){
      isTimerSettingModeOn = !isTimerSettingModeOn;
      toggleTimer();
    }
    void setHour(int h){
      hours = h;
    }
    void setMinutes(int m){
      minutes = m;
    }
    void setSeconds(int s){
      seconds = s;
    }
    void setMilliseconds(int m){
      milliseconds = m;
    }
    void changeHour(int howMuch){
      howMuch%=100;
      hours = (hours + howMuch + 100) % 100;
    }
    void changeMinutes(int howMuch){
      howMuch%=60;
      minutes = (minutes + howMuch + 60) % 60;
    }
    void changeSeconds(int howMuch){
      howMuch%=60;
      seconds = (seconds + howMuch + 60) % 60;
    }
    void resetTimer(){
      setHour(0);
      setMinutes(0);
      setSeconds(0);
      setMilliseconds(0);
    }
    String getCurrentTime(){
      return currentTimeString;
    }
    bool getIsTimerOn(){
      return isTimerOn;
    }
    bool getIsTimerSettingModeOn(){
      return isTimerSettingModeOn;
    }
    
    
};

class MainMenu{
  private:
  MENU_ENUM TILE_LAYOUT[4][3]; //X, Y
  unsigned int x, y, maxX, maxY, iconSize;

  public:
  MainMenu(){
    x=0;
    y=0;
    maxX = 3;
    maxY = 2;
    iconSize = 16;
    TILE_LAYOUT[0][0] = WATCH;
    TILE_LAYOUT[0][1] = STOPWATCH;
    TILE_LAYOUT[0][2] = TIMER;
    TILE_LAYOUT[1][0] = MEDIA_CONTROLLER;
    TILE_LAYOUT[1][1] = NOTES;
    TILE_LAYOUT[1][2] = WEATHER;
    TILE_LAYOUT[2][0] = AIR_MOUSE_SETTINGS;
    TILE_LAYOUT[2][1] = PEDOMETER;
    TILE_LAYOUT[2][2] = PARTICLE_SIMULATOR;
    TILE_LAYOUT[3][0] = GAMES_FOLDER;
    TILE_LAYOUT[3][1] = SETTINGS;
    TILE_LAYOUT[3][2] = CONNECTIVITY_SETTINGS;
  }
  void draw(){
    display.setTextSize(1);

    for(int i = 0; i <= maxX; i++){
      for(int j = 0; j <= maxY; j++){
        display.setCursor(i * iconSize, j * iconSize);
        display.print(TILE_LAYOUT[i][j]);
      }
    }

    display.drawRect(x * iconSize, y * iconSize, iconSize, iconSize, WHITE);
  }
  void moveDown(){
    if(y<maxY){
      y++;
    }
    else{
      y = 0;
    }
  }
  void moveUp(){
    if(y>0){
      y--;
    }
    else{
      y = maxY;
    }
  }
  void moveLeft(){
    if(x>0){
      x--;
    }
    else{
      x = maxX;
    }
  }
  void moveRight(){
    if(x<maxX){
      x++;
    }
    else{
      x = 0;
    }
  }
  MENU_ENUM getSelectedMenu(){
    return TILE_LAYOUT[x][y];
  }
};

enum GAMES_ENUM{
  SNAKE_GAME,
  DINO_GAME,
  BIRD_GAME,
  MAZE_GAME,
  PONG_GAME,
};

class GamesFolder{
  private:
    GAMES_ENUM SELECTED_GAME;
  public:
    GamesFolder(GAMES_ENUM DEFAULT_GAME){
      SELECTED_GAME = DEFAULT_GAME;
    }
    void draw(){

      display.clearDisplay();
      display.setTextSize(1);
      display.setCursor(1,1);
      display.print("SNAKE");
      display.setCursor(1,12);
      display.print("DINO");
      display.setCursor(1,23);
      display.print("BIRD");
      display.setCursor(1,34);
      display.print("MAZE");
      display.setCursor(1,45);
      display.print("PONG");

      switch(SELECTED_GAME){
          case SNAKE_GAME:
        display.drawRect(0,0,127,12, SSD1306_WHITE);
        break;
          case DINO_GAME:
        display.drawRect(0,11,127,12, SSD1306_WHITE);
        break;
          case BIRD_GAME:
        display.drawRect(0,22,127,12, SSD1306_WHITE);
        break;
          case MAZE_GAME:
        display.drawRect(0,33,127,12, SSD1306_WHITE);
        break;
          case PONG_GAME:
        display.drawRect(0,44,127,12, SSD1306_WHITE);
        break;
      }
    }
    void Down(){
      if(SELECTED_GAME == PONG_GAME){
        SELECTED_GAME = SNAKE_GAME;
      }
      else SELECTED_GAME = static_cast<GAMES_ENUM>(static_cast<int>(SELECTED_GAME) + 1);
    }
    void Up(){
      if(SELECTED_GAME == SNAKE_GAME){
        SELECTED_GAME = PONG_GAME;
      }
      else SELECTED_GAME = static_cast<GAMES_ENUM>(static_cast<int>(SELECTED_GAME) - 1);
    }
    void Middle(MENU_ENUM &prev, MENU_ENUM &curr){
      prev = GAMES_FOLDER;
      switch(SELECTED_GAME){
          case SNAKE_GAME:
        curr = SNAKE;
        break;
          case DINO_GAME:
        curr = DINO;
        break;
          case BIRD_GAME:
        curr = BIRD;
        break;
          case MAZE_GAME:
        curr = MAZE;
        break;
          case PONG_GAME:
        curr = PONG;
        break;
      }
    }
};

struct snakePart{
  int x, y;
};

enum SNAKE_SETTINGS_ENUM{
  MAP_WIDTH,
  MAP_HEIGHT,
  SNAKE_SPEED
};

class Snake{ // ADD ADJUSTABLE MAP SIZE AND SNAKE SPEED, AND ALSO PROGRESSIVE SPEED INCREASE MODE
  private:
    unsigned int highScore, tickDelay, previousTickTime, mapWidth, mapHeight, cellXSize, cellYSize;
    unsigned int cellHeightSettings[4] = {4, 8, 16, 32};
    unsigned int cellWidthSettings[4] = {8, 16, 32, 64};
    unsigned int snakeSpeedSettings[20] = {1000, 950, 900, 850, 800, 750, 700, 650, 600, 550, 500, 450, 400, 350, 300, 250, 200, 150, 100, 50};
    unsigned int mapWidthSettingsIndex, mapHeightSettingsIndex, snakeSpeedSettingsIndex;
    int foodX, foodY, xDir, yDir; //in cells, not pixels
    bool isStopped;
    std::vector<snakePart> snake;
    snakePart snakeHead;
    SNAKE_SETTINGS_ENUM SELECTED_SETTING;
  public:
    Snake(unsigned int hiScore = 0, unsigned int width = 1, unsigned int height = 1, unsigned int speed = 10){
      highScore = hiScore;
      previousTickTime = 0;
      snakeHead.x = -1;
      snakeHead.y = -1;
      isStopped = true;
      foodX = -1;
      foodY = -1;
      xDir = 1;
      yDir = 0;
      snake.push_back(snakeHead);
      mapWidthSettingsIndex = width;
      mapHeightSettingsIndex = height;
      snakeSpeedSettingsIndex = speed;
      this->tickDelay = snakeSpeedSettings[snakeSpeedSettingsIndex];
      mapWidth = cellWidthSettings[mapWidthSettingsIndex];
      mapHeight = cellHeightSettings[mapHeightSettingsIndex];
      cellXSize = 128/mapWidth;
      cellYSize = 64/mapHeight;
      SELECTED_SETTING = MAP_WIDTH;
    }
    void update(unsigned int currentTime){
      if(!isStopped && currentTime - previousTickTime >= snakeSpeedSettings[snakeSpeedSettingsIndex] ){
        previousTickTime = currentTime;

        if(snakeHead.x <= 0 && xDir < 0) {
          snakeHead.x = mapWidth;
        }
        if(snakeHead.y <= 0  && yDir < 0) {
          snakeHead.y = mapHeight;
        }

        snakeHead.x =  snakeHead.x + xDir;
        snakeHead.y = snakeHead.y + yDir;
        

        if(snakeHead.x >= mapWidth) snakeHead.x = 0;
        if(snakeHead.y >= mapHeight) snakeHead.y = 0;
        

        snake.emplace_back(snakeHead);
        if (foodX == snakeHead.x && foodY == snakeHead.y){ // remove last element of snake vector if not eating food
          randomizeFood();
          if(highScore < snake.size()) highScore = snake.size() - 1;
        }
        else{ //don't remove last element of snake vector and randomize food position if food is eaten
          snake.erase(snake.begin());
          Serial.println("Snake did not eat food!");
        }
        
        for (unsigned int i = 0; i < snake.size()-1; i++) {
          if (snake[i].x == snakeHead.x && snake[i].y == snakeHead.y) {
            stop();
            return;
          }
        }
        

      }
    }
    void draw(Adafruit_SSD1306 &display) {
        display.clearDisplay();
        
        if(!isStopped){
          // Draw snake
          for (int i = 0; i < snake.size(); i++) {
              snakePart part = snake[i];
              display.fillRect(part.x * cellXSize, part.y * cellYSize, cellXSize, cellYSize, SSD1306_WHITE);
          }
          // Draw food
          display.drawRect(foodX * cellXSize, foodY * cellYSize, cellXSize, cellYSize, SSD1306_WHITE);
        }
        else{
          display.setCursor(0, 0);
          display.println("High score: " + String(highScore));
          display.setCursor(1,12);
          display.print("Map width: " + String(cellWidthSettings[mapWidthSettingsIndex]) );
          display.setCursor(1,23);
          display.print("Map height: " + String(cellHeightSettings[mapHeightSettingsIndex]) );
          display.setCursor(1,34);
          display.print("Snake speed: " + String(snakeSpeedSettingsIndex+1) + "/20" );

          switch(SELECTED_SETTING){
              case MAP_WIDTH:
            display.drawRect(0,11,127,12, SSD1306_WHITE);
            break;
              case MAP_HEIGHT:
            display.drawRect(0,22,127,12, SSD1306_WHITE);
            break;
              case SNAKE_SPEED:
            display.drawRect(0,33,127,12, SSD1306_WHITE);
            break;
          }
        }
    }

    void start(){
      isStopped = false;
      xDir = 1;
      yDir = 0; 
      snake.clear();
      mapWidth = cellWidthSettings[mapWidthSettingsIndex];
      mapHeight = cellHeightSettings[mapHeightSettingsIndex];
      cellXSize = 128/mapWidth;
      cellYSize = 64/mapHeight;
      SELECTED_SETTING = MAP_WIDTH;
      randomizeFood();
      snakeHead.x = mapWidth/2;
      snakeHead.y = mapHeight/2;
      snake.emplace_back(snakeHead);
      previousTickTime = 0;
    }
    void up() { //add previous ydir later and analogous to other directions, this one isnt perfect
      if(isStopped){
        if(SELECTED_SETTING == MAP_WIDTH){
        SELECTED_SETTING = SNAKE_SPEED;
        }
        else{
          SELECTED_SETTING = static_cast<SNAKE_SETTINGS_ENUM>(static_cast<int>(SELECTED_SETTING) - 1);
        }
      }
      else{
        if (yDir != 1) {  
            yDir = -1;
            xDir = 0; 
        }
      }
  }

  void down() {
    if(isStopped){
      if(SELECTED_SETTING == SNAKE_SPEED){
        SELECTED_SETTING = MAP_WIDTH;
      }
      else{
        SELECTED_SETTING = static_cast<SNAKE_SETTINGS_ENUM>(static_cast<int>(SELECTED_SETTING) + 1);
      }
    }
    else{
      if (yDir != -1) {  
          yDir = 1;
          xDir = 0; 
      }
    }
  }

  void left() {
    if(isStopped){
      switch(SELECTED_SETTING){
              case MAP_WIDTH:
            decreaseMapWidth();
            break;
              case MAP_HEIGHT:
            decreaseMapHeight();
            break;
              case SNAKE_SPEED:
            decreaseSnakeSpeed();
            break;
          }
    }
    else{
      if (xDir != 1) {  
          xDir = -1;
          yDir = 0;  
      }
    }
  }

  void right() {
    if(isStopped){
      switch(SELECTED_SETTING){
              case MAP_WIDTH:
            increaseMapWidth();
            break;
              case MAP_HEIGHT:
            increaseMapHeight();
            break;
              case SNAKE_SPEED:
            increaseSnakeSpeed();
            break;
          }
    }
    else{
      if (xDir != -1) { 
          xDir = 1;
          yDir = 0;  
      }
    }
  }

  void middle(){
      if(isStopped) start();
      else stop();
    }
  void stop(){
      isStopped = true;
      snake.clear();
    }
  void randomizeFood() {
    bool foodPlaced = false;
    while (!foodPlaced) {
        foodX = random(0, mapWidth);
        foodY = random(0, mapHeight);
        foodPlaced = true;

        // Check if food is placed on the snake head
        if (foodX == snakeHead.x && foodY == snakeHead.y) {
            foodPlaced = false; // Retry if food is on the head
            continue;
        }

        // Check if food overlaps with any snake segment
        for (const auto& part : snake) {
            if (part.x == foodX && part.y == foodY) {
                foodPlaced = false; // Retry if food is inside the snake
                break;
            }
        }
    }
}

    void increaseMapWidth(){
      if(mapWidthSettingsIndex < 3){
        mapWidthSettingsIndex++;
      }
    }
    void decreaseMapWidth(){
      if(mapWidthSettingsIndex > 0){
        mapWidthSettingsIndex--;
      }
    }
    void increaseMapHeight(){
      if(mapHeightSettingsIndex < 3){
        mapHeightSettingsIndex++;
      }
    }
    void decreaseMapHeight(){
      if(mapHeightSettingsIndex > 0){
        mapHeightSettingsIndex--;
      }
    }
    void increaseSnakeSpeed(){
      if(snakeSpeedSettingsIndex < 19){
        snakeSpeedSettingsIndex++;
      }
    }
    void decreaseSnakeSpeed(){
      if(snakeSpeedSettingsIndex > 0){
        snakeSpeedSettingsIndex--;
      }
    }
};

TactSwitch LeftButton(1, 20, 333);
TactSwitch RightButton(2, 20, 333);
TactSwitch UpButton(3, 20, 333);
TactSwitch DownButton(10, 20, 333);
TactSwitch MiddleButton(0, 20, 333);
TactSwitch MenuButton(6, 20, 333);
TactSwitch FunctionButton(7, 20, 333);

Watch WatchObject;
Stopwatch StopwatchObject;
Timer TimerObject;
MainMenu MainMenuObject;
AirMouseSettings AirMouseSettingsObject;
AirMouse AirMouseObject(1.2, 0.8);
GamesFolder GamesFolderObject(SNAKE_GAME);
Snake snakeGameObject;
MediaController MediaControllerObject;

void moveMouseToTopLeft(){

  if( bleMouse.isConnected() ){

      for(int i = 0; i < 100; i++){
        bleMouse.move(-127, -127);
      }
  }
}

MENU_ENUM MENU, PREVIOUS_MENU;

unsigned int currentTime = 0, previousTickTime = 0, deltaTime = 0, currentTick = 0;
bool isDisplayInverted = false;

const unsigned int ledPin = 5;
bool ledState = 0;

void toggleLed(){
  ledState = !ledState;
  digitalWrite(ledPin, ledState);
}

void setup() {
  setCpuFrequencyMhz(80);

  Serial.begin(9600);

  //setup deep sleep
  esp_deep_sleep_enable_gpio_wakeup(1ULL << WAKEUP_PIN, ESP_GPIO_WAKEUP_GPIO_LOW);
  pinMode(WAKEUP_PIN, INPUT_PULLUP);
  
  if (esp_sleep_get_wakeup_cause() == ESP_SLEEP_WAKEUP_GPIO) {
        Serial.println("Woke up from deep sleep due to GPIO wakeup.");
    } else {
        Serial.println("Fresh start, no deep sleep.");
        rtc.setTime(0, 0, 12, 1, 1, 2025); // Set RTC only on fresh start
  }

  Serial.println("Current Time: " + rtc.getDateTime(true));

  // Initialize LED flashlight;
  pinMode(5, OUTPUT);
  digitalWrite(ledPin, ledState);

  // Initialize 128x64 OLED
  if (!initOLED()) {
    Serial.println("Failed to initialize OLED!");
    return;
  }

  // Put MPU6500 to sleep mode
  AirMouseObject.sleepMPU6500();

  display.invertDisplay(isDisplayInverted);

  MENU = WATCH;
  PREVIOUS_MENU = WATCH; // change this when going to tile menu screens
}

void loop() {
  display.clearDisplay();

  if(!digitalRead(WAKEUP_PIN) && millis()>1000) {
    display.display();
    delay(1000);
    esp_deep_sleep_start();
  }

  previousTickTime = currentTime;
  currentTime = millis();
  deltaTime = currentTime - previousTickTime;
  currentTick++;

  LeftButton.updateVars(currentTime);
  RightButton.updateVars(currentTime);
  MiddleButton.updateVars(currentTime);
  UpButton.updateVars(currentTime);
  DownButton.updateVars(currentTime);
  MenuButton.updateVars(currentTime);
  FunctionButton.updateVars(currentTime);

  //WatchObject.update();
  StopwatchObject.update(deltaTime);
  TimerObject.update(deltaTime);

  //Serial.println(MiddleButton.isButtonDown() + LeftButton.isButtonDown() + RightButton.isButtonDown() + UpButton.isButtonDown() + DownButton.isButtonDown() + MenuButton.isButtonDown() + FunctionButton.isButtonDown());
  //Serial.println(MENU);
  switch (MENU) {
      case WATCH:

          if( FunctionButton.isButtonPressed() ){
              toggleLed();
          }
          if( MenuButton.isButtonFirstPressed()){
            PREVIOUS_MENU = MENU;
            MENU = MAIN_MENU;
          }
          if( MiddleButton.isButtonPressed() ){
              WatchObject.toggleIsTimeSettingModeOn();
            }

          if( WatchObject.getIsTimeSettingModeOn() ){
            if(UpButton.isButtonPressed()){
              WatchObject.addTime(1);
            }
            if(DownButton.isButtonPressed()){
              WatchObject.subtractTime(1);
            }
            if(RightButton.isButtonPressed()){
              WatchObject.moveTimeSettingModeRight();
            }
            if(LeftButton.isButtonPressed()){
              WatchObject.moveTimeSettingModeLeft();
            }
          }
          else{
            if(UpButton.isButtonPressed()){
              WatchObject.toggleIs12HourTimeModeOn();
            }
            if(LeftButton.isButtonPressed()){
              MENU = STOPWATCH;
            }
            if(RightButton.isButtonPressed()){
              MENU = TIMER;
            }
          }
          WatchObject.draw();
          break;

      case STOPWATCH:

        if( FunctionButton.isButtonPressed() ){
          toggleLed();
        }
        if( MenuButton.isButtonFirstPressed()){
            PREVIOUS_MENU = MENU;
            MENU = MAIN_MENU;
        }

        if( RightButton.isButtonPressed() ){
          MENU = WATCH;
        }
        else if ( LeftButton.isButtonPressed() ){
          MENU = TIMER;
        }
        else if ( UpButton.isButtonFirstPressed() ){
          StopwatchObject.resetStopwatch();
          if( StopwatchObject.getIsStopwatchOn() ) StopwatchObject.toggleStopwatch();
        }
        else if ( DownButton.isButtonFirstPressed() ){
          StopwatchObject.resetStopwatch(); 
        }
        else if ( MiddleButton.isButtonFirstPressed() ){
          StopwatchObject.toggleStopwatch();
        }


        StopwatchObject.updateCurrentTimeString();
        StopwatchObject.draw();
        
        break;

      case TIMER:

        if( MiddleButton.isButtonFirstPressed() ) TimerObject.toggleTimerSettingMode();

        if( FunctionButton.isButtonPressed() ){
          toggleLed();
        }
        if( MenuButton.isButtonFirstPressed()){
            PREVIOUS_MENU = MENU;
            MENU = MAIN_MENU;
        }

        // if timer is not in setting mode
        if( !TimerObject.getIsTimerSettingModeOn() ){
          if( LeftButton.isButtonPressed() ){
            MENU = WATCH;
          }   
          else if( RightButton.isButtonPressed() ){
            MENU = STOPWATCH;
          }
          else if ( UpButton.isButtonPressed() ){
            TimerObject.resetTimer();
          }
        }
        // if timer is in setting mode;
        else{
          if(RightButton.isButtonPressed()){
              if(TimerObject.TIMER_SETTING_ENUM == HOUR){
                TimerObject.TIMER_SETTING_ENUM = MINUTE;
              }
              else if(TimerObject.TIMER_SETTING_ENUM == MINUTE){
                TimerObject.TIMER_SETTING_ENUM = SECOND;
              }
              else TimerObject.TIMER_SETTING_ENUM = HOUR;
          }
          else if(LeftButton.isButtonPressed()){
              if(TimerObject.TIMER_SETTING_ENUM == HOUR){
                TimerObject.TIMER_SETTING_ENUM = SECOND;
              }
              else if(TimerObject.TIMER_SETTING_ENUM == MINUTE){
                TimerObject.TIMER_SETTING_ENUM = HOUR;
              }
              else TimerObject.TIMER_SETTING_ENUM = MINUTE;
          }
          else if (UpButton.isButtonPressed()){
            if(TimerObject.TIMER_SETTING_ENUM == HOUR) TimerObject.changeHour(1);
            if(TimerObject.TIMER_SETTING_ENUM == MINUTE) TimerObject.changeMinutes(1);
            if(TimerObject.TIMER_SETTING_ENUM == SECOND) TimerObject.changeSeconds(1);
          }
          else if (DownButton.isButtonPressed()){
            if(TimerObject.TIMER_SETTING_ENUM == HOUR) TimerObject.changeHour(-1);
            if(TimerObject.TIMER_SETTING_ENUM == MINUTE) TimerObject.changeMinutes(-1);
            if(TimerObject.TIMER_SETTING_ENUM == SECOND) TimerObject.changeSeconds(-1);
          }
        }
        TimerObject.updateCurrentTimeString();
        TimerObject.draw();
        break;

      case MAIN_MENU:

        if( FunctionButton.isButtonPressed() ){
          toggleLed();
        }
        if( MenuButton.isButtonFirstPressed()){
            MENU = PREVIOUS_MENU;
            PREVIOUS_MENU = MAIN_MENU;
            if( MENU == AIR_MOUSE) {
              AirMouseObject.begin();
              AirMouseObject.wakeMPU6500();
            }
            else if ( MENU == MEDIA_CONTROLLER){
              bleKeyboard.begin();
            }
        }

        if( UpButton.isButtonPressed() ) MainMenuObject.moveUp();
        else if( DownButton.isButtonPressed() ) MainMenuObject.moveDown();
        else if( LeftButton.isButtonPressed() ) MainMenuObject.moveLeft();
        else if( RightButton.isButtonPressed() ) MainMenuObject.moveRight();
        else if ( MiddleButton.isButtonPressed() ){
          MENU = MainMenuObject.getSelectedMenu();
          PREVIOUS_MENU = MAIN_MENU;
        }

        MainMenuObject.draw();

        break;
      case AIR_MOUSE_SETTINGS:
          if(MenuButton.isButtonFirstPressed()){
            PREVIOUS_MENU = MENU;
            MENU = MAIN_MENU;
          }
          if(MiddleButton.isButtonFirstPressed()){
            AirMouseObject.begin();
            AirMouseObject.wakeMPU6500();
            PREVIOUS_MENU = MENU;
            MENU = AIR_MOUSE;
            setCpuFrequencyMhz(240);
          }
          if(UpButton.isButtonPressed()){
            AirMouseSettingsObject.up();
          }
          if(DownButton.isButtonPressed()){
            AirMouseSettingsObject.down();
          }
          if(LeftButton.isButtonPressed()){
            AirMouseSettingsObject.decrease(AirMouseObject);
          }
          if(RightButton.isButtonPressed()){
            AirMouseSettingsObject.increase(AirMouseObject);
          }

          AirMouseSettingsObject.draw(AirMouseObject);
        break;
      case AIR_MOUSE:

        AirMouseObject.update();

        if( FunctionButton.isButtonFirstPressed() ) {
          AirMouseObject.calibrate();
          moveMouseToTopLeft();

        }
        if( MenuButton.isButtonFirstPressed()){
          PREVIOUS_MENU = MENU;
          MENU = AIR_MOUSE_SETTINGS;
          bleMouse.end();  
          AirMouseObject.sleepMPU6500();
          setCpuFrequencyMhz(80);
        }

        if( RightButton.isButtonPressed()){
          bleMouse.click(MOUSE_RIGHT);
        }
        else if ( MiddleButton.isButtonPressed() ){
          bleMouse.click(MOUSE_LEFT);
        }
        else if ( LeftButton.isButtonPressed() ){
          bleMouse.click(MOUSE_MIDDLE);
        }
        else if ( DownButton.isButtonPressed() ){
          bleMouse.move(0, 0, -1);
        }
        else if ( UpButton.isButtonPressed() ){
          bleMouse.move(0, 0, 1);
        }

        AirMouseObject.draw();
        
        break;
      
      case MEDIA_CONTROLLER:
          if( FunctionButton.isButtonPressed() ){
              toggleLed();
          }
          if( MenuButton.isButtonFirstPressed()){
            PREVIOUS_MENU = MENU;
            MENU = MAIN_MENU;
            bleKeyboard.end();
          }
          if(bleKeyboard.isConnected()){
            if( MiddleButton.isButtonPressed() ){
                MediaControllerObject.playPause();
            }
            if( LeftButton.isButtonPressed()){
              MediaControllerObject.previousTrack();
            }
            if( RightButton.isButtonPressed()){
              MediaControllerObject.nextTrack();
            }
            if( UpButton.isButtonPressed()){
              MediaControllerObject.volumeUp();
            }
            if( DownButton.isButtonPressed()){
              MediaControllerObject.volumeDown();
            }
          }
          MediaControllerObject.draw();
      case SETTINGS:
          break;
      case GAMES_FOLDER:
          if( FunctionButton.isButtonPressed() ){
              toggleLed();
          }
          if( MenuButton.isButtonFirstPressed()){
            PREVIOUS_MENU = MENU;
            MENU = MAIN_MENU;
          }
          if( UpButton.isButtonPressed() ){
            GamesFolderObject.Up();
          }
          if( DownButton.isButtonPressed() ){
            GamesFolderObject.Down();
          }
          if( MiddleButton.isButtonPressed() ){
            GamesFolderObject.Middle(PREVIOUS_MENU, MENU);
          }
        GamesFolderObject.draw();

        break;
      case SNAKE:
        if( FunctionButton.isButtonPressed() ){
              toggleLed();
          }
        if( MenuButton.isButtonFirstPressed()){
          PREVIOUS_MENU = SNAKE;
          MENU = GAMES_FOLDER;
        }
        if( DownButton.isButtonPressed() ){
          snakeGameObject.down();
        }
        if( LeftButton.isButtonPressed() ){
          snakeGameObject.left();
        }
        if( RightButton.isButtonPressed() ){
          snakeGameObject.right();
        }
        if( UpButton.isButtonPressed() ){
          snakeGameObject.up();
        }
        if( MiddleButton.isButtonPressed() ){
          snakeGameObject.middle();
        }

        snakeGameObject.update(currentTime);

        snakeGameObject.draw(display);

        break;
      //TO DO THE REST
  }
  display.display();
}
  
