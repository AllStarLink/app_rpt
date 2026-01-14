
#include "asterisk.h"

#include <math.h>

#include "asterisk/utils.h"
#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/audiohook.h"

#include "app_rpt.h"
#include "rpt_vox.h"

void voxinit_rpt(struct rpt *myrpt, char enable)
{
	myrpt->vox.speech_energy = 0.0;
	myrpt->vox.noise_energy = 0.0;
	myrpt->vox.enacount = 0;
	myrpt->vox.voxena = 0;
	if (!enable)
		myrpt->vox.voxena = -1;
	myrpt->vox.lastvox = 0;
	myrpt->vox.ondebcnt = VOX_ON_DEBOUNCE_COUNT;
	myrpt->vox.offdebcnt = VOX_OFF_DEBOUNCE_COUNT;
	myrpt->wasvox = 0;
	myrpt->voxtotimer = 0;
	myrpt->voxtostate = 0;
}

void voxinit_link(struct rpt_link *mylink, char enable)
{
	mylink->vox.speech_energy = 0.0;
	mylink->vox.noise_energy = 0.0;
	mylink->vox.enacount = 0;
	mylink->vox.voxena = 0;
	if (!enable)
		mylink->vox.voxena = -1;
	mylink->vox.lastvox = 0;
	mylink->vox.ondebcnt = VOX_ON_DEBOUNCE_COUNT;
	mylink->vox.offdebcnt = VOX_OFF_DEBOUNCE_COUNT;
	mylink->wasvox = 0;
	mylink->voxtotimer = 0;
	mylink->voxtostate = 0;
}

int dovox(struct vox *v, short *buf, int bs)
{
	int i;
	float esquare = 0.0;
	float energy = 0.0;
	float threshold = 0.0;

	if (v->voxena < 0)
		return (v->lastvox);
	for (i = 0; i < bs; i++) {
		esquare += (float) buf[i] * (float) buf[i];
	}
	energy = sqrt(esquare);

	if (energy >= v->speech_energy)
		v->speech_energy += (energy - v->speech_energy) / 4;
	else
		v->speech_energy += (energy - v->speech_energy) / 64;

	if (energy >= v->noise_energy)
		v->noise_energy += (energy - v->noise_energy) / 64;
	else
		v->noise_energy += (energy - v->noise_energy) / 4;

	if (v->voxena)
		threshold = v->speech_energy / 8;
	else {
		threshold = MAX(v->speech_energy / 16, v->noise_energy * 2);
		threshold = MIN(threshold, VOX_MAX_THRESHOLD);
	}
	threshold = MAX(threshold, VOX_MIN_THRESHOLD);
	if (energy > threshold) {
		if (v->voxena)
			v->noise_energy *= 0.75;
		v->voxena = 1;
	} else
		v->voxena = 0;
	if (v->lastvox != v->voxena) {
		if (v->enacount++ >= ((v->lastvox) ? v->offdebcnt : v->ondebcnt)) {
			v->lastvox = v->voxena;
			v->enacount = 0;
		}
	} else
		v->enacount = 0;
	return (v->lastvox);
}
