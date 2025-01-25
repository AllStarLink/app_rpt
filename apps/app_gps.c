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
	<support_level>extended</support_level>
 ***/

/*
 * app_gps is responsible for posting APRS (Automated Packet Reporting System) 
 * information to APRS-IS Internet servers. APRS is a registered trademark of 
 * Bob Bruninga, WB4APR (SK).
 *
 * The APRS-IS server requires a password to post status messages.  The password
 * is constructed as a hash on the call sign. This website can be used to 
 * to generate the password: https://n5dux.com/ham/aprs-passcode/ 
 *
 * app_gps can connect to a serial GPS receiver to get position information.  
 * If a GPS receiver is not configured, it can provide default position information 
 * entered in the gps.conf file.  It decodes the NEMA-0183 $GPGGA sentence.
 *
 * The $GPGGA sentence looks like the following:
 * $GPGGA,011530.00,3255.21780,N,08556.91695,W,2,06,3.45,217.4,M,-30.3,M,,0000*63
 *
 * Name	                  Example Data        Description
 * Sentence Identifier	  $GPGGA              Global Positioning System Fix Data
 * Time                   011530.00           01:15:30 UTC
 * Latitude               3255.21780          32.920297°N or 32° 55' 13.0692"N
 * Latitude direction	  N                   N = North or S = South
 * Longitude              08556.91695         85.948616°W or 85° 56' 55.0176"W
 * Longitude direction    W                   W = West or E = East
 * Fix Quality            2                   0 = Invalid (no fix), 1 = GPS fix, 2 = DGPS fix
 * Number of Satellites   06                  6 Satellites in view
 * Horizontal Precision   3.45                Relative accuracy of horizontal position
 * Altitude               217.4               217.4 meters above mean sea level
 * Altitude Unit		  M                   M = meters
 * Height of geoid        -30.3               -30.3 meters
 * Height Unit            M                   M = meters
 * Time since last update blank               No last update
 * DGPS reference         0000                No station id
 * Checksum               *63                 Checksum 
 * Terminator             [CR][LF]            Line terminator
 *
 * apps_gps supports standard APRS position updates, along with support for APRS Touchtone.
 * Standard updates are posted to 'APRS'.  APRStt updates are posted to 'APSTAR'.
 *
 * APRStt allows analog users to use DTMF to update the APRS system.  app_rpt can 
 * receive specially crafted DTMF strings, send those to app_gps through a named pipe,
 * for app_gps to post to the APRS-IS server.
 *
 * The recommended status message is:
 *
 * APRStt information - http://www.aprs.org/aprstt.html, http://www.aprs.org/aprstt/aprstt-user.txt
 *
 * The reporting interval (beacon rate) can be configured based on your needs.  
 * Beacon rates should be set as if the station was on a busy RF frequency.
 * Never faster than 1 minute for mobile, 5 minutes for weather, 10 minutes for local 
 * infrastructure , and 20 minutes for fixed stations).
 */

/* The following are the recognized APRS icon codes: 
 *
 * NOTE: Since the semicolon (';') is recognized by the Asterisk config 
 * subsystem as a comment, we use the question-mark ('?') instead when 
 * we want to specify a 'portable tent'.
 *
 * NOTE: The configuration setting icontable can be changed to select the alternate table.
 *
 * Code  Primary Table '/'  Alternate Table '\'
 * !     Police, Sheriff    EMERGENCY
 * "     Reserve            Reserved
 * #     DIGI               Numbered Star
 * $     Phone              Bank or ATM
 * %     DX Cluster
 * &     HF GATEway         Numbered Diamond
 * '     AIRCRAFT (small)   Crash site
 * (     CLOUDY             Cloudy
 * )     was Mic-Rptr
 * *     Snow               Snow
 * +     Red Cross          Church  
 * ,     reverse L shape
 * -     House QTH
 * .     X
 * /     Dot
 * 0-8   Numbered Circle    Numbered Circle
 * 9     Numeral Circle     Gas Station
 * :     FIRE               Hail
 * ?     Campground         Park/Picnic area (note different then standard ';' * )
 * <     Motorcycle         Advisory 
 * =     Railroad Engine
 * >     CAR (SSID * -9)    Numbered Car
 * 
 * @     HURRICANE or tropical storm Hurricane
 * A     Aid Station        Numbered Box
 * B     BBS                Blowing Snow
 * C     Canoe              Coast Guard
 * D                        Drizzle
 * E                        Smoke
 * F                        Freezing rain
 * G     Grid Square        Snow Shower
 * H     Hotel              Haze
 * I     TCP-IP             Rain Shower
 * J                        Lightning
 * K     School
 * L     avail              Lighthouse
 * M     MacAPRS
 * N     NTS Station        Navigation Buoy
 * O     BALLOON
 * P     Police             Parking
 * Q     TBD                Quake
 * R     RECREATIONAL VEHICLE   Restaurant
 * S     Space/Satellite    Satellite/Pacsat
 * T     Thunderstorm       Thunderstorm
 * U     BUS                Sunny
 * V     TBD                VORTAC Nav Aid
 * W     National WX Service Site  NWS Site W-R DIGI
 * X     HELO (SSID-6)      Pharmacy Rx
 * Y     YACHT (sail SSID-5)
 * Z     WinAPRS
 * 
 * [     RUNNER             Wall Cloud
 * \     TRIANGLE (DF)
 * ]     PBBS
 * ^     LARGE AIRCRAFT     Numbered Aircraft
 * _     WEATHER SURFACE CONDITIONS WX and W-R DIGI
 * `     Dish Antenna       Rain
 * a     AMBULANCE
 * b     BIKE               Blowing Dust/Sand
 * c     TBD
 * d     Dual Garage (Fire dept)  DX spot by callsign
 * e     Horse              Sleet
 * f     FIRE TRUCK         Funnel Cloud
 * g     Glider             GALE FLAGS
 * h     HOSPITAL           HAM Store
 * i     IOTA (islands on the air)
 * j     JEEP (SSID-12)     Workzone (Steam Shovel)
 * k     TRUCK (SSID-14)
 * l     Area Locations     Area Locations (box,circle,line,triangle) 
 * m     Mic-Repeater       MILEPOST (box displays 2 letters )
 * n     Node               Numbered Triangle
 * o     EOC                small circle
 * p     Rover Puppy        PARTLY CLOUDY
 * q     GRID SQUARE 
 * r     ANTENNA            Restrooms
 * s     SHIP (pwr boat SSID-8)  Numbered Ship
 * t     Truck Stop         TORNADO
 * u     TRUCK (18 wheeler) Numbered Truck
 * v     VAN (SSID-15)      Numbered Van
 * w     Water Station      FLOODING
 * x     xAPRS (Unix)
 * y     YAGI @ QTH
 * z 
 * {                        FOG
 * |     reserved (Stream Switch)
 * }     diamond 
 * ~     reserved (Stream Switch)
 * 
 */
 
 /* Power, Height, Gain, Dir (direction) codes (PHG)
  *   DIGITS   0   1   2    3    4    5    6     7     8     9  Units       
  *   -------------------------------------------------------------------
  *   POWER    0,  1,  4,   9,  16,  25,  36,   49,   64,   81  watts  
  *   HEIGHT  10, 20, 40,  80, 160, 320, 640, 1280, 2560, 5120  feet   
  *   GAIN     0,  1,  2,   3,   4,   5,   6,    7,    8,    9  dB
  *   DIR   omni, 45, 90, 135, 180, 225, 270,  315,  360,    .  deg   
  */  

#include "asterisk.h"

#include <sys/ioctl.h>
#include <termios.h>
#include <sys/mman.h>

#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/app.h"
#include "asterisk/cli.h"

