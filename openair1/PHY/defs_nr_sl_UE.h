/*
 * Licensed to the OpenAirInterface (OAI) Software Alliance under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The OpenAirInterface Software Alliance licenses this file to You under
 * the OAI Public License, Version 1.1  (the "License"); you may not use this file
 * except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.openairinterface.org/?page_id=698
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *-------------------------------------------------------------------------------
 * For more information about the OpenAirInterface (OAI) Software Alliance:
 *      contact@openairinterface.org
 */

/*! \file PHY/defs_nr_sl_UE.h
 \brief Top-level defines and structure definitions
 \author
 \date
 \version
 \company Fraunhofer
 \email:
 \note
 \warning
*/

#ifndef _DEFS_NR_SL_UE_H_
#define _DEFS_NR_SL_UE_H_

#include "PHY/types.h"
#include "PHY/defs_nr_common.h"
#include "nfapi/open-nFAPI/nfapi/public_inc/sidelink_nr_ue_interface.h"
#include "common/utils/time_meas.h"

// (33*(13-4))
// Normal CP - NUM_SSB_Symbols = 13. 4 symbols for PSS, SSS
#define SL_NR_NUM_PSBCH_DMRS_RE 297
// ceil(2(QPSK)*SL_NR_NUM_PSBCH_DMRS_RE/32)
#define SL_NR_NUM_PSBCH_DMRS_RE_DWORD 20
// 11 RBs for PSBCH in one symbol * 12 REs
#define SL_NR_NUM_PSBCH_RE_IN_ONE_SYMBOL 132
// 3 DMRS REs per RB * 11 RBS in one symbol
#define SL_NR_NUM_PSBCH_DMRS_RE_IN_ONE_SYMBOL 33
// 9 PSBCH DATA REs * 11 RBS in one symbol
#define SL_NR_NUM_PSBCH_DATA_RE_IN_ONE_SYMBOL 99
#define SL_NR_NUM_PSBCH_RBS_IN_ONE_SYMBOL 11
// SL_NR_POLAR_PSBCH_E_NORMAL_CP/2 bits because QPSK used for PSBCH.
// 11 * (12-3 DMRS REs) * 9 symbols for PSBCH
#define SL_NR_NUM_PSBCH_MODULATED_SYMBOLS 891
#define SL_NR_NUM_PSBCH_DATA_RE_IN_ONE_RB 9
#define SL_NR_NUM_PSBCH_DMRS_RE_IN_ONE_RB 3
// 11 * (12-3 DMRS REs) * 9 symbols for PSBCH
#define SL_NR_NUM_PSBCH_DATA_RE_IN_ALL_SYMBOLS 891

#define SL_NR_NUM_SYMBOLS_SSB_NORMAL_CP 13
#define SL_NR_NUM_SYMBOLS_SSB_EXT_CP 11
#define SL_NR_NUM_PSS_SYMBOLS 2
#define SL_NR_NUM_SSS_SYMBOLS 2
#define SL_NR_PSS_START_SYMBOL 1
#define SL_NR_SSS_START_SYMBOL 3
#define SL_NR_NUM_PSS_OR_SSS_SYMBOLS 2
#define SL_NR_PSS_SEQUENCE_LENGTH 127
#define SL_NR_SSS_SEQUENCE_LENGTH 127
#define SL_NR_NUM_IDs_IN_PSS 2
#define SL_NR_NUM_IDs_IN_SSS 336
#define SL_NR_NUM_SLSS_IDs 672
#define SL_NR_PSBCH_REPETITION_IN_FRAMES 16

typedef enum sl_nr_sidelink_mode { SL_NOT_SUPPORTED = 0, SL_MODE1_SUPPORTED, SL_MODE2_SUPPORTED } sl_nr_sidelink_mode_t;

//(11*(12-3 DMRS REs) * 2 (QPSK used)
#define SL_NR_NUM_PSBCH_DATA_BITS_IN_ONE_SYMBOL 198

typedef struct SL_NR_UE_INIT_PARAMS {
  // gold sequences for PSBCH DMRS
  uint32_t psbch_dmrs_gold_sequences[SL_NR_NUM_SLSS_IDs][SL_NR_NUM_PSBCH_DMRS_RE_DWORD]; // Gold sequences for PSBCH DMRS

  // PSBCH DMRS QPSK modulated symbols for all possible SLSS Ids
  struct complex16 psbch_dmrs_modsym[SL_NR_NUM_SLSS_IDs][SL_NR_NUM_PSBCH_DMRS_RE];

  // Scaled values
  int16_t sl_pss[SL_NR_NUM_IDs_IN_PSS][SL_NR_PSS_SEQUENCE_LENGTH];
  int16_t sl_sss[SL_NR_NUM_SLSS_IDs][SL_NR_SSS_SEQUENCE_LENGTH];

  // Contains Not scaled values just the simple generated sequence
  int16_t sl_pss_for_sync[SL_NR_NUM_IDs_IN_PSS][SL_NR_PSS_SEQUENCE_LENGTH];
  int16_t sl_sss_for_sync[SL_NR_NUM_SLSS_IDs][SL_NR_SSS_SEQUENCE_LENGTH];

  int32_t **sl_pss_for_correlation; // IFFT samples for correlation

} SL_NR_UE_INIT_PARAMS_t;

typedef struct SL_NR_SYNC_PARAMS {
  // Indicating start of SSB block in the initial set of samples
  uint32_t ssb_offset;
  // Freq Offset calculated
  int32_t freq_offset;

  uint32_t remaining_frames;
  uint32_t rx_offset;
  uint32_t slot_offset;
  uint16_t N_sl_id2; // id2 determined from PSS during sync ref UE selection
  uint16_t N_sl_id1; // id2 determined from SSS during sync ref UE selection
  uint16_t N_sl_id; // ID calculated from ID1 and ID2
  int32_t psbch_rsrp; // rsrp of the decoded psbch during sync ref ue selection
  uint32_t DFN; // DFN calculated after sync ref UE search

} SL_NR_SYNC_PARAMS_t;

typedef struct SL_NR_UE_PSBCH {
  // AVG POWER OF PSBCH DMRS in dB/RE
  int16_t rsrp_dB_per_RE;
  // AVG POWER OF PSBCH DMRS in dBm/RE
  int16_t rsrp_dBm_per_RE;

  // STATS - CRC Errors observed during PSBCH reception
  uint16_t rx_errors;

  // STATS - Receptions with CRC OK
  uint16_t rx_ok;

  // STATS - transmissions of PSBCH by the UE
  uint16_t num_psbch_tx;

} SL_NR_UE_PSBCH_t;

typedef struct sl_nr_ue_phy_params {
  SL_NR_UE_INIT_PARAMS_t init_params;

  SL_NR_SYNC_PARAMS_t sync_params;

  // Sidelink PHY PARAMETERS USED FOR PSBCH reception/Txn
  SL_NR_UE_PSBCH_t psbch;

  // Configuration parameters from MAC
  sl_nr_phy_config_request_t sl_config;

  NR_DL_FRAME_PARMS sl_frame_params;

  time_stats_t phy_proc_sl_tx;
  time_stats_t phy_proc_sl_rx;
  time_stats_t channel_estimation_stats;
  time_stats_t ue_sl_indication_stats;

} sl_nr_ue_phy_params_t;

#endif