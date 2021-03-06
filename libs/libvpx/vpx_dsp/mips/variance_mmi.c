/*
 *  Copyright (c) 2017 The WebM project authors. All Rights Reserved.
 *
 *  Use of this source code is governed by a BSD-style license
 *  that can be found in the LICENSE file in the root of the source
 *  tree. An additional intellectual property rights grant can be found
 *  in the file PATENTS.  All contributing project authors may
 *  be found in the AUTHORS file in the root of the source tree.
 */

#include "./vpx_dsp_rtcd.h"
#include "vpx_dsp/variance.h"
#include "vpx_ports/mem.h"
#include "vpx/vpx_integer.h"
#include "vpx_ports/asmdefs_mmi.h"

static const uint8_t bilinear_filters[8][2] = {
  { 128, 0 }, { 112, 16 }, { 96, 32 }, { 80, 48 },
  { 64, 64 }, { 48, 80 },  { 32, 96 }, { 16, 112 },
};

/* Use VARIANCE_SSE_SUM_8_FOR_W64 in vpx_variance64x64,vpx_variance64x32,
   vpx_variance32x64. VARIANCE_SSE_SUM_8 will lead to sum overflow. */
#define VARIANCE_SSE_SUM_8_FOR_W64                                  \
  /* sse */                                                         \
  "pasubub    %[ftmp3],   %[ftmp1],       %[ftmp2]            \n\t" \
  "punpcklbh  %[ftmp4],   %[ftmp3],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp5],   %[ftmp3],       %[ftmp0]            \n\t" \
  "pmaddhw    %[ftmp6],   %[ftmp4],       %[ftmp4]            \n\t" \
  "pmaddhw    %[ftmp7],   %[ftmp5],       %[ftmp5]            \n\t" \
  "paddw      %[ftmp10],  %[ftmp10],      %[ftmp6]            \n\t" \
  "paddw      %[ftmp10],  %[ftmp10],      %[ftmp7]            \n\t" \
                                                                    \
  /* sum */                                                         \
  "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]            \n\t" \
  "punpcklhw  %[ftmp1],   %[ftmp3],       %[ftmp0]            \n\t" \
  "punpckhhw  %[ftmp2],   %[ftmp3],       %[ftmp0]            \n\t" \
  "punpcklhw  %[ftmp7],   %[ftmp5],       %[ftmp0]            \n\t" \
  "punpckhhw  %[ftmp8],   %[ftmp5],       %[ftmp0]            \n\t" \
  "psubw      %[ftmp3],   %[ftmp1],       %[ftmp7]            \n\t" \
  "psubw      %[ftmp5],   %[ftmp2],       %[ftmp8]            \n\t" \
  "punpcklhw  %[ftmp1],   %[ftmp4],       %[ftmp0]            \n\t" \
  "punpckhhw  %[ftmp2],   %[ftmp4],       %[ftmp0]            \n\t" \
  "punpcklhw  %[ftmp7],   %[ftmp6],       %[ftmp0]            \n\t" \
  "punpckhhw  %[ftmp8],   %[ftmp6],       %[ftmp0]            \n\t" \
  "psubw      %[ftmp4],   %[ftmp1],       %[ftmp7]            \n\t" \
  "psubw      %[ftmp6],   %[ftmp2],       %[ftmp8]            \n\t" \
  "paddw      %[ftmp9],   %[ftmp9],       %[ftmp3]            \n\t" \
  "paddw      %[ftmp9],   %[ftmp9],       %[ftmp4]            \n\t" \
  "paddw      %[ftmp9],   %[ftmp9],       %[ftmp5]            \n\t" \
  "paddw      %[ftmp9],   %[ftmp9],       %[ftmp6]            \n\t"

#define VARIANCE_SSE_SUM_4                                          \
  /* sse */                                                         \
  "pasubub    %[ftmp3],   %[ftmp1],       %[ftmp2]            \n\t" \
  "punpcklbh  %[ftmp4],   %[ftmp3],       %[ftmp0]            \n\t" \
  "pmaddhw    %[ftmp5],   %[ftmp4],       %[ftmp4]            \n\t" \
  "paddw      %[ftmp6],   %[ftmp6],       %[ftmp5]            \n\t" \
                                                                    \
  /* sum */                                                         \
  "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpcklbh  %[ftmp4],   %[ftmp2],       %[ftmp0]            \n\t" \
  "paddh      %[ftmp7],   %[ftmp7],       %[ftmp3]            \n\t" \
  "paddh      %[ftmp8],   %[ftmp8],       %[ftmp4]            \n\t"

#define VARIANCE_SSE_SUM_8                                          \
  /* sse */                                                         \
  "pasubub    %[ftmp3],   %[ftmp1],       %[ftmp2]            \n\t" \
  "punpcklbh  %[ftmp4],   %[ftmp3],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp5],   %[ftmp3],       %[ftmp0]            \n\t" \
  "pmaddhw    %[ftmp6],   %[ftmp4],       %[ftmp4]            \n\t" \
  "pmaddhw    %[ftmp7],   %[ftmp5],       %[ftmp5]            \n\t" \
  "paddw      %[ftmp8],   %[ftmp8],       %[ftmp6]            \n\t" \
  "paddw      %[ftmp8],   %[ftmp8],       %[ftmp7]            \n\t" \
                                                                    \
  /* sum */                                                         \
  "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp4],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpcklbh  %[ftmp5],   %[ftmp2],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp6],   %[ftmp2],       %[ftmp0]            \n\t" \
  "paddh      %[ftmp10],  %[ftmp10],      %[ftmp3]            \n\t" \
  "paddh      %[ftmp10],  %[ftmp10],      %[ftmp4]            \n\t" \
  "paddh      %[ftmp12],  %[ftmp12],      %[ftmp5]            \n\t" \
  "paddh      %[ftmp12],  %[ftmp12],      %[ftmp6]            \n\t"

#define VARIANCE_SSE_8                                              \
  "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t" \
  "gsldlc1    %[ftmp2],   0x07(%[b])                          \n\t" \
  "gsldrc1    %[ftmp2],   0x00(%[b])                          \n\t" \
  "pasubub    %[ftmp3],   %[ftmp1],       %[ftmp2]            \n\t" \
  "punpcklbh  %[ftmp4],   %[ftmp3],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp5],   %[ftmp3],       %[ftmp0]            \n\t" \
  "pmaddhw    %[ftmp6],   %[ftmp4],       %[ftmp4]            \n\t" \
  "pmaddhw    %[ftmp7],   %[ftmp5],       %[ftmp5]            \n\t" \
  "paddw      %[ftmp8],   %[ftmp8],       %[ftmp6]            \n\t" \
  "paddw      %[ftmp8],   %[ftmp8],       %[ftmp7]            \n\t"

#define VARIANCE_SSE_16                                             \
  VARIANCE_SSE_8                                                    \
  "gsldlc1    %[ftmp1],   0x0f(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x08(%[a])                          \n\t" \
  "gsldlc1    %[ftmp2],   0x0f(%[b])                          \n\t" \
  "gsldrc1    %[ftmp2],   0x08(%[b])                          \n\t" \
  "pasubub    %[ftmp3],   %[ftmp1],       %[ftmp2]            \n\t" \
  "punpcklbh  %[ftmp4],   %[ftmp3],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp5],   %[ftmp3],       %[ftmp0]            \n\t" \
  "pmaddhw    %[ftmp6],   %[ftmp4],       %[ftmp4]            \n\t" \
  "pmaddhw    %[ftmp7],   %[ftmp5],       %[ftmp5]            \n\t" \
  "paddw      %[ftmp8],   %[ftmp8],       %[ftmp6]            \n\t" \
  "paddw      %[ftmp8],   %[ftmp8],       %[ftmp7]            \n\t"

