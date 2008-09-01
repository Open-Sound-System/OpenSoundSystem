#define HAVE_SYSDEP
static void
check_sysdep (conf_t * conf, struct utsname *un)
{
  if (strcmp (un->sysname, "Haiku") == 0)
    strcpy (un->sysname, "BeOS");

  strcpy (conf->cplusplus, "g++ -fno-rtti -fno-exceptions -I.");

  /* fixup machine names */
  if (strcmp (un->machine, "BePC") == 0)
    {
      strcpy (conf->arch, "i586");
    }
  if (strcmp (un->machine, "BePC") == 0 ||
      strcmp (un->machine, "i386") == 0 ||
      strcmp (un->machine, "i486") == 0 ||
      strcmp (un->machine, "i586") == 0 || strcmp (un->machine, "i686") == 0)
    {
      strcpy (conf->platform, "i86pc");
    }
  if (strcmp (un->machine, "BeMac") == 0 ||
      strcmp (un->machine, "BeBox") == 0)
    {
      /* XXX: those are PPC 
      strcpy (conf->arch, "ppc");
      strcpy (conf->platform, "ppc");*/
      fprintf(stderr, "FIXME: convert arch/machine BeMac/BeBox\n");
      exit(1);
    }
}
