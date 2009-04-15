
int
DRIVER_NAME(void)
{
#if DRIVER_TYPE == DRV_PCI
	if (id_table[0] == 0)
           {
		cmn_err (CE_WARN, DRIVER_NICK ": ID table is empty\n");
		return OSS_EIO;
           }
#endif

	return 0;
}
