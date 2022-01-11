/* #define	OLD_ASTERISK */
/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Copyright (C) 2010, Jim Dixon/WB6NIL
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief GPS device interface module
 * 
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
 ***/

/* The following are the recognized APRS icon codes: 

NOTE: Since the semicolon (';') is recognized by the
Asterisk config subsystem as a comment, we use the
question-mark ('?') instead when we want to specify
a 'portable tent'.


! - 3 vert bars (EMERGENCY)
" - RAIN
# - DIGI

$ - SUN (always yellow)
% - DX CLUSTER
& - HF GATEway
' - AIRCRAFT (small)
( - CLOUDY
) - Hump
* - SNOW
+ - Cross
, - reverse L shape

- - QTH
. - X
/ - Dot
0-9 Numerial Boxes
: - FIRE
? - Portable tent (note different then standard ';')
< - Advisory flag
= - RAILROAD ENGINE
> - CAR (SSID-9)

@ - HURRICANE or tropical storm
A - reserved
B - Blowing Snow  (also BBS)
C - reserved
D - Drizzle
E - Smoke
F - Freezing rain
G - Snow Shower
H - Haze

I - Rain Shower   (Also TCP-IP)
J - Lightening
K - School
L - Lighthouse
M - MacAPRS
N - Navigation Buoy
O - BALLOON
P - Police
Q - QUAKE

R - RECREATIONAL VEHICLE
S - Space/Satellite
T - THUNDERSTORM
U - BUS
V - VORTAC Nav Aid
W - National WX Service Site
X - HELO  (SSID-5)
Y - YACHT (sail SSID-6)
Z - UNIX X-APRS

[ - RUNNER
\ - TRIANGLE   (DF)
] - BOX with X (PBBS's)
^ - LARGE AIRCRAFT
_ - WEATHER SURFACE CONDITIONS (always blue)
` - Satellite Ground Station
a - AMBULANCE
b - BIKE
c - DX spot by callsign

d - Dual Garage (Fire dept)
e - SLEET
f - FIRE TRUCK
g - GALE FLAGS
h - HOSPITAL
i - IOTA (islands on the air)
j - JEEP (SSID-12)
k - TRUCK (SSID-14)
l - AREAS (box,circle,line,triangle) See below

m - MILEPOST (box displays 2 letters if in {35})
n - small triangle
o - small circle
p - PARTLY CLOUDY
q - GRID SQUARE (4 digit.  Not shown below 128 miles)
r - ANTENNA
s - SHIP (pwr boat SSID-8)
t - TORNADO
u - TRUCK (18 wheeler)

v - VAN (SSID-15)
w - FLOODING(water)
x - diamond (NODE)
y - YAGI @ QTH
z - WinAPRS
{ - FOG
| - reserved (Stream Switch)
} - diamond 
~ - reserved (Stream Switch)

*/

#include "asterisk.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <search.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>
#include <math.h>
#include <sys/mman.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/translate.h"
#include "asterisk/cli.h"

#ifdef OLD_ASTERISK
#define ast_free free
#define ast_malloc malloc
#endif

#define	GPS_DEFAULT_SERVER "second.aprs.net"
#define	GPS_DEFAULT_PORT "10151"
#define	GPS_DEFAULT_COMMENT "Asterisk app_rpt server"
#define	TT_DEFAULT_COMMENT "Asterisk app_rpt user"
#define	TT_DEFAULT_OVERLAY '0'
#define	DEFAULT_TTLIST 10
#define	DEFAULT_TTOFFSET 10
#define	TT_LIST_TIMEOUT 3600
#define	GPS_DEFAULT_BAUDRATE B4800
#define	GPS_DEFAULT_ICON '>' /* car */
#define	GPS_WORK_FILE "/tmp/gps.tmp"
#define	GPS_DATA_FILE "/tmp/gps.dat"
#define	GPS_SUB_FILE "/tmp/gps_%s.dat"
#define	TT_PIPE "/tmp/aprs_ttfifo"
#define	TT_SUB_PIPE "/tmp/aprs_ttfifo_%s"
#define	TT_COMMON "/tmp/aprs_ttcommon"
#define	TT_SUB_COMMON "/tmp/aprs_ttcommon_%s"
#define	GPS_UPDATE_SECS 30
#define	GPS_VALID_SECS 60
#define	SERIAL_MAXMS 10000

struct ttentry {
char call[20];
time_t t;
};

AST_MUTEX_DEFINE_STATIC(gps_lock);

static char *config = "gps.conf";