/*** DOCUMENTATION
	<function name="GPS_READ" language="en_US">
		<synopsis>
			Read GPS position.
		</synopsis>
		<description>
			<para>Read current or default GPS position.
			Returns a string in the format epoch, space, latitude, spaces,
			longitude, spaces, and elevation.</para>
		</description>
	</function>
	<function name="APRS_SENDTT" language="en_US">
		<synopsis>
			Report a callsign to APRS.
		</synopsis>
		<syntax>
			<parameter name="section" required="true">
				<para>Section in gps.conf to use.</para>
			</parameter>
			<parameter name="overlay" required="true">
				<para>Overlay to report. Number 1 to 9.</para>
			</parameter>
		</syntax>
		<description>
			<para>Nothing is returned.</para>
				<example title="Sending a report.">
					exten => s,1,Set(APRS_SENDTT(general,1)=WB6NIL)
				</example>
		</description>
	</function>
 ***/

/*! Defines */
#define	APRS_DEFAULT_SERVER "rotate.aprs.net"
#define	APRS_DEFAULT_PORT "14580"
#define	APRS_DEFAULT_COMMENT "Asterisk/app_rpt Node"
#define	APRSTT_DEFAULT_COMMENT "Asterisk/app_rpt TT Report"
#define	APRSTT_DEFAULT_OVERLAY '0'
#define APRS_DEFAULT_ICON_TABLE '/'		/* primary table */
#define	APRS_DEFAULT_ICON '-'			/* house */
#define	DEFAULT_TTLIST 10
#define	DEFAULT_TTOFFSET 10
#define	TT_LIST_TIMEOUT 3600
#define	TT_COMMON "/tmp/aprs_ttcommon"
#define	TT_SUB_COMMON "/tmp/aprs_ttcommon_%s"
#define	GPS_DEFAULT_BAUDRATE B4800
#define	GPS_UPDATE_SECS 60
#define	GPS_VALID_SECS 60
#define	SERIAL_MAXMS 10000

/*!
 * \brief APRS TT entry.
 */
struct ttentry {
	char call[20];
	time_t last_updated;
};

AST_MUTEX_DEFINE_STATIC(aprs_socket_lock);
AST_MUTEX_DEFINE_STATIC(position_update_lock);

static char *config = "gps.conf";

/* Thread information */
static pthread_t gps_reader_thread_id = 0;
static pthread_t aprs_connection_thread_id = 0;
static int run_forever = 1;

/* Global configuration information */
static char *comport, *server, *port;
static int baudrate;
static int sockfd = -1;

/* Message flags */
static int gps_unlock_shown = 0;

/*!
 * \brief Position information structure
 */
struct position_info {
	unsigned int is_valid:1;	/* contains valid values indicator */
	char latitude[25];			/* latitude format DDMM.SSS */
	char longitude[25];			/* longitude format DDDMM.SSS */
	char elevation[25];			/* elevation format VVVV.V */
	time_t last_updated;		/* the time these values were last updated */
};

static struct position_info current_gps_position;
static struct position_info general_def_position;

/*!
 *\brief Enum for aprs sender info type.
 */
enum aprs_sender_type {
	APRS,
	APRSTT
};

/*!		
 * \brief Structure to track APRS and APRStt sender threads.
 */
struct aprs_sender_info {
	AST_LIST_ENTRY(aprs_sender_info) list;
	enum aprs_sender_type type;					/* sender type enum */
	char section[50]; 						/* section associated with this aprs thread */
	pthread_t thread_id;					/* thread id for this sender */
	ast_cond_t condition;					/* condition indicator for this sender */
	ast_mutex_t lock;						/* lock for condition */
	char their_call[50];					/* their callsign for processing */
	char overlay;							/* the overlay to use with the callsign */
};

AST_LIST_HEAD_NOLOCK_STATIC(aprs_sender_list, aprs_sender_info);


/*!
 * \brief Get system monotonic 
 * This returns the CLOCK_MONOTONIC time
 * \retval		Monotonic seconds.
 */
static int time_monotonic(void)
{
	struct timespec ts;
	
	clock_gettime(CLOCK_MONOTONIC, &ts);
	
	return ts.tv_sec;
}


/*!
 * \brief Break up a delimited string into a table of substrings
 *
 * \note This modifies the string str, be sure to save an intact copy if you need it later.
 *
 * \param str		Pointer to string to process (it will be modified).
 * \param strp		Pointer to a list of substrings created, NULL will be placed at the end of the list.
 * \param limit		Maximum number of substrings to process.
 * \param delim		Specified delimiter
 * \param quote		User specified quote for escaping a substring. Set to zero to escape nothing.
 *
 * \retval 			Returns number of substrings found.
 */
static int explode_string(char *str, char *strp[], int limit, char delim, char quote)
{
	int i, l, inquo;

	inquo = 0;
	i = 0;
	strp[i++] = str;
	if (!*str) {
		strp[0] = 0;
		return 0;
	}
	for (l = 0; *str && (l < limit); str++) {
		if (quote) {
			if (*str == quote) {
				if (inquo) {
					*str = 0;
					inquo = 0;
				} else {
					strp[i - 1] = str + 1;
					inquo = 1;
				}
			}
		}
		if ((*str == delim) && (!inquo)) {
			*str = 0;
			l++;
			strp[i++] = str + 1;
		}
	}
	strp[i] = 0;
	return i;
}

/*!
 * \brief Read one character from serial device.
 *
 * Read one character from the serial device.  Timeout after SERIAL_MAXMS.
 *
 * \param fd		File descriptor to read.
 *
 * \retval 			Character read, 0 if timed out, or -1 on select error.
 */
static int getserialchar(int fd)
{
	int res;
	char c;
	int i;
	ast_fdset fds;
	struct timeval tv;

	for (i = 0; (i < (SERIAL_MAXMS / 100)) && run_forever; i++) {
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
		FD_ZERO(&fds);
		FD_SET(fd, &fds);
		res = ast_select(fd + 1, &fds, NULL, NULL, &tv);
		if (res < 0) {
			return -1;
		}
		if (res) {
			if (read(fd, &c, 1) < 1) {
				ast_debug(1, "Read error: %s", strerror(errno));
				return -1;
			}
			return c;
		}
	}
	return 0;
}

/*!
 * \brief Get a line of characters from serial device.
 *
 * Get one line of characters from the serial device.  Timeout after SERIAL_MAXMS.
 *
 * \param fd		File descriptor to read.
 * \param str		Pointer to string buffer.
 * \param max		Maximum characters to read.
 *
 * \retval 			Number of characters read, 0 for time out or -1 for error.
 */

static int getserialline(int fd, char *str, int max)
{
	int i;
	char c;

	for (i = 0; (i < max) && run_forever; i++) {
		c = getserialchar(fd);
		/* See if we timed out or received an error */
		if (c < 1) {
			return c;
		}
		if ((i == 0) && (c < ' ')) {
			i--;
			continue;
		}
		/* Any character < ' ' indicates the end of line */
		if (c < ' ') {
			break;
		}
		str[i] = c;
	}
	str[i] = 0;
	
	return i;
}

/*!
 * \brief APRS connection thread.
 * This thread opens and maintains a TCP/IP connection to the APRS-IS server.
 * It logs into the server using the call sign and password specified 
 * in the configuration.
 *
 * Data sent from the APRS-IS server is read and discarded.
 *
 * In the event that the connection is lost, the routine
 * will automatically attempt to reconnect.
 *
 * \param data		Pointer to data (nothing passed)
 */
