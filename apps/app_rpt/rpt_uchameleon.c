
/*! \file
 *
 * \brief Uchameleon specific routines
 */

#include "asterisk.h"

#include <termios.h> /* use B115200 */

#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_serial.h"
#include "rpt_uchameleon.h"
#include "rpt_lock.h"
#include "rpt_utils.h" /* use explode_string */

extern struct rpt rpt_vars[MAXRPTS];
static struct ast_flags config_flags = { CONFIG_FLAG_WITHCOMMENTS };

int uchameleon_thread_start(struct daq_entry_tag *t)
{
	int res, tries = 50;

	ast_mutex_init(&t->lock);

	/* Start up uchameleon monitor thread */

	res = ast_pthread_create_detached(&t->threadid, NULL, uchameleon_monitor_thread, (void *) t);
	if (res) {
		ast_log(LOG_WARNING, "Could not start uchameleon monitor thread\n");
		return -1;
	}

	ast_mutex_lock(&t->lock);
	while ((!t->active) && (tries)) {
		ast_mutex_unlock(&t->lock);
		usleep(100 * 1000);
		ast_mutex_lock(&t->lock);
		tries--;
	}
	ast_mutex_unlock(&t->lock);

	if (!tries)
		return -1;

	return 0;
}

int uchameleon_connect(struct daq_entry_tag *t)
{
	int count;
	static const char idbuf[] = "id\n";
	static const char ledbuf[] = "led on\n";
	static const char expect[] = "Chameleon";
	char rxbuf[20];

	if ((t->fd = serial_open(t->dev, B115200, 0)) == -1) {
		ast_log(LOG_WARNING, "serial_open on %s failed!\n", t->name);
		return -1;
	}
	if ((count = serial_io(t->fd, idbuf, rxbuf, sizeof(idbuf) - 1, 14, DAQ_RX_TIMEOUT, 0x0a)) < 1) {
		ast_log(LOG_WARNING, "serial_io on %s failed\n", t->name);
		close(t->fd);
		t->fd = -1;
		return -1;
	}
	ast_debug(3, "count = %d, rxbuf = %s\n", count, rxbuf);
	if ((count != 13) || (strncmp(expect, rxbuf + 4, sizeof(expect) - 1))) {
		ast_log(LOG_WARNING, "%s is not a uchameleon device\n", t->name);
		close(t->fd);
		t->fd = -1;
		return -1;
	}
	/* uchameleon LED on solid once we communicate with it successfully */

	if (serial_io(t->fd, ledbuf, NULL, sizeof(ledbuf) - 1, 0, DAQ_RX_TIMEOUT, 0) == -1) {
		ast_log(LOG_WARNING, "Can't set LED on uchameleon device\n");
		close(t->fd);
		t->fd = -1;
		return -1;
	}
	return 0;
}

void uchameleon_alarm_handler(struct daq_pin_entry_tag *p)
{
	char *valuecopy;
	int i, busy;
	char *s;
	char *argv[7];
	int argc;
	int nrpts = rpt_num_rpts();

	if (!(valuecopy = ast_strdup(p->alarmargs))) {
		return;
	}

	argc = explode_string(valuecopy, argv, 6, ',', 0);

	ast_debug(3, "Alarm event on device %s, pin %d, state = %d\n", argv[0], p->num, p->value);

	/*
	 * Node: argv[3]
	 * low function: argv[4]
	 * high function: argv[5]
	 *
	 */
	i = busy = 0;
	s = (p->value) ? argv[5] : argv[4];
	if ((argc == 6) && (s[0] != '-')) {
		for (i = 0; i < nrpts; i++) {
			if (!strcmp(argv[3], rpt_vars[i].name)) {

				struct rpt *myrpt = &rpt_vars[i];
				rpt_mutex_lock(&myrpt->lock);
				if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(s)) {
					rpt_mutex_unlock(&myrpt->lock);
					busy = 1;
				}
				if (!busy) {
					myrpt->macrotimer = MACROTIME;
					strncat(myrpt->macrobuf, s, MAXMACRO - 1);
				}
				rpt_mutex_unlock(&myrpt->lock);

			}
		}
	}
	if (argc != 6) {
		ast_log(LOG_WARNING, "Not enough arguments to process alarm\n");
	} else if (busy) {
		ast_log(LOG_WARNING, "Function decoder busy while processing alarm");
	}
	ast_free(valuecopy);
}