static char *app = "GPS";

static char *synopsis = "GPS Device Interface Module";

static char *descrip = "Interfaces app_rpt to a NMEA 0183 (GGA records) compliant GPS device\n";

static  pthread_t gps_thread = 0;
static  pthread_t aprs_thread = 0;
static int run_forever = 1;
static char *comport,*server,*port;
static char *general_deflat,*general_deflon,*general_defelev;
static int baudrate;
static int debug = 0;
static int sockfd = -1;


/*
* Break up a delimited string into a table of substrings
*
* str - delimited string ( will be modified )
* strp- list of pointers to substrings (this is built by this function), NULL will be placed at end of list
* limit- maximum number of substrings to process
* delim- user specified delimeter
* quote- user specified quote for escaping a substring. Set to zero to escape nothing.
*
* Note: This modifies the string str, be suer to save an intact copy if you need it later.
*
* Returns number of substrings found.
*/
	

static int explode_string(char *str, char *strp[], int limit, char delim, char quote)
{
int     i,l,inquo;

        inquo = 0;
        i = 0;
        strp[i++] = str;
        if (!*str)
           {
                strp[0] = 0;
                return(0);
           }
        for(l = 0; *str && (l < limit) ; str++)
        {
		if(quote)
		{
                	if (*str == quote)
                   	{	
                        	if (inquo)
                           	{
                                	*str = 0;
                                	inquo = 0;
                           	}
                        	else
                           	{
                                	strp[i - 1] = str + 1;
                                	inquo = 1;
                           	}
			}
		}	
                if ((*str == delim) && (!inquo))
                {
                        *str = 0;
			l++;
                        strp[i++] = str + 1;
                }
        }
        strp[i] = 0;
        return(i);

}

static void strupr(char *str)
{
        while (*str)
           {
                *str = toupper(*str);
                str++;
           }
        return;
}

static int getserialchar(int fd)
{
int	res;
char	c;
int	i;
fd_set	fds;
struct	timeval tv;


	for(i = 0; (i < (SERIAL_MAXMS / 100)) && run_forever; i++)
	{
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
	        FD_ZERO(&fds);
	        FD_SET(fd, &fds);
		res = ast_select(fd + 1,&fds,NULL,NULL,&tv);
		if (res < 0) return -1;
		if (res)
		{
			if (read(fd,&c,1) < 1) return -1;
			return(c);
		}
	}
	return(0);
}

static int getserial(int fd, char *str, int max)
{
int	i;
char	c;

	for(i = 0; (i < max) && run_forever; i++)
	{
		c = getserialchar(fd);
		if (c < 1) return(c);
		if ((i == 0) && (c < ' '))
		{
			i--;
			continue;
		}
		if (c < ' ') break;
		str[i] = c;
	}
	str[i] = 0;
	return(i);
}

