
#include "mybmm.h"
#include "uuid.h"

int inverter_read(mybmm_inverter_t *inv) {
	int r;

	if (!inv) return 1;
	mybmm_clear_state(inv,MYBMM_INVERTER_STATE_UPDATED);

//	dprintf(3,"inv: name: %s, type: %s, transport: %s\n", inv->name, inv->type, inv->transport);
	dprintf(5,"%s: opening...\n", inv->name);
	if (inv->open(inv->handle)) {
		dprintf(1,"%s: open error\n",inv->name);
		return 1;
	}
	dprintf(5,"%s: reading...\n", inv->name);
	r = inv->read(inv->handle,0,0);
	dprintf(5,"%s: closing\n", inv->name);
	inv->close(inv->handle);
	dprintf(5,"%s: returning: %d\n", inv->name, r);
	if (!r) mybmm_set_state(inv,MYBMM_INVERTER_STATE_UPDATED);
	return r;
}

int inverter_write(mybmm_inverter_t *inv) {
	if (!inv) return 1;
	if (inv->open(inv->handle)) return 1;
	if (inv->write(inv->handle) < 0) {
		dprintf(1,"error writing to %s\n", inv->name);
		return 1;
	}
	inv->close(inv->handle);
	return 0;
}

static void *inverter_thread(void *arg) {
	mybmm_config_t *conf = arg;
	mybmm_inverter_t *inv = conf->inverter;
	int is_open,r;

	/* All we do here is just write out the vals every <inverter_update> seconds*/
	is_open = (inv->open(inv->handle) == 0);
	while(1) {
		mybmm_clear_state(inv,MYBMM_INVERTER_STATE_UPDATED);
		dprintf(1,"is_open: %d\n", is_open);
		if (!is_open) {
			is_open = (inv->open(inv->handle) == 0);
			if (!is_open) {
				sleep(1);
				continue;
			}
		}
		dprintf(1,"reading...\n");
		r = inv->read(inv->handle);
		dprintf(1,"r: %d\n", r);
		if (!r) mybmm_set_state(inv,MYBMM_INVERTER_STATE_UPDATED);
		else continue;
		dprintf(1,"writing...\n");
		inv->write(inv->handle);
		sleep(1);
	}
	inv->close(inv->handle);
	return 0;
}

static void get_tab(mybmm_config_t *conf, char *name,mybmm_inverter_t *inv) {
        struct cfg_proctab invertertab[] = {
		{ name, "name", "Inverter name", DATA_TYPE_STRING,&inv->name,sizeof(inv->name), 0 },
		{ name, "uuid", "Inverter UUID", DATA_TYPE_STRING,&inv->uuid,sizeof(inv->uuid), 0 },
		{ name, "type", "Inverter type", DATA_TYPE_STRING,&inv->type,sizeof(inv->type), 0 },
		{ name, "transport", "Transport", DATA_TYPE_STRING,&inv->transport,sizeof(inv->transport), 0 },
		{ name, "target", "Transport-specific", DATA_TYPE_STRING,&inv->target,sizeof(inv->target), 0 },
		{ name, "params", "Inverter-specific params", DATA_TYPE_STRING,&inv->params,sizeof(inv->params), 0 },
		CFG_PROCTAB_END

	};

	dprintf(2,"name: %s\n", name);

	cfg_get_tab(conf->cfg,invertertab);
	if (!strlen(inv->name)) strcpy(inv->name,name);
	if (debug >= 3) cfg_disp_tab(invertertab,0,1);

	/* if we dont have a UUID, gen one */
	if (!strlen(inv->uuid)) {
		uint8_t uuid[16];

		dprintf(1,"gen'ing UUID...\n");
		uuid_generate_random(uuid);
		uuid_unparse(uuid, inv->uuid);
		dprintf(1,"inv->uuid: %s\n", inv->uuid);
		printf("name: %s\n", name);
		cfg_set_item(conf->cfg,name,"uuid",0,inv->uuid);
		/* Signal conf to save the file */
		mybmm_set_state(conf,MYBMM_CONFIG_DIRTY);
	}

	return;
}

int inverter_add(mybmm_config_t *conf, mybmm_inverter_t *inv) {
	mybmm_module_t *mp, *tp;

	/* Get the transport */
	tp = mybmm_load_module(conf,inv->transport,MYBMM_MODTYPE_TRANSPORT);
	if (!tp) return 1;

	/* Load our module */
	mp = mybmm_load_module(conf,inv->type,MYBMM_MODTYPE_INVERTER);
	if (!mp) return 1;

	/* Create an instance of the inverter*/
	dprintf(3,"mp: %p\n",mp);
	if (mp) {
		inv->handle = mp->new(conf, inv, tp);
		if (!inv->handle) {
			fprintf(stderr,"module %s->new returned null!\n", mp->name);
			return 1;
		}
	}

	/* Get capability mask */
	dprintf(1,"capabilities: %02x\n", mp->capabilities);
	inv->capabilities = mp->capabilities;

	/* Set the convienience funcs */
	inv->open = mp->open;
	inv->read = mp->read;
	inv->write = mp->write;
	inv->close = mp->close;

	/* Update conf */
	conf->inverter = inv;

	dprintf(3,"done!\n");
	return 0;
}

int inverter_init(mybmm_config_t *conf) {
	mybmm_inverter_t *inv;

	/* Get the inverter config, if any (not an error if not found) */
	if (!cfg_get_item(conf->cfg,"inverter","type")) return 0;

	inv = calloc(1,sizeof(*inv));
	if (!inv) {
		perror("calloc inverter\n");
		return 1;
	}
	get_tab(conf,"inverter",inv);
	return inverter_add(conf,inv);
}


int inverter_start_update(mybmm_config_t *conf) {
	pthread_attr_t attr;

	/* Create a detached thread */
	dprintf(3,"Creating thread...\n");
	if (pthread_attr_init(&attr)) {
		dprintf(0,"pthread_attr_init: %s\n",strerror(errno));
		return 1;
	}
	if (pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED)) {
		dprintf(0,"pthread_attr_setdetachstate: %s\n",strerror(errno));
		return 1;
	}
	if (pthread_create(&conf->inverter_tid,&attr,&inverter_thread,conf)) {
		dprintf(0,"pthread_create: %s\n",strerror(errno));
		return 1;
	}
	return 0;
}