static void *aprs_connection_thread(void *data)
{
	char *call, *password, *val, buf[300];
	struct ast_config *cfg = NULL;
	struct ast_flags zeroflag = { 0 };
	struct ast_sockaddr addr = { {0,} };
	struct pollfd fds[1];
	int res;
	
	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		pthread_exit(NULL);
		return NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, "general", "call");
	if (val) {
		call = ast_strdupa(val);
	} else {
		call = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, "general", "password");
	if (val) {
		password = ast_strdupa(val);
	} else {
		password = NULL;
	}
	/* Verify that we have a callsign and password */
	if ((!call) || (!password)) {
		ast_log(LOG_ERROR, "You must specify call and password\n");
		pthread_exit(NULL);
		return NULL;
	}
	ast_config_destroy(cfg);
	cfg = NULL;
	
	while (run_forever) {
		ast_mutex_lock(&aprs_socket_lock);
		/* See the socket is open.  Close it so that it can be reopened. */
		if (sockfd > -1) {
			close(sockfd);
			sockfd = -1;
		}
		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0) {
			ast_log(LOG_ERROR, "Error opening socket: %s\n", strerror(errno));
			ast_mutex_unlock(&aprs_socket_lock);
			/* Wait 1 second before trying again. */
			sleep(1);
			continue;
		}
		
		if (ast_sockaddr_resolve_first_af(&addr, server, PARSE_PORT_IGNORE, AST_AF_INET)) {
			ast_log(LOG_WARNING, "Server %s cannot be found!\n", server);
			ast_mutex_unlock(&aprs_socket_lock);
			/* Wait 1 second before trying again. */
			sleep(1);
			continue;
		}
		ast_sockaddr_set_port(&addr, atoi(port));
		
		if (ast_connect(sockfd, &addr) < 0) {
			ast_log(LOG_WARNING, "Cannot connect to server %s. Error: %s\n", server, strerror(errno));
			ast_mutex_unlock(&aprs_socket_lock);
			/* Wait 1 second before trying again. */
			sleep(1);
			continue;
		}

		/* Log into the APRS-IS server */
		sprintf(buf, "user %s pass %s vers Asterisk app_gps_V3\n", call, password);
		
		if (send(sockfd, buf, strlen(buf), 0) < 0) {
			ast_log(LOG_WARNING, "Can not send sign on to server: %s\n", strerror(errno));
			ast_mutex_unlock(&aprs_socket_lock);
			continue;
		}
		ast_debug(1, "Sent packet(login): %s", buf);
		ast_mutex_unlock(&aprs_socket_lock);
		
		memset(&fds, 0, sizeof(fds));
		fds[0].fd = sockfd;
		fds[0].events = POLLIN;
		
		/* Consume the received data from the APRS-IS server.
		 * We do not use the returned information at this time.
		 */
		 while (run_forever) {
			/*
			 * poll for activity
			 * time out after 500ms
			 */
			res = ast_poll(fds, 1, 500);
			if (res == 0) {
				continue;
			}
			if (res < 0 || fds[0].revents & POLLHUP) {
				break;
			}
			if (fds[0].revents & POLLIN) {
				if (recv(sockfd, buf, sizeof(buf) - 1, 0) > 0) {
					ast_debug(4, "APRS-IS: %s\n", buf);
				}
			}
		 }
	}
	
	close(sockfd);
	sockfd = -1;
	
	ast_debug(2, "%s has exited\n", __FUNCTION__);
	return NULL;
}

/*!
 * \brief Report APRS information.
 * This routine send an APRS position report to the APRS-IS server.
 *
 * Message type 'position without timestamp'.
 * Data extension PHG 'Station Power and Effective Antenna Height/Gain/Directivity'
 * Optionally includes elevation.
 *
 * \param ctg		Pointer to configuration section to process
 * \param lat		Pointer to latitude to report
 * \param lon		Pointer to longitude to report
 * \param elev		Pointer to elevation to report
 *
 * \retval 0		Success
 * \retval -1		Failure
 */
static int report_aprs(char *ctg, char *lat, char *lon, char *elev)
{
	struct ast_config *cfg = NULL;
	char *call, *comment, icon, icon_table;
	char power, height, gain, dir, *val, servercall[20], buf[350], *cp;
	struct ast_flags zeroflag = { 0 };
	char elev_str[25];
	float elev_f;

	call = NULL;
	comment = NULL;
	
	/* Load the configuration settings for the section requested */
	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		return -1;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "call");
	if (val) {
		call = ast_strdupa(val);
	} else {
		call = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "comment");
	if (val) {
		comment = ast_strdupa(val);
	} else {
		comment = ast_strdupa(APRS_DEFAULT_COMMENT);
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "power");
	if (val) {
		power = (char) strtol(val, NULL, 0);
	} else {
		power = 0;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "height");
	if (val) {
		height = (char) strtol(val, NULL, 0);
	} else {
		height = 0;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "gain");
	if (val) {
		gain = (char) strtol(val, NULL, 0);
	} else {
		gain = 0;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "dir");
	if (val) {
		dir = (char) strtol(val, NULL, 0);
	} else {
		dir = 0;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "icon");
	if (val && *val) {
		icon = *val;
	} else {
		icon = APRS_DEFAULT_ICON;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "icontable");
	if (val && *val) {
		icon_table = *val;
	} else {
		icon_table = APRS_DEFAULT_ICON_TABLE;
	}
	ast_config_destroy(cfg);
	cfg = NULL;
	
	/* Remap '?' to ';' due to Asterisk config limitation on using ';' (; = portable tent) */
	if (icon == '?') {
		icon = ';';	
	}

	/* We must have a callsign to report information */
	if (!call) {
		ast_log(LOG_ERROR, "You must configure a callsign\n");
		return -1;
	}

    /* Setup the server call sign 
	 * If the SID is a single character, append 'S' 
	 * If there is no SID, append '-VS'
	 */
	ast_copy_string(servercall, call, sizeof(servercall));
	cp = strchr(servercall, '-');
	if (cp) {
		if (strlen(cp) == 2) {
			strcat(servercall, "S");
		}
	} else {
		strcat(servercall, "-VS");
	}
	
	/* Reduce the precision of latitude and longitude
	 *
	 * Latitude is expressed as a fixed 8-character field, in degrees and decimal
	 * minutes (to two decimal places), followed by the letter N for north or 
	 * S for south.
	 * Longitude is expressed as a fixed 9-character field, in degrees and decimal
	 * minutes (to two decimal places), followed by the letter E for east or 
	 * W for west.
	 */
	cp = strchr(lat, '.');
	if (cp && (strlen(cp) >= 3)) {
		*(cp + 3) = lat[strlen(lat) - 1];
		*(cp + 4) = 0;
	}
	cp = strchr(lon, '.');
	if (cp && (strlen(cp) >= 3)) {
		*(cp + 3) = lon[strlen(lon) - 1];
		*(cp + 4) = 0;
	}
	
	/* Setup optional elevation */
	elev_f = 0;
	sscanf(elev, "%f", &elev_f);
	if (elev_f > 0) {
		snprintf(elev_str, sizeof(elev_str), "/A=%06.0f", elev_f * 3.28);
	} else {
		elev_str[0] = '\0';
	}

	snprintf(buf, sizeof(buf), "%s>APSTAR,TCPIP*,qAC,%s:!%s%c%s%cPHG%d%d%d%d%s%s\r\n",
			call, servercall, lat, icon_table, lon, icon, power, height, gain, dir, elev_str, comment);

	ast_mutex_lock(&aprs_socket_lock);
	
	if (sockfd == -1) {
		ast_log(LOG_WARNING, "Attempt to send APRS data with no connection open!\n");
		ast_mutex_unlock(&aprs_socket_lock);
		return -1;
	}
	if (send(sockfd, buf, strlen(buf), 0) < 0) {
		ast_log(LOG_WARNING, "Can not send APRS (GPS) data: %s\n", strerror(errno));
		ast_mutex_unlock(&aprs_socket_lock);
		return -1;
	}
	
	ast_debug(1, "Sent packet(%s): %s", ctg, buf);
	ast_mutex_unlock(&aprs_socket_lock);
	
	return 0;
}