static void *aprsthread(void *data)
{
char *call,*password,*val,buf[300];
struct ast_config *cfg = NULL;
struct ast_hostent ahp;
struct hostent *hp;
struct sockaddr_in servaddr;

	call = NULL;
	password = NULL;
#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		pthread_exit(NULL);
                return NULL;
        }
	val = (char *) ast_variable_retrieve(cfg,"general","call");	
	if (val) call = ast_strdup(val); else call = NULL;
	val = (char *) ast_variable_retrieve(cfg,"general","password");	
	if (val) password = ast_strdup(val); else password = NULL;
	if ((!call) || (!password))
	{
		ast_log(LOG_ERROR,"You must specify call and password\n");
		if (call) ast_free(call);
		if (password) ast_free(password);
		pthread_exit(NULL);
		return NULL;
	}		
        ast_config_destroy(cfg);
        cfg = NULL; 
	while(run_forever)
	{
		ast_mutex_lock(&gps_lock);
		if (sockfd < 0) close(sockfd);
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0)
		{
			ast_log(LOG_ERROR,"Error opening socket\n");
			sockfd = -1;
			ast_mutex_unlock(&gps_lock);
			usleep(500000);
			continue;
		}
		bzero(&servaddr, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(atoi(port));
		hp = ast_gethostbyname(server, &ahp);
		if (!hp)
		{
			ast_log(LOG_WARNING, "server %s cannot be found!!\n",server);
			close(sockfd);
			sockfd = -1;
			ast_mutex_unlock(&gps_lock);
			usleep(500000);
			continue;
		}
		memcpy(&servaddr.sin_addr,hp->h_addr,sizeof(in_addr_t));
		if (connect(sockfd,(struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
		{
			ast_log(LOG_WARNING, "server %s cannot be found!!\n",server);
			close(sockfd);
			sockfd = -1;
			ast_mutex_unlock(&gps_lock);
			usleep(500000);
			continue;
		}
		sprintf(buf,"user %s pass %s vers \"Asterisk app_gps\"\n",
			call,password);
		if (send(sockfd,buf,strlen(buf),0) < 0)
		{
			ast_log(LOG_WARNING, "Can not send signon to server\n");
			close(sockfd);
			sockfd = -1;
			ast_mutex_unlock(&gps_lock);
			continue;
		}		
		if (debug) ast_log(LOG_NOTICE,"sent packet(login): %s",buf);
		ast_mutex_unlock(&gps_lock);
		while(recv(sockfd,buf,sizeof(buf) - 1,0) > 0) ;
	}
	pthread_exit(NULL);
	return NULL;
}


static int report_aprs(char *ctg,char *lat,char *lon)
{

struct ast_config *cfg = NULL;
char *call,*comment,icon;
char power,height,gain,dir,*val,basecall[300],buf[350],*cp;
time_t t;

#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif

	call = NULL;
	comment = NULL;
#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
                return -1;
        }
	val = (char *) ast_variable_retrieve(cfg,ctg,"call");	
	if (val) call = ast_strdup(val); else call = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"comment");	
	if (val) comment = ast_strdup(val); else comment = ast_strdup(GPS_DEFAULT_COMMENT);
	val = (char *) ast_variable_retrieve(cfg,ctg,"power");	
	if (val) power = (char)strtol(val,NULL,0); else power = 0;
	val = (char *) ast_variable_retrieve(cfg,ctg,"height");	
	if (val) height = (char)strtol(val,NULL,0); else height = 0;
	val = (char *) ast_variable_retrieve(cfg,ctg,"gain");	
	if (val) gain = (char)strtol(val,NULL,0); else gain = 0;
	val = (char *) ast_variable_retrieve(cfg,ctg,"dir");	
	if (val) dir = (char)strtol(val,NULL,0); else dir = 0;
	val = (char *) ast_variable_retrieve(cfg,ctg,"icon");	
	if (val && *val) icon = *val; else icon = GPS_DEFAULT_ICON;
	if (icon == '?') icon = ';';  /* allow for entry of portable tent */

	if (!call)
	{
		ast_log(LOG_ERROR,"You must specify call\n");
		if (call) ast_free(call);
		if (comment) ast_free(comment);
		return -1;
	}		
        ast_config_destroy(cfg);
        cfg = NULL; 

	strncpy(basecall,call,sizeof(basecall) - 1);
	cp = strchr(basecall,'-');
	if (cp) *cp = 0;
	cp = strchr(lat,'.');
	if (cp && (strlen(cp) >= 3))
	{
		*(cp + 3) = lat[strlen(lat) - 1];
		*(cp + 4) = 0;
	}
	cp = strchr(lon,'.');
	if (cp && (strlen(cp) >= 3)) 
	{
		*(cp + 3) = lon[strlen(lon) - 1];
		*(cp + 4) = 0;
	}

	sprintf(buf,"%s>APRS,qAR,%s-VS:=%s/%s%cPHG%d%d%d%d/%s\n",
		call,basecall,lat,lon,icon,power,height,gain,dir,comment);
	time(&t);
	ast_mutex_lock(&gps_lock);
	if (sockfd == -1)
	{
		ast_log(LOG_WARNING,"Attempt to send APRS data with no connection open!!\n");
		ast_mutex_unlock(&gps_lock);
		return -1;
	}
	if (send(sockfd,buf,strlen(buf),0) < 0)
	{
		ast_log(LOG_WARNING, "Can not send APRS (GPS) data\n");
		ast_mutex_unlock(&gps_lock);
		return -1;
	}
	if (debug) ast_log(LOG_NOTICE,"sent packet(%s): %s",ctg,buf);
	ast_mutex_unlock(&gps_lock);
	if (call) ast_free(call);
	if (comment) ast_free(comment);
	return 0;
}

