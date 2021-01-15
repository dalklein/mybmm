
#include <dlfcn.h>
#include <errno.h>
#include "mybmm.h"
#include "cfg.h"

int read_config(mybmm_config_t *conf) {
        struct cfg_proctab myconf[] = {
		{ "mybmm", "interval", "Time between updates", DATA_TYPE_INT, &conf->interval, 0, "20" },
		{ "mybmm", "capacity", "Force Battery Capacity", DATA_TYPE_INT, &conf->user_capacity, 0, "-1" },
		{ "mybmm", "max_charge_amps", "Charge Current", DATA_TYPE_FLOAT, &conf->max_charge_amps, 0, 0 },
		{ "mybmm", "max_discharge_amps", "Discharge Current", DATA_TYPE_FLOAT, &conf->max_discharge_amps, 0, 0 },
		{ "mybmm", "charge_voltage", "Charge Voltage", DATA_TYPE_FLOAT, &conf->charge_voltage, 0, 0 },
		{ "mybmm", "charge_amps", "Charge Current", DATA_TYPE_FLOAT, &conf->charge_amps, 0, 0 },
		{ "mybmm", "system_voltage", "Battery/System Voltage", DATA_TYPE_INT, &conf->system_voltage, 0, "48" },
		{ "mybmm", "battery_chem", "Battery Chemistry", DATA_TYPE_INT, &conf->battery_chem, 0, "1" },
		{ "mybmm", "cells", "Number of battery cells per pack", DATA_TYPE_INT, &conf->cells, 0, "14" },
		{ "mybmm", "cell_low", "Cell low voltage", DATA_TYPE_FLOAT, &conf->cell_low, 0, "-1" },
		{ "mybmm", "cell_crit_low", "Critical cell low voltage", DATA_TYPE_FLOAT, &conf->cell_crit_low, 0, "-1" },
		{ "mybmm", "cell_high", "Cell high voltage", DATA_TYPE_FLOAT, &conf->cell_high, 0, "-1" },
		{ "mybmm", "cell_crit_high", "Critical cell high voltage", DATA_TYPE_FLOAT, &conf->cell_crit_high, 0, "-1" },
		{ "mybmm", "c_rate", "Current rate", DATA_TYPE_FLOAT, &conf->c_rate, 0, "-1" },
		{ "mybmm", "database", "Database Connection", DATA_TYPE_STRING, &conf->db_name, sizeof(conf->db_name), "" },
		{ "mybmm", "user_soc", "Force State of Charge", DATA_TYPE_FLOAT, &conf->user_soc, 0, "-1.0" },
		CFG_PROCTAB_END
	};

	conf->cfg = cfg_read(conf->filename);
	dprintf(3,"cfg: %p\n", conf->cfg);
	if (!conf->cfg) {
		printf("error: unable to read config file '%s': %s\n", conf->filename, strerror(errno));
		return 1;
	}

	cfg_get_tab(conf->cfg,myconf);
#ifdef DEBUG
	if (debug) cfg_disp_tab(myconf,0,1);
#endif

	dprintf(1,"db_name: %s\n", conf->db_name);
//	if (strlen(conf->db_name)) db_init(conf,conf->db_name);

	/* Set battery chem parms if not set by user */
	switch(conf->battery_chem) {
	default:
	case BATTERY_CHEM_LITHIUM:
		dprintf(1,"battery_chem: LITHIUM\n");
		if (conf->cell_low < 0) conf->cell_low = 3.2;
		if (conf->cell_crit_low < 0) conf->cell_crit_low = 2.8;
		if (conf->cell_high < 0) conf->cell_high = 4.05;
		if (conf->cell_crit_high < 0) conf->cell_crit_high = 4.2;
		if (conf->c_rate < 0) conf->c_rate = .5;
		break;
	case BATTERY_CHEM_LIFEPO4:
		dprintf(1,"battery_chem: LITHIUM\n");
		if (conf->cell_low < 0) conf->cell_low = 3.0;
		if (conf->cell_crit_low < 0) conf->cell_crit_low = 2.5;
		if (conf->cell_high < 0) conf->cell_high = 3.4;
		if (conf->cell_crit_high < 0) conf->cell_crit_high = 3.65;
		if (conf->c_rate < 0) conf->c_rate = .4;
		break;
	case BATTERY_CHEM_TITANATE:
		dprintf(1,"battery_chem: LITHIUM\n");
		if (conf->cell_low < 0) conf->cell_low = 2.0;
		if (conf->cell_crit_low < 0) conf->cell_crit_low = 1.8;
		if (conf->cell_high < 0) conf->cell_high = 2.65;
		if (conf->cell_crit_high < 0) conf->cell_crit_high = 2.85;
		if (conf->c_rate < 0) conf->c_rate = 10;
		break;
	case BATTERY_CHEM_UNKNOWN:
		dprintf(1,"battery_chem: UNKNOWN\n");
		return 1;
		break;
	}

#if 0
	conf->dlsym_handle = dlopen(0,RTLD_LAZY);
	if (!conf->dlsym_handle) {
		printf("error getting dlsym_handle: %s\n",dlerror());
		return 1;
	}
	dprintf(3,"dlsym_handle: %p\n",conf->dlsym_handle);
#endif

	/* Init inverter */
	if (inverter_init(conf)) return 1;

	/* Init battery pack */
	if (pack_init(conf)) return 1;

	/* If config is dirty, write it back out */
	dprintf(1,"conf: dirty? %d\n", mybmm_check_state(conf,MYBMM_CONFIG_DIRTY));
	if (mybmm_check_state(conf,MYBMM_CONFIG_DIRTY)) {
		cfg_write(conf->cfg);
		mybmm_clear_state(conf,MYBMM_CONFIG_DIRTY);
	}

	return 0;
}

int reconfig(mybmm_config_t *conf) {
	dprintf(1,"destroying lists...\n");
	list_destroy(conf->modules);
	list_destroy(conf->packs);
	dprintf(1,"destroying threads...\n");
	pthread_cancel(conf->inverter_tid);
	pthread_cancel(conf->pack_tid);
	dprintf(1,"creating lists...\n");
	conf->modules = list_create();
	conf->packs = list_create();
	free(conf->cfg);
	return read_config(conf);
}

mybmm_config_t *get_config(char *filename) {
	mybmm_config_t *conf;

	conf = calloc(1,sizeof(mybmm_config_t));
	if (!conf) {
		perror("malloc mybmm_config_t");
		return 0;
	}
	conf->modules = list_create();
	conf->packs = list_create();

	conf->filename = filename;
	if (read_config(conf)) {
		free(conf);
		return 0;
	}
	return conf;
}