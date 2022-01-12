

/*
 * Start the Uchameleon monitor thread
 */
int uchameleon_thread_start(struct daq_entry_tag *t);
int uchameleon_connect(struct daq_entry_tag *t);

/*
 * Uchameleon alarm handler
 */
void uchameleon_alarm_handler(struct daq_pin_entry_tag *p);

/*
 * Initialize pins
 */
int uchameleon_pin_init(struct daq_entry_tag *t);

/*
 * Open the serial channel and test for the uchameleon device at the end of the link
 */
int uchameleon_open(struct daq_entry_tag *t);

/*
 * Close uchameleon
 */
int uchameleon_close(struct daq_entry_tag *t);

/*
 * Uchameleon generic interface which supports monitor thread
 */
int uchameleon_do_long( struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);

/*
 * Reset a minimum or maximum reading
 */
int uchameleon_reset_minmax(struct daq_entry_tag *t, int pin, int minmax);

/*
 * Queue up a tx command (used exclusively by uchameleon_monitor() )
 */
void uchameleon_queue_tx(struct daq_entry_tag *t, char *txbuff);

/*
 * Monitor thread for Uchameleon devices
 *
 * started by uchameleon_open() and shutdown by uchameleon_close()
 *
 */
void *uchameleon_monitor_thread(void *this);