static int report_aprstt(char *ctg,char *lat,char *lon,char *theircall,char overlay)
{

struct ast_config *cfg = NULL;
char *call,*comment;
char *val,basecall[300],buf[300],buf1[100],*cp;
time_t t;
struct tm *tm;

#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif

	call = NULL;
	comment = NULL;
#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
                return -1;
        }
	val = (char *) ast_variable_retrieve(cfg,ctg,"call");	
	if (val) call = ast_strdup(val); else call = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"ttcomment");	
	if (val) comment = ast_strdup(val); else comment = ast_strdup(TT_DEFAULT_COMMENT);

	if (!call)
	{
		ast_log(LOG_ERROR,"You must specify call\n");
		if (call) ast_free(call);
		if (comment) ast_free(comment);
		return -1;
	}		
        ast_config_destroy(cfg);
        cfg = NULL; 

	strncpy(basecall,call,sizeof(basecall) - 1);
	cp = strchr(basecall,'-');
	if (cp) *cp = 0;
	cp = strchr(lat,'.');
	if (cp && (strlen(cp) >= 3))
	{
		*(cp + 3) = lat[strlen(lat) - 1];
		*(cp + 4) = 0;
	}
	cp = strchr(lon,'.');
	if (cp && (strlen(cp) >= 3)) 
	{
		*(cp + 3) = lon[strlen(lon) - 1];
		*(cp + 4) = 0;
	}
	time(&t);
	tm = gmtime(&t);
	sprintf(buf1,"%s-12",theircall);
	sprintf(buf,"%s>APSTAR:;%-9s*%02d%02d%02dz%s%c%sA%s\n",
		call,buf1,tm->tm_hour,tm->tm_min,tm->tm_sec,lat,overlay,lon,comment);
	if (send(sockfd,buf,strlen(buf),0) < 0)
	{
		ast_log(LOG_WARNING, "Can not send APRS (APSTAR) data\n");
		ast_mutex_unlock(&gps_lock);
		if (call) ast_free(call);
		if (comment) ast_free(comment);
		return -1;
	}
	if (debug) ast_log(LOG_NOTICE,"sent packet(%s): %s",ctg,buf);
	ast_mutex_unlock(&gps_lock);
	if (call) ast_free(call);
	if (comment) ast_free(comment);
	return 0;
}

static void *gpsthread(void *data)
{
char	buf[300],c,*strs[100],lat[100],lon[100];
char	latc,lonc,astr[50];
int	res,i,n,fd,has_comport = 0;
float	mylat,lata,latb,latd;
float	mylon,lona,lonb,lond;
FILE	*fp;
time_t	t;
struct termios mode;


	if (comport) has_comport = 1;
	else comport = "/dev/null";

	fd = open(comport,O_RDWR);
	if (fd == -1)
	{
		ast_log(LOG_WARNING,"Cannot open serial port %s\n",comport);
		goto err;
	}

	if (has_comport)
	{
		memset(&mode, 0, sizeof(mode));
		if (tcgetattr(fd, &mode)) {
			ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", comport, strerror(errno));
			close(fd);
			goto err;
		}
#ifndef	SOLARIS
		cfmakeraw(&mode);
#else
	        mode.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                        |INLCR|IGNCR|ICRNL|IXON);
	        mode.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	        mode.c_cflag &= ~(CSIZE|PARENB|CRTSCTS);
	        mode.c_cflag |= CS8;
		mode.c_cc[VTIME] = 3;
		mode.c_cc[VMIN] = 1; 
#endif

		cfsetispeed(&mode, baudrate);
		cfsetospeed(&mode, baudrate);
		if (tcsetattr(fd, TCSANOW, &mode)) 
		{
			ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", comport, strerror(errno));
			close(fd);
			goto err;
		}
	}
	usleep(100000);

	while(run_forever)
	{
		res = getserial(fd,buf,sizeof(buf) - 1);
		if (res < 0)
		{
			ast_log(LOG_ERROR,"GPS fatal error!!\n");
			continue;
		}
		if (!res)
		{
			if ((!general_deflat) || (!general_deflon))
			{
				ast_log(LOG_WARNING,"GPS timeout!!\n");
				continue;
			}
			ast_log(LOG_WARNING,"GPS timeout -- Using default (fixed location) parameters instead\n");
			mylat = strtof(general_deflat,NULL);
			mylon = strtof(general_deflon,NULL);
			latc = (mylat >= 0.0) ? 'N' : 'S';
			lonc = (mylon >= 0.0) ? 'E' : 'W';
			lata = fabs(mylat);
			lona = fabs(mylon);
			latb = (lata - floor(lata)) * 60;
                        latd = (latb - floor(latb)) * 100 + 0.5;
                        lonb = (lona - floor(lona)) * 60;
                        lond = (lonb - floor(lonb)) * 100 + 0.5;
			sprintf(lat,"%02d%02d.%02d%c",(int)lata,(int)latb,(int)latd,latc);
			sprintf(lon,"%03d%02d.%02d%c",(int)lona,(int)lonb,(int)lond,lonc);
			if (general_defelev)
			{
				mylat = strtof(general_defelev,NULL);
				lata = (mylat - floor(mylat)) * 10 + 0.5;
				sprintf(astr,"%03d.%1d",(int)mylat,(int)lata);
				strs[9] = astr;
			}
			else
				strs[9] = "000.0";
			strs[10] = "M";
		}
		else
		{
			c = 0;
			if (buf[0] != '$')
			{
				ast_log(LOG_WARNING,"GPS Invalid data format (no '$' at beginning)\n");
				continue;
			}
			for(i = 1; buf[i]; i++)
			{
				if (buf[i] == '*') break;
				c ^= buf[i];
			}
			if ((!buf[i]) || (strlen(buf) < (i + 3)))
			{
				ast_log(LOG_WARNING,"GPS Invalid data format (checksum format)\n");
				continue;
			}
			if ((sscanf(buf + i + 1,"%x",&i) != 1) || (c != i))
			{
				ast_log(LOG_WARNING,"GPS Invalid checksum\n");
				continue;
			}
			n = explode_string(buf,strs,100,',','\"');
			if (!n)
			{
				ast_log(LOG_WARNING,"GPS Invalid data format (no data)\n");
				continue;
			}
			if (strcasecmp(strs[0],"$GPGGA")) continue;
			if (n != 15)
			{
				ast_log(LOG_WARNING,"GPS Invalid data format (invalid format for GGA record)\n");
				continue;
			}
			if (*strs[6] < '1')
			{
				ast_log(LOG_WARNING,"GPS data not available\n");
				continue;
			}
			snprintf(lat,sizeof(lat) - 1,"%s%s",strs[2],strs[3]);
			snprintf(lon,sizeof(lon) - 1,"%s%s",strs[4],strs[5]);
		}
		if (debug) ast_log(LOG_NOTICE,"got lat: %s, long: %s, elev: %s%s\n",lat,lon,strs[9],strs[10]);
		fp = fopen(GPS_WORK_FILE,"w");
		if (!fp)
		{
			ast_log(LOG_ERROR,"Unable to open GPS work file!!\n");
			continue;
		}
		time(&t);
		fprintf(fp,"%u %s %s %s%s\n",(unsigned int) t,lat,lon,
			strs[9],strs[10]);
		fclose(fp);
		sprintf(buf,"/bin/mv %s %s > /dev/null 2>&1",GPS_WORK_FILE,GPS_DATA_FILE);
		ast_safe_system(buf);
	}
	close(fd);
