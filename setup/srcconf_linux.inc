#define HAVE_SYSDEP
static void
check_sysdep (conf_t * conf, struct utsname *un)
{
  /*
   * Check if cc supports -fno-stack-protector
   */
  if (system("cc -c -o srcconf.o -fno-stack-protector setup/srcconf.c >/dev/null 2>&1")==0)
     {
  	strcpy(conf->OSflags, "-ffreestanding -fno-common -fno-stack-protector");
	unlink("srcconf.o");
     }
  else
     {
  	strcpy(conf->OSflags, "-ffreestanding -fno-common");
     }

  strcpy (conf->cplusplus, "g++ -fno-rtti -fno-exceptions -I.");
  strcpy (conf->platform, "i86pc");
}