/*!
 * \brief Report APRStt information.
 * This routine send an APRStt position report to the APRS-IS server.
 *
 * Message type 'object'
 * The call sign being reported is shown in APRS as an object with
 * a SSID of '-12'.
 *
 * \param ctg		Pointer to configuration section to process
 * \param lat		Pointer to latitude to report
 * \param lon		Pointer to longitude to report
 * \param theircall	Pointer to the received callsign to report
 * \param overlay	The overlay character to use
 *
 * \retval 0		Success
 * \retval -1		Failure
 */

static int report_aprstt(char *ctg, char *lat, char *lon, char *theircall, char overlay)
{
	struct ast_config *cfg = NULL;
	char *call, *comment;
	char *val, basecall[20], buf[300], buf1[100], *cp;
	time_t t;
	struct tm *tm;
	struct ast_flags zeroflag = { 0 };

	call = NULL;
	comment = NULL;
	
	/* Load the configuration settings for the section requested */
	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		return -1;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "call");
	if (val) {
		call = ast_strdupa(val);
	} else {
		call = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "ttcomment");
	if (val) {
		comment = ast_strdupa(val);
	} else {
		comment = ast_strdupa(APRSTT_DEFAULT_COMMENT);
	}
	ast_config_destroy(cfg);
	cfg = NULL;

	/* We must have a callsign to report information */
	if (!call) {
		ast_log(LOG_ERROR, "You must configure a callsign\n");
		return -1;
	}

	/* Get the base callsign - everything before the '-' */
	ast_copy_string(basecall, call, sizeof(basecall));
	cp = strchr(basecall, '-');
	if (cp) {
		*cp = 0;
	}
	
	/* Reduce the precision of latitude and longitude
	 *
	 * Latitude is expressed as a fixed 8-character field, in degrees and decimal
	 * minutes (to two decimal places), followed by the letter N for north or 
	 * S for south.
	 * Longitude is expressed as a fixed 9-character field, in degrees and decimal
	 * minutes (to two decimal places), followed by the letter E for east or 
	 * W for west.
	 */
	cp = strchr(lat, '.');
	if (cp && (strlen(cp) >= 3)) {
		*(cp + 3) = lat[strlen(lat) - 1];
		*(cp + 4) = 0;
	}
	cp = strchr(lon, '.');
	if (cp && (strlen(cp) >= 3)) {
		*(cp + 3) = lon[strlen(lon) - 1];
		*(cp + 4) = 0;
	}
	
	t = time(NULL);
	tm = gmtime(&t);
	
	sprintf(buf1, "%s-12", theircall);
	sprintf(buf, "%s>APSTAR:;%-9s*%02d%02d%02dz%s%c%sA%s\r\n",
			call, buf1, tm->tm_hour, tm->tm_min, tm->tm_sec, lat, overlay, lon, comment);
	
	ast_mutex_lock(&aprs_socket_lock);
		
	if (sockfd == -1) {
		ast_log(LOG_WARNING, "Attempt to send APRS (APSTAR) data with no connection open!\n");
		ast_mutex_unlock(&aprs_socket_lock);
		return -1;
	}
	
	if (send(sockfd, buf, strlen(buf), 0) < 0) {
		ast_log(LOG_WARNING, "Can not send APRS (APSTAR) data: %s\n", strerror(errno));
		ast_mutex_unlock(&aprs_socket_lock);
		return -1;
	}
	
	ast_debug(1, "Sent packet(%s): %s", ctg, buf);
	ast_mutex_unlock(&aprs_socket_lock);
	
	return 0;
}
/*!
 * \brief Convert latitude in decimal to DMS string.
 *
 * \param dec		Latitude in decimal
 * \param value		Pointer to buffer to receive the value
 * \param len		Length of the value buffer
 */
static void lat_decimal_to_DMS(float dec, char *value, int len)
{
	char direction;
	float lata, latb, latd;

	direction = (dec >= 0.0) ? 'N' : 'S';
	lata = fabs(dec);
	latb = (lata - floor(lata)) * 60;
	latd = (latb - floor(latb)) * 100 + 0.5;
	snprintf(value, len, "%02d%02d.%02d%c", (int) lata, (int) latb, (int) latd, direction);
}
/*!
 * \brief Convert longitude in decimal to DMS string.
 *
 * \param dec		Longitude in decimal
 * \param value		Pointer to buffer to receive the value
 * \param len		Length of the value buffer
 */
static void lon_decimal_to_DMS(float dec, char *value, int len)
{
	char direction;
	float lona, lonb, lond;

	direction = (dec >= 0.0) ? 'E' : 'W';
	lona = fabs(dec);
	lonb = (lona - floor(lona)) * 60;
	lond = (lonb - floor(lonb)) * 100 + 0.5;
	snprintf(value, len, "%03d%02d.%02d%c", (int) lona, (int) lonb, (int) lond, direction);
}

/*!
 * \brief GPS device processing thread.
 * This routine continously reads and parses the serial GPS data.
 *
 * The position information is made available through the global 
 * current_gps_position structure.
 *
 * \param data		Pointer to data (nothing passed)
 */