err:
	pthread_exit(NULL);
	return NULL;
}


static void *gps_sub_thread(void *data)
{
struct ast_config *cfg = NULL;
char *ctg = (char *)data,gotfiledata;
char *val,*deflat,*deflon,latc,lonc;
char fname[200],lat[300],lon[300];
FILE *fp;
unsigned int u;
int interval,my_update_secs,ehlert;
float	mylat,lata,latb,latd;
float	mylon,lona,lonb,lond;
struct stat mystat;
time_t	now,was,lastupdate;


#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		pthread_exit(NULL);
		return NULL;
        }
	val = (char *) ast_variable_retrieve(cfg,ctg,"lat");	
	if (val) deflat = ast_strdup(val); else deflat = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"lon");	
	if (val) deflon = ast_strdup(val); else deflon = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"elev");	
	val = (char *) ast_variable_retrieve(cfg,ctg,"interval");	
	if (val) interval = atoi(val); else interval = GPS_UPDATE_SECS;
	val = (char *) ast_variable_retrieve(cfg,ctg,"ehlert");	
	if (val) ehlert = ast_true(val); else ehlert = 0;
        ast_config_destroy(cfg);
        cfg = NULL; 
	time(&lastupdate);
	my_update_secs = GPS_UPDATE_SECS;
	while(run_forever)
	{
		gotfiledata = 0;
		if (!strcmp(ctg,"general"))
			strcpy(fname,GPS_DATA_FILE);
		else
			snprintf(fname,sizeof(fname) - 1,GPS_SUB_FILE,ctg);
		time(&now);
		fp = fopen(fname,"r");
		if (fp && (fstat(fileno(fp),&mystat) != -1) &&
			(mystat.st_size < 100))
		{
			if (fscanf(fp,"%u %s %s",&u,lat,lon) == 3)
			{
				was = (time_t) u;
				if ((was + GPS_VALID_SECS) >= now)
				{
					gotfiledata = 1;
					if (now >= (lastupdate + my_update_secs))
					{
						report_aprs(ctg,lat,lon);
						lastupdate = now;
						my_update_secs = interval;
					}
				}
			}
		}
		if ((!gotfiledata) && (!ehlert) && deflat && deflon)
		{
			if (now >= (lastupdate + my_update_secs))
			{
				mylat = strtof(deflat,NULL);
				mylon = strtof(deflon,NULL);
				latc = (mylat >= 0.0) ? 'N' : 'S';
				lonc = (mylon >= 0.0) ? 'E' : 'W';
				lata = fabs(mylat);
				lona = fabs(mylon);
				latb = (lata - floor(lata)) * 60;
	                        latd = (latb - floor(latb)) * 100 + 0.5;
	                        lonb = (lona - floor(lona)) * 60;
	                        lond = (lonb - floor(lonb)) * 100 + 0.5;
				sprintf(lat,"%02d%02d.%02d%c",(int)lata,(int)latb,(int)latd,latc);
				sprintf(lon,"%03d%02d.%02d%c",(int)lona,(int)lonb,(int)lond,lonc);
				report_aprs(ctg,lat,lon);
				lastupdate = now;
				my_update_secs = interval;
			}

		}
		if (fp) fclose(fp);
		sleep(10);
	}
	pthread_exit(NULL);
	return NULL;
}

