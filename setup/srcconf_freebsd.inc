#define HAVE_SYSDEP
static void
check_sysdep (conf_t * conf, struct utsname *un)
{
  strcpy (conf->cplusplus, "g++ -fno-rtti -fno-exceptions -I.");
  strcpy (conf->platform, "i86pc");
}