static void *gps_reader(void *data)
{
	char buf[300], c, *strs[100];
	int res, i, n, fd, has_comport = 0;
	struct termios mode;
	struct position_info *selected_info;
	time_t now;

	if (comport) {
		has_comport = 1;
	} else {
		comport = "/dev/null";
	}

	/* Open the serial port configured for the GPS device */
	fd = open(comport, O_RDWR);
	if (fd == -1) {
		ast_log(LOG_WARNING, "Cannot open serial port %s: %s\n", comport, strerror(errno));
		goto err;
	}

	if (has_comport) {
		memset(&mode, 0, sizeof(mode));
		if (tcgetattr(fd, &mode)) {
			ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", comport, strerror(errno));
			close(fd);
			goto err;
		}
#ifndef	SOLARIS
		cfmakeraw(&mode);
#else
		mode.c_iflag &= ~(IGNBRK | BRKINT | PARMRK | ISTRIP | INLCR | IGNCR | ICRNL | IXON);
		mode.c_lflag &= ~(ECHO | ECHONL | ICANON | ISIG | IEXTEN);
		mode.c_cflag &= ~(CSIZE | PARENB | CRTSCTS);
		mode.c_cflag |= CS8;
		mode.c_cc[VTIME] = 3;
		mode.c_cc[VMIN] = 1;
#endif

		cfsetispeed(&mode, baudrate);
		cfsetospeed(&mode, baudrate);
		if (tcsetattr(fd, TCSANOW, &mode)) {
			ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", comport, strerror(errno));
			close(fd);
			goto err;
		}
	}
	
	/* Give the device a few milliseconds to come on-line */
	usleep(100000);

	/*! \todo we need to deal with someone unplugging the device */
	
	while (run_forever) {
		
		/* Read a line from the serial port */
		res = getserialline(fd, buf, sizeof(buf) - 1);
		if (res < 0) {
			ast_log(LOG_ERROR, "GPS fatal error!\n");
			continue;
		}
		if (!res) {
			ast_mutex_lock(&position_update_lock);
			current_gps_position.is_valid = 0;
			ast_mutex_unlock(&position_update_lock);
			/* 
			 * A timeout has occurred.  No data received from the GPS device.
			 * If we don't have default position information, report the timeout.
			 */
			if (!general_def_position.is_valid) {
				ast_log(LOG_WARNING, "GPS timeout!\n");
				continue;
			}
			
			ast_log(LOG_WARNING, "GPS timeout -- Using default (fixed location) parameters instead\n");
			
			selected_info = &general_def_position;
			
		} else {
			now = time_monotonic();
			/* Check for no data receiption */
			if (current_gps_position.last_updated + GPS_VALID_SECS < now) {
				ast_mutex_lock(&position_update_lock);
				current_gps_position.is_valid = 0;
				ast_mutex_unlock(&position_update_lock);
			}
			/* Validate the GPS data */
			if (buf[0] != '$') {
				ast_log(LOG_WARNING, "GPS Invalid data format (no '$' at beginning)\n");
				continue;
			}
			/* Calculate the check sum */
			c = 0;
			for (i = 1; buf[i]; i++) {
				if (buf[i] == '*')
					break;
				c ^= buf[i];
			}
			if ((!buf[i]) || (strlen(buf) < (i + 3))) {
				ast_log(LOG_WARNING, "GPS Invalid data format (checksum format)\n");
				continue;
			}
			if ((sscanf(buf + i + 1, "%x", &i) != 1) || (c != i)) {
				ast_log(LOG_WARNING, "GPS Invalid checksum\n");
				continue;
			}
			
			n = explode_string(buf, strs, 100, ',', '\"');
			if (!n) {
				ast_log(LOG_WARNING, "GPS Invalid data format (no data)\n");
				continue;
			}
			/* We only process the $GPGGA sentence */
			if (strcasecmp(strs[0], "$GPGGA")) {
				continue;
			}
			if (n != 15) {
				ast_log(LOG_WARNING, "GPS Invalid data format (invalid format for GGA record)\n");
				continue;
			}
			/* See if the GPS is locked */
			if (*strs[6] < '1') {
				if (!gps_unlock_shown) {
					ast_log(LOG_WARNING, "GPS data not available (signal not locked)\n");
					gps_unlock_shown = 1;
				}
				continue;
			}
			
			/* If we have been unlocked, let them know that we are locked */
			if (gps_unlock_shown) {
				ast_log(LOG_NOTICE, "GPS locked\n");
				gps_unlock_shown = 0;
			}
			
			ast_mutex_lock(&position_update_lock);
			current_gps_position.is_valid = 1;
			snprintf(current_gps_position.latitude, sizeof(current_gps_position.latitude) - 1, "%s%s", strs[2], strs[3]);
			snprintf(current_gps_position.longitude, sizeof(current_gps_position.longitude) - 1, "%s%s", strs[4], strs[5]);
			snprintf(current_gps_position.elevation, sizeof(current_gps_position.elevation) - 1, "%s%s", strs[9], strs[10]);
			current_gps_position.last_updated = time(NULL);
			ast_mutex_unlock(&position_update_lock);
			
			selected_info = & current_gps_position;
		}
		
		ast_debug(5, "Got latitude: %s, longitude: %s, elevation: %s from: %s\n", 
			selected_info->latitude, selected_info->longitude, selected_info->elevation,
			(selected_info == &current_gps_position) ? "GPS" : "Default");
	}
	
	if (fd != -1) {
		close(fd);
	}
	
err:
	ast_debug(2, "%s has exited\n", __FUNCTION__);
	return NULL;
}

/*!
 * \brief APRS sender thread.
 * The routine sends the packet report based on the configured
 * interval (beacon time).
 *
 * This thread is setup initially for the [general] section.
 * If other sections are present in the configuration file,
 * additional threads will be created passing in the name
 * of the respective section.
 *
 * \param data		Pointer to aprs_sender_info struct.
 */
static void *aprs_sender_thread(void *data)
{
	struct ast_config *cfg = NULL;
	char *ctg;
	char *val, *deflat, *deflon, *defelev;
	int interval, my_update_secs, ehlert;
	time_t now, lastupdate;
	struct ast_flags zeroflag = { 0 };
	struct position_info this_def_position, selected_position;
	struct aprs_sender_info *sender_entry = data;
	
	ctg = ast_strdupa(sender_entry->section);
	
	ast_debug(2, "Starting aprs sender thread: %s\n", ctg);

	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		pthread_exit(NULL);
		return NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "lat");
	if (val) {
		deflat = ast_strdupa(val);
	} else {
		deflat = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "lon");
	if (val) {
		deflon = ast_strdupa(val);
	} else {
		deflon = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "elev");
	if (val) {
		defelev = ast_strdupa(val);
	} else {
		defelev = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "interval");
	if (val) {
		interval = atoi(val);
	} else {
		interval = GPS_UPDATE_SECS;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "ehlert");
	if (val) {
		ehlert = ast_true(val);
	} else {
		ehlert = 0;
	}
	ast_config_destroy(cfg);
	cfg = NULL;
	/*
	 * Set the default position for this section
	 * If it is [general], we already have the defaults
	 * otherwise, we need to build the specific defaults
	 * for this section.
	 */
	 if (!strcmp(ctg, "general") || (!deflat && !deflon)) {
		 this_def_position = general_def_position;
	 } else {
		this_def_position.is_valid = 1;
		lat_decimal_to_DMS(strtof(deflat, NULL), this_def_position.latitude, sizeof(this_def_position.latitude));
		lon_decimal_to_DMS(strtof(deflon, NULL), this_def_position.longitude, sizeof(this_def_position.longitude));
		/* See if we have a default elevation */
		if (defelev) {
			float eleva, elevd;
			eleva = strtof(defelev, NULL);
			elevd = (eleva - floor(eleva)) * 10 + 0.5;
			snprintf(this_def_position.elevation, sizeof(this_def_position.elevation), "%03d.%1d", (int) eleva, (int) elevd);
		} else {
			strcpy(this_def_position.elevation, "000.0");
		}
	 }
	
	memset(&selected_position, 0, sizeof(selected_position));
	lastupdate = time_monotonic();
	my_update_secs = GPS_UPDATE_SECS;
	
	while (run_forever) {
		now = time_monotonic();
		
		ast_mutex_lock(&position_update_lock);
		selected_position.is_valid = 0;
		/* See if we need to send live GPS or the default */
		if (current_gps_position.is_valid) {
			selected_position = current_gps_position;
		} else if (this_def_position.is_valid && !ehlert) {
				selected_position = this_def_position;
				selected_position.last_updated = time(NULL);
		}
		ast_mutex_unlock(&position_update_lock);
		/* 
		 * See if it is time to send the position report
		 * The last_updated time must be current so that
		 * we know we are getting good GPS information.
		 */
		if (selected_position.is_valid && (selected_position.last_updated + GPS_VALID_SECS) >= time(NULL) &&
			now >= (lastupdate + my_update_secs)) {
			report_aprs(ctg, selected_position.latitude, selected_position.longitude, selected_position.elevation);
			lastupdate = now;
			my_update_secs = interval;
		}
		/* wait 1 second */
		sleep(1);
	}
	ast_debug(2, "%s has exited\n", __FUNCTION__);
	return NULL;
}

/*!
 * \brief APRStt (touch tone) processing thread.
 * The routine sends the touch tone packet report.
 *
 * This thread is setup initially for the [general] section.
 * If other sections are present in the configuration file,
 * additional threads will be created passing in the name
 * of the respective section.
 *
 * \param data		Pointer to aprs_sender_info struct.
 */