static void *gps_tt_thread(void *data)
{
struct ast_config *cfg = NULL;
int i,j,ttlist,ttoffset,ttslot,myoffset;
char *ctg = (char *)data,gotfiledata,c;
char *val,*deflat,*deflon,latc,lonc,ttsplit,*ttlat,*ttlon;
char fname[200],lat[300],lon[300],buf[100],theircall[100],overlay;
FILE *fp,*fp1,*mfp;
unsigned int u;
float	mylat,lata,latb,latd;
float	mylon,lona,lonb,lond;
struct stat mystat;
time_t	now,was,lastupdate;
struct ttentry *ttentries,ttempty;


#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		pthread_exit(NULL);
		return NULL;
        }
	val = (char *) ast_variable_retrieve(cfg,ctg,"lat");	
	if (val) deflat = ast_strdup(val); else deflat = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"lon");	
	if (val) deflon = ast_strdup(val); else deflon = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"ttlat");	
	if (val) ttlat = ast_strdup(val); else ttlat = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"ttlon");	
	if (val) ttlon = ast_strdup(val); else ttlon = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"ttlist");	
	if (val) ttlist = atoi(val); else ttlist = DEFAULT_TTLIST;
	val = (char *) ast_variable_retrieve(cfg,ctg,"ttoffset");	
	if (val) ttoffset = atoi(val); else ttoffset = DEFAULT_TTOFFSET;
	val = (char *) ast_variable_retrieve(cfg,ctg,"ttsplit");	
	if (val) ttsplit = ast_true(val); else ttsplit = 0;


	mfp = NULL;
	if (!strcmp(ctg,"general"))
		strcpy(fname,TT_COMMON);
	else
		snprintf(fname,sizeof(fname) - 1,TT_SUB_COMMON,ctg);
	if (stat(fname,&mystat) == -1)
	{
		mfp = fopen(fname,"w");
		if (!mfp)
		{
			ast_log(LOG_ERROR,"Can not create aprstt common block file %s\n",fname);
			pthread_exit(NULL);
		}
		memset(&ttempty,0,sizeof(ttempty));
		for(i = 0; i < ttlist; i++)
		{
			if (fwrite(&ttempty,1,sizeof(ttempty),mfp) != sizeof(ttempty))
			{
				ast_log(LOG_ERROR,"Error initializing aprtss common block file %s\n",fname);
				fclose(mfp);
				pthread_exit(NULL);
			}
		}
		fclose(mfp);
		if (stat(fname,&mystat) == -1)
		{
			ast_log(LOG_ERROR,"Unable to stat new aprstt common block file %s\n",fname);
			pthread_exit(NULL);
		}
	}
	if (mystat.st_size < (sizeof(struct ttentry) * ttlist))
	{
		mfp = fopen(fname,"r+");
		if (!mfp)
		{
			ast_log(LOG_ERROR,"Can not open aprstt common block file %s\n",fname);
			pthread_exit(NULL);
		}
		memset(&ttempty,0,sizeof(ttempty));
		if (fseek(mfp,0,SEEK_END))
		{
			ast_log(LOG_ERROR,"Can not seek aprstt common block file %s\n",fname);
			pthread_exit(NULL);
		}
		for(i = mystat.st_size; i < (sizeof(struct ttentry) * ttlist); i += sizeof(struct ttentry))
		{
			if (fwrite(&ttempty,1,sizeof(ttempty),mfp) != sizeof(ttempty))
			{
				ast_log(LOG_ERROR,"Error growing aprtss common block file %s\n",fname);
				fclose(mfp);
				pthread_exit(NULL);
			}
		}
		fclose(mfp);
		if (stat(fname,&mystat) == -1)
		{
			ast_log(LOG_ERROR,"Unable to stat updated aprstt common block file %s\n",fname);
			pthread_exit(NULL);
		}
	}
	mfp = fopen(fname,"r+");
	if (!mfp)
	{
		ast_log(LOG_ERROR,"Can not open aprstt common block file %s\n",fname);
		pthread_exit(NULL);
	}
	ttentries = mmap(NULL,mystat.st_size,PROT_READ | PROT_WRITE,MAP_SHARED,fileno(mfp),0);
	if (ttentries == NULL)
	{
		ast_log(LOG_ERROR,"Cannot map aprtss common file %s!!\n",fname);
		pthread_exit(NULL);
	}
        ast_config_destroy(cfg);
        cfg = NULL; 
	time(&lastupdate);
	if (!strcmp(ctg,"general"))
		strcpy(fname,TT_PIPE);
	else
		snprintf(fname,sizeof(fname) - 1,TT_SUB_PIPE,ctg);
	mkfifo(fname,0666);
	fp1 = fopen(fname,"r");
	while(fp1 && run_forever)
	{
		if (fgets(buf,sizeof(buf) - 1,fp1) == NULL)
		{
			usleep(500000);
			continue;
		}
		strupr(buf);
		i = sscanf(buf,"%s %c",theircall,&overlay);
		if (i < 1) 
		{
			usleep(500000);
			continue;
		}
		if (i < 2) overlay = TT_DEFAULT_OVERLAY;
		time(&now);
		/* if we already have it, just update time */
		for(ttslot = 0; ttslot < ttlist; ttslot++)
		{
			if (!strcmp(theircall,ttentries[ttslot].call)) break;
		}
		if (ttslot < ttlist)
		{
			time(&ttentries[ttslot].t);
		}
		else /* otherwise, look for empty or timed-out */
		{
			for(ttslot = 0; ttslot < ttlist; ttslot++)
			{
				/* if empty */
				if (!ttentries[ttslot].call[0]) break;
				/* if timed-out */
				if ((ttentries[ttslot].t + TT_LIST_TIMEOUT) < now) break;
			}
			if (ttslot < ttlist)
			{
				ast_copy_string(ttentries[ttslot].call,theircall,sizeof(ttentries[ttslot].call) - 1);
				time(&ttentries[ttslot].t);
			}
			else
			{
				ast_log(LOG_WARNING,"APRSTT attempting to add call %s to full list (%d items)\n",theircall,ttlist);
				continue;
			}
		}
		msync(ttentries,mystat.st_size,MS_SYNC);
		if (ttsplit)
		{
			myoffset = ttoffset * ((ttslot >> 1) + 1);
			if (!(ttslot & 1)) myoffset = -myoffset;
		}
		else
		{
			myoffset = ttoffset * (ttslot + 1);
		}
		gotfiledata = 0;
		if (!strcmp(ctg,"general"))
			strcpy(fname,GPS_DATA_FILE);
		else
			snprintf(fname,sizeof(fname) - 1,GPS_SUB_FILE,ctg);
		fp = fopen(fname,"r");
		if (fp && (fstat(fileno(fp),&mystat) != -1) &&
			(mystat.st_size < 100))
		{
			if (fscanf(fp,"%u %d.%d%c %s",&u,&i,&j,&c,lon) == 5)
			{
				if (c == 'S')
				{
					i = -i;
				}
				if (i >= 0)
				{
					j -= myoffset;
				}
				else
				{
					j += myoffset;
				}
				i += (j / 60);
				if (j < 0)
					sprintf(lat,"%04d.%02d%c",(i >= 0) ? i : -i,-j % 60,(i >= 0) ? 'N' : 'S');
				else
					sprintf(lat,"%04d.%02d%c",(i >= 0) ? i : -i,j % 60,(i >= 0) ? 'N' : 'S');
				was = (time_t) u;
				if ((was + GPS_VALID_SECS) >= now)
				{
					gotfiledata = 1;
					report_aprstt(ctg,lat,lon,theircall,overlay);
				}
			}
		}
		if ((!gotfiledata) && deflat && deflon)
		{
			mylat = strtof((ttlat) ? ttlat : deflat,NULL);
			mylon = strtof((ttlon) ? ttlon : deflon,NULL);
			if (mylat >= 0.0)
			{
				mylat -= ((float) myoffset) * 0.00016666666666666666666666666666667;
			}
			else
			{
				mylat += ((float) myoffset) * 0.00016666666666666666666666666666667;
			}
			latc = (mylat >= 0.0) ? 'N' : 'S';
			lonc = (mylon >= 0.0) ? 'E' : 'W';
			lata = fabs(mylat);
			lona = fabs(mylon);
			latb = (lata - floor(lata)) * 60;
                        latd = (latb - floor(latb)) * 100 + 0.5;
                        lonb = (lona - floor(lona)) * 60;
                        lond = (lonb - floor(lonb)) * 100 + 0.5;
			sprintf(lat,"%02d%02d.%02d%c",(int)lata,(int)latb,(int)latd,latc);
			sprintf(lon,"%03d%02d.%02d%c",(int)lona,(int)lonb,(int)lond,lonc);
			report_aprstt(ctg,lat,lon,theircall,overlay);
			lastupdate = now;

		}
		if (fp) fclose(fp);
	}
	munmap(ttentries,mystat.st_size);
	if (mfp) fclose(mfp);
	pthread_exit(NULL);
	return NULL;
}


