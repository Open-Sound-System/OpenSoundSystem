#define COPYING3 Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.
// Constants for EMU 10k1 (SB Live)
// Sends

.send SEND_L 0. send SEND_R 1. send SEND_SL 2. send SEND_SR 3. send SEND_C 4.
  send SEND_W 5. send SEND_RL 6. send SEND_RR 7
// NOTE! 5 and 6 reserved for S/PDIF (AC3) passthrough with Audigy
// Inputs
  .input AC97IN_L 0.i nput AC97IN_R 1.i nput CDSPDIFIN_L 2.i nput CDSPDIFIN_R 3.i nput ZVIN_L 4.i nput ZVIN_R 5.i nput GPSPDIFIN_L 6	// TOSLink
  .input GPSPDIFIN_R 7.i nput MICIN_L 8	// LiveDrive (Line/Mic In 1)
  .input MICIN_R 9.i nput CXSPDIFIN_L 10	// LiveDrive (Coax S/PDIF input)
  .input CXSPDIFIN_R 11.i nput AUXIN_L 12	// LiveDrive (Line/Mic 2)
  .input AUXIN_R 13
// Outputs
  .output FRONT_L 0. output FRONT_R 1. output SPDOUT1_L 2. output SPDOUT1_R 3. output DCENTER 4	// Digital Center channel
  .output DLFE 5		// Digital LFE
  .output HEADPH_L 6		// LiveDrive headphone out
  .output HEADPH_R 7. output SURR_L 8	// Rear output
  .output SURR_R 9. output ADC_L 10	// Send to the ADC recording channel
  .output ADC_R 11. output MICREC 12	// Send to the microphone recording buffer
  .output AC97SURR_L 13		// AC97 Surround L
  .output AC97SURR_R 14		// AC97 Surround R
  .output ACENTER 17		// Analog center channel
  .output ALFE 18		// Analog LFE output