static void *aprstt_sender_thread(void *data)
{
	struct ast_config *cfg = NULL;
	struct ast_flags zeroflag = { 0 };
	int i, j, ttlist, ttoffset, ttslot, myoffset;
	char *ctg, c;
	char *val, *deflat, *deflon, *defelev, ttsplit, *ttlat, *ttlon;
	char fname[200], lat[25], theircall[20], overlay;
	FILE *mfp;
	struct stat mystat;
	time_t now;
	struct ttentry *ttentries, ttempty;
	struct position_info this_def_position, selected_position;
	struct timeval tv;
	struct timespec ts = {0};
	struct aprs_sender_info *sender_entry = data;
	
	ctg = ast_strdupa(sender_entry->section);
	
	ast_debug(2, "Starting aprstt sender thread: %s\n", ctg);

	/* Load our configuration */
	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		pthread_exit(NULL);
		return NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "lat");
	if (val) {
		deflat = ast_strdupa(val);
	} else {
		deflat = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "lon");
	if (val) {
		deflon = ast_strdupa(val);
	} else {
		deflon = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "elev");
	if (val) {
		defelev = ast_strdupa(val);
	} else {
		defelev = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "ttlat");
	if (val) {
		ttlat = ast_strdupa(val);
	} else {
		ttlat = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "ttlon");
	if (val) {
		ttlon = ast_strdupa(val);
	} else {
		ttlon = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "ttlist");
	if (val) {
		ttlist = atoi(val);
	} else {
		ttlist = DEFAULT_TTLIST;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "ttoffset");
	if (val) {
		ttoffset = atoi(val);
	} else {
		ttoffset = DEFAULT_TTOFFSET;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "ttsplit");
	if (val) {
		ttsplit = ast_true(val);
	} else {
		ttsplit = 0;
	}
	ast_config_destroy(cfg);
	cfg = NULL;

	/*
	 * Set the default position for this section
	 * If it is [general], we already have the defaults
	 * otherwise, we need to build the specific defaults
	 * for this section.
	 */
	 if (!strcmp(ctg, "general") || (!deflat && !deflon)) {
		 this_def_position = general_def_position;
	 } else {
		this_def_position.is_valid = 1;
		lat_decimal_to_DMS(strtof((ttlat ? ttlat : deflat), NULL), this_def_position.latitude, sizeof(this_def_position.latitude));
		lon_decimal_to_DMS(strtof((ttlon? ttlon : deflon), NULL), this_def_position.longitude, sizeof(this_def_position.longitude));
		/* See if we have a default elevation */
		if (defelev) {
			float eleva, elevd;
			eleva = strtof(defelev, NULL);
			elevd = (eleva - floor(eleva)) * 10 + 0.5;
			snprintf(this_def_position.elevation, sizeof(this_def_position.elevation), "%03d.%1d", (int) eleva, (int) elevd);
		} else {
			strcpy(this_def_position.elevation, "000.0");
		}
	 }

	/*
	 * Open the common block file for this section.
	 * We will store the callsign and last update time.
	 */
	mfp = NULL;
	if (!strcmp(sender_entry->section, "general")) {
		strcpy(fname, TT_COMMON);
	} else { 
		snprintf(fname, sizeof(fname) - 1, TT_SUB_COMMON, sender_entry->section);
	}
	if (stat(fname, &mystat) == -1) {
		mfp = fopen(fname, "w");
		if (!mfp) {
			ast_log(LOG_ERROR, "Can not create aprstt common block file %s: %s\n", fname, strerror(errno));
			pthread_exit(NULL);
		}
		memset(&ttempty, 0, sizeof(ttempty));
		for (i = 0; i < ttlist; i++) {
			if (fwrite(&ttempty, 1, sizeof(ttempty), mfp) != sizeof(ttempty)) {
				ast_log(LOG_ERROR, "Error initializing aprtss common block file %s: %s\n", fname, strerror(errno));
				fclose(mfp);
				pthread_exit(NULL);
			}
		}
		fclose(mfp);
		if (stat(fname, &mystat) == -1) {
			ast_log(LOG_ERROR, "Unable to stat new aprstt common block file %s: %s\n", fname, strerror(errno));
			pthread_exit(NULL);
		}
	}
	if (mystat.st_size < (sizeof(struct ttentry) * ttlist)) {
		mfp = fopen(fname, "r+");
		if (!mfp) {
			ast_log(LOG_ERROR, "Can not open aprstt common block file %s: %s\n", fname, strerror(errno));
			pthread_exit(NULL);
		}
		memset(&ttempty, 0, sizeof(ttempty));
		if (fseek(mfp, 0, SEEK_END)) {
			ast_log(LOG_ERROR, "Can not seek aprstt common block file %s: %s\n", fname, strerror(errno));
			pthread_exit(NULL);
		}
		for (i = mystat.st_size; i < (sizeof(struct ttentry) * ttlist); i += sizeof(struct ttentry)) {
			if (fwrite(&ttempty, 1, sizeof(ttempty), mfp) != sizeof(ttempty)) {
				ast_log(LOG_ERROR, "Error growing aprtss common block file %s: %s\n", fname, strerror(errno));
				fclose(mfp);
				pthread_exit(NULL);
			}
		}
		fclose(mfp);
		if (stat(fname, &mystat) == -1) {
			ast_log(LOG_ERROR, "Unable to stat updated aprstt common block file %s: %s\n", fname, strerror(errno));
			pthread_exit(NULL);
		}
	}
	mfp = fopen(fname, "r+");
	if (!mfp) {
		ast_log(LOG_ERROR, "Can not open aprstt common block file %s: %s\n", fname, strerror(errno));
		pthread_exit(NULL);
	}
	ttentries = mmap(NULL, mystat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fileno(mfp), 0);
	if (ttentries == NULL) {
		ast_log(LOG_ERROR, "Cannot map aprtss common file %s: %s\n", fname, strerror(errno));
		pthread_exit(NULL);
	}
	
	while (run_forever) {
		/* Wait for the aprs_sendtt function to give us data or time out after 500ms */
		ast_mutex_lock(&sender_entry->lock);
		
		tv = ast_tvadd(ast_tvnow(), ast_samp2tv(500,1000));	/* Setup the time value for 500ms from now */
		ts.tv_sec = tv.tv_sec;
		ts.tv_nsec = tv.tv_usec * 1000;
		ast_cond_timedwait(&sender_entry->condition, &sender_entry->lock, &ts);

		ast_mutex_unlock(&sender_entry->lock);

		/* Make sure we have some data to process - if we did not get anything we could have timedout */
		if (ast_strlen_zero(sender_entry->their_call)) {
			continue;
		}
		
		ast_copy_string(theircall, sender_entry->their_call, sizeof(theircall));
		sender_entry->their_call[0] = '\0';
		overlay = sender_entry->overlay;

		ast_str_to_upper(theircall);
		if (overlay < '0') {
			overlay = APRSTT_DEFAULT_OVERLAY;
		}
		
		now = time(NULL);
		
		/* if we already have it, just update time */
		for (ttslot = 0; ttslot < ttlist; ttslot++) {
			if (!strcmp(theircall, ttentries[ttslot].call)) {
				break;
			}
		}
		if (ttslot < ttlist) {
			ttentries[ttslot].last_updated = now;
		} else {				
			/* otherwise, look for empty or timed-out */
			for (ttslot = 0; ttslot < ttlist; ttslot++) {
				/* if empty */
				if (!ttentries[ttslot].call[0]) {
					break;
				}
				/* if timed-out */
				if ((ttentries[ttslot].last_updated + TT_LIST_TIMEOUT) < now) {
					break;
				}
			}
			if (ttslot < ttlist) {
				ast_copy_string(ttentries[ttslot].call, theircall, sizeof(ttentries[ttslot].call) - 1);
				ttentries[ttslot].last_updated = now;
			} else {
				ast_log(LOG_WARNING, "APRStt attempting to add call %s to full list (%d items)\n", theircall, ttlist);
				continue;
			}
		}
		/* Sync the entries to the file */
		msync(ttentries, mystat.st_size, MS_SYNC);
		
		/* Center tt reports around the origin */
		if (ttsplit) {
			myoffset = ttoffset * ((ttslot >> 1) + 1);
			if (!(ttslot & 1)) {
				myoffset = -myoffset;
			}
		} else {
			myoffset = ttoffset * (ttslot + 1);
		}
						
		ast_mutex_lock(&position_update_lock);
		selected_position.is_valid = 0;
		/* See if we need to send live GPS or the default */
		if (current_gps_position.is_valid) {
			selected_position = current_gps_position;
		} else {
			if (this_def_position.is_valid) {
				selected_position = this_def_position;
				selected_position.last_updated = now;
			}
		}
		ast_mutex_unlock(&position_update_lock);

		if (selected_position.is_valid) {
			if (sscanf(selected_position.latitude, "%d.%d%c", &i, &j, &c) == 3) {
				/* Adjust the latitude for the offset */
				if (c == 'S') {
					i = -i;
				}
				if (i >= 0) {
					j -= myoffset;
				} else {
					j += myoffset;
				}
				i += (j / 60);
				if (j < 0){
					sprintf(lat, "%04d.%02d%c", (i >= 0) ? i : -i, -j % 60, (i >= 0) ? 'N' : 'S');
				} else {
					sprintf(lat, "%04d.%02d%c", (i >= 0) ? i : -i, j % 60, (i >= 0) ? 'N' : 'S');
				}
				/* If our last position update is good, send an update */
				if ((selected_position.last_updated + GPS_VALID_SECS) >= now) {
					report_aprstt(ctg, lat, selected_position.longitude, theircall, overlay);
				}
			}
		}
	}
	munmap(ttentries, mystat.st_size);
	if (mfp) {
		fclose(mfp);
	}
	
	ast_debug(2, "%s has exited\n", __FUNCTION__);
	return NULL;
}

