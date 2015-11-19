#include <sys/types.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <time.h>
#include <limits.h>
#include "libping-1.15/lib/ping.h"

#define MAXWAITTIME 60 /* max wait time for ping reply */

void usage(void);
void main_loop(void);
void readHostsFile(void);
int do_ping(const char *);
void syserr(int);
void message(const char *, const char *, int);

int	pingInterval = 60;	/* interval between ping whole list */
int	noFork = 0; 		/* don't fork - don't run in daemon mode */
char *hostsFile = "/etc/pping.hosts"; /* list of pinging hosts */
int hostsCount = 0; /* count of hosts for ping */
short int debug = 0; /* debug messages */
char *hostname; // It's me

struct hostRecord {
	char *name; /* host name */
	int status; /* link status < 0 - down, > 0 - round trip in millisec */
};

struct hostRecord **hostsData;

int main(int argc, char **argv)
{
	extern char *optarg;
	int ch;

	while ((ch = getopt(argc, argv, "i:nhf:D")) != EOF)
		switch ((char)ch) {
		case 'i':		/* ping interval seconds */
			pingInterval = atoi(optarg);
			break;
		case 'n':		/* don't fork */
			noFork = 1;
			break;
		case 'f':
			hostsFile = optarg;
			break;
		case 'D':
			debug = 1;
			break;
		case 'h':
		default:
			usage();
		}

	hostname = malloc(sizeof(char) * HOST_NAME_MAX+1);
	if (!hostname) 
		return 1;
	gethostname(hostname, HOST_NAME_MAX);
	if (debug) printf("hostname: %s\n", hostname);

	readHostsFile();

	if (!noFork) {
		if (fork()) {
			/* Parent process */
			exit(0);
		}
	}
	main_loop();
	free(hostname);
	return 0;
}

void readHostsFile()
{
	FILE *cf;
	char buff[255];
	register int j, i = 0;

	if ((cf = fopen(hostsFile, "r")) == NULL) {
		fprintf(stderr, "Can't open hosts file: %s\n", hostsFile);
		exit(1);
	}
	/* Calculate host's number (It must doing easy, I don't know how) */
	while (fgets(buff, sizeof(buff), cf) != NULL)
		if (*buff != '#' && *buff != '\n') hostsCount++;
	message("Read from file %s %d entries", hostsFile, hostsCount);

	fseek(cf, 0, SEEK_SET);

	hostsData = (struct hostRecord**) malloc(hostsCount * sizeof(struct hostRecord *));

	for(i=0; i< hostsCount; i++)
		hostsData[i] = (struct hostRecord *) malloc(sizeof(struct hostRecord));

	i=0;
	while (fgets(buff, sizeof(buff), cf) != NULL) {
		if (*buff != '#' && *buff != '\n') {
			j = strlen(buff);
			if (buff[j-1] == '\n') j--;
			hostsData[i]->name = (char *) malloc((j+1)*sizeof(char));

			buff[j] = '\0';
			strncpy(hostsData[i]->name, buff, j+1);
			hostsData[i]->status = 0;
			if (debug) fprintf(stderr, "*%s* *%d*\n", hostsData[i]->name, hostsData[i]->status);
			i++;
		}
	}
	fclose(cf);
}

void main_loop(void)
{
	int i, retval=-10;
	while (1) {
		for(i=0; i<hostsCount; i++) {
			retval = do_ping(hostsData[i]->name);
			if (retval >= 1 && hostsData[i]->status < 1)
				/* Host alive now. retval = round trip time */
				message("Host %s alive (%d)", hostsData[i]->name, retval);
			if (retval < 1 && hostsData[i]->status >= 1)
				/* Host down now */
				message("Host %s down.", hostsData[i]->name, -1);
			hostsData[i]->status = retval;
		}
		sleep(pingInterval);
	}
}

int do_ping(const char *host)
{
	int retval = -10;
	register int waittime = 1; /* ping reply wait time */

	while (waittime < MAXWAITTIME && retval < 1 ) {
		retval = tpingthost(host, waittime);
		if (retval < -1) syserr(retval);
		waittime+=5;
	}
	return retval;
}

void usage(void)
{
	fprintf(stderr, "usage: pping [-hn] [-i <ping interval>] [-f <hosts file>]\n");
	exit(1);
}

void syserr(int val)
{
	if (val == -2)
		fprintf(stderr, "pping: Socket error\n");
	else if (val == -3)
		fprintf(stderr, "pping: Connection refused\n");
	exit(-1);
}

void message(const char *fmt, const char *name, int val)
{
	char date[9];
	char msg[100];
	char mailcmd[1024];
	time_t timep, delta;
	struct tm *tms;
	static time_t prevtime = 0;
	
	time(&timep);
	
	if ( prevtime ) {
		delta = timep - prevtime;
		if ( debug ) fprintf(stderr, "Time delta=%d\n", delta);
		if ( delta < 30 ) {// Previos message has been send between 30 sec. interval
			if (debug) fprintf(stderr, "Waiting %d sec.\n", 30-delta);
			sleep(30 - delta); // Waiting 30 sec. before send next message
		}
	}

	time(&timep);
	prevtime = timep;
		
	tms = localtime(&timep);
	snprintf(date, sizeof(date), "%02d%02d%02d%02d\0", tms->tm_mon+1, tms->tm_mday, tms->tm_hour, tms->tm_min);

	if (val == -1) snprintf(msg, sizeof(msg), fmt, name);
	else snprintf(msg, sizeof(msg), fmt, name, val);

	openlog("pping", LOG_PID, LOG_USER);
	syslog(LOG_NOTICE, msg);
	closelog();

//	snprintf(mailcmd, sizeof(mailcmd), "echo \"%s %s\"|mail sms@bizzone.biz", date, msg);
	snprintf(mailcmd, sizeof(mailcmd), "/usr/local/sbin/twitter.pl \"%s: %s %s\"", hostname, date, msg);
	if (debug) fprintf(stderr, "%s\n", mailcmd);
	else system(mailcmd);
	sleep(1);
}
