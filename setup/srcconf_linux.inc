#define HAVE_SYSDEP
static void
check_sysdep (conf_t * conf, struct utsname *un)
{
  char *p;

  char cmd[512];

#ifndef __arm__
  vmix_mode = "FLOAT";
  exact_architectures = 1; /* Compile only the drivers with targetcpu=arm */
#endif

  if ((p=getenv("CROSSCOMPILE"))!=NULL)
     {
	if (strcmp(p, "uclinux-blackfin")==0)
	   {
		hostcc="cc";
		targetcc="uclinux-blackfin-gcc";
	   }
     }

  sprintf(conf->ccomp, "%s -ffreestanding", targetcc);

  /*
   * Check if cc supports -fno-stack-protector
   */
  sprintf (cmd, "%s -c -o srcconf.o -fno-stack-protector setup/srcconf.c >/dev/null 2>&1", targetcc);
  if (system(cmd)==0)
     {
	if (do_warning_checks)
  	   strcpy(conf->OSflags, "-fno-common -fno-stack-protector -Wall -Werror");
else
  	   strcpy(conf->OSflags, "-fno-common -fno-stack-protector");
	unlink("srcconf.o");
     }
  else
     {
	if (do_warning_checks)
  	   strcpy(conf->OSflags, "-fno-common -Wall -Werror");
	else
  	   strcpy(conf->OSflags, "-fno-common");
     }

  strcpy (conf->cplusplus, "g++ -fno-rtti -fno-exceptions -I.");
  strcpy (conf->platform, "i86pc");
}