/*!
 * \brief GPS read helper function.
 * The function "GPS_READ" responds with current GPS information.
 *
 * The response is in the format, with each element delimited by a space:
 *	unix time (EPOCH) format %llu
 *  latitude DDMM.SSX		(degrees, minutes, seconds, direction)
 *  longitude DDMM.SSX		(degrees, minutes, seconds, direction)
 *  elevation NNNN.NU		(value, unit - default is "M" meters)
 *
 * \param	chan	Pointer to the asterisk channel struct.
 * \param	cmd		Pointer to the command passed to the function.
 * \param	data	Pointer to the data passed to the function.
 * \param	buf		Pointer to the buffer to receive the returned data.
 * \param	len		Length of the buffer.
 *
 * \retval	0		Success
 * \retval	-1		Failure
 */
static int gps_read_helper(struct ast_channel *chan, const char *cmd, char *data, char *buf, size_t len)
{
	struct position_info selected_position;
	
	ast_mutex_lock(&position_update_lock);
	selected_position.is_valid = 0;
	/* See if we need to send live GPS or the default */
	if (current_gps_position.is_valid) {
		selected_position = current_gps_position;
	} else if (general_def_position.is_valid) {
			selected_position = general_def_position;
			selected_position.last_updated = time(NULL);
	}
	ast_mutex_unlock(&position_update_lock);
	/* 
	 * Format the response if we have a valid position
	 */
	if (selected_position.is_valid ) {
		snprintf(buf, len, "%llu %s %s %s", (unsigned long long) selected_position.last_updated, 
			selected_position.latitude, selected_position.longitude, selected_position.elevation);
		return 0;
	}

	*buf = '\0';
	return -1;
}
/*!
 * \brief Send APRStt (touch tone) position report function.
 * The write function "APRS_SENDTT" sends an APRS position report
 * for the specified section, overlay and callsign.
 *
 * APRS_SENDTT(section, overlay) = callsign
 *
 * \param	chan	Pointer to the asterisk channel struct.
 * \param	cmd		Pointer to the command passed to the function.
 * \param	data	Pointer to the data passed to the function.
 * \param	value	Pointer to value that represents the callsign.
 *
 * \retval	0		Success
 * \retval	-1		Failure
 */
static int aprs_sendtt_helper(struct ast_channel *chan, const char *function, char *data, const char *value)
{
	char *parse;
	struct aprs_sender_info *sender_entry;
	
	AST_DECLARE_APP_ARGS(args,
		AST_APP_ARG(section);
		AST_APP_ARG(overlay);
	);
	
	if (ast_strlen_zero(data)) {
		ast_log(LOG_ERROR, "APRS_SENDTT requires arguments\n");
		return -1;
	}

	parse = ast_strdupa(data);
	AST_STANDARD_APP_ARGS(args, parse);

	if (ast_strlen_zero(args.section)) {
		ast_log(LOG_ERROR, "APRS_SENDTT requires a section\n");
		return -1;
	}
	if (ast_strlen_zero(args.overlay)) {
		ast_log(LOG_ERROR, "APRS_SENDTT requires an overlay\n");
		return -1;
	}
	if (ast_strlen_zero(value)) {
		ast_log(LOG_ERROR, "APRS_SENDTT requires a callsign\n");
		return -1;
	}
	/* Find the section */
	AST_LIST_TRAVERSE(&aprs_sender_list, sender_entry, list) {
		if (!strcasecmp(sender_entry->section, args.section) && sender_entry->type == APRSTT) {
			break;
		}
	}
	if (!sender_entry) {
		ast_log(LOG_WARNING, "APRS_SENDTT cannot find associated section: %s\n", args.section);
		return -1;
	}

	ast_mutex_lock(&sender_entry->lock);
	
	sender_entry->overlay = args.overlay[0];
	ast_copy_string(sender_entry->their_call, value, sizeof(sender_entry->their_call));
	ast_cond_signal(&sender_entry->condition);	/* Signal the thread to start working */
	
	ast_mutex_unlock(&sender_entry->lock);

	return 0;
}

/*!
 * \brief Asterisk function setup struct for gps_read_helper
 */
static struct ast_custom_function gps_read_function = {
	.name = "GPS_READ",
	.read = gps_read_helper,
};

/*!
 * \brief Asterisk function setup struct for aprstt_send_helper
 */
static struct ast_custom_function aprs_sendtt_function = {
	.name = "APRS_SENDTT",
	.write = aprs_sendtt_helper,
};

/*!
 * \brief Handle asterisk cli request for status
 * \param e				Asterisk cli entry.
 * \param cmd			Cli command type.
 * \param a				Pointer to asterisk cli arguments.
 * \return	Cli success or failure.
 */
static char *handle_cli_status(struct ast_cli_entry *e, int cmd, struct ast_cli_args *a)
{
	switch (cmd) {
	case CLI_INIT:
		e->command = "gps show status";
		e->usage =
			"Usage: gps show status\n"
			"       Displays the GPS status.\n";
		return NULL;
	case CLI_GENERATE:
		return NULL;
	}

	if (a->argc > 3) {
		return CLI_SHOWUSAGE;
	}

	ast_mutex_lock(&position_update_lock);
	ast_cli(a->fd, "GPS: %s, Signal: %s \n", 
		ast_strlen_zero(comport) ? "Disconnected" : "Connected",
		current_gps_position.is_valid ? "Locked" : "Unlocked");
	if (current_gps_position.is_valid) {
		ast_cli(a->fd, "Position: %s %s Elevation: %s\n", 
			current_gps_position.latitude, current_gps_position.longitude,
			current_gps_position.elevation);
	}
	if (general_def_position.is_valid) {
		ast_cli(a->fd, "Default Position: %s %s Elevation: %s\n", 
			general_def_position.latitude, general_def_position.longitude,
			general_def_position.elevation);
	}
	ast_mutex_unlock(&position_update_lock);
	
	return CLI_SUCCESS;
}

/*!
 * \brief Define cli entries for this module
 */
static struct ast_cli_entry cli_status = AST_CLI_DEFINE(handle_cli_status, "Display the GPS status");