#define VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_4_A                       \
  /* calculate fdata3[0]~fdata3[3], store at ftmp2*/                \
  "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t" \
  "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t" \
  "gsldlc1    %[ftmp1],   0x08(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x01(%[a])                          \n\t" \
  "punpcklbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t" \
  "pmullh     %[ftmp2],   %[ftmp2],       %[filter_x0]        \n\t" \
  "paddh      %[ftmp2],   %[ftmp2],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp3],   %[ftmp3],       %[filter_x1]        \n\t" \
  "paddh      %[ftmp2],   %[ftmp2],       %[ftmp3]            \n\t" \
  "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp6]            \n\t"

#define VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_4_B                       \
  /* calculate fdata3[0]~fdata3[3], store at ftmp4*/                \
  "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t" \
  "punpcklbh  %[ftmp4],   %[ftmp1],       %[ftmp0]            \n\t" \
  "gsldlc1    %[ftmp1],   0x08(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x01(%[a])                          \n\t" \
  "punpcklbh  %[ftmp5],   %[ftmp1],       %[ftmp0]            \n\t" \
  "pmullh     %[ftmp4],   %[ftmp4],       %[filter_x0]        \n\t" \
  "paddh      %[ftmp4],   %[ftmp4],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp5],   %[ftmp5],       %[filter_x1]        \n\t" \
  "paddh      %[ftmp4],   %[ftmp4],       %[ftmp5]            \n\t" \
  "psrlh      %[ftmp4],   %[ftmp4],       %[ftmp6]            \n\t"

#define VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_4_A                      \
  /* calculate: temp2[0] ~ temp2[3] */                              \
  "pmullh     %[ftmp2],   %[ftmp2],       %[filter_y0]        \n\t" \
  "paddh      %[ftmp2],   %[ftmp2],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp4],       %[filter_y1]        \n\t" \
  "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t" \
  "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp6]            \n\t" \
                                                                    \
  /* store: temp2[0] ~ temp2[3] */                                  \
  "and        %[ftmp2],   %[ftmp2],       %[mask]             \n\t" \
  "packushb   %[ftmp2],   %[ftmp2],       %[ftmp0]            \n\t" \
  "gssdrc1    %[ftmp2],   0x00(%[temp2_ptr])                  \n\t"

#define VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_4_B                      \
  /* calculate: temp2[0] ~ temp2[3] */                              \
  "pmullh     %[ftmp4],   %[ftmp4],       %[filter_y0]        \n\t" \
  "paddh      %[ftmp4],   %[ftmp4],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp2],       %[filter_y1]        \n\t" \
  "paddh      %[ftmp4],   %[ftmp4],       %[ftmp1]            \n\t" \
  "psrlh      %[ftmp4],   %[ftmp4],       %[ftmp6]            \n\t" \
                                                                    \
  /* store: temp2[0] ~ temp2[3] */                                  \
  "and        %[ftmp4],   %[ftmp4],       %[mask]             \n\t" \
  "packushb   %[ftmp4],   %[ftmp4],       %[ftmp0]            \n\t" \
  "gssdrc1    %[ftmp4],   0x00(%[temp2_ptr])                  \n\t"

#define VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_8_A                       \
  /* calculate fdata3[0]~fdata3[7], store at ftmp2 and ftmp3*/      \
  "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t" \
  "punpcklbh  %[ftmp2],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp3],   %[ftmp1],       %[ftmp0]            \n\t" \
  "gsldlc1    %[ftmp1],   0x08(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x01(%[a])                          \n\t" \
  "punpcklbh  %[ftmp4],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp5],   %[ftmp1],       %[ftmp0]            \n\t" \
  "pmullh     %[ftmp2],   %[ftmp2],       %[filter_x0]        \n\t" \
  "pmullh     %[ftmp3],   %[ftmp3],       %[filter_x0]        \n\t" \
  "paddh      %[ftmp2],   %[ftmp2],       %[ff_ph_40]         \n\t" \
  "paddh      %[ftmp3],   %[ftmp3],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp4],   %[ftmp4],       %[filter_x1]        \n\t" \
  "pmullh     %[ftmp5],   %[ftmp5],       %[filter_x1]        \n\t" \
  "paddh      %[ftmp2],   %[ftmp2],       %[ftmp4]            \n\t" \
  "paddh      %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t" \
  "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp14]           \n\t" \
  "psrlh      %[ftmp3],   %[ftmp3],       %[ftmp14]           \n\t"

#define VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_8_B                       \
  /* calculate fdata3[0]~fdata3[7], store at ftmp8 and ftmp9*/      \
  "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t" \
  "punpcklbh  %[ftmp8],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp9],   %[ftmp1],       %[ftmp0]            \n\t" \
  "gsldlc1    %[ftmp1],   0x08(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x01(%[a])                          \n\t" \
  "punpcklbh  %[ftmp10],  %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp11],  %[ftmp1],       %[ftmp0]            \n\t" \
  "pmullh     %[ftmp8],   %[ftmp8],       %[filter_x0]        \n\t" \
  "pmullh     %[ftmp9],   %[ftmp9],       %[filter_x0]        \n\t" \
  "paddh      %[ftmp8],   %[ftmp8],       %[ff_ph_40]         \n\t" \
  "paddh      %[ftmp9],   %[ftmp9],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp10],  %[ftmp10],      %[filter_x1]        \n\t" \
  "pmullh     %[ftmp11],  %[ftmp11],      %[filter_x1]        \n\t" \
  "paddh      %[ftmp8],   %[ftmp8],       %[ftmp10]           \n\t" \
  "paddh      %[ftmp9],   %[ftmp9],       %[ftmp11]           \n\t" \
  "psrlh      %[ftmp8],   %[ftmp8],       %[ftmp14]           \n\t" \
  "psrlh      %[ftmp9],   %[ftmp9],       %[ftmp14]           \n\t"

#define VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_8_A                      \
  /* calculate: temp2[0] ~ temp2[3] */                              \
  "pmullh     %[ftmp2],   %[ftmp2],       %[filter_y0]        \n\t" \
  "paddh      %[ftmp2],   %[ftmp2],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp8],       %[filter_y1]        \n\t" \
  "paddh      %[ftmp2],   %[ftmp2],       %[ftmp1]            \n\t" \
  "psrlh      %[ftmp2],   %[ftmp2],       %[ftmp14]           \n\t" \
                                                                    \
  /* calculate: temp2[4] ~ temp2[7] */                              \
  "pmullh     %[ftmp3],   %[ftmp3],       %[filter_y0]        \n\t" \
  "paddh      %[ftmp3],   %[ftmp3],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp9],       %[filter_y1]        \n\t" \
  "paddh      %[ftmp3],   %[ftmp3],       %[ftmp1]            \n\t" \
  "psrlh      %[ftmp3],   %[ftmp3],       %[ftmp14]           \n\t" \
                                                                    \
  /* store: temp2[0] ~ temp2[7] */                                  \
  "and        %[ftmp2],   %[ftmp2],       %[mask]             \n\t" \
  "and        %[ftmp3],   %[ftmp3],       %[mask]             \n\t" \
  "packushb   %[ftmp2],   %[ftmp2],       %[ftmp3]            \n\t" \
  "gssdlc1    %[ftmp2],   0x07(%[temp2_ptr])                  \n\t" \
  "gssdrc1    %[ftmp2],   0x00(%[temp2_ptr])                  \n\t"

