
/*! \todo get rid of these duplicated dsp.c/.h things */
typedef struct {
	int v2;
	int v3;
	int chunky;
	int fac;
	int samples;
} goertzel_state_t;

typedef struct {
	int value;
	int power;
} goertzel_result_t;

typedef struct
{
	int freq;
	int block_size;
	int squelch;		/* Remove (squelch) tone */
	goertzel_state_t tone;
	float energy;		/* Accumulated energy of the current block */
	int samples_pending;	/* Samples remain to complete the current block */
	int mute_samples;	/* How many additional samples needs to be muted to suppress already detected tone */

	int hits_required;	/* How many successive blocks with tone we are looking for */
	float threshold;	/* Energy of the tone relative to energy from all other signals to consider a hit */

	int hit_count;		/* How many successive blocks we consider tone present */
	int last_hit;		/* Indicates if the last processed block was a hit */

} tone_detect_state_t;

/* WAS inline */
void goertzel_reset(goertzel_state_t *s);
void tone_detect_init(tone_detect_state_t *s, int freq, int duration, int amp);
int tone_detect(tone_detect_state_t *s, int16_t *amp, int samples);