static int unload_module(void)
{
	struct aprs_sender_info *sender_entry;
	int res;

	run_forever = 0;
	ast_debug(2, "Waiting for threads to exit\n");
	if (sockfd != -1) {
		shutdown(sockfd, SHUT_RDWR);
	}
	ast_debug(2, "Waiting for aprs_connection_thread to exit\n");
	pthread_join(aprs_connection_thread_id, NULL);
	
	if (comport) {
		ast_debug(2, "Waiting for gps_reader_thread to exit\n");
		pthread_join(gps_reader_thread_id, NULL);
		ast_free(comport);
	}
	
	/* Shutdown and clean up sender threads */
	while ((sender_entry = AST_LIST_REMOVE_HEAD(&aprs_sender_list, list))) {
		ast_debug(2, "Waiting for %s sender thread %s to exit\n", sender_entry->type == APRS ? "aprs" : "aprstt", sender_entry->section);
		pthread_join(sender_entry->thread_id, NULL);
		ast_mutex_destroy(&sender_entry->lock);
		ast_cond_destroy(&sender_entry->condition);
		ast_free(sender_entry);
	}

	ast_debug(1, "Threads have exited\n");
	
	if (server) {
		ast_free(server);
	}
	if (port) {
		ast_free(port);
	}
	
	/* Unregister our functions */
	res = ast_custom_function_unregister(&gps_read_function);
	res |= ast_custom_function_unregister(&aprs_sendtt_function);
	
	/* Unregister cli */
	ast_cli_unregister(&cli_status);
	
	return res;
}

static int load_module(void)
{
	struct ast_config *cfg = NULL;
	char *ctg = "general", *val;
	char *def_lat, *def_lon, *def_elev;
	struct aprs_sender_info *sender_entry;
	int res;

	struct ast_flags zeroflag = { 0 };

	if (!(cfg = ast_config_load(config, zeroflag))) {
		ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
		return AST_MODULE_LOAD_DECLINE;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "comport");
	if (val) {
		comport = ast_strdup(val);
	} else {
		comport = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "lat");
	if (val) {
		def_lat = ast_strdupa(val);
	} else {
		def_lat = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "lon");
	if (val) {
		def_lon = ast_strdupa(val);
	} else {
		def_lon = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "elev");
	if (val) {
		def_elev = ast_strdupa(val);
	} else {
		def_elev = NULL;
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "server");
	if (val) {
		server = ast_strdup(val);
	} else {
		server = ast_strdup(APRS_DEFAULT_SERVER);
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "port");
	if (val) {
		port = ast_strdup(val);
	} else {
		port = ast_strdup(APRS_DEFAULT_PORT);
	}
	val = (char *) ast_variable_retrieve(cfg, ctg, "baudrate");
	if (val) {
		switch (atoi(val)) {
		case 2400:
			baudrate = B2400;
			break;
		case 4800:
			baudrate = B4800;
			break;
		case 9600:
			baudrate = B9600;
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
			ast_log(LOG_ERROR, "%s is not valid baud rate for iospeed\n", val);
			break;
		}
	} else {
		baudrate = GPS_DEFAULT_BAUDRATE;
	}
	
	/* 
	 * Setup the general default position.
	 * This is used when the GPS device is not available.
	 */
	if (def_lat && def_lon) {
		general_def_position.is_valid = 1;
		lat_decimal_to_DMS(strtof(def_lat, NULL), general_def_position.latitude, sizeof(general_def_position.latitude));
		lon_decimal_to_DMS(strtof(def_lon, NULL), general_def_position.longitude, sizeof(general_def_position.longitude));
		/* See if we have a default elevation */
		if (def_elev) {
			float eleva, elevd;
			eleva = strtof(def_elev, NULL);
			elevd = (eleva - floor(eleva)) * 10 + 0.5;
			snprintf(general_def_position.elevation, sizeof(general_def_position.elevation), "%03d.%1dM", (int) eleva, (int) elevd);
		} else {
			strcpy(general_def_position.elevation, "000.0M");
		}
	}

	/* Create the aprs connection thread */
	if (ast_pthread_create(&aprs_connection_thread_id, NULL, aprs_connection_thread, NULL)) {
		ast_log(LOG_ERROR, "Cannot create APRS connection thread");
		return -1;
	}
	/* If we have a comport specified, start the GPS processing thread */
	if (comport) {
		if (ast_pthread_create(&gps_reader_thread_id, NULL, gps_reader, NULL)) {
			ast_log(LOG_ERROR, "Cannot create APRS reader thread");
			return -1;
		}
	}
	/* Create the aprs sender thread for 'general' */
	sender_entry = ast_calloc(1, sizeof(*sender_entry));
	if (!sender_entry) {
		return -1;
	}
	sender_entry->type = APRS;
	strcpy(sender_entry->section, "general");
	ast_mutex_init(&sender_entry->lock);
	ast_cond_init(&sender_entry->condition, 0);
	AST_LIST_INSERT_TAIL(&aprs_sender_list, sender_entry, list);
	if (ast_pthread_create(&sender_entry->thread_id, NULL, aprs_sender_thread, sender_entry)) {
		ast_log(LOG_ERROR, "Cannot create APRS sender thread %s", sender_entry->section);
		return -1;
	}
	
	/* Create the aprs tt sender thread for 'general' */
	sender_entry = ast_calloc(1, sizeof(*sender_entry));
	if (!sender_entry) {
		return -1;
	}
	sender_entry->type = APRSTT;
	strcpy(sender_entry->section, "general");
	ast_mutex_init(&sender_entry->lock);
	ast_cond_init(&sender_entry->condition, 0);
	AST_LIST_INSERT_TAIL(&aprs_sender_list, sender_entry, list);
	if (ast_pthread_create(&sender_entry->thread_id, NULL, aprstt_sender_thread, sender_entry)) {
		ast_log(LOG_ERROR, "Cannot create APRStt sender thread %s", sender_entry->section);
		return -1;
	}
	/* 
	 * See if we have sections other than general. 
	 * If present, create aprs processing threads for those sections 
	 */
	while ((ctg = ast_category_browse(cfg, ctg)) != NULL) {
		if (ctg == NULL) {
			continue;
		}
		/* Create the aprs sender thread for this category */
		sender_entry = ast_calloc(1, sizeof(*sender_entry));
		if (!sender_entry) {
			return -1;
		}
		sender_entry->type = APRS;
		ast_copy_string(sender_entry->section, ctg, sizeof(sender_entry->section));
		ast_mutex_init(&sender_entry->lock);
		ast_cond_init(&sender_entry->condition, 0);
		AST_LIST_INSERT_TAIL(&aprs_sender_list, sender_entry, list);
		if (ast_pthread_create(&sender_entry->thread_id, NULL, aprs_sender_thread, sender_entry)) {
			ast_log(LOG_ERROR, "Cannot create APRS sender thread %s", sender_entry->section);
			return -1;
		}
		
		/* Create the aprs tt sender thread for this category */
		sender_entry = ast_calloc(1, sizeof(*sender_entry));
		if (!sender_entry) {
			return -1;
		}
		sender_entry->type = APRSTT;
		ast_copy_string(sender_entry->section, ctg, sizeof(sender_entry));
		ast_mutex_init(&sender_entry->lock);
		ast_cond_init(&sender_entry->condition, 0);
		AST_LIST_INSERT_TAIL(&aprs_sender_list, sender_entry, list);
		if (ast_pthread_create_detached(&sender_entry->thread_id, NULL, aprstt_sender_thread, sender_entry->section)) {
			ast_log(LOG_ERROR, "Cannot create APRStt sender thread %s", sender_entry->section);
			return -1;
		}
	}
	ast_config_destroy(cfg);
	cfg = NULL;
	
	/* Register our functions */
	res = ast_custom_function_register(&gps_read_function);
	res |= ast_custom_function_register(&aprs_sendtt_function);
	
	/* Register cli */
	ast_cli_register(&cli_status);
	
	return res;
}

AST_MODULE_INFO_STANDARD_EXTENDED(ASTERISK_GPL_KEY, "GPS Interface");
