#define COPYING3 Copyright (C) Hannu Savolainen and Dev Mazumdar 2000-2006. All rights reserved.
// VU meter computation macros
//
// Simple VU
#define VU(vuvar, input) \
  TSTNEG(VUTMP, input, input, 0);\
  LIMIT(vuvar, vuvar, vuvar, VUTMP)
/* VU meter with volume scaling */
#define VUSCALE(vuvar, input, scale) \
  MACS(VUTMP2, 0, input, scale) ;\
  TSTNEG(VUTMP, VUTMP2, VUTMP2, 0);\
  LIMIT(vuvar, vuvar, vuvar, VUTMP)
/* VU meter with volume scaling and summing with a 'base' signal */
#define VUSCALESUM(vuvar, basevar, input, scale) \
  MACS(VUTMP2, basevar, input, scale) ;\
  TSTNEG(VUTMP, VUTMP2, VUTMP2, 0);\
  LIMIT(vuvar, vuvar, vuvar, VUTMP)
