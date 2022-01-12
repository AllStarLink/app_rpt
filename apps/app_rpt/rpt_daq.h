
/*
 * Open a daq device
 */
struct daq_entry_tag *daq_open(int type, char *name, char *dev);

/*
 * Close a daq device
 */
int daq_close(struct daq_entry_tag *t);

/*
 * Look up a device entry for a particular device name
 */
struct daq_entry_tag *daq_devtoentry(char *name);

/*
 * Do something with the daq subsystem
 */
int daq_do_long( struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);

/*
 * Short version of above
 */
int daq_do( struct daq_entry_tag *t, int pin, int cmd, int arg1);

/*
 * Function to reset the long term minimum or maximum
 */
int daq_reset_minmax(char *device, int pin, int minmax);

/*
 * Initialize DAQ subsystem
 */
void daq_init(struct ast_config *cfg);

/*
 * Uninitialize DAQ Subsystem
 */
void daq_uninit(void);