#define VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_8_B                      \
  /* calculate: temp2[0] ~ temp2[3] */                              \
  "pmullh     %[ftmp8],   %[ftmp8],       %[filter_y0]        \n\t" \
  "paddh      %[ftmp8],   %[ftmp8],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp2],       %[filter_y1]        \n\t" \
  "paddh      %[ftmp8],   %[ftmp8],       %[ftmp1]            \n\t" \
  "psrlh      %[ftmp8],   %[ftmp8],       %[ftmp14]           \n\t" \
                                                                    \
  /* calculate: temp2[4] ~ temp2[7] */                              \
  "pmullh     %[ftmp9],   %[ftmp9],       %[filter_y0]        \n\t" \
  "paddh      %[ftmp9],   %[ftmp9],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp3],       %[filter_y1]        \n\t" \
  "paddh      %[ftmp9],   %[ftmp9],       %[ftmp1]            \n\t" \
  "psrlh      %[ftmp9],   %[ftmp9],       %[ftmp14]           \n\t" \
                                                                    \
  /* store: temp2[0] ~ temp2[7] */                                  \
  "and        %[ftmp8],   %[ftmp8],       %[mask]             \n\t" \
  "and        %[ftmp9],   %[ftmp9],       %[mask]             \n\t" \
  "packushb   %[ftmp8],   %[ftmp8],       %[ftmp9]            \n\t" \
  "gssdlc1    %[ftmp8],   0x07(%[temp2_ptr])                  \n\t" \
  "gssdrc1    %[ftmp8],   0x00(%[temp2_ptr])                  \n\t"

#define VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_16_A                      \
  /* calculate fdata3[0]~fdata3[7], store at ftmp2 and ftmp3*/      \
  VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_8_A                             \
                                                                    \
  /* calculate fdata3[8]~fdata3[15], store at ftmp4 and ftmp5*/     \
  "gsldlc1    %[ftmp1],   0x0f(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x08(%[a])                          \n\t" \
  "punpcklbh  %[ftmp4],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp5],   %[ftmp1],       %[ftmp0]            \n\t" \
  "gsldlc1    %[ftmp1],   0x10(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x09(%[a])                          \n\t" \
  "punpcklbh  %[ftmp6],   %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp7],   %[ftmp1],       %[ftmp0]            \n\t" \
  "pmullh     %[ftmp4],   %[ftmp4],       %[filter_x0]        \n\t" \
  "pmullh     %[ftmp5],   %[ftmp5],       %[filter_x0]        \n\t" \
  "paddh      %[ftmp4],   %[ftmp4],       %[ff_ph_40]         \n\t" \
  "paddh      %[ftmp5],   %[ftmp5],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp6],   %[ftmp6],       %[filter_x1]        \n\t" \
  "pmullh     %[ftmp7],   %[ftmp7],       %[filter_x1]        \n\t" \
  "paddh      %[ftmp4],   %[ftmp4],       %[ftmp6]            \n\t" \
  "paddh      %[ftmp5],   %[ftmp5],       %[ftmp7]            \n\t" \
  "psrlh      %[ftmp4],   %[ftmp4],       %[ftmp14]           \n\t" \
  "psrlh      %[ftmp5],   %[ftmp5],       %[ftmp14]           \n\t"

#define VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_16_B                      \
  /* calculate fdata3[0]~fdata3[7], store at ftmp8 and ftmp9*/      \
  VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_8_B                             \
                                                                    \
  /* calculate fdata3[8]~fdata3[15], store at ftmp10 and ftmp11*/   \
  "gsldlc1    %[ftmp1],   0x0f(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x08(%[a])                          \n\t" \
  "punpcklbh  %[ftmp10],  %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp11],  %[ftmp1],       %[ftmp0]            \n\t" \
  "gsldlc1    %[ftmp1],   0x10(%[a])                          \n\t" \
  "gsldrc1    %[ftmp1],   0x09(%[a])                          \n\t" \
  "punpcklbh  %[ftmp12],  %[ftmp1],       %[ftmp0]            \n\t" \
  "punpckhbh  %[ftmp13],  %[ftmp1],       %[ftmp0]            \n\t" \
  "pmullh     %[ftmp10],  %[ftmp10],      %[filter_x0]        \n\t" \
  "pmullh     %[ftmp11],  %[ftmp11],      %[filter_x0]        \n\t" \
  "paddh      %[ftmp10],  %[ftmp10],      %[ff_ph_40]         \n\t" \
  "paddh      %[ftmp11],  %[ftmp11],      %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp12],  %[ftmp12],      %[filter_x1]        \n\t" \
  "pmullh     %[ftmp13],  %[ftmp13],      %[filter_x1]        \n\t" \
  "paddh      %[ftmp10],  %[ftmp10],      %[ftmp12]           \n\t" \
  "paddh      %[ftmp11],  %[ftmp11],      %[ftmp13]           \n\t" \
  "psrlh      %[ftmp10],  %[ftmp10],      %[ftmp14]           \n\t" \
  "psrlh      %[ftmp11],  %[ftmp11],      %[ftmp14]           \n\t"

#define VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_16_A                     \
  VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_8_A                            \
                                                                    \
  /* calculate: temp2[8] ~ temp2[11] */                             \
  "pmullh     %[ftmp4],   %[ftmp4],       %[filter_y0]        \n\t" \
  "paddh      %[ftmp4],   %[ftmp4],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp10],      %[filter_y1]        \n\t" \
  "paddh      %[ftmp4],   %[ftmp4],       %[ftmp1]            \n\t" \
  "psrlh      %[ftmp4],   %[ftmp4],       %[ftmp14]           \n\t" \
                                                                    \
  /* calculate: temp2[12] ~ temp2[15] */                            \
  "pmullh     %[ftmp5],   %[ftmp5],       %[filter_y0]        \n\t" \
  "paddh      %[ftmp5],   %[ftmp5],       %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp11],       %[filter_y1]       \n\t" \
  "paddh      %[ftmp5],   %[ftmp5],       %[ftmp1]            \n\t" \
  "psrlh      %[ftmp5],   %[ftmp5],       %[ftmp14]           \n\t" \
                                                                    \
  /* store: temp2[8] ~ temp2[15] */                                 \
  "and        %[ftmp4],   %[ftmp4],       %[mask]             \n\t" \
  "and        %[ftmp5],   %[ftmp5],       %[mask]             \n\t" \
  "packushb   %[ftmp4],   %[ftmp4],       %[ftmp5]            \n\t" \
  "gssdlc1    %[ftmp4],   0x0f(%[temp2_ptr])                  \n\t" \
  "gssdrc1    %[ftmp4],   0x08(%[temp2_ptr])                  \n\t"

#define VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_16_B                     \
  VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_8_B                            \
                                                                    \
  /* calculate: temp2[8] ~ temp2[11] */                             \
  "pmullh     %[ftmp10],  %[ftmp10],      %[filter_y0]        \n\t" \
  "paddh      %[ftmp10],  %[ftmp10],      %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp4],       %[filter_y1]        \n\t" \
  "paddh      %[ftmp10],  %[ftmp10],      %[ftmp1]            \n\t" \
  "psrlh      %[ftmp10],  %[ftmp10],      %[ftmp14]           \n\t" \
                                                                    \
  /* calculate: temp2[12] ~ temp2[15] */                            \
  "pmullh     %[ftmp11],  %[ftmp11],      %[filter_y0]        \n\t" \
  "paddh      %[ftmp11],  %[ftmp11],      %[ff_ph_40]         \n\t" \
  "pmullh     %[ftmp1],   %[ftmp5],       %[filter_y1]        \n\t" \
  "paddh      %[ftmp11],  %[ftmp11],      %[ftmp1]            \n\t" \
  "psrlh      %[ftmp11],  %[ftmp11],      %[ftmp14]           \n\t" \
                                                                    \
  /* store: temp2[8] ~ temp2[15] */                                 \
  "and        %[ftmp10],  %[ftmp10],      %[mask]             \n\t" \
  "and        %[ftmp11],  %[ftmp11],      %[mask]             \n\t" \
  "packushb   %[ftmp10],  %[ftmp10],      %[ftmp11]           \n\t" \
  "gssdlc1    %[ftmp10],  0x0f(%[temp2_ptr])                  \n\t" \
  "gssdrc1    %[ftmp10],  0x08(%[temp2_ptr])                  \n\t"

