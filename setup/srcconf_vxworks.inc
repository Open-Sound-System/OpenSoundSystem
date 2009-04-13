
static void
vxworks_setup(conf_t *conf, char *arch)
{
	char tmp[256];

	printf ("\n*** Setting up cross compiling for VxWorks %s target ***\n\n", arch);

	config_vxworks=1;
	hostcc="cc";

#ifdef linux
	targetcc="cc";
#else
	sprintf(tmp, "cc%s", arch);
	targetcc=strdup(tmp);
#endif
	strcpy(conf->OSflags, "");
	strcpy(conf->platform, "vxworks");
	strcpy(conf->ccomp, targetcc);
	strcpy(this_os, "kernel/OS/VxWorks");
	strcpy(conf->system, "VxWorks");
	strcpy(conf->arch, arch);
	sprintf(conf->cplusplus, "c++%s", arch);

	shlib_cflags = "-shared -fPIC";
	shlib_ldflags = "-shared -fPIC";

	strcpy (conf->endianess, "LITTLE"); // TODO
}

static void
vxworks_genheader(FILE *f, char *path)
{
#ifdef linux
	fprintf (f, "\n");
	fprintf (f, "WIND_BASE=/disk6/vxworks/tornado-2.0\n");
	//fprintf (f, "CFLAGS+=-mpentium -ansi -nostdinc -O2 -fvolatile -nostdlib -fno-builtin -fno-defer-pop -Wall -I$(WIND_BASE)/target/config/all\n");
	fprintf (f, "CFLAGS+=-nostdinc -O2 -nostdlib -fno-builtin -fno-defer-pop -Wall -I$(WIND_BASE)/target/config/all\n");
	fprintf (f, "CFLAGS+=-I$(WIND_BASE)/target/h -I$(WIND_BASE)/target/src/config -I$(WIND_BASE)/target/src/drv -DCPU=PENTIUM\n");
	fprintf (f, "\n");
#else
	fprintf (f, "\n");
	fprintf (f, "WIND_BASE=/usr/tornado-2.0\n");
	fprintf (f, "CPU=PENTIUM\n");
	fprintf (f, "TOOL=gnu\n");
	fprintf (f, "WIND_HOST_TYPE=sun4-solaris2\n");
	fprintf (f, "\n");
	fprintf (f, "include $(WIND_BASE)/target/h/make/defs.bsp\n");
	fprintf (f, "include $(WIND_BASE)/target/h/make/make.$(CPU)$(TOOL)\n");
	fprintf (f, "include $(WIND_BASE)/target/h/make/defs.$(WIND_HOST_TYPE)\n");
	fprintf (f, "\n");
	fprintf (f, "exe: all\n");
	fprintf (f, "\n");
#endif
}