static int gps_exec(struct ast_channel *chan, void *data)
{
	return 0;
}

#ifdef	OLD_ASTERISK
int unload_module()
#else
static int unload_module(void)
#endif
{
int	res;

	run_forever = 0;
	res = ast_unregister_application(app);
	return res;
}

#ifndef	OLD_ASTERISK
static
#endif
int load_module(void)
{
	struct ast_config *cfg = NULL;
        char *ctg = "general",*val;
	int res;
	pthread_attr_t attr;

#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif

#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
                return AST_MODULE_LOAD_DECLINE;
        }
	val = (char *) ast_variable_retrieve(cfg,ctg,"comport");	
	if (val) comport = ast_strdup(val); else comport = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"lat");	
	if (val) general_deflat = ast_strdup(val); else general_deflat = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"lon");	
	if (val) general_deflon = ast_strdup(val); else general_deflon = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"elev");	
	if (val) general_defelev = ast_strdup(val); else general_defelev = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"server");	
	if (val) server = ast_strdup(val); else server = ast_strdup(GPS_DEFAULT_SERVER);
	val = (char *) ast_variable_retrieve(cfg,ctg,"port");	
	if (val) port = ast_strdup(val); else port = ast_strdup(GPS_DEFAULT_PORT);
	val = (char *) ast_variable_retrieve(cfg,ctg,"baudrate");	
	if (val)
	{
	    switch(atoi(val))
	    {
		case 2400:
			baudrate = B2400;
			break;
		case 4800:
			baudrate = B4800;
			break;
		case 19200:
			baudrate = B19200;
			break;
		case 38400:
			baudrate = B38400;
			break;
		case 57600:
			baudrate = B57600;
			break;
		default:
			ast_log(LOG_ERROR,"%s is not valid baud rate for iospeed\n",val);
			break;
	    }
	} else baudrate = GPS_DEFAULT_BAUDRATE;
	val = (char *) ast_variable_retrieve(cfg,ctg,"debug");	
	if (val) debug = ast_true(val);

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&aprs_thread,&attr,aprsthread,NULL);
        if (comport) ast_pthread_create(&gps_thread,&attr,gpsthread,NULL);
        ast_pthread_create(&gps_thread,&attr,gps_sub_thread,ast_strdup("general"));
        ast_pthread_create(&gps_thread,&attr,gps_tt_thread,ast_strdup("general"));
        while ( (ctg = ast_category_browse(cfg, ctg)) != NULL)
	{
		if (ctg == NULL) continue;
	        ast_pthread_create(&gps_thread,&attr,gps_sub_thread,ast_strdup(ctg));
	        ast_pthread_create(&gps_thread,&attr,gps_tt_thread,ast_strdup(ctg));
	}
        ast_config_destroy(cfg);
        cfg = NULL; 
	res = ast_register_application(app, gps_exec, synopsis, descrip);
	return res;
}

#ifdef	OLD_ASTERISK
char *description()
{
	return (char *)gps_tech.description;
}

int usecount()
{
	return usecnt;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
#else
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "GPS interface module");
#endif