// Applies a 1-D 2-tap bilinear filter to the source block in either horizontal
// or vertical direction to produce the filtered output block. Used to implement
// the first-pass of 2-D separable filter.
//
// Produces int16_t output to retain precision for the next pass. Two filter
// taps should sum to FILTER_WEIGHT. pixel_step defines whether the filter is
// applied horizontally (pixel_step = 1) or vertically (pixel_step = stride).
// It defines the offset required to move from one input to the next.
static void var_filter_block2d_bil_first_pass(const uint8_t *a, uint16_t *b,
                                              unsigned int src_pixels_per_line,
                                              int pixel_step,
                                              unsigned int output_height,
                                              unsigned int output_width,
                                              const uint8_t *filter) {
  unsigned int i, j;

  for (i = 0; i < output_height; ++i) {
    for (j = 0; j < output_width; ++j) {
      b[j] = ROUND_POWER_OF_TWO(
          (int)a[0] * filter[0] + (int)a[pixel_step] * filter[1], FILTER_BITS);

      ++a;
    }

    a += src_pixels_per_line - output_width;
    b += output_width;
  }
}

// Applies a 1-D 2-tap bilinear filter to the source block in either horizontal
// or vertical direction to produce the filtered output block. Used to implement
// the second-pass of 2-D separable filter.
//
// Requires 16-bit input as produced by filter_block2d_bil_first_pass. Two
// filter taps should sum to FILTER_WEIGHT. pixel_step defines whether the
// filter is applied horizontally (pixel_step = 1) or vertically
// (pixel_step = stride). It defines the offset required to move from one input
// to the next. Output is 8-bit.
static void var_filter_block2d_bil_second_pass(const uint16_t *a, uint8_t *b,
                                               unsigned int src_pixels_per_line,
                                               unsigned int pixel_step,
                                               unsigned int output_height,
                                               unsigned int output_width,
                                               const uint8_t *filter) {
  unsigned int i, j;

  for (i = 0; i < output_height; ++i) {
    for (j = 0; j < output_width; ++j) {
      b[j] = ROUND_POWER_OF_TWO(
          (int)a[0] * filter[0] + (int)a[pixel_step] * filter[1], FILTER_BITS);
      ++a;
    }

    a += src_pixels_per_line - output_width;
    b += output_width;
  }
}

