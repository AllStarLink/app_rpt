
#include "asterisk.h"

#include "asterisk/channel.h"

#include "app_rpt.h"
#include "rpt_config.h"
#include "rpt_utils.h" /* use myatoi */

int retrieve_astcfgint(struct rpt *myrpt, char *category, char *name, int min, int max, int defl)
{
	char *var;
	int ret;
	char include_zero = 0;

	if (min < 0) {				/* If min is negative, this means include 0 as a valid entry */
		min = -min;
		include_zero = 1;
	}

	var = (char *) ast_variable_retrieve(myrpt->cfg, category, name);
	if (var) {
		ret = myatoi(var);
		if (include_zero && !ret)
			return 0;
		if (ret < min)
			ret = min;
		if (ret > max)
			ret = max;
	} else
		ret = defl;
	return ret;
}

int get_wait_interval(struct rpt *myrpt, int type)
{
	int interval;
	char *wait_times;
	char *wait_times_save;

	wait_times_save = NULL;
	wait_times = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name, "wait_times");

	if (wait_times) {
		wait_times_save = ast_strdup(wait_times);
		if (!wait_times_save)
			return 0;

	}

	switch (type) {
	case DLY_TELEM:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "telemwait", 500, 5000, 1000);
		else
			interval = 1000;
		break;

	case DLY_ID:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "idwait", 250, 5000, 500);
		else
			interval = 500;
		break;

	case DLY_UNKEY:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "unkeywait", 50, 5000, 1000);
		else
			interval = 1000;
		break;

	case DLY_LINKUNKEY:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "linkunkeywait", 500, 5000, 1000);
		else
			interval = 1000;
		break;

	case DLY_CALLTERM:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "calltermwait", 500, 5000, 1500);
		else
			interval = 1500;
		break;

	case DLY_COMP:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "compwait", 500, 5000, 200);
		else
			interval = 200;
		break;

	case DLY_PARROT:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "parrotwait", 500, 5000, 200);
		else
			interval = 200;
		break;
	case DLY_MDC1200:
		if (wait_times)
			interval = retrieve_astcfgint(myrpt, wait_times_save, "mdc1200wait", 500, 5000, 200);
		else
			interval = 350;
		break;
	default:
		interval = 0;
		break;
	}
	if (wait_times_save)
		ast_free(wait_times_save);
	return interval;
}

int retrieve_memory(struct rpt *myrpt, char *memory)
{
	char tmp[15], *s, *s1, *s2, *val;

	ast_debug(1, "memory=%s block=%s\n", memory, myrpt->p.memory);

	val = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->p.memory, memory);
	if (!val) {
		return -1;
	}
	strncpy(tmp, val, sizeof(tmp) - 1);
	tmp[sizeof(tmp) - 1] = 0;

	s = strchr(tmp, ',');
	if (!s)
		return 1;
	*s++ = 0;
	s1 = strchr(s, ',');
	if (!s1)
		return 1;
	*s1++ = 0;
	s2 = strchr(s1, ',');
	if (!s2)
		s2 = s1;
	else
		*s2++ = 0;
	ast_copy_string(myrpt->freq, tmp, sizeof(myrpt->freq) - 1);
	ast_copy_string(myrpt->rxpl, s, sizeof(myrpt->rxpl) - 1);
	ast_copy_string(myrpt->txpl, s, sizeof(myrpt->rxpl) - 1);
	myrpt->remmode = REM_MODE_FM;
	myrpt->offset = REM_SIMPLEX;
	myrpt->powerlevel = REM_MEDPWR;
	myrpt->txplon = myrpt->rxplon = 0;
	myrpt->splitkhz = 0;
	if (s2 != s1)
		myrpt->splitkhz = atoi(s1);
	while (*s2) {
		switch (*s2++) {
		case 'A':
		case 'a':
			strcpy(myrpt->rxpl, "100.0");
			strcpy(myrpt->txpl, "100.0");
			myrpt->remmode = REM_MODE_AM;
			break;
		case 'B':
		case 'b':
			strcpy(myrpt->rxpl, "100.0");
			strcpy(myrpt->txpl, "100.0");
			myrpt->remmode = REM_MODE_LSB;
			break;
		case 'F':
			myrpt->remmode = REM_MODE_FM;
			break;
		case 'L':
		case 'l':
			myrpt->powerlevel = REM_LOWPWR;
			break;
		case 'H':
		case 'h':
			myrpt->powerlevel = REM_HIPWR;
			break;

		case 'M':
		case 'm':
			myrpt->powerlevel = REM_MEDPWR;
			break;

		case '-':
			myrpt->offset = REM_MINUS;
			break;

		case '+':
			myrpt->offset = REM_PLUS;
			break;

		case 'S':
		case 's':
			myrpt->offset = REM_SIMPLEX;
			break;

		case 'T':
		case 't':
			myrpt->txplon = 1;
			break;

		case 'R':
		case 'r':
			myrpt->rxplon = 1;
			break;

		case 'U':
		case 'u':
			strcpy(myrpt->rxpl, "100.0");
			strcpy(myrpt->txpl, "100.0");
			myrpt->remmode = REM_MODE_USB;
			break;
		default:
			return 1;
		}
	}
	return 0;
}
