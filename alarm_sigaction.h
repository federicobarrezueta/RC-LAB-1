#ifndef ALARM_SIGACTION_H
#define ALARM_SIGACTION_H

#define FALSE 0
#define TRUE 1

extern volatile int alarmEnabled;
extern volatile int alarmCount;

void alarmHandler(int signal);
void configure_and_run_alarms(int max_alarm_count);

#endif