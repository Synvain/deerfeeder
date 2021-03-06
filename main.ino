#include <RTClib.h>
#include <TimeLib.h>
#include <Time.h>
#include <LowPower.h>

#define BUTTON_INTERRUPT_PIN 7
#define RTC_INTERRUPT_PIN 3
#define LED 13
#define FET 7

typedef enum _State {
  FEED_ON,
  FEED_OFF
} State;

typedef struct _Event {
  const char* time;     // time of the day, format hh:mm:ss
  State desired_state;  // OPENED or CLOSED
} Event;

// Chronologically ascending events, othewise it won't work
Event EVENTS[] = {
  { "05:30:00", FEED_ON },
  { "05:31:00", FEED_OFF },
  { "09:50:00", FEED_ON },
  { "09:51:00", FEED_OFF },
  { "11:55:00", FEED_ON },
  { "11:56:00", FEED_OFF },
  { "13:55:00", FEED_ON },
  { "13:56:00", FEED_OFF },
  { "16:00:00", FEED_ON },
  { "16:05:00", FEED_OFF }
};

RTC_DS3231 rtc;
Event current_event;
int button_pressed = 0;
int rtc_alarmed = 0;

bool feed_is_on = false;

void feed_on();
void feed_off();
void get_next_event(DateTime now, Event *next_event, DateTime *event_time );
int32_t get_event_sod(Event event);

void setup() {
  pinMode(LED, OUTPUT);
  pinMode(FET, OUTPUT);
  pinMode(BUTTON_INTERRUPT_PIN, INPUT_PULLUP);
  pinMode(RTC_INTERRUPT_PIN, INPUT_PULLUP);
  Serial.begin(115200);
  while (!Serial) ; // wait for serial
  delay(200);
  Serial.println("Arduino Starting");
  rtc = RTC_DS3231();
  for(int i = 1; i < 3; i++) {
    rtc.clearAlarm(i);
    rtc.disableAlarm(i);
  }
  feed_off();
  set_next_alarm();
}

void rtc_isr()  {
  if( rtc_alarmed == 0 ) {
    rtc_alarmed++;
  }
}

void feed_on() {
  digitalWrite(FET, HIGH);
  feed_is_on = true;
}

void feed_off() {
  digitalWrite(FET, LOW);
  feed_is_on = false;
}

void button_isr() {
  if( button_pressed == 0 ) {
    button_pressed++;
  }
}

void set_next_alarm() {
  char now_buf[] = "DDD, DD MMM YYYY hh:mm:ss";
  char event_buf[] = "DDD, DD MMM YYYY hh:mm:ss";
  int result;
  DateTime event_time;
  DateTime now = rtc.now();

  get_next_event(now, &current_event, &event_time);
  rtc.clearAlarm(1);
  result = rtc.setAlarm1(event_time, DS3231_A1_Date);
  if( result != true ) {
    Serial.println("Failed to set Alarm1");
  }

  Serial.print("Current time: ");
  Serial.println(now.toString(now_buf));

  Serial.print("Next event: ");
  Serial.print(current_event.desired_state == FEED_ON ? "FEED_ON" : "FEED_OFF");
  Serial.print(" at ");
  Serial.println(event_time.toString(event_buf));
}

void loop() {
    char buf[] = "DDD, DD MMM YYYY hh:mm:ss";
    attachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN), rtc_isr, FALLING);
    attachInterrupt(digitalPinToInterrupt(BUTTON_INTERRUPT_PIN), button_isr, FALLING);

    Serial.println("Going to sleep");
    Serial.flush();
    digitalWrite(LED, LOW);
    LowPower.powerDown(SLEEP_FOREVER, ADC_OFF, BOD_OFF);

    // woke up
    detachInterrupt(digitalPinToInterrupt(RTC_INTERRUPT_PIN));
    detachInterrupt(digitalPinToInterrupt(BUTTON_INTERRUPT_PIN));

    digitalWrite(LED, HIGH);
    DateTime now = rtc.now();
    Serial.print("Woke up at: ");
    Serial.println(now.toString(buf));

    if( button_pressed ) {
      if(feed_is_on) {
        feed_off();
      } else {
        feed_on();
      }
      button_pressed = 0;
    }

    if( rtc_alarmed ) {
      if( current_event.desired_state == FEED_ON ) {
        feed_on();
      }
      if( current_event.desired_state == FEED_OFF ) {
        feed_off();
      }
      set_next_alarm();
      rtc_alarmed = 0;
    }
}

int32_t get_event_sod(Event event) {
  int min, hour, sec;
  if( 3 != sscanf(event.time, "%02d:%02d:%02d", &hour, &min, &sec) ) {
    Serial.print("Invalid time format for event with time : ");
    Serial.println(event.time);

    return 0;
  }

  return TimeSpan(0, hour, min, sec).totalseconds();
}

void get_next_event(DateTime now, Event *next_event, DateTime *event_time )
{
  int32_t current_sod, next_sod, next_alarm_sod, event_sod; // second of the day
  int min, hour, sec;
  DateTime next_event_time;
  int32_t offset_sec;

  // Get current time and set alarm to a time to wake
  current_sod = TimeSpan(0, now.hour(), now.minute(), now.second()).totalseconds();

  *next_event = EVENTS[0]; // earliest event in the day
  for(int idx = 0;idx < sizeof(EVENTS)/sizeof(*EVENTS); idx++) {
    event_sod = get_event_sod(EVENTS[idx]);
    if(current_sod < event_sod ) {
      // we found an event later today
      *next_event = EVENTS[idx];
      break;
    }
  }
  next_sod = get_event_sod(*next_event);

  next_alarm_sod = next_sod-current_sod;
  if(next_alarm_sod >= 0) {
    // Later today: now + delta
    offset_sec = next_alarm_sod;
  } else {
    // Tomorrow: now + 24h + (negative delta)
    offset_sec = 24L*60L*60L+next_alarm_sod;
  }

  *event_time = now + TimeSpan(offset_sec);

  return;
}