static inline uint32_t vpx_variance64x(const uint8_t *a, int a_stride,
                                       const uint8_t *b, int b_stride,
                                       uint32_t *sse, int high) {
  int sum;
  double ftmp[12];
  uint32_t tmp[3];

  *sse = 0;

  __asm__ volatile (
    "li         %[tmp0],    0x20                                \n\t"
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"
    MMI_L(%[tmp0], %[high], 0x00)
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    "xor        %[ftmp9],   %[ftmp9],       %[ftmp9]            \n\t"
    "xor        %[ftmp10],  %[ftmp10],      %[ftmp10]           \n\t"
    "1:                                                         \n\t"
    "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x07(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x00(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x0f(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x08(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x0f(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x08(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x17(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x10(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x17(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x10(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x1f(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x18(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x1f(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x18(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x27(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x20(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x27(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x20(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x2f(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x28(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x2f(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x28(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x37(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x30(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x37(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x30(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x3f(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x38(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x3f(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x38(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "addiu      %[tmp0],    %[tmp0],        -0x01               \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    MMI_ADDU(%[b], %[b], %[b_stride])
    "bnez       %[tmp0],    1b                                  \n\t"

    "mfc1       %[tmp1],    %[ftmp9]                            \n\t"
    "mfhc1      %[tmp2],    %[ftmp9]                            \n\t"
    "addu       %[sum],     %[tmp1],        %[tmp2]             \n\t"
    "dsrl       %[ftmp1],   %[ftmp10],      %[ftmp11]           \n\t"
    "paddw      %[ftmp1],   %[ftmp1],       %[ftmp10]           \n\t"
    "swc1       %[ftmp1],   0x00(%[sse])                        \n\t"
    : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
      [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
      [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
      [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
      [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
      [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
      [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
      [tmp2]"=&r"(tmp[2]),
      [a]"+&r"(a),                      [b]"+&r"(b),
      [sum]"=&r"(sum)
    : [a_stride]"r"((mips_reg)a_stride),[b_stride]"r"((mips_reg)b_stride),
      [high]"r"(&high), [sse]"r"(sse)
    : "memory"
  );

  return *sse - (((int64_t)sum * sum) / (64 * high));
}

#define VPX_VARIANCE64XN(n)                                         \
  uint32_t vpx_variance64x##n##_mmi(const uint8_t *a, int a_stride, \
                                    const uint8_t *b, int b_stride, \
                                    uint32_t *sse) {                \
    return vpx_variance64x(a, a_stride, b, b_stride, sse, n);       \
  }

VPX_VARIANCE64XN(64)
VPX_VARIANCE64XN(32)

uint32_t vpx_variance32x64_mmi(const uint8_t *a, int a_stride, const uint8_t *b,
                               int b_stride, uint32_t *sse) {
  int sum;
  double ftmp[12];
  uint32_t tmp[3];

  *sse = 0;

  __asm__ volatile (
    "li         %[tmp0],    0x20                                \n\t"
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"
    "li         %[tmp0],    0x40                                \n\t"
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    "xor        %[ftmp9],   %[ftmp9],       %[ftmp9]            \n\t"
    "xor        %[ftmp10],  %[ftmp10],      %[ftmp10]           \n\t"
    "1:                                                         \n\t"
    "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x07(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x00(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x0f(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x08(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x0f(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x08(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x17(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x10(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x17(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x10(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "gsldlc1    %[ftmp1],   0x1f(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x18(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x1f(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x18(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8_FOR_W64

    "addiu      %[tmp0],    %[tmp0],        -0x01               \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    MMI_ADDU(%[b], %[b], %[b_stride])
    "bnez       %[tmp0],    1b                                  \n\t"

    "mfc1       %[tmp1],    %[ftmp9]                            \n\t"
    "mfhc1      %[tmp2],    %[ftmp9]                            \n\t"
    "addu       %[sum],     %[tmp1],        %[tmp2]             \n\t"
    "dsrl       %[ftmp1],   %[ftmp10],      %[ftmp11]           \n\t"
    "paddw      %[ftmp1],   %[ftmp1],       %[ftmp10]           \n\t"
    "swc1       %[ftmp1],   0x00(%[sse])                        \n\t"
    : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
      [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
      [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
      [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
      [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
      [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
      [tmp0]"=&r"(tmp[0]),              [tmp1]"=&r"(tmp[1]),
      [tmp2]"=&r"(tmp[2]),
      [a]"+&r"(a),                      [b]"+&r"(b),
      [sum]"=&r"(sum)
    : [a_stride]"r"((mips_reg)a_stride),[b_stride]"r"((mips_reg)b_stride),
      [sse]"r"(sse)
    : "memory"
  );

  return *sse - (((int64_t)sum * sum) / 2048);
}

static inline uint32_t vpx_variance32x(const uint8_t *a, int a_stride,
                                       const uint8_t *b, int b_stride,
                                       uint32_t *sse, int high) {
  int sum;
  double ftmp[13];
  uint32_t tmp[3];

  *sse = 0;

  __asm__ volatile (
    "li         %[tmp0],    0x20                                \n\t"
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"
    MMI_L(%[tmp0], %[high], 0x00)
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]            \n\t"
    "xor        %[ftmp10],  %[ftmp10],      %[ftmp10]           \n\t"
    "xor        %[ftmp12],  %[ftmp12],      %[ftmp12]           \n\t"
    "1:                                                         \n\t"
    "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x07(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x00(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8
    "gsldlc1    %[ftmp1],   0x0f(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x08(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x0f(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x08(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8
    "gsldlc1    %[ftmp1],   0x17(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x10(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x17(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x10(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8
    "gsldlc1    %[ftmp1],   0x1f(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x18(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x1f(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x18(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8

    "addiu      %[tmp0],    %[tmp0],        -0x01               \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    MMI_ADDU(%[b], %[b], %[b_stride])
    "bnez       %[tmp0],    1b                                  \n\t"

    "dsrl       %[ftmp9],   %[ftmp8],       %[ftmp11]           \n\t"
    "paddw      %[ftmp9],   %[ftmp9],       %[ftmp8]            \n\t"
    "swc1       %[ftmp9],   0x00(%[sse])                        \n\t"

    "punpcklhw  %[ftmp3],   %[ftmp10],      %[ftmp0]            \n\t"
    "punpckhhw  %[ftmp4],   %[ftmp10],      %[ftmp0]            \n\t"
    "punpcklhw  %[ftmp5],   %[ftmp12],      %[ftmp0]            \n\t"
    "punpckhhw  %[ftmp6],   %[ftmp12],      %[ftmp0]            \n\t"
    "paddw      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"
    "psubw      %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"
    "psubw      %[ftmp3],   %[ftmp3],       %[ftmp6]            \n\t"
    "dsrl       %[ftmp0],   %[ftmp3],       %[ftmp11]           \n\t"
    "paddw      %[ftmp0],   %[ftmp0],       %[ftmp3]            \n\t"
    "swc1       %[ftmp0],   0x00(%[sum])                        \n\t"

    : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
      [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
      [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
      [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
      [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
      [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
      [ftmp12]"=&f"(ftmp[12]),          [tmp0]"=&r"(tmp[0]),
      [a]"+&r"(a),                      [b]"+&r"(b)
    : [a_stride]"r"((mips_reg)a_stride),[b_stride]"r"((mips_reg)b_stride),
      [high]"r"(&high), [sse]"r"(sse), [sum]"r"(&sum)
    : "memory"
  );

  return *sse - (((int64_t)sum * sum) / (32 * high));
}

#define VPX_VARIANCE32XN(n)                                         \
  uint32_t vpx_variance32x##n##_mmi(const uint8_t *a, int a_stride, \
                                    const uint8_t *b, int b_stride, \
                                    uint32_t *sse) {                \
    return vpx_variance32x(a, a_stride, b, b_stride, sse, n);       \
  }

VPX_VARIANCE32XN(32)
VPX_VARIANCE32XN(16)

static inline uint32_t vpx_variance16x(const uint8_t *a, int a_stride,
                                       const uint8_t *b, int b_stride,
                                       uint32_t *sse, int high) {
  int sum;
  double ftmp[13];
  uint32_t tmp[3];

  *sse = 0;

  __asm__ volatile (
    "li         %[tmp0],    0x20                                \n\t"
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"
    MMI_L(%[tmp0], %[high], 0x00)
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]            \n\t"
    "xor        %[ftmp10],  %[ftmp10],      %[ftmp10]           \n\t"
    "xor        %[ftmp12],  %[ftmp12],      %[ftmp12]           \n\t"
    "1:                                                         \n\t"
    "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x07(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x00(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8
    "gsldlc1    %[ftmp1],   0x0f(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x08(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x0f(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x08(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8

    "addiu      %[tmp0],    %[tmp0],        -0x01               \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    MMI_ADDU(%[b], %[b], %[b_stride])
    "bnez       %[tmp0],    1b                                  \n\t"

    "dsrl       %[ftmp9],   %[ftmp8],       %[ftmp11]           \n\t"
    "paddw      %[ftmp9],   %[ftmp9],       %[ftmp8]            \n\t"
    "swc1       %[ftmp9],   0x00(%[sse])                        \n\t"

    "punpcklhw  %[ftmp3],   %[ftmp10],      %[ftmp0]            \n\t"
    "punpckhhw  %[ftmp4],   %[ftmp10],      %[ftmp0]            \n\t"
    "punpcklhw  %[ftmp5],   %[ftmp12],      %[ftmp0]            \n\t"
    "punpckhhw  %[ftmp6],   %[ftmp12],      %[ftmp0]            \n\t"
    "paddw      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"
    "psubw      %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"
    "psubw      %[ftmp3],   %[ftmp3],       %[ftmp6]            \n\t"
    "dsrl       %[ftmp0],   %[ftmp3],       %[ftmp11]           \n\t"
    "paddw      %[ftmp0],   %[ftmp0],       %[ftmp3]            \n\t"
    "swc1       %[ftmp0],   0x00(%[sum])                        \n\t"

    : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
      [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
      [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
      [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
      [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
      [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
      [ftmp12]"=&f"(ftmp[12]),          [tmp0]"=&r"(tmp[0]),
      [a]"+&r"(a),                      [b]"+&r"(b)
    : [a_stride]"r"((mips_reg)a_stride),[b_stride]"r"((mips_reg)b_stride),
      [high]"r"(&high), [sse]"r"(sse), [sum]"r"(&sum)
    : "memory"
  );

  return *sse - (((int64_t)sum * sum) / (16 * high));
}

#define VPX_VARIANCE16XN(n)                                         \
  uint32_t vpx_variance16x##n##_mmi(const uint8_t *a, int a_stride, \
                                    const uint8_t *b, int b_stride, \
                                    uint32_t *sse) {                \
    return vpx_variance16x(a, a_stride, b, b_stride, sse, n);       \
  }

VPX_VARIANCE16XN(32)
VPX_VARIANCE16XN(16)
VPX_VARIANCE16XN(8)

static inline uint32_t vpx_variance8x(const uint8_t *a, int a_stride,
                                      const uint8_t *b, int b_stride,
                                      uint32_t *sse, int high) {
  int sum;
  double ftmp[13];
  uint32_t tmp[3];

  *sse = 0;

  __asm__ volatile (
    "li         %[tmp0],    0x20                                \n\t"
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"
    MMI_L(%[tmp0], %[high], 0x00)
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]            \n\t"
    "xor        %[ftmp10],  %[ftmp10],      %[ftmp10]           \n\t"
    "xor        %[ftmp12],  %[ftmp12],      %[ftmp12]           \n\t"
    "1:                                                         \n\t"
    "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x07(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x00(%[b])                          \n\t"
    VARIANCE_SSE_SUM_8

    "addiu      %[tmp0],    %[tmp0],        -0x01               \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    MMI_ADDU(%[b], %[b], %[b_stride])
    "bnez       %[tmp0],    1b                                  \n\t"

    "dsrl       %[ftmp9],   %[ftmp8],       %[ftmp11]           \n\t"
    "paddw      %[ftmp9],   %[ftmp9],       %[ftmp8]            \n\t"
    "swc1       %[ftmp9],   0x00(%[sse])                        \n\t"

    "punpcklhw  %[ftmp3],   %[ftmp10],      %[ftmp0]            \n\t"
    "punpckhhw  %[ftmp4],   %[ftmp10],      %[ftmp0]            \n\t"
    "punpcklhw  %[ftmp5],   %[ftmp12],      %[ftmp0]            \n\t"
    "punpckhhw  %[ftmp6],   %[ftmp12],      %[ftmp0]            \n\t"
    "paddw      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"
    "psubw      %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"
    "psubw      %[ftmp3],   %[ftmp3],       %[ftmp6]            \n\t"
    "dsrl       %[ftmp0],   %[ftmp3],       %[ftmp11]           \n\t"
    "paddw      %[ftmp0],   %[ftmp0],       %[ftmp3]            \n\t"
    "swc1       %[ftmp0],   0x00(%[sum])                        \n\t"

    : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
      [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
      [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
      [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
      [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
      [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
      [ftmp12]"=&f"(ftmp[12]),          [tmp0]"=&r"(tmp[0]),
      [a]"+&r"(a),                      [b]"+&r"(b)
    : [a_stride]"r"((mips_reg)a_stride),[b_stride]"r"((mips_reg)b_stride),
      [high]"r"(&high), [sse]"r"(sse), [sum]"r"(&sum)
    : "memory"
  );

  return *sse - (((int64_t)sum * sum) / (8 * high));
}

#define VPX_VARIANCE8XN(n)                                         \
  uint32_t vpx_variance8x##n##_mmi(const uint8_t *a, int a_stride, \
                                   const uint8_t *b, int b_stride, \
                                   uint32_t *sse) {                \
    return vpx_variance8x(a, a_stride, b, b_stride, sse, n);       \
  }

VPX_VARIANCE8XN(16)
VPX_VARIANCE8XN(8)
VPX_VARIANCE8XN(4)

static inline uint32_t vpx_variance4x(const uint8_t *a, int a_stride,
                                      const uint8_t *b, int b_stride,
                                      uint32_t *sse, int high) {
  int sum;
  double ftmp[12];
  uint32_t tmp[3];

  *sse = 0;

  __asm__ volatile (
    "li         %[tmp0],    0x20                                \n\t"
    "mtc1       %[tmp0],    %[ftmp10]                           \n\t"
    MMI_L(%[tmp0], %[high], 0x00)
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    "xor        %[ftmp6],   %[ftmp6],       %[ftmp6]            \n\t"
    "xor        %[ftmp7],   %[ftmp7],       %[ftmp7]            \n\t"
    "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]            \n\t"
    "1:                                                         \n\t"
    "gsldlc1    %[ftmp1],   0x07(%[a])                          \n\t"
    "gsldrc1    %[ftmp1],   0x00(%[a])                          \n\t"
    "gsldlc1    %[ftmp2],   0x07(%[b])                          \n\t"
    "gsldrc1    %[ftmp2],   0x00(%[b])                          \n\t"
    VARIANCE_SSE_SUM_4

    "addiu      %[tmp0],    %[tmp0],        -0x01               \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    MMI_ADDU(%[b], %[b], %[b_stride])
    "bnez       %[tmp0],    1b                                  \n\t"

    "dsrl       %[ftmp9],   %[ftmp6],       %[ftmp10]           \n\t"
    "paddw      %[ftmp9],   %[ftmp9],       %[ftmp6]            \n\t"
    "swc1       %[ftmp9],   0x00(%[sse])                        \n\t"

    "punpcklhw  %[ftmp3],   %[ftmp7],       %[ftmp0]            \n\t"
    "punpckhhw  %[ftmp4],   %[ftmp7],       %[ftmp0]            \n\t"
    "punpcklhw  %[ftmp5],   %[ftmp8],       %[ftmp0]            \n\t"
    "punpckhhw  %[ftmp6],   %[ftmp8],       %[ftmp0]            \n\t"
    "paddw      %[ftmp3],   %[ftmp3],       %[ftmp4]            \n\t"
    "psubw      %[ftmp3],   %[ftmp3],       %[ftmp5]            \n\t"
    "psubw      %[ftmp3],   %[ftmp3],       %[ftmp6]            \n\t"
    "dsrl       %[ftmp0],   %[ftmp3],       %[ftmp10]           \n\t"
    "paddw      %[ftmp0],   %[ftmp0],       %[ftmp3]            \n\t"
    "swc1       %[ftmp0],   0x00(%[sum])                        \n\t"
    : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
      [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
      [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
      [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
      [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
      [ftmp10]"=&f"(ftmp[10]),
      [tmp0]"=&r"(tmp[0]),
      [a]"+&r"(a),                      [b]"+&r"(b)
    : [a_stride]"r"((mips_reg)a_stride),[b_stride]"r"((mips_reg)b_stride),
      [high]"r"(&high), [sse]"r"(sse), [sum]"r"(&sum)
    : "memory"
  );

  return *sse - (((int64_t)sum * sum) / (4 * high));
}

#define VPX_VARIANCE4XN(n)                                         \
  uint32_t vpx_variance4x##n##_mmi(const uint8_t *a, int a_stride, \
                                   const uint8_t *b, int b_stride, \
                                   uint32_t *sse) {                \
    return vpx_variance4x(a, a_stride, b, b_stride, sse, n);       \
  }

VPX_VARIANCE4XN(8)
VPX_VARIANCE4XN(4)

static inline uint32_t vpx_mse16x(const uint8_t *a, int a_stride,
                                  const uint8_t *b, int b_stride, uint32_t *sse,
                                  uint64_t high) {
  double ftmp[12];
  uint32_t tmp[1];

  *sse = 0;

  __asm__ volatile (
    "li         %[tmp0],    0x20                                \n\t"
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"
    MMI_L(%[tmp0], %[high], 0x00)
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]            \n\t"

    "1:                                                         \n\t"
    VARIANCE_SSE_16

    "addiu      %[tmp0],    %[tmp0],        -0x01               \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    MMI_ADDU(%[b], %[b], %[b_stride])
    "bnez       %[tmp0],    1b                                  \n\t"

    "dsrl       %[ftmp9],   %[ftmp8],       %[ftmp11]           \n\t"
    "paddw      %[ftmp9],   %[ftmp9],       %[ftmp8]            \n\t"
    "swc1       %[ftmp9],   0x00(%[sse])                        \n\t"
    : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
      [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
      [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
      [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
      [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
      [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
      [tmp0]"=&r"(tmp[0]),
      [a]"+&r"(a),                      [b]"+&r"(b)
    : [a_stride]"r"((mips_reg)a_stride),[b_stride]"r"((mips_reg)b_stride),
      [high]"r"(&high), [sse]"r"(sse)
    : "memory"
  );

  return *sse;
}

#define vpx_mse16xN(n)                                         \
  uint32_t vpx_mse16x##n##_mmi(const uint8_t *a, int a_stride, \
                               const uint8_t *b, int b_stride, \
                               uint32_t *sse) {                \
    return vpx_mse16x(a, a_stride, b, b_stride, sse, n);       \
  }

vpx_mse16xN(16);
vpx_mse16xN(8);

static inline uint32_t vpx_mse8x(const uint8_t *a, int a_stride,
                                 const uint8_t *b, int b_stride, uint32_t *sse,
                                 uint64_t high) {
  double ftmp[12];
  uint32_t tmp[1];

  *sse = 0;

  __asm__ volatile (
    "li         %[tmp0],    0x20                                \n\t"
    "mtc1       %[tmp0],    %[ftmp11]                           \n\t"
    MMI_L(%[tmp0], %[high], 0x00)
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    "xor        %[ftmp8],   %[ftmp8],       %[ftmp8]            \n\t"

    "1:                                                         \n\t"
    VARIANCE_SSE_8

    "addiu      %[tmp0],    %[tmp0],        -0x01               \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    MMI_ADDU(%[b], %[b], %[b_stride])
    "bnez       %[tmp0],    1b                                  \n\t"

    "dsrl       %[ftmp9],   %[ftmp8],       %[ftmp11]           \n\t"
    "paddw      %[ftmp9],   %[ftmp9],       %[ftmp8]            \n\t"
    "swc1       %[ftmp9],   0x00(%[sse])                        \n\t"
    : [ftmp0]"=&f"(ftmp[0]),            [ftmp1]"=&f"(ftmp[1]),
      [ftmp2]"=&f"(ftmp[2]),            [ftmp3]"=&f"(ftmp[3]),
      [ftmp4]"=&f"(ftmp[4]),            [ftmp5]"=&f"(ftmp[5]),
      [ftmp6]"=&f"(ftmp[6]),            [ftmp7]"=&f"(ftmp[7]),
      [ftmp8]"=&f"(ftmp[8]),            [ftmp9]"=&f"(ftmp[9]),
      [ftmp10]"=&f"(ftmp[10]),          [ftmp11]"=&f"(ftmp[11]),
      [tmp0]"=&r"(tmp[0]),
      [a]"+&r"(a),                      [b]"+&r"(b)
    : [a_stride]"r"((mips_reg)a_stride),[b_stride]"r"((mips_reg)b_stride),
      [high]"r"(&high), [sse]"r"(sse)
    : "memory"
  );

  return *sse;
}

#define vpx_mse8xN(n)                                                          \
  uint32_t vpx_mse8x##n##_mmi(const uint8_t *a, int a_stride,                  \
                              const uint8_t *b, int b_stride, uint32_t *sse) { \
    return vpx_mse8x(a, a_stride, b, b_stride, sse, n);                        \
  }

vpx_mse8xN(16);
vpx_mse8xN(8);

#define SUBPIX_VAR(W, H)                                                \
  uint32_t vpx_sub_pixel_variance##W##x##H##_mmi(                       \
      const uint8_t *a, int a_stride, int xoffset, int yoffset,         \
      const uint8_t *b, int b_stride, uint32_t *sse) {                  \
    uint16_t fdata3[(H + 1) * W];                                       \
    uint8_t temp2[H * W];                                               \
                                                                        \
    var_filter_block2d_bil_first_pass(a, fdata3, a_stride, 1, H + 1, W, \
                                      bilinear_filters[xoffset]);       \
    var_filter_block2d_bil_second_pass(fdata3, temp2, W, W, H, W,       \
                                       bilinear_filters[yoffset]);      \
                                                                        \
    return vpx_variance##W##x##H##_mmi(temp2, W, b, b_stride, sse);     \
  }

SUBPIX_VAR(64, 64)
SUBPIX_VAR(64, 32)
SUBPIX_VAR(32, 64)
SUBPIX_VAR(32, 32)
SUBPIX_VAR(32, 16)
SUBPIX_VAR(16, 32)

static inline void var_filter_block2d_bil_16x(const uint8_t *a, int a_stride,
                                              int xoffset, int yoffset,
                                              uint8_t *temp2, int counter) {
  uint8_t *temp2_ptr = temp2;
  mips_reg l_counter = counter;
  double ftmp[15];
  mips_reg tmp[2];
  DECLARE_ALIGNED(8, const uint64_t, ff_ph_40) = { 0x0040004000400040ULL };
  DECLARE_ALIGNED(8, const uint64_t, mask) = { 0x00ff00ff00ff00ffULL };

  const uint8_t *filter_x = bilinear_filters[xoffset];
  const uint8_t *filter_y = bilinear_filters[yoffset];

  __asm__ volatile (
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    MMI_LI(%[tmp0], 0x07)
    MMI_MTC1(%[tmp0], %[ftmp14])
    "pshufh     %[filter_x0], %[filter_x0], %[ftmp0]            \n\t"
    "pshufh     %[filter_x1], %[filter_x1], %[ftmp0]            \n\t"
    "pshufh     %[filter_y0], %[filter_y0], %[ftmp0]            \n\t"
    "pshufh     %[filter_y1], %[filter_y1], %[ftmp0]            \n\t"

    // fdata3: fdata3[0] ~ fdata3[15]
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_16_A

    // fdata3 +a_stride*1: fdata3[0] ~ fdata3[15]
    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_16_B
    // temp2: temp2[0] ~ temp2[15]
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_16_A

    // fdata3 +a_stride*2: fdata3[0] ~ fdata3[15]
    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_16_A
    // temp2+16*1: temp2[0] ~ temp2[15]
    MMI_ADDIU(%[temp2_ptr], %[temp2_ptr], 0x10)
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_16_B

    "1:                                                         \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_16_B
    MMI_ADDIU(%[temp2_ptr], %[temp2_ptr], 0x10)
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_16_A

    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_16_A
    MMI_ADDIU(%[temp2_ptr], %[temp2_ptr], 0x10)
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_16_B
    "addiu      %[counter], %[counter],     -0x01               \n\t"
    "bnez       %[counter], 1b                                  \n\t"
    : [ftmp0] "=&f"(ftmp[0]), [ftmp1] "=&f"(ftmp[1]), [ftmp2] "=&f"(ftmp[2]),
      [ftmp3] "=&f"(ftmp[3]), [ftmp4] "=&f"(ftmp[4]), [ftmp5] "=&f"(ftmp[5]),
      [ftmp6] "=&f"(ftmp[6]), [ftmp7] "=&f"(ftmp[7]), [ftmp8] "=&f"(ftmp[8]),
      [ftmp9] "=&f"(ftmp[9]), [ftmp10] "=&f"(ftmp[10]),
      [ftmp11] "=&f"(ftmp[11]), [ftmp12] "=&f"(ftmp[12]),
      [ftmp13] "=&f"(ftmp[13]), [ftmp14] "=&f"(ftmp[14]),
      [tmp0] "=&r"(tmp[0]), [a] "+&r"(a), [temp2_ptr] "+&r"(temp2_ptr),
      [counter]"+&r"(l_counter)
    : [filter_x0] "f"((uint64_t)filter_x[0]),
      [filter_x1] "f"((uint64_t)filter_x[1]),
      [filter_y0] "f"((uint64_t)filter_y[0]),
      [filter_y1] "f"((uint64_t)filter_y[1]),
      [a_stride] "r"((mips_reg)a_stride), [ff_ph_40] "f"(ff_ph_40),
      [mask] "f"(mask)
    : "memory"
  );
}

#define SUBPIX_VAR16XN(H)                                            \
  uint32_t vpx_sub_pixel_variance16x##H##_mmi(                       \
      const uint8_t *a, int a_stride, int xoffset, int yoffset,      \
      const uint8_t *b, int b_stride, uint32_t *sse) {               \
    uint8_t temp2[16 * H];                                           \
    var_filter_block2d_bil_16x(a, a_stride, xoffset, yoffset, temp2, \
                               (H - 2) / 2);                         \
                                                                     \
    return vpx_variance16x##H##_mmi(temp2, 16, b, b_stride, sse);    \
  }

SUBPIX_VAR16XN(16)
SUBPIX_VAR16XN(8)

static inline void var_filter_block2d_bil_8x(const uint8_t *a, int a_stride,
                                             int xoffset, int yoffset,
                                             uint8_t *temp2, int counter) {
  uint8_t *temp2_ptr = temp2;
  mips_reg l_counter = counter;
  double ftmp[15];
  mips_reg tmp[2];
  DECLARE_ALIGNED(8, const uint64_t, ff_ph_40) = { 0x0040004000400040ULL };
  DECLARE_ALIGNED(8, const uint64_t, mask) = { 0x00ff00ff00ff00ffULL };
  const uint8_t *filter_x = bilinear_filters[xoffset];
  const uint8_t *filter_y = bilinear_filters[yoffset];

  __asm__ volatile (
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    MMI_LI(%[tmp0], 0x07)
    MMI_MTC1(%[tmp0], %[ftmp14])
    "pshufh     %[filter_x0], %[filter_x0], %[ftmp0]            \n\t"
    "pshufh     %[filter_x1], %[filter_x1], %[ftmp0]            \n\t"
    "pshufh     %[filter_y0], %[filter_y0], %[ftmp0]            \n\t"
    "pshufh     %[filter_y1], %[filter_y1], %[ftmp0]            \n\t"

    // fdata3: fdata3[0] ~ fdata3[7]
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_8_A

    // fdata3 +a_stride*1: fdata3[0] ~ fdata3[7]
    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_8_B
    // temp2: temp2[0] ~ temp2[7]
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_8_A

    // fdata3 +a_stride*2: fdata3[0] ~ fdata3[7]
    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_8_A
    // temp2+8*1: temp2[0] ~ temp2[7]
    MMI_ADDIU(%[temp2_ptr], %[temp2_ptr], 0x08)
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_8_B

    "1:                                                         \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_8_B
    MMI_ADDIU(%[temp2_ptr], %[temp2_ptr], 0x08)
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_8_A

    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_8_A
    MMI_ADDIU(%[temp2_ptr], %[temp2_ptr], 0x08)
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_8_B
    "addiu      %[counter], %[counter],     -0x01               \n\t"
    "bnez       %[counter], 1b                                  \n\t"
    : [ftmp0] "=&f"(ftmp[0]), [ftmp1] "=&f"(ftmp[1]), [ftmp2] "=&f"(ftmp[2]),
      [ftmp3] "=&f"(ftmp[3]), [ftmp4] "=&f"(ftmp[4]), [ftmp5] "=&f"(ftmp[5]),
      [ftmp6] "=&f"(ftmp[6]), [ftmp7] "=&f"(ftmp[7]), [ftmp8] "=&f"(ftmp[8]),
      [ftmp9] "=&f"(ftmp[9]), [ftmp10] "=&f"(ftmp[10]),
      [ftmp11] "=&f"(ftmp[11]), [ftmp12] "=&f"(ftmp[12]),
      [ftmp13] "=&f"(ftmp[13]), [ftmp14] "=&f"(ftmp[14]),
      [tmp0] "=&r"(tmp[0]), [a] "+&r"(a), [temp2_ptr] "+&r"(temp2_ptr),
      [counter]"+&r"(l_counter)
    : [filter_x0] "f"((uint64_t)filter_x[0]),
      [filter_x1] "f"((uint64_t)filter_x[1]),
      [filter_y0] "f"((uint64_t)filter_y[0]),
      [filter_y1] "f"((uint64_t)filter_y[1]),
      [a_stride] "r"((mips_reg)a_stride), [ff_ph_40] "f"(ff_ph_40),
      [mask] "f"(mask)
    : "memory"
  );
}

#define SUBPIX_VAR8XN(H)                                            \
  uint32_t vpx_sub_pixel_variance8x##H##_mmi(                       \
      const uint8_t *a, int a_stride, int xoffset, int yoffset,     \
      const uint8_t *b, int b_stride, uint32_t *sse) {              \
    uint8_t temp2[8 * H];                                           \
    var_filter_block2d_bil_8x(a, a_stride, xoffset, yoffset, temp2, \
                              (H - 2) / 2);                         \
                                                                    \
    return vpx_variance8x##H##_mmi(temp2, 8, b, b_stride, sse);     \
  }

SUBPIX_VAR8XN(16)
SUBPIX_VAR8XN(8)
SUBPIX_VAR8XN(4)

static inline void var_filter_block2d_bil_4x(const uint8_t *a, int a_stride,
                                             int xoffset, int yoffset,
                                             uint8_t *temp2, int counter) {
  uint8_t *temp2_ptr = temp2;
  mips_reg l_counter = counter;
  double ftmp[7];
  mips_reg tmp[2];
  DECLARE_ALIGNED(8, const uint64_t, ff_ph_40) = { 0x0040004000400040ULL };
  DECLARE_ALIGNED(8, const uint64_t, mask) = { 0x00ff00ff00ff00ffULL };
  const uint8_t *filter_x = bilinear_filters[xoffset];
  const uint8_t *filter_y = bilinear_filters[yoffset];

  __asm__ volatile (
    "xor        %[ftmp0],   %[ftmp0],       %[ftmp0]            \n\t"
    MMI_LI(%[tmp0], 0x07)
    MMI_MTC1(%[tmp0], %[ftmp6])
    "pshufh     %[filter_x0], %[filter_x0], %[ftmp0]            \n\t"
    "pshufh     %[filter_x1], %[filter_x1], %[ftmp0]            \n\t"
    "pshufh     %[filter_y0], %[filter_y0], %[ftmp0]            \n\t"
    "pshufh     %[filter_y1], %[filter_y1], %[ftmp0]            \n\t"
    // fdata3: fdata3[0] ~ fdata3[3]
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_4_A

    // fdata3 +a_stride*1: fdata3[0] ~ fdata3[3]
    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_4_B
    // temp2: temp2[0] ~ temp2[7]
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_4_A

    // fdata3 +a_stride*2: fdata3[0] ~ fdata3[3]
    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_4_A
    // temp2+4*1: temp2[0] ~ temp2[7]
    MMI_ADDIU(%[temp2_ptr], %[temp2_ptr], 0x04)
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_4_B

    "1:                                                         \n\t"
    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_4_B
    MMI_ADDIU(%[temp2_ptr], %[temp2_ptr], 0x04)
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_4_A

    MMI_ADDU(%[a], %[a], %[a_stride])
    VAR_FILTER_BLOCK2D_BIL_FIRST_PASS_4_A
    MMI_ADDIU(%[temp2_ptr], %[temp2_ptr], 0x04)
    VAR_FILTER_BLOCK2D_BIL_SECOND_PASS_4_B
    "addiu      %[counter], %[counter],     -0x01               \n\t"
    "bnez       %[counter], 1b                                  \n\t"
    : [ftmp0] "=&f"(ftmp[0]), [ftmp1] "=&f"(ftmp[1]), [ftmp2] "=&f"(ftmp[2]),
      [ftmp3] "=&f"(ftmp[3]), [ftmp4] "=&f"(ftmp[4]), [ftmp5] "=&f"(ftmp[5]),
      [ftmp6] "=&f"(ftmp[6]), [tmp0] "=&r"(tmp[0]), [a] "+&r"(a),
      [temp2_ptr] "+&r"(temp2_ptr), [counter]"+&r"(l_counter)
    : [filter_x0] "f"((uint64_t)filter_x[0]),
      [filter_x1] "f"((uint64_t)filter_x[1]),
      [filter_y0] "f"((uint64_t)filter_y[0]),
      [filter_y1] "f"((uint64_t)filter_y[1]),
      [a_stride] "r"((mips_reg)a_stride), [ff_ph_40] "f"(ff_ph_40),
      [mask] "f"(mask)
    : "memory"
  );
}

#define SUBPIX_VAR4XN(H)                                            \
  uint32_t vpx_sub_pixel_variance4x##H##_mmi(                       \
      const uint8_t *a, int a_stride, int xoffset, int yoffset,     \
      const uint8_t *b, int b_stride, uint32_t *sse) {              \
    uint8_t temp2[4 * H];                                           \
    var_filter_block2d_bil_4x(a, a_stride, xoffset, yoffset, temp2, \
                              (H - 2) / 2);                         \
                                                                    \
    return vpx_variance4x##H##_mmi(temp2, 4, b, b_stride, sse);     \
  }

SUBPIX_VAR4XN(8)
SUBPIX_VAR4XN(4)

#define SUBPIX_AVG_VAR(W, H)                                            \
  uint32_t vpx_sub_pixel_avg_variance##W##x##H##_mmi(                   \
      const uint8_t *a, int a_stride, int xoffset, int yoffset,         \
      const uint8_t *b, int b_stride, uint32_t *sse,                    \
      const uint8_t *second_pred) {                                     \
    uint16_t fdata3[(H + 1) * W];                                       \
    uint8_t temp2[H * W];                                               \
    DECLARE_ALIGNED(16, uint8_t, temp3[H * W]);                         \
                                                                        \
    var_filter_block2d_bil_first_pass(a, fdata3, a_stride, 1, H + 1, W, \
                                      bilinear_filters[xoffset]);       \
    var_filter_block2d_bil_second_pass(fdata3, temp2, W, W, H, W,       \
                                       bilinear_filters[yoffset]);      \
                                                                        \
    vpx_comp_avg_pred_c(temp3, second_pred, W, H, temp2, W);            \
                                                                        \
    return vpx_variance##W##x##H##_mmi(temp3, W, b, b_stride, sse);     \
  }

SUBPIX_AVG_VAR(64, 64)
SUBPIX_AVG_VAR(64, 32)
SUBPIX_AVG_VAR(32, 64)
SUBPIX_AVG_VAR(32, 32)
SUBPIX_AVG_VAR(32, 16)
SUBPIX_AVG_VAR(16, 32)
SUBPIX_AVG_VAR(16, 16)
SUBPIX_AVG_VAR(16, 8)
SUBPIX_AVG_VAR(8, 16)
SUBPIX_AVG_VAR(8, 8)
SUBPIX_AVG_VAR(8, 4)
SUBPIX_AVG_VAR(4, 8)
SUBPIX_AVG_VAR(4, 4)
