#define HAVE_SYSDEP
#define HAVE_KERNEL_FLAGS
static void
check_sysdep (conf_t * conf, struct utsname *un)
{
  strcpy (conf->cplusplus, "g++ -I.");

  /* fixup machine names */
  if (strcmp (un->machine, "BePC") == 0)
    {
      strcpy (conf->arch, "i586");
    }
  if (strcmp (un->machine, "BePC") == 0 ||
      strcmp (un->machine, "i386") == 0 ||
      strcmp (un->machine, "i486") == 0 ||
      strcmp (un->machine, "i586") == 0 ||
      strcmp (un->machine, "i686") == 0 ||
      strcmp (un->machine, "x86_64") == 0)
    {
      strcpy (conf->platform, "i86pc");
    }
  if (strcmp (un->machine, "BeMac") == 0 ||
      strcmp (un->machine, "BeBox") == 0)
    {
      /* seems to be what Linux uses */
      /* XXX: check for ppc64 ? */
      strcpy (conf->arch, "ppc");
      strcpy (conf->platform, "ppc");
    }
}

static void
add_kernel_flags (FILE * f)
{
  fprintf (f, "CFLAGS=-O2 -D_KERNEL -D_KERNEL_MODE=1 \
  	-fno-strict-aliasing -fno-builtin-fork -fno-builtin-vfork \
  	-finline -fno-builtin -ffreestanding -fno-exceptions -fno-omit-frame-pointer\n");
}
