#define HAVE_SYSDEP
static void
check_sysdep (conf_t * conf, struct utsname *un)
{
  char *p;

  char cmd[512];

  vmix_mode = "FLOAT";

  if ((p=getenv("CROSSCOMPILE"))!=NULL)
     {
	if (strcmp(p, "uclinux-blackfin")==0)
	   {
		hostcc="cc";
		targetcc="uclinux-blackfin-gcc";
	   }
     }

  strcpy(conf->ccomp, targetcc);

  /*
   * Check if cc supports -fno-stack-protector
   */
  sprintf (cmd, "%s -c -o srcconf.o -fno-stack-protector setup/srcconf.c >/dev/null 2>&1", targetcc);
  if (system(cmd)==0)
     {
  	strcpy(conf->OSflags, "-ffreestanding -fno-common -fno-stack-protector -Wall -Werror");
	unlink("srcconf.o");
     }
  else
     {
  	strcpy(conf->OSflags, "-ffreestanding -fno-common -Wall -Werror");
     }

  strcpy (conf->cplusplus, "g++ -fno-rtti -fno-exceptions -I.");
  strcpy (conf->platform, "i86pc");
}
