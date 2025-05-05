
/*! \file
 *
 * \brief Uchameleon specific routines
 */

/*! \brief Start the Uchameleon monitor thread */
int uchameleon_thread_start(struct daq_entry_tag *t);

int uchameleon_connect(struct daq_entry_tag *t);

/*! \brief Uchameleon alarm handler */
void uchameleon_alarm_handler(struct daq_pin_entry_tag *p);

/*! \brief Initialize pins */
int uchameleon_pin_init(struct daq_entry_tag *t);

/*! \brief Open the serial channel and test for the uchameleon device at the end of the link */
int uchameleon_open(struct daq_entry_tag *t);

/*! \brief Close uchameleon */
int uchameleon_close(struct daq_entry_tag *t);

/*! \brief Uchameleon generic interface which supports monitor thread */
int uchameleon_do_long(struct daq_entry_tag *t, int pin, enum rpt_daq_cmd cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);

/*! \brief Queue up a tx command (used exclusively by uchameleon_monitor()) */
void uchameleon_queue_tx(struct daq_entry_tag *t, char *txbuff);

/*! \brief Monitor thread for Uchameleon devices */
/*! \note started by uchameleon_open() and shutdown by uchameleon_close() */
void *uchameleon_monitor_thread(void *this);
