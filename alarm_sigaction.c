#define _POSIX_SOURCE 1

#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include "alarm_sigaction.h"

// DEFINIÇÃO real das variáveis (só neste ficheiro)
volatile int alarmEnabled = FALSE;
volatile int alarmCount = 0;

void alarmHandler(int signal)
{
    alarmEnabled = FALSE;
    alarmCount++;
    printf("Alarm #%d received\n", alarmCount);
}

void configure_and_run_alarms(int max_alarm_count)
{
    struct sigaction act = {0};
    act.sa_handler = &alarmHandler;
    if (sigaction(SIGALRM, &act, NULL) == -1)
    {
        perror("sigaction");
        exit(1);
    }

    printf("Alarm configured\n");

    while (alarmCount < max_alarm_count + 1)
    {
        if (alarmEnabled == FALSE)
        {
            alarm(3);
            alarmEnabled = TRUE;
        }
    }

    printf("Ending program\n");
}