#include "rpt_lock.h"

#ifdef	APP_RPT_LOCK_DEBUG

#warning COMPILING WITH LOCK-DEBUGGING ENABLED!!

#define	MAXLOCKTHREAD 100

struct lockthread {
	pthread_t id;
	int lockcount;
	int lastlock;
	int lastunlock;
} lockthreads[MAXLOCKTHREAD];

struct by_lightning {
	int line;
	struct timeval tv;
	struct rpt *rpt;
	struct lockthread lockthread;
} lock_ring[32];

int lock_ring_index = 0;

AST_MUTEX_DEFINE_STATIC(locklock);

static struct lockthread *get_lockthread(pthread_t id)
{
	int i;

	for (i = 0; i < MAXLOCKTHREAD; i++) {
		if (lockthreads[i].id == id)
			return (&lockthreads[i]);
	}
	return (NULL);
}

static struct lockthread *put_lockthread(pthread_t id)
{
	int i;

	for (i = 0; i < MAXLOCKTHREAD; i++) {
		if (lockthreads[i].id == id)
			return (&lockthreads[i]);
	}
	for (i = 0; i < MAXLOCKTHREAD; i++) {
		if (!lockthreads[i].id) {
			lockthreads[i].lockcount = 0;
			lockthreads[i].lastlock = 0;
			lockthreads[i].lastunlock = 0;
			lockthreads[i].id = id;
			return (&lockthreads[i]);
		}
	}
	return (NULL);
}

/*
 * Functions related to the threading used in app_rpt dealing with locking
*/

static void rpt_mutex_spew(void)
{
	struct by_lightning lock_ring_copy[32];
	int lock_ring_index_copy;
	int i, j;
	long long diff;
	char a[100];
	struct timeval lasttv;

	ast_mutex_lock(&locklock);
	memcpy(&lock_ring_copy, &lock_ring, sizeof(lock_ring_copy));
	lock_ring_index_copy = lock_ring_index;
	ast_mutex_unlock(&locklock);

	lasttv.tv_sec = lasttv.tv_usec = 0;
	for (i = 0; i < 32; i++) {
		j = (i + lock_ring_index_copy) % 32;
		strftime(a, sizeof(a) - 1, "%m/%d/%Y %H:%M:%S", localtime(&lock_ring_copy[j].tv.tv_sec));
		diff = 0;
		if (lasttv.tv_sec) {
			diff = (lock_ring_copy[j].tv.tv_sec - lasttv.tv_sec)
				* 1000000;
			diff += (lock_ring_copy[j].tv.tv_usec - lasttv.tv_usec);
		}
		lasttv.tv_sec = lock_ring_copy[j].tv.tv_sec;
		lasttv.tv_usec = lock_ring_copy[j].tv.tv_usec;
		if (!lock_ring_copy[j].tv.tv_sec)
			continue;
		if (lock_ring_copy[j].line < 0) {
			ast_log(LOG_NOTICE, "LOCKDEBUG [#%d] UNLOCK app_rpt.c:%d node %s pid %x diff %lld us at %s.%06d\n",
					i - 31, -lock_ring_copy[j].line, lock_ring_copy[j].rpt->name, (int) lock_ring_copy[j].lockthread.id,
					diff, a, (int) lock_ring_copy[j].tv.tv_usec);
		} else {
			ast_log(LOG_NOTICE, "LOCKDEBUG [#%d] LOCK app_rpt.c:%d node %s pid %x diff %lld us at %s.%06d\n",
					i - 31, lock_ring_copy[j].line, lock_ring_copy[j].rpt->name, (int) lock_ring_copy[j].lockthread.id,
					diff, a, (int) lock_ring_copy[j].tv.tv_usec);
		}
	}
}

static void _rpt_mutex_lock(ast_mutex_t * lockp, struct rpt *myrpt, int line)
{
	struct lockthread *t;
	pthread_t id;

	id = pthread_self();
	ast_mutex_lock(&locklock);
	t = put_lockthread(id);
	if (!t) {
		ast_mutex_unlock(&locklock);
		return;
	}
	if (t->lockcount) {
		int lastline = t->lastlock;
		ast_mutex_unlock(&locklock);
		ast_log(LOG_NOTICE, "rpt_mutex_lock: Double lock request line %d node %s pid %x, last lock was line %d\n", line,
				myrpt->name, (int) t->id, lastline);
		rpt_mutex_spew();
		return;
	}
	t->lastlock = line;
	t->lockcount = 1;
	gettimeofday(&lock_ring[lock_ring_index].tv, NULL);
	lock_ring[lock_ring_index].rpt = myrpt;
	memcpy(&lock_ring[lock_ring_index].lockthread, t, sizeof(struct lockthread));
	lock_ring[lock_ring_index++].line = line;
	if (lock_ring_index == 32)
		lock_ring_index = 0;
	ast_mutex_unlock(&locklock);
	ast_mutex_lock(lockp);
}

static void _rpt_mutex_unlock(ast_mutex_t * lockp, struct rpt *myrpt, int line)
{
	struct lockthread *t;
	pthread_t id;

	id = pthread_self();
	ast_mutex_lock(&locklock);
	t = put_lockthread(id);
	if (!t) {
		ast_mutex_unlock(&locklock);
		return;
	}
	if (!t->lockcount) {
		int lastline = t->lastunlock;
		ast_mutex_unlock(&locklock);
		ast_log(LOG_NOTICE, "rpt_mutex_lock: Double un-lock request line %d node %s pid %x, last un-lock was line %d\n",
				line, myrpt->name, (int) t->id, lastline);
		rpt_mutex_spew();
		return;
	}
	t->lastunlock = line;
	t->lockcount = 0;
	gettimeofday(&lock_ring[lock_ring_index].tv, NULL);
	lock_ring[lock_ring_index].rpt = myrpt;
	memcpy(&lock_ring[lock_ring_index].lockthread, t, sizeof(struct lockthread));
	lock_ring[lock_ring_index++].line = -line;
	if (lock_ring_index == 32)
		lock_ring_index = 0;
	ast_mutex_unlock(&locklock);
	ast_mutex_unlock(lockp);
}

#endif							/* APP_RPT_LOCK_DEBUG */