int uchameleon_pin_init(struct daq_entry_tag *t)
{
	int i;
	struct ast_config *ourcfg;
	struct ast_variable *var, *var2;

	/* Pin Initialization */

	ourcfg = ast_config_load("rpt.conf", config_flags);

	if (!ourcfg)
		return -1;

	var2 = ast_variable_browse(ourcfg, t->name);
	while (var2) {
		unsigned int pin;
		int x = 0;
		static char *pin_keywords[] = { "inadc", "inp", "in", "out", NULL };
		if ((var2->name[0] < '0') || (var2->name[0] > '9')) {
			var2 = var2->next;
			continue;
		}
		pin = (unsigned int) atoi(var2->name);
		i = matchkeyword((char *) var2->value, NULL, pin_keywords);
		ast_debug(3, "Pin = %d, Pintype = %d\n", pin, i);
		if (i && i < 5) {
			uchameleon_do_long(t, pin, DAQ_CMD_PINSET, NULL, &i, NULL);	/* Set pin type */
			uchameleon_do_long(t, pin, DAQ_CMD_MONITOR, NULL, &x, NULL);	/* Monitor off */
			if (i == DAQ_PT_OUT) {
				ast_debug(3, "Set output pin %d low\n", pin);	/* Set output pins low */
				uchameleon_do_long(t, pin, DAQ_CMD_OUT, NULL, &x, NULL);
			}
		} else
			ast_log(LOG_WARNING, "Invalid pin type: %s\n", var2->value);
		var2 = var2->next;
	}

	/*
	 * Alarm initialization
	 */

	var = ast_variable_browse(ourcfg, "alarms");
	while (var) {
		int ignorefirst, pin;
		char s[64];
		char *argv[7];
		struct daq_pin_entry_tag *p;

		/* Parse alarm entry */

		ast_copy_string(s, var->value, sizeof(s) - 1);

		if (explode_string(s, argv, 6, ',', 0) != 6) {
			ast_log(LOG_WARNING, "Alarm arguments must be 6 for %s\n", var->name);
			var = var->next;
			continue;
		}

		ignorefirst = atoi(argv[2]);

		if (!(pin = atoi(argv[1]))) {
			ast_log(LOG_WARNING, "Pin must be greater than 0 for %s\n", var->name);
			var = var->next;
			continue;
		}

		/* Find the pin entry */
		p = t->pinhead;
		while (p) {
			if (p->num == pin)
				break;
			p = p->next;
		}
		if (!p) {
			ast_log(LOG_WARNING, "Can't find pin %d for device %s\n", pin, argv[0]);
			var = var->next;
			continue;
		}

		if (!strcmp(argv[0], t->name)) {
			ast_copy_string(p->alarmargs, var->value, sizeof(p->alarmargs));	/* Save the alarm arguments in the pin entry */
			ast_debug(1, "Adding alarm %s on pin %d\n", var->name, pin);
			uchameleon_do_long(t, pin, DAQ_CMD_MONITOR, uchameleon_alarm_handler, &ignorefirst, NULL);
		}
		var = var->next;
	}

	ast_config_destroy(ourcfg);
	time(&t->adcacqtime);		/* Start ADC Acquisition */
	return -0;
}

int uchameleon_open(struct daq_entry_tag *t)
{
	int res;

	if (!t)
		return -1;

	if (uchameleon_connect(t)) {
		ast_log(LOG_WARNING, "Cannot open device %s", t->name);
		return -1;
	}

	res = uchameleon_thread_start(t);

	if (!res)
		res = uchameleon_pin_init(t);

	return res;

}

