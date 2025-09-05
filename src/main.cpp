#include <Arduino.h>
#include <Preferences.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiManager.h>


//Pins


//Reset can be pressed to force Wifi Provisioning
#define RESET_PIN GPIO_NUM_10

//Pin connected to the coin acceptor
#define PULSE_PIN GPIO_NUM_2

#define PREFERENCES_NAMESPACE "Booth_Data"
#define URL_PARAMETER_NAME "url"
#define WAITTIME_PARAMETER_NAME "waittime"

// Global Variables

//The url to be called to increase the print limit. Use printf-format. Can be changed while Wifi Provisioning
String url = "http://photobooth.local/commands/increase-print?i=%i";
Preferences prefs;

//How much is the limit supossed to be increased
volatile int count = 0;


volatile uint32_t printTime;

//Can be changed while Wifi Provisioning
int delay_time = 300;

// Function Declarations
void increasePrintCount(int amount);
void IRAM_ATTR increaseCount();
void setWifiParamters(WiFiManager &wm);
void handleWifiParamters(WiFiManager &wm);

void setup()
{
  // Connecting over JTag usb takes some time
  delay(2000);
  Serial.begin(115200);
  Serial.println("setup");
  pinMode(RESET_PIN, INPUT_PULLUP);
  pinMode(PULSE_PIN, INPUT);

  Serial.println("Trying to Autoconnect");
  WiFi.mode(WIFI_STA);
  WiFiManager wm;
  wm.setConnectTimeout(60);
  wm.setConnectRetries(10);
  setWifiParamters(wm);
  if (!wm.autoConnect("Muenzeinwurf_Auto"))
  {
    Serial.println("Autoconnection failed, looping forever");
    while (true)
      ;
  }
  handleWifiParamters(wm);

  Serial.println("Waiting for manual Wifi Setup");


  //Start a manual Provisioning portal when RESET_PIN is pressed.
  uint32_t wait = millis() + 5000;
  while (millis() < wait)
  {
    if (digitalRead(RESET_PIN) == LOW)
    {
      Serial.println("starting manual config portal.");
      WiFiManager manualWifi;
      setWifiParamters(manualWifi);
      if (!manualWifi.startConfigPortal("Muenzeinwurf_Manual"))
      {
        Serial.println("Manual Connection failed, rebooting");
        ESP.restart();
      }
      handleWifiParamters(manualWifi);
    }
  }

  Serial.println("finished manual setup");

  //Interrupt to count the pulses eg. number of pictures the limit should be increased by.
  attachInterrupt(PULSE_PIN, increaseCount, RISING);


  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Failed to connect to WIFI :( , press reset button to set up WIFI!)");
    ESP.restart();
  }

  Serial.println("trying to get preferences");

  //Get all the preferences.
  prefs.begin(PREFERENCES_NAMESPACE, true);
  delay_time = prefs.getInt(WAITTIME_PARAMETER_NAME);
  url = prefs.getString(URL_PARAMETER_NAME);

  Serial.println("Setup finished");
}

void loop()
{
  //If there wasn't a pulse for some time, use remote buzzer api to increase print limit
  if (millis() > printTime && count > 0)
  {
    //Set count to zero immediately to prevent pulses from being lost.
    int _count = count;
    count = 0;
    increasePrintCount(_count);
  }

  delay(10); // Add a small delay to avoid busy-waiting
  // Serial.printf("count; %d", count);
}



/** 
 * @brief Uses Http and the remote buzzer server to increase the print limit
 * @param amount The amount the print limit should be increased by.
*/
void increasePrintCount(int amount)
{
  HTTPClient http;

  char buffer[url.length() + 10];

  //Replace the first placeholder in the given url with the amount
  snprintf(buffer, sizeof(buffer), url.c_str(), amount);
  Serial.printf("Trying request: %s\n", buffer);

  http.begin(buffer);
  int httpCode = http.GET();

  if (httpCode > 0)
  {
    Serial.printf("HTTP GET Response code: %d\n", httpCode);
    if (httpCode == HTTP_CODE_OK)
    {
      String payload = http.getString();
      Serial.println("Response: " + payload);
    }
  }
  else
  {
    Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
  }

  http.end();
}
void IRAM_ATTR increaseCount()
{
  static uint32_t debounce = 0;
  if(millis() > debounce){

    count++;
    // Serial.printf("Count increased to: %d\n", count);
    
    printTime = millis() + delay_time;
    debounce = millis() + 17;
  }
}


//Adds the extra parameters to the Wifi Provisioner
void setWifiParamters(WiFiManager &wm)
{
  prefs.begin(PREFERENCES_NAMESPACE, true);
  String url_old = prefs.getString(URL_PARAMETER_NAME, "http://photobooth.local/commands/increase-print?i=%d");
  int time_old = prefs.getInt(WAITTIME_PARAMETER_NAME, 300);

  // Allocate parameters on the heap
  WiFiManagerParameter *customURL = new WiFiManagerParameter(
      URL_PARAMETER_NAME,
      "Url the Coin Acceptor calls to increase print limit. Can contain printf placeholders. eg. http://photobooth.local/commands/increase-print?i=%d",
      url_old.c_str(),
      100);
  WiFiManagerParameter *pulse_wait_time = new WiFiManagerParameter(
      WAITTIME_PARAMETER_NAME,
      "Time between Pulses of the coin acceptor",
      std::to_string(time_old).c_str(),
      100);

  // If adding new Parameters, remember to handle the user input in the handleWifiParamters() function

  wm.addParameter(customURL);
  wm.addParameter(pulse_wait_time);
  prefs.end();
}


//Saves the extra Parameters and clears the objects.
void handleWifiParamters(WiFiManager &wm)
{

  WiFiManagerParameter **wifiParamters = wm.getParameters();

  prefs.begin(PREFERENCES_NAMESPACE, false);

  for (int i = 0; i < wm.getParametersCount(); i++)
  {
    WiFiManagerParameter *parameter = wifiParamters[i];
    if (strcmp(parameter->getID(), URL_PARAMETER_NAME) == 0)
    {
      // We're looking at the url parameter
      const char *url_new = parameter->getValue();
      prefs.putString(URL_PARAMETER_NAME, url_new);
    }
    else if (strcmp(parameter->getID(), WAITTIME_PARAMETER_NAME) == 0)
    {
      // We're looking at the time parameter
      int time = atoi(parameter->getValue());
      prefs.putInt(WAITTIME_PARAMETER_NAME, time);
    }
    else
    {
      Serial.printf("UnHandled Parameter! Id: %s  , Label: %s  , Value: %s  \n", parameter->getID(), parameter->getLabel(), parameter->getValue());
    }

    delete parameter;
  }

  prefs.end();
}