int uchameleon_close(struct daq_entry_tag *t)
{
	int res = 0;
	char *ledpat = "led pattern 253\n";
	struct daq_pin_entry_tag *p, *pn;
	struct daq_tx_entry_tag *q, *qn;

	if (!t)
		return -1;

	ast_mutex_lock(&t->lock);

	if (t->active) {
		res = pthread_kill(t->threadid, 0);
		if (res)
			ast_log(LOG_WARNING, "Can't kill monitor thread");
		ast_mutex_unlock(&t->lock);
		return -1;
	}

	if (t->fd > 0)
		serial_io(t->fd, ledpat, NULL, strlen(ledpat), 0, 0, 0);	/* LED back to flashing */

	/* Free linked lists */

	if (t->pinhead) {
		p = t->pinhead;
		while (p) {
			pn = p->next;
			ast_free(p);
			p = pn;
		}
		t->pinhead = NULL;
	}

	if (t->txhead) {
		q = t->txhead;
		while (q) {
			qn = q->next;
			ast_free(q);
			q = qn;
		}
		t->txhead = t->txtail = NULL;
	}

	if (t->fd > 0) {
		res = close(t->fd);
		if (res)
			ast_log(LOG_WARNING, "Error closing serial port");
		t->fd = -1;
	}
	ast_mutex_unlock(&t->lock);
	ast_mutex_destroy(&t->lock);
	return res;
}

int uchameleon_do_long(struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2)
{
	int i, j, x;
	struct daq_pin_entry_tag *p, *listl, *listp;

	if (!t)
		return -1;

	ast_mutex_lock(&t->lock);

	if (!t->active) {
		/* Try to restart thread and re-open device */
		ast_mutex_unlock(&t->lock);
		uchameleon_close(t);
		usleep(10 * 1000);
		if (uchameleon_open(t)) {
			ast_log(LOG_WARNING, "Could not re-open Uchameleon\n");
			return -1;
		}
		ast_mutex_lock(&t->lock);
		/* We're back in business! */
	}

	/* Find our pin */

	listp = listl = t->pinhead;
	while (listp) {
		listl = listp;
		if (listp->num == pin)
			break;
		listp = listp->next;
	}
	if (listp) {
		if (cmd == DAQ_CMD_PINSET) {
			if (arg1 && *arg1 && (*arg1 < 19)) {
				while (listp->state) {
					ast_mutex_unlock(&t->lock);
					usleep(10 * 1000);	/* Wait */
					ast_mutex_lock(&t->lock);
				}
				listp->command = DAQ_CMD_PINSET;
				listp->pintype = *arg1;	/* Pin redefinition */
				listp->valuemin = 255;
				listp->valuemax = 0;
				listp->state = DAQ_PS_START;
			} else {
				ast_log(LOG_WARNING, "Invalid pin number for pinset\n");
			}
		} else {
			/* Return ADC value */

			if (cmd == DAQ_CMD_ADC) {
				if (arg2) {
					switch (*((int *) arg2)) {
					case DAQ_SUB_CUR:
						if (arg1)
							*arg1 = listp->value;
						break;

					case DAQ_SUB_STAVG:	/* Short term average */
						x = 0;
						i = listp->adcnextupdate;
						for (j = 0; j < ADC_HISTORY_DEPTH; j++) {
							ast_debug(4, "Sample for avg: %d\n", listp->adchistory[i]);
							x += listp->adchistory[i];
							if (++i >= ADC_HISTORY_DEPTH)
								i = 0;
						}
						x /= ADC_HISTORY_DEPTH;
						ast_debug(3, "Average: %d\n", x);
						if (arg1)
							*arg1 = x;
						break;

					case DAQ_SUB_STMAX:	/* Short term maximum */
						x = 0;
						i = listp->adcnextupdate;
						for (j = 0; j < ADC_HISTORY_DEPTH; j++) {
							ast_debug(4, "Sample for max: %d\n", listp->adchistory[i]);
							if (listp->adchistory[i] > x)
								x = listp->adchistory[i];
							if (++i >= ADC_HISTORY_DEPTH)
								i = 0;
						}
						ast_debug(3, "Maximum: %d\n", x);
						if (arg1)
							*arg1 = x;
						break;

					case DAQ_SUB_STMIN:	/* Short term minimum */
						x = 255;
						i = listp->adcnextupdate;
						if (i >= ADC_HISTORY_DEPTH)
							i = 0;
						for (j = 0; j < ADC_HISTORY_DEPTH; j++) {
							ast_debug(4, "Sample for min: %d\n", listp->adchistory[i]);
							if (listp->adchistory[i] < x)
								x = listp->adchistory[i];
							if (++i >= ADC_HISTORY_DEPTH)
								i = 0;
						}
						ast_debug(3, "Minimum: %d\n", x);
						if (arg1)
							*arg1 = x;
						break;

					case DAQ_SUB_MAX:	/* Max since start or reset */
						if (arg1)
							*arg1 = listp->valuemax;
						break;

					case DAQ_SUB_MIN:	/* Min since start or reset */
						if (arg1)
							*arg1 = listp->valuemin;
						break;

					default:
						ast_mutex_unlock(&t->lock);
						return -1;
					}
				} else {
					if (arg1)
						*arg1 = listp->value;
				}
				ast_mutex_unlock(&t->lock);
				return 0;
			}

			/* Don't deadlock if monitor has been previously issued for a pin */

			if (listp->state == DAQ_PS_IN_MONITOR) {
				if ((cmd != DAQ_CMD_MONITOR) || (exec)) {
					ast_log(LOG_WARNING, "Monitor was previously set on pin %d, command ignored\n", listp->num);
					ast_mutex_unlock(&t->lock);
					return -1;
				}
			}

			/* Rest of commands are processed here */

			while (listp->state) {
				ast_mutex_unlock(&t->lock);
				usleep(10 * 1000);	/* Wait */
				ast_mutex_lock(&t->lock);
			}

			if (cmd == DAQ_CMD_MONITOR) {
				if (arg1)
					listp->ignorefirstalarm = *arg1;
				listp->monexec = exec;
			}

			listp->command = cmd;

			if (cmd == DAQ_CMD_OUT) {
				if (arg1) {
					listp->value = *arg1;
				} else {
					ast_mutex_unlock(&t->lock);
					return 0;
				}
			}
			listp->state = DAQ_PS_START;
			if ((cmd == DAQ_CMD_OUT) || (cmd == DAQ_CMD_MONITOR)) {
				ast_mutex_unlock(&t->lock);
				return 0;
			}

			while (listp->state) {
				ast_mutex_unlock(&t->lock);
				usleep(10 * 1000);	/* Wait */
				ast_mutex_lock(&t->lock);
			}
			*arg1 = listp->value;
			ast_mutex_unlock(&t->lock);
			return 0;
		}
	} else {					/* Pin not in list */
		if (cmd == DAQ_CMD_PINSET) {
			if (arg1 && *arg1 && (*arg1 < 19)) {
				/* New pin definition */
				if (!(p = (struct daq_pin_entry_tag *) ast_malloc(sizeof(struct daq_pin_entry_tag)))) {
					ast_log(LOG_ERROR, "Out of memory");
					ast_mutex_unlock(&t->lock);
					return -1;
				}
				memset(p, 0, sizeof(struct daq_pin_entry_tag));
				p->pintype = *arg1;
				p->command = DAQ_CMD_PINSET;
				p->num = pin;
				if (!listl) {
					t->pinhead = p;
				} else {
					listl->next = p;
				}
				p->state = DAQ_PS_START;
				ast_mutex_unlock(&t->lock);
				return 0;
			} else {
				ast_log(LOG_WARNING, "Invalid pin number for pinset\n");
			}
		} else {
			ast_log(LOG_WARNING, "Invalid pin number for pin I/O command\n");
		}
	}
	ast_mutex_unlock(&t->lock);
	return -1;
}

void uchameleon_queue_tx(struct daq_entry_tag *t, char *txbuff)
{
	struct daq_tx_entry_tag *q;

	if (!t)
		return;

	if (!(q = (struct daq_tx_entry_tag *) ast_malloc(sizeof(struct daq_tx_entry_tag)))) {
		ast_log(LOG_WARNING, "Out of memory\n");
		return;
	}

	memset(q, 0, sizeof(struct daq_tx_entry_tag));

	ast_copy_string(q->txbuff, txbuff, sizeof(q->txbuff));

	if (t->txtail) {
		t->txtail->next = q;
		q->prev = t->txtail;
		t->txtail = q;
	} else
		t->txhead = t->txtail = q;
	return;
}

void *uchameleon_monitor_thread(void *this)
{
	int pin = 0, sample = 0;
	int i, res, valid, adc_acquire;
	time_t now;
	char rxbuff[32];
	char txbuff[32];
	char *rxargs[4];
	struct daq_entry_tag *t = (struct daq_entry_tag *) this;
	struct daq_pin_entry_tag *p;
	struct daq_tx_entry_tag *q;

	ast_debug(1, "DAQ: thread started\n");

	ast_mutex_lock(&t->lock);
	t->active = 1;
	ast_mutex_unlock(&t->lock);

	for (;;) {
		adc_acquire = 0;
		/* If receive data */
		res = serial_rx(t->fd, rxbuff, sizeof(rxbuff), DAQ_RX_TIMEOUT, 0x0a);
		if (res == -1) {
			ast_log(LOG_ERROR, "serial_rx failed\n");
			close(t->fd);
			ast_mutex_lock(&t->lock);
			t->fd = -1;
			t->active = 0;
			ast_mutex_unlock(&t->lock);
			return this;		/* Now, we die */
		}
		if (res) {
			ast_debug(5, "Received: %s\n", rxbuff);
			valid = 0;
			/* Parse return string */
			i = explode_string(rxbuff, rxargs, 3, ' ', 0);
			if (i == 3) {
				if (!strcmp(rxargs[0], "pin")) {
					valid = 1;
					pin = atoi(rxargs[1]);
					sample = atoi(rxargs[2]);
				}
				if (!strcmp(rxargs[0], "adc")) {
					valid = 2;
					pin = atoi(rxargs[1]);
					sample = atoi(rxargs[2]);
				}
			}
			if (valid) {
				/* Update the correct pin list entry */
				ast_mutex_lock(&t->lock);
				p = t->pinhead;
				while (p) {
					if (p->num == pin) {
						if ((valid == 1)
							&& ((p->pintype == DAQ_PT_IN) || (p->pintype == DAQ_PT_INP)
								|| (p->pintype == DAQ_PT_OUT))) {
							p->value = sample ? 1 : 0;
							ast_debug(3, "Input pin %d is a %d\n", p->num, p->value);
							/* Exec monitor fun if state is monitor */

							if (p->state == DAQ_PS_IN_MONITOR) {
								if (!p->alarmmask && !p->ignorefirstalarm && p->monexec) {
									(*p->monexec) (p);
								}
								p->ignorefirstalarm = 0;
							} else
								p->state = DAQ_PS_IDLE;
						}
						if ((valid == 2) && (p->pintype == DAQ_PT_INADC)) {
							p->value = sample;
							if (sample > p->valuemax)
								p->valuemax = sample;
							if (sample < p->valuemin)
								p->valuemin = sample;
							p->adchistory[p->adcnextupdate++] = sample;
							if (p->adcnextupdate >= ADC_HISTORY_DEPTH)
								p->adcnextupdate = 0;
							p->state = DAQ_PS_IDLE;
						}
						break;
					}
					p = p->next;
				}
				ast_mutex_unlock(&t->lock);
			}
		}

		if (time(&now) >= t->adcacqtime) {
			t->adcacqtime = now + DAQ_ADC_ACQINT;
			ast_debug(4, "Acquiring analog data\n");
			adc_acquire = 1;
		}

		/* Go through the pin linked list looking for new work */
		ast_mutex_lock(&t->lock);
		p = t->pinhead;
		while (p) {
			/* Time to acquire all ADC channels ? */
			if ((adc_acquire) && (p->pintype == DAQ_PT_INADC)) {
				p->state = DAQ_PS_START;
				p->command = DAQ_CMD_ADC;
			}
			if (p->state == DAQ_PS_START) {
				p->state = DAQ_PS_BUSY;	/* Assume we are busy */
				switch (p->command) {
				case DAQ_CMD_OUT:
					if (p->pintype == DAQ_PT_OUT) {
						snprintf(txbuff, sizeof(txbuff), "pin %d %s\n", p->num, (p->value) ? "hi" : "lo");
						ast_debug(3, "DAQ_CMD_OUT: %s\n", txbuff);
						uchameleon_queue_tx(t, txbuff);
						p->state = DAQ_PS_IDLE;	/* TX is considered done */
					} else {
						ast_log(LOG_WARNING, "Wrong pin type for out command\n");
						p->state = DAQ_PS_IDLE;
					}
					break;

				case DAQ_CMD_MONITOR:
					snprintf(txbuff, sizeof(txbuff), "pin %d monitor %s\n", p->num, p->monexec ? "on" : "off");
					uchameleon_queue_tx(t, txbuff);
					if (!p->monexec)
						p->state = DAQ_PS_IDLE;	/* Restore to idle channel */
					else {
						p->state = DAQ_PS_IN_MONITOR;
					}
					break;

				case DAQ_CMD_IN:
					if ((p->pintype == DAQ_PT_IN) || (p->pintype == DAQ_PT_INP) || (p->pintype == DAQ_PT_OUT)) {
						snprintf(txbuff, sizeof(txbuff), "pin %d state\n", p->num);
						uchameleon_queue_tx(t, txbuff);
					} else {
						ast_log(LOG_WARNING, "Wrong pin type for in or inp command\n");
						p->state = DAQ_PS_IDLE;
					}
					break;

				case DAQ_CMD_ADC:
					if (p->pintype == DAQ_PT_INADC) {
						snprintf(txbuff, sizeof(txbuff), "adc %d\n", p->num);
						uchameleon_queue_tx(t, txbuff);
					} else {
						ast_log(LOG_WARNING, "Wrong pin type for adc command\n");
						p->state = DAQ_PS_IDLE;
					}
					break;

				case DAQ_CMD_PINSET:
					if ((!p->num) || (p->num > 18)) {
						ast_log(LOG_WARNING, "Invalid pin number %d\n", p->num);
						p->state = DAQ_PS_IDLE;
					}
					switch (p->pintype) {
					case DAQ_PT_IN:
					case DAQ_PT_INADC:
					case DAQ_PT_INP:
						if ((p->pintype == DAQ_PT_INADC) && (p->num > 8)) {
							ast_log(LOG_WARNING, "Invalid ADC pin number %d\n", p->num);
							p->state = DAQ_PS_IDLE;
							break;
						}
						if ((p->pintype == DAQ_PT_INP) && (p->num < 9)) {
							ast_log(LOG_WARNING, "Invalid INP pin number %d\n", p->num);
							p->state = DAQ_PS_IDLE;
							break;
						}
						snprintf(txbuff, sizeof(txbuff), "pin %d in\n", p->num);
						uchameleon_queue_tx(t, txbuff);
						if (p->num > 8) {
							snprintf(txbuff, sizeof(txbuff), "pin %d pullup %d\n", p->num,
									 (p->pintype == DAQ_PT_INP) ? 1 : 0);
							uchameleon_queue_tx(t, txbuff);
						}
						p->valuemin = 255;
						p->valuemax = 0;
						p->state = DAQ_PS_IDLE;
						break;

					case DAQ_PT_OUT:
						snprintf(txbuff, sizeof(txbuff), "pin %d out\n", p->num);
						uchameleon_queue_tx(t, txbuff);
						p->state = DAQ_PS_IDLE;
						break;

					default:
						break;
					}
					break;

				default:
					ast_log(LOG_WARNING, "Unrecognized uchameleon command\n");
					p->state = DAQ_PS_IDLE;
					break;
				}				/* switch */
			}					/* if */
			p = p->next;
		}						/* while */

		/* Transmit queued commands */
		while (t->txhead) {
			q = t->txhead;
			ast_copy_string(txbuff, q->txbuff, sizeof(txbuff));
			t->txhead = q->next;
			if (t->txhead)
				t->txhead->prev = NULL;
			else
				t->txtail = NULL;
			ast_free(q);
			ast_mutex_unlock(&t->lock);
			if (serial_txstring(t->fd, txbuff) == -1) {
				close(t->fd);
				ast_mutex_lock(&t->lock);
				t->active = 0;
				t->fd = -1;
				ast_mutex_unlock(&t->lock);
				ast_log(LOG_ERROR, "Tx failed, terminating monitor thread\n");
				return this;	/* Now, we die */
			}

			ast_mutex_lock(&t->lock);
		}						/* while */
		ast_mutex_unlock(&t->lock);
	}							/* for(;;) */
	return this;
}
