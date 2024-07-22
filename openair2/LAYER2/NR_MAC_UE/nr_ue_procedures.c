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

/* \file ue_procedures.c
 * \brief procedures related to UE
 * \author R. Knopp, K.H. HSU, G. Casati
 * \date 2018
 * \version 0.1
 * \company Eurecom / NTUST
 * \email: knopp@eurecom.fr, kai-hsiang.hsu@eurecom.fr, guido.casati@iis.fraunhofer.de
 * \note
 * \warning
 */


#include <stdio.h>
#include <math.h>

/* exe */
#include "executables/nr-softmodem.h"

/* RRC*/
#include "RRC/NR_UE/rrc_proto.h"

/* MAC */
#include "NR_MAC_COMMON/nr_mac.h"
#include "NR_MAC_UE/mac_proto.h"
#include "NR_MAC_UE/mac_extern.h"
#include "NR_MAC_COMMON/nr_mac_extern.h"
#include "common/utils/nr/nr_common.h"
#include "openair2/NR_UE_PHY_INTERFACE/NR_Packet_Drop.h"

/* PHY */
#include "executables/softmodem-common.h"
#include "openair1/PHY/defs_nr_UE.h"

/* utils */
#include "assertions.h"
#include "oai_asn1.h"
#include "common/utils/LOG/log.h"
#include "common/utils/LOG/vcd_signal_dumper.h"

// #define DEBUG_MIB
// #define ENABLE_MAC_PAYLOAD_DEBUG 1
// #define DEBUG_RAR
extern uint32_t N_RB_DL;

/* TS 36.213 Table 9.2.3-3: Mapping of values for one HARQ-ACK bit to sequences */
static const int sequence_cyclic_shift_1_harq_ack_bit[2]
/*        HARQ-ACK Value        0    1 */
/* Sequence cyclic shift */ = { 0,   6 };

/* TS 36.213 Table 9.2.5-1: Mapping of values for one HARQ-ACK bit and positive SR to sequences */
static const int sequence_cyclic_shift_1_harq_ack_bit_positive_sr[2]
/*        HARQ-ACK Value        0    1 */
/* Sequence cyclic shift */ = { 3,   9 };

/* TS 36.213 Table 9.2.5-2: Mapping of values for two HARQ-ACK bits and positive SR to sequences */
static const int sequence_cyclic_shift_2_harq_ack_bits_positive_sr[4]
/*        HARQ-ACK Value      (0,0)  (0,1)   (1,0)  (1,1) */
/* Sequence cyclic shift */ = {  1,     4,     10,     7 };

/* TS 38.213 Table 9.2.3-4: Mapping of values for two HARQ-ACK bits to sequences */
static const int sequence_cyclic_shift_2_harq_ack_bits[4]
/*        HARQ-ACK Value       (0,0)  (0,1)  (1,0)  (1,1) */
/* Sequence cyclic shift */ = {   0,     3,     9,     6 };

/* \brief Function called by PHY to process the received RAR and check that the preamble matches what was sent by the gNB. It
provides the timing advance and t-CRNTI.
@param Mod_id Index of UE instance
@param CC_id Index to a component carrier
@param frame Frame index
@param ra_rnti RA_RNTI value
@param dlsch_buffer  Pointer to dlsch_buffer containing RAR PDU
@param t_crnti Pointer to PHY variable containing the T_CRNTI
@param preamble_index Preamble Index used by PHY to transmit the PRACH.  This should match the received RAR to trigger the rest of
random-access procedure
@param selected_rar_buffer the output buffer for storing the selected RAR header and RAR payload
@returns timing advance or 0xffff if preamble doesn't match
*/
static void nr_ue_process_rar(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info, int pdu_id);

int get_pucch0_mcs(const int O_ACK, const int O_SR, const int ack_payload, const int sr_payload)
{
  int mcs = 0;
  if (O_SR == 0 || sr_payload == 0) { /* only ack is transmitted TS 36.213 9.2.3 UE procedure for reporting HARQ-ACK */
    if (O_ACK == 1)
      mcs = sequence_cyclic_shift_1_harq_ack_bit[ack_payload & 0x1]; /* only harq of 1 bit */
    else
      mcs = sequence_cyclic_shift_2_harq_ack_bits[ack_payload & 0x3]; /* only harq with 2 bits */
  } else { /* SR + eventually ack are transmitted TS 36.213 9.2.5.1 UE procedure for multiplexing HARQ-ACK or CSI and SR */
    if (sr_payload == 1) { /* positive scheduling request */
      if (O_ACK == 1)
        mcs = sequence_cyclic_shift_1_harq_ack_bit_positive_sr[ack_payload & 0x1]; /* positive SR and harq of 1 bit */
      else if (O_ACK == 2)
        mcs = sequence_cyclic_shift_2_harq_ack_bits_positive_sr[ack_payload & 0x3]; /* positive SR and harq with 2 bits */
      else
        mcs = 0; /* only positive SR */
    }
  }
  return mcs;
}

/* TS 36.213 Table 9.2.1-1: PUCCH resource sets before dedicated PUCCH resource configuration */
const initial_pucch_resource_t initial_pucch_resource[16] = {
/*              format           first symbol     Number of symbols        PRB offset    nb index for       set of initial CS */
/*  0  */ {  0,      12,                  2,                   0,            2,       {    0,   3,    0,    0  }   },
/*  1  */ {  0,      12,                  2,                   0,            3,       {    0,   4,    8,    0  }   },
/*  2  */ {  0,      12,                  2,                   3,            3,       {    0,   4,    8,    0  }   },
/*  3  */ {  1,      10,                  4,                   0,            2,       {    0,   6,    0,    0  }   },
/*  4  */ {  1,      10,                  4,                   0,            4,       {    0,   3,    6,    9  }   },
/*  5  */ {  1,      10,                  4,                   2,            4,       {    0,   3,    6,    9  }   },
/*  6  */ {  1,      10,                  4,                   4,            4,       {    0,   3,    6,    9  }   },
/*  7  */ {  1,       4,                 10,                   0,            2,       {    0,   6,    0,    0  }   },
/*  8  */ {  1,       4,                 10,                   0,            4,       {    0,   3,    6,    9  }   },
/*  9  */ {  1,       4,                 10,                   2,            4,       {    0,   3,    6,    9  }   },
/* 10  */ {  1,       4,                 10,                   4,            4,       {    0,   3,    6,    9  }   },
/* 11  */ {  1,       0,                 14,                   0,            2,       {    0,   6,    0,    0  }   },
/* 12  */ {  1,       0,                 14,                   0,            4,       {    0,   3,    6,    9  }   },
/* 13  */ {  1,       0,                 14,                   2,            4,       {    0,   3,    6,    9  }   },
/* 14  */ {  1,       0,                 14,                   4,            4,       {    0,   3,    6,    9  }   },
/* 15  */ {  1,       0,                 14,                   0,            4,       {    0,   3,    6,    9  }   },
};

static nr_dci_format_t nr_extract_dci_info(NR_UE_MAC_INST_t *mac,
                                           const nfapi_nr_dci_formats_e dci_format,
                                           const uint8_t dci_size,
                                           const uint16_t rnti,
                                           const int ss_type,
                                           const uint8_t *dci_pdu,
                                           const int slot);

static int get_NTN_UE_Koffset(NR_NTN_Config_r17_t *ntn_Config_r17, int scs)
{
  if (ntn_Config_r17 && ntn_Config_r17->cellSpecificKoffset_r17)
    return *ntn_Config_r17->cellSpecificKoffset_r17 << scs;
  return 0;
}

int get_rnti_type(const NR_UE_MAC_INST_t *mac, const uint16_t rnti)
{
  const RA_config_t *ra = &mac->ra;
  nr_rnti_type_t rnti_type;

  if (rnti == ra->ra_rnti) {
    rnti_type = TYPE_RA_RNTI_;
  } else if (rnti == ra->t_crnti && (ra->ra_state == nrRA_WAIT_RAR || ra->ra_state == nrRA_WAIT_CONTENTION_RESOLUTION)) {
    rnti_type = TYPE_TC_RNTI_;
  } else if (rnti == mac->crnti) {
    rnti_type = TYPE_C_RNTI_;
  } else if (rnti == 0xFFFE) {
    rnti_type = TYPE_P_RNTI_;
  } else if (rnti == 0xFFFF) {
    rnti_type = TYPE_SI_RNTI_;
  } else {
    AssertFatal(1 == 0, "Not identified/handled rnti %d \n", rnti);
  }
  LOG_D(MAC, "Returning rnti_type %s \n", rnti_types(rnti_type));
  return rnti_type;
}

void nr_ue_decode_mib(NR_UE_MAC_INST_t *mac, int cc_id)
{
  LOG_D(MAC,"[L2][MAC] decode mib\n");

  if (mac->mib->cellBarred == NR_MIB__cellBarred_barred) {
    LOG_W(MAC, "Cell is barred. Going back to sync mode.\n");
    mac->synch_request.Mod_id = mac->ue_id;
    mac->synch_request.CC_id = cc_id;
    mac->synch_request.synch_req.target_Nid_cell = -1;
    mac->if_module->synch_request(&mac->synch_request);
    return;
  }

  uint16_t frame = (mac->mib->systemFrameNumber.buf[0] >> mac->mib->systemFrameNumber.bits_unused);
  uint16_t frame_number_4lsb = 0;

  int extra_bits = mac->mib_additional_bits;
  for (int i = 0; i < 4; i++)
    frame_number_4lsb |= ((extra_bits >> i) & 1) << (3 - i);

  uint8_t ssb_subcarrier_offset_msb = (extra_bits >> 5) & 0x1;    //	extra bits[5]
  uint8_t ssb_subcarrier_offset = (uint8_t)mac->mib->ssb_SubcarrierOffset;

  frame = frame << 4;
  mac->mib_frame = frame | frame_number_4lsb;
  if (mac->frequency_range == FR2) {
    for (int i = 0; i < 3; i++)
      mac->mib_ssb += (((extra_bits >> (7 - i)) & 0x01) << (3 + i));
  } else{
    if(ssb_subcarrier_offset_msb)
      ssb_subcarrier_offset = ssb_subcarrier_offset | 0x10;
  }

#ifdef DEBUG_MIB
  uint8_t half_frame_bit = (extra_bits >> 4) & 0x1; //	extra bits[4]
  LOG_I(MAC,"system frame number(6 MSB bits): %d\n",  mac->mib->systemFrameNumber.buf[0]);
  LOG_I(MAC,"system frame number(with LSB): %d\n", (int) mac->mib_frame);
  LOG_I(MAC,"subcarrier spacing (0=15or60, 1=30or120): %d\n", (int)mac->mib->subCarrierSpacingCommon);
  LOG_I(MAC,"ssb carrier offset(with MSB):  %d\n", (int)ssb_subcarrier_offset);
  LOG_I(MAC,"dmrs type A position (0=pos2,1=pos3): %d\n", (int)mac->mib->dmrs_TypeA_Position);
  LOG_I(MAC,"controlResourceSetZero: %d\n", (int)mac->mib->pdcch_ConfigSIB1.controlResourceSetZero);
  LOG_I(MAC,"searchSpaceZero: %d\n", (int)mac->mib->pdcch_ConfigSIB1.searchSpaceZero);
  LOG_I(MAC,"cell barred (0=barred,1=notBarred): %d\n", (int)mac->mib->cellBarred);
  LOG_I(MAC,"intra frequency reselection (0=allowed,1=notAllowed): %d\n", (int)mac->mib->intraFreqReselection);
  LOG_I(MAC,"half frame bit(extra bits):    %d\n", (int)half_frame_bit);
  LOG_I(MAC,"ssb index(extra bits):         %d\n", (int)mac->mib_ssb);
#endif

  mac->ssb_subcarrier_offset = ssb_subcarrier_offset;
  mac->dmrs_TypeA_Position = mac->mib->dmrs_TypeA_Position;

  if (mac->first_sync_frame == -1)
    mac->first_sync_frame = frame;

  if(get_softmodem_params()->phy_test)
    mac->state = UE_CONNECTED;
  else if(mac->state == UE_NOT_SYNC)
    mac->state = UE_SYNC;
}

static void configure_ratematching_csi(fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu,
                                       fapi_nr_dl_config_request_t *dl_config,
                                       int rnti_type,
                                       int frame,
                                       int slot,
                                       int mu,
                                       NR_PDSCH_Config_t *pdsch_config)
{
  // only for C-RNTI, MCS-C-RNTI, CS-RNTI (and only C-RNTI is supported for now)
  if (rnti_type != TYPE_C_RNTI_)
    return;

  if (pdsch_config && pdsch_config->zp_CSI_RS_ResourceToAddModList) {
    for (int i = 0; i < pdsch_config->zp_CSI_RS_ResourceToAddModList->list.count; i++) {
      NR_ZP_CSI_RS_Resource_t *zp_res = pdsch_config->zp_CSI_RS_ResourceToAddModList->list.array[i];
      NR_ZP_CSI_RS_ResourceId_t id = zp_res->zp_CSI_RS_ResourceId;
      NR_SetupRelease_ZP_CSI_RS_ResourceSet_t *zp_set = pdsch_config->p_ZP_CSI_RS_ResourceSet;
      AssertFatal(zp_set && zp_set->choice.setup, "Only periodic ZP resource set is implemented\n");
      bool found = false;
      for (int j = 0; j < zp_set->choice.setup->zp_CSI_RS_ResourceIdList.list.count; j++) {
        if (*zp_set->choice.setup->zp_CSI_RS_ResourceIdList.list.array[j] == id) {
          found = true;
          break;
        }
      }
      AssertFatal(found, "Couldn't find periodic ZP resouce in set\n");
      AssertFatal(zp_res->periodicityAndOffset, "periodicityAndOffset cannot be null for periodic ZP resource\n");
      int period, offset;
      csi_period_offset(NULL, zp_res->periodicityAndOffset, &period, &offset);
      if((frame * nr_slots_per_frame[mu] + slot - offset) % period != 0)
        continue;
      AssertFatal(dlsch_pdu->numCsiRsForRateMatching < NFAPI_MAX_NUM_CSI_RATEMATCH, "csiRsForRateMatching out of bounds\n");
      fapi_nr_dl_config_csirs_pdu_rel15_t *csi_pdu = &dlsch_pdu->csiRsForRateMatching[dlsch_pdu->numCsiRsForRateMatching];
      csi_pdu->csi_type = 2; // ZP-CSI
      csi_pdu->subcarrier_spacing = mu;
      configure_csi_resource_mapping(csi_pdu, &zp_res->resourceMapping, dlsch_pdu->BWPSize, dlsch_pdu->BWPStart);
      dlsch_pdu->numCsiRsForRateMatching++;
    }
  }

  for (int i = 0; i < dl_config->number_pdus; i++) {
    // This assumes that CSI-RS are scheduled before this moment which is true in current implementation
    fapi_nr_dl_config_request_pdu_t *csi_req = &dl_config->dl_config_list[i];
    if (csi_req->pdu_type == FAPI_NR_DL_CONFIG_TYPE_CSI_RS) {
      AssertFatal(dlsch_pdu->numCsiRsForRateMatching < NFAPI_MAX_NUM_CSI_RATEMATCH, "csiRsForRateMatching out of bounds\n");
      dlsch_pdu->csiRsForRateMatching[dlsch_pdu->numCsiRsForRateMatching] = csi_req->csirs_config_pdu.csirs_config_rel15;
      dlsch_pdu->numCsiRsForRateMatching++;
    }
  }
}

int8_t nr_ue_decode_BCCH_DL_SCH(NR_UE_MAC_INST_t *mac,
                                int cc_id,
                                unsigned int gNB_index,
                                uint8_t ack_nack,
                                uint8_t *pduP,
                                uint32_t pdu_len)
{
  if(ack_nack) {
    LOG_D(NR_MAC, "Decoding NR-BCCH-DL-SCH-Message (SIB1 or SI)\n");
    nr_mac_rrc_data_ind_ue(mac->ue_id, cc_id, gNB_index, 0, 0, 0, mac->physCellId, 0, NR_BCCH_DL_SCH, (uint8_t *) pduP, pdu_len);
    mac->get_sib1 = false;
    mac->get_otherSI = false;
  }
  else
    LOG_E(NR_MAC, "Got NACK on NR-BCCH-DL-SCH-Message (%s)\n", mac->get_sib1 ? "SIB1" : "other SI");
  return 0;
}

/*
 * This code contains all the functions needed to process all dci fields.
 * These tables and functions are going to be called by function nr_ue_process_dci
 */

static inline int writeBit(uint8_t *bitmap, int offset, int value, int size)
{
  for (int i = offset; i < offset + size; i++)
    bitmap[i / 8] |= value << (i % 8);
  return size;
}

// 38.214 Section 5.1.3.1
static uint8_t get_dlsch_mcs_table(nr_dci_format_t format,
                                   nr_rnti_type_t rnti_type,
                                   int ss_type,
                                   long *mcs_Table,
                                   long *sps_mcs_Table,
                                   bool sps_transmission,
                                   long *mcs_C_RNTI)
{
  // TODO procedures for SPS-Config (semi-persistent scheduling) not implemented
  if (mcs_Table && *mcs_Table == NR_PDSCH_Config__mcs_Table_qam256 && format == NR_DL_DCI_FORMAT_1_1 && rnti_type == TYPE_C_RNTI_)
    return 1; // Table 5.1.3.1-2: MCS index table 2 for PDSCH
  else if (!mcs_C_RNTI
           && mcs_Table
           && *mcs_Table == NR_PDSCH_Config__mcs_Table_qam64LowSE
           && ss_type == NR_SearchSpace__searchSpaceType_PR_ue_Specific
           && rnti_type == TYPE_C_RNTI_)
    return 2; // Table 5.1.3.1-3: MCS index table 3 for PDSCH
  else if (mcs_C_RNTI && rnti_type == TYPE_MCS_C_RNTI_)
    return 2; // Table 5.1.3.1-3: MCS index table 3 for PDSCH
  else if (!sps_mcs_Table && mcs_Table && *mcs_Table == NR_PDSCH_Config__mcs_Table_qam256) {
    if ((format == NR_DL_DCI_FORMAT_1_1 && rnti_type == TYPE_CS_RNTI_) || sps_transmission)
      return 1; // Table 5.1.3.1-2: MCS index table 2 for PDSCH
  }
  else if (sps_mcs_Table) {
    if (rnti_type == TYPE_CS_RNTI_ || sps_transmission)
      return 2; // Table 5.1.3.1-3: MCS index table 3 for PDSCH
  }
  // otherwise
  return 0; // Table 5.1.3.1-1: MCS index table 1 for PDSCH
}

int8_t nr_ue_process_dci_freq_dom_resource_assignment(nfapi_nr_ue_pusch_pdu_t *pusch_config_pdu,
                                                      fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_config_pdu,
                                                      NR_PDSCH_Config_t *pdsch_Config,
                                                      uint16_t n_RB_ULBWP,
                                                      uint16_t n_RB_DLBWP,
                                                      int start_DLBWP,
                                                      dci_field_t frequency_domain_assignment)
{

  /*
   * TS 38.214 subclause 5.1.2.2 Resource allocation in frequency domain (downlink)
   * when the scheduling grant is received with DCI format 1_0, then downlink resource allocation type 1 is used
   */
  if(dlsch_config_pdu != NULL) {
    if (pdsch_Config &&
        pdsch_Config->resourceAllocation == NR_PDSCH_Config__resourceAllocation_resourceAllocationType0) {
      // TS 38.214 subclause 5.1.2.2.1 Downlink resource allocation type 0
      dlsch_config_pdu->resource_alloc = 0;
      uint8_t *rb_bitmap = dlsch_config_pdu->rb_bitmap;
      memset(rb_bitmap, 0, sizeof(dlsch_config_pdu->rb_bitmap));
      int P = getRBGSize(n_RB_DLBWP, pdsch_Config->rbg_Size);
      int currentBit = 0;

      // write first bit sepcial case
      const int n_RBG = frequency_domain_assignment.nbits;
      int first_bit_rbg = (frequency_domain_assignment.val >> (n_RBG - 1)) & 0x01;
      currentBit += writeBit(rb_bitmap, currentBit, first_bit_rbg, P - (start_DLBWP % P));

      // write all bits until last bit special case
      for (int i = 1; i < n_RBG - 1; i++) {
        // The order of RBG bitmap is such that RBG 0 to RBG n_RBG − 1 are mapped from MSB to LSB
        int bit_rbg = (frequency_domain_assignment.val >> (n_RBG - 1 - i)) & 0x01;
        currentBit += writeBit(rb_bitmap, currentBit, bit_rbg, P);
      }

      // write last bit
      int last_bit_rbg = frequency_domain_assignment.val & 0x01;
      const int tmp=(start_DLBWP + n_RB_DLBWP) % P;
      int last_RBG = tmp ? tmp : P;
      writeBit(rb_bitmap, currentBit, last_bit_rbg, last_RBG);

      dlsch_config_pdu->number_rbs = count_bits(dlsch_config_pdu->rb_bitmap, sizeofArray(dlsch_config_pdu->rb_bitmap));
      // Temporary code to process type0 as type1 when the RB allocation is contiguous
      int state = 0;
      for (int i = 0; i < sizeof(dlsch_config_pdu->rb_bitmap) * 8; i++) {
        int allocated = dlsch_config_pdu->rb_bitmap[i / 8] & (1 << (i % 8));
        if (allocated) {
          if (state == 0) {
            dlsch_config_pdu->start_rb = i;
            state = 1;
          } else
            AssertFatal(state == 1, "non-contiguous RB allocation in RB allocation type 0 not implemented");
        } else {
          if (state == 1) {
            state = 2;
          }
        }
      }
    }
    else if (pdsch_Config &&
             pdsch_Config->resourceAllocation == NR_PDSCH_Config__resourceAllocation_dynamicSwitch)
      AssertFatal(false, "DLSCH dynamic switch allocation not yet supported\n");
    else {
      // TS 38.214 subclause 5.1.2.2.2 Downlink resource allocation type 1
      dlsch_config_pdu->resource_alloc = 1;
      int riv = frequency_domain_assignment.val;
      dlsch_config_pdu->number_rbs = NRRIV2BW(riv,n_RB_DLBWP);
      dlsch_config_pdu->start_rb   = NRRIV2PRBOFFSET(riv,n_RB_DLBWP);

      // Sanity check in case a false or erroneous DCI is received
      if ((dlsch_config_pdu->number_rbs < 1) || (dlsch_config_pdu->number_rbs > n_RB_DLBWP - dlsch_config_pdu->start_rb)) {
        // DCI is invalid!
        LOG_W(MAC, "Frequency domain assignment values are invalid! #RBs: %d, Start RB: %d, n_RB_DLBWP: %d \n", dlsch_config_pdu->number_rbs, dlsch_config_pdu->start_rb, n_RB_DLBWP);
        return -1;
      }
      LOG_D(MAC,"DLSCH riv = %i\n", riv);
      LOG_D(MAC,"DLSCH n_RB_DLBWP = %i\n", n_RB_DLBWP);
      LOG_D(MAC,"DLSCH number_rbs = %i\n", dlsch_config_pdu->number_rbs);
      LOG_D(MAC,"DLSCH start_rb = %i\n", dlsch_config_pdu->start_rb);
    }
  }
  if(pusch_config_pdu != NULL){
    /*
     * TS 38.214 subclause 6.1.2.2 Resource allocation in frequency domain (uplink)
     */
    /*
     * TS 38.214 subclause 6.1.2.2.1 Uplink resource allocation type 0
     */
    /*
     * TS 38.214 subclause 6.1.2.2.2 Uplink resource allocation type 1
     */
    int riv = frequency_domain_assignment.val;
    pusch_config_pdu->rb_size  = NRRIV2BW(riv,n_RB_ULBWP);
    pusch_config_pdu->rb_start = NRRIV2PRBOFFSET(riv,n_RB_ULBWP);

    // Sanity check in case a false or erroneous DCI is received
    if ((pusch_config_pdu->rb_size < 1) || (pusch_config_pdu->rb_size > n_RB_ULBWP - pusch_config_pdu->rb_start)) {
      // DCI is invalid!
      LOG_W(MAC, "Frequency domain assignment values are invalid! #RBs: %d, Start RB: %d, n_RB_ULBWP: %d \n",pusch_config_pdu->rb_size, pusch_config_pdu->rb_start, n_RB_ULBWP);
      return -1;
    }
    LOG_D(MAC,"ULSCH riv = %i\n", riv);
    LOG_D(MAC,"ULSCH n_RB_DLBWP = %i\n", n_RB_ULBWP);
    LOG_D(MAC,"ULSCH number_rbs = %i\n", pusch_config_pdu->rb_size);
    LOG_D(MAC,"ULSCH start_rb = %i\n", pusch_config_pdu->rb_start);
  }
  return 0;
}

static int nr_ue_process_dci_ul_00(NR_UE_MAC_INST_t *mac,
                                   frame_t frame,
                                   int slot,
                                   dci_pdu_rel15_t *dci,
                                   fapi_nr_dci_indication_pdu_t *dci_ind)
{
  /*
   *  with CRC scrambled by C-RNTI or CS-RNTI or new-RNTI or TC-RNTI
   *    0  IDENTIFIER_DCI_FORMATS:
   *    10 FREQ_DOM_RESOURCE_ASSIGNMENT_UL: PUSCH hopping with resource allocation type 1 not considered
   *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 6.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
   *    17 FREQ_HOPPING_FLAG: 0 bit if only resource allocation type 0
   *    24 MCS:
   *    25 NDI:
   *    26 RV:
   *    27 HARQ_PROCESS_NUMBER:
   *    32 TPC_PUSCH:
   *    49 PADDING_NR_DCI: (Note 2) If DCI format 0_0 is monitored in common search space
   *    50 SUL_IND_0_0:
   */
  // Calculate the slot in which ULSCH should be scheduled. This is current slot + K2,
  // where K2 is the offset between the slot in which UL DCI is received and the slot
  // in which ULSCH should be scheduled. K2 is configured in RRC configuration.
  // todo:
  // - SUL_IND_0_0

  // Schedule PUSCH
  const int coreset_type = dci_ind->coreset_type == NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG; // 0 for coreset0, 1 otherwise;

  NR_tda_info_t tda_info = get_ul_tda_info(mac->current_UL_BWP,
                                           coreset_type,
                                           dci_ind->ss_type,
                                           get_rnti_type(mac, dci_ind->rnti),
                                           dci->time_domain_assignment.val);

  if (!tda_info.valid_tda || tda_info.nrOfSymbols == 0)
    return -1;

  frame_t frame_tx;
  int slot_tx;
  const int ntn_ue_koffset = get_NTN_UE_Koffset(mac->sc_info.ntn_Config_r17, mac->current_UL_BWP->scs);
  if (-1 == nr_ue_pusch_scheduler(mac, 0, frame, slot, &frame_tx, &slot_tx, tda_info.k2 + ntn_ue_koffset)) {
    LOG_E(MAC, "Cannot schedule PUSCH\n");
    return -1;
  }

  fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame_tx, slot_tx, FAPI_NR_UL_CONFIG_TYPE_PUSCH);
  if (!pdu)
    return -1;

  int ret = nr_config_pusch_pdu(mac,
                                &tda_info,
                                &pdu->pusch_config_pdu,
                                dci,
                                NULL,
                                NULL,
                                dci_ind->rnti,
                                dci_ind->ss_type,
                                NR_UL_DCI_FORMAT_0_0);
  if (ret != 0)
    remove_ul_config_last_item(pdu);
  release_ul_config(pdu, false);
  return ret;
}

static int nr_ue_process_dci_ul_01(NR_UE_MAC_INST_t *mac,
                                   frame_t frame,
                                   int slot,
                                   dci_pdu_rel15_t *dci,
                                   fapi_nr_dci_indication_pdu_t *dci_ind)
{
  /*
   *  with CRC scrambled by C-RNTI or CS-RNTI or SP-CSI-RNTI or new-RNTI
   *    0  IDENTIFIER_DCI_FORMATS:
   *    1  CARRIER_IND
   *    2  SUL_IND_0_1
   *    7  BANDWIDTH_PART_IND
   *    10 FREQ_DOM_RESOURCE_ASSIGNMENT_UL: PUSCH hopping with resource allocation type 1 not considered
   *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 6.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
   *    17 FREQ_HOPPING_FLAG: 0 bit if only resource allocation type 0
   *    24 MCS:
   *    25 NDI:
   *    26 RV:
   *    27 HARQ_PROCESS_NUMBER:
   *    29 FIRST_DAI
   *    30 SECOND_DAI
   *    32 TPC_PUSCH:
   *    36 SRS_RESOURCE_IND:
   *    37 PRECOD_NBR_LAYERS:
   *    38 ANTENNA_PORTS:
   *    40 SRS_REQUEST:
   *    42 CSI_REQUEST:
   *    43 CBGTI
   *    45 PTRS_DMRS
   *    46 BETA_OFFSET_IND
   *    47 DMRS_SEQ_INI
   *    48 UL_SCH_IND
   *    49 PADDING_NR_DCI: (Note 2) If DCI format 0_0 is monitored in common search space
   */
  // TODO:
  // - FIRST_DAI
  // - SECOND_DAI
  // - SRS_RESOURCE_IND

  /* CSI_REQUEST */
  long csi_K2 = -1;
  csi_payload_t csi_report = {0};
  if (dci->csi_request.nbits > 0 && dci->csi_request.val > 0)
    csi_report = nr_ue_aperiodic_csi_reporting(mac, dci->csi_request, dci->time_domain_assignment.val, &csi_K2);

  /* SRS_REQUEST */
  AssertFatal(dci->srs_request.nbits == 2, "If SUL is supported in the cell, there is an additional bit in SRS request field\n");
  if (dci->srs_request.val > 0)
    nr_ue_aperiodic_srs_scheduling(mac, dci->srs_request.val, frame, slot);

  // Schedule PUSCH
  frame_t frame_tx;
  int slot_tx;
  const int coreset_type = dci_ind->coreset_type == NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG; // 0 for coreset0, 1 otherwise;

  NR_tda_info_t tda_info = get_ul_tda_info(mac->current_UL_BWP,
                                           coreset_type,
                                           dci_ind->ss_type,
                                           get_rnti_type(mac, dci_ind->rnti),
                                           dci->time_domain_assignment.val);

  if (!tda_info.valid_tda || tda_info.nrOfSymbols == 0)
    return -1;

  if (dci->ulsch_indicator == 0) {
    // in case of CSI on PUSCH and no ULSCH we need to use reportSlotOffset in trigger state
    if (csi_K2 <= 0) {
      LOG_E(MAC, "Invalid CSI K2 value %ld\n", csi_K2);
      return -1;
    }
    tda_info.k2 = csi_K2;
  }

  const int ntn_ue_koffset = get_NTN_UE_Koffset(mac->sc_info.ntn_Config_r17, mac->current_UL_BWP->scs);
  if (-1 == nr_ue_pusch_scheduler(mac, 0, frame, slot, &frame_tx, &slot_tx, tda_info.k2 + ntn_ue_koffset)) {
    LOG_E(MAC, "Cannot schedule PUSCH\n");
    return -1;
  }

  fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame_tx, slot_tx, FAPI_NR_UL_CONFIG_TYPE_PUSCH);
  if (!pdu)
    return -1;
  int ret = nr_config_pusch_pdu(mac,
                                &tda_info,
                                &pdu->pusch_config_pdu,
                                dci,
                                &csi_report,
                                NULL,
                                dci_ind->rnti,
                                dci_ind->ss_type,
                                NR_UL_DCI_FORMAT_0_1);
  if (ret != 0)
    remove_ul_config_last_item(pdu);
  release_ul_config(pdu, false);
  return ret;
}

static int nr_ue_process_dci_dl_10(NR_UE_MAC_INST_t *mac,
                                   frame_t frame,
                                   int slot,
                                   dci_pdu_rel15_t *dci,
                                   fapi_nr_dci_indication_pdu_t *dci_ind)
{
  /*
   *  with CRC scrambled by C-RNTI or CS-RNTI or new-RNTI
   *    0  IDENTIFIER_DCI_FORMATS:
   *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    25 NDI:
     *    26 RV:
     *    27 HARQ_PROCESS_NUMBER:
     *    28 DAI_: For format1_1: 4 if more than one serving cell are configured in the DL and the higher layer parameter HARQ-ACK-codebook=dynamic, where the 2 MSB bits are the counter DAI and the 2 LSB bits are the total DAI
     *    33 TPC_PUCCH:
     *    34 PUCCH_RESOURCE_IND:
     *    35 PDSCH_TO_HARQ_FEEDBACK_TIME_IND:
     *    55 RESERVED_NR_DCI
     *  with CRC scrambled by P-RNTI
     *    8  SHORT_MESSAGE_IND
     *    9  SHORT_MESSAGES
     *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    31 TB_SCALING
     *    55 RESERVED_NR_DCI
     *  with CRC scrambled by SI-RNTI
     *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    26 RV:
     *    55 RESERVED_NR_DCI
     *  with CRC scrambled by RA-RNTI
     *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    31 TB_SCALING
     *    55 RESERVED_NR_DCI
     *  with CRC scrambled by TC-RNTI
     *    0  IDENTIFIER_DCI_FORMATS:
     *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
     *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
     *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
     *    24 MCS:
     *    25 NDI:
     *    26 RV:
     *    27 HARQ_PROCESS_NUMBER:
     *    28 DAI_: For format1_1: 4 if more than one serving cell are configured in the DL and the higher layer parameter HARQ-ACK-codebook=dynamic, where the 2 MSB bits are the counter DAI and the 2 LSB bits are the total DAI
     *    33 TPC_PUCCH:
   */

  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, slot);
  fapi_nr_dl_config_request_pdu_t *dl_conf_req = &dl_config->dl_config_list[dl_config->number_pdus];
  dl_conf_req->dlsch_config_pdu.rnti = dci_ind->rnti;

  fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu = &dl_conf_req->dlsch_config_pdu.dlsch_config_rel15;

  dlsch_pdu->pduBitmap = 0;
  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  NR_PDSCH_Config_t *pdsch_config = (current_DL_BWP || !mac->get_sib1) ? current_DL_BWP->pdsch_Config : NULL;
  if (dci_ind->ss_type == NR_SearchSpace__searchSpaceType_PR_common) {
    dlsch_pdu->BWPSize =
        mac->type0_PDCCH_CSS_config.num_rbs ? mac->type0_PDCCH_CSS_config.num_rbs : mac->sc_info.initial_dl_BWPSize;
    dlsch_pdu->BWPStart = dci_ind->cset_start;
  } else {
    dlsch_pdu->BWPSize = current_DL_BWP->BWPSize;
    dlsch_pdu->BWPStart = current_DL_BWP->BWPStart;
  }

  nr_rnti_type_t rnti_type = get_rnti_type(mac, dci_ind->rnti);

  int mux_pattern = 1;
  if (rnti_type == TYPE_SI_RNTI_) {
    NR_Type0_PDCCH_CSS_config_t type0_PDCCH_CSS_config = mac->type0_PDCCH_CSS_config;
    mux_pattern = type0_PDCCH_CSS_config.type0_pdcch_ss_mux_pattern;
    dl_conf_req->pdu_type = FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH;
    // in MIB SCS is signaled as 15or60 and 30or120
    dlsch_pdu->SubcarrierSpacing = mac->mib->subCarrierSpacingCommon;
    if (mac->frequency_range == FR2)
      dlsch_pdu->SubcarrierSpacing = mac->mib->subCarrierSpacingCommon + 2;
  } else {
    dlsch_pdu->SubcarrierSpacing = current_DL_BWP->scs;
    if (mac->ra.RA_window_cnt >= 0 && rnti_type == TYPE_RA_RNTI_) {
      dl_conf_req->pdu_type = FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH;
    } else {
      dl_conf_req->pdu_type = FAPI_NR_DL_CONFIG_TYPE_DLSCH;
    }
  }

  dlsch_pdu->numCsiRsForRateMatching = 0;
  configure_ratematching_csi(dlsch_pdu, dl_config, rnti_type, frame, slot, dlsch_pdu->SubcarrierSpacing, pdsch_config);


  /* IDENTIFIER_DCI_FORMATS */
  /* FREQ_DOM_RESOURCE_ASSIGNMENT_DL */
  if (nr_ue_process_dci_freq_dom_resource_assignment(NULL,
                                                     dlsch_pdu,
                                                     NULL,
                                                     0,
                                                     dlsch_pdu->BWPSize,
                                                     0,
                                                     dci->frequency_domain_assignment)
      < 0) {
    LOG_W(MAC, "[%d.%d] Invalid frequency_domain_assignment. Possibly due to false DCI. Ignoring DCI!\n", frame, slot);
    return -1;
  }
  dlsch_pdu->rb_offset = dlsch_pdu->start_rb + dlsch_pdu->BWPStart;
  if (mac->get_sib1)
    dlsch_pdu->rb_offset -= dlsch_pdu->BWPStart;

  /* TIME_DOM_RESOURCE_ASSIGNMENT */
  int dmrs_typeA_pos = mac->dmrs_TypeA_Position;
  const int coreset_type = dci_ind->coreset_type == NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG; // 0 for coreset0, 1 otherwise;


  NR_tda_info_t tda_info = get_dl_tda_info(current_DL_BWP,
                                           dci_ind->ss_type,
                                           dci->time_domain_assignment.val,
                                           dmrs_typeA_pos,
                                           mux_pattern,
                                           rnti_type,
                                           coreset_type,
                                           mac->get_sib1);
  if (!tda_info.valid_tda)
    return -1;

  dlsch_pdu->number_symbols = tda_info.nrOfSymbols;
  dlsch_pdu->start_symbol = tda_info.startSymbolIndex;

  struct NR_DMRS_DownlinkConfig *dl_dmrs_config = NULL;
  if (pdsch_config)
    dl_dmrs_config = (tda_info.mapping_type == typeA) ? pdsch_config->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup
                                                      : pdsch_config->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup;

  dlsch_pdu->nscid = 0;
  if (dl_dmrs_config && dl_dmrs_config->scramblingID0)
    dlsch_pdu->dlDmrsScramblingId = *dl_dmrs_config->scramblingID0;
  else
    dlsch_pdu->dlDmrsScramblingId = mac->physCellId;

  if (rnti_type == TYPE_C_RNTI_
      && dci_ind->ss_type != NR_SearchSpace__searchSpaceType_PR_common
      && pdsch_config->dataScramblingIdentityPDSCH)
    dlsch_pdu->dlDataScramblingId = *pdsch_config->dataScramblingIdentityPDSCH;
  else
    dlsch_pdu->dlDataScramblingId = mac->physCellId;

  /* dmrs symbol positions*/
  dlsch_pdu->dlDmrsSymbPos = fill_dmrs_mask(pdsch_config,
                                            NR_DL_DCI_FORMAT_1_0,
                                            mac->dmrs_TypeA_Position,
                                            dlsch_pdu->number_symbols,
                                            dlsch_pdu->start_symbol,
                                            tda_info.mapping_type,
                                            1);

  dlsch_pdu->dmrsConfigType = (dl_dmrs_config != NULL) ? (dl_dmrs_config->dmrs_Type == NULL ? 0 : 1) : 0;

  /* number of DM-RS CDM groups without data according to subclause 5.1.6.2 of 3GPP TS 38.214 version 15.9.0 Release 15 */
  if (dlsch_pdu->number_symbols == 2)
    dlsch_pdu->n_dmrs_cdm_groups = 1;
  else
    dlsch_pdu->n_dmrs_cdm_groups = 2;
  dlsch_pdu->dmrs_ports = 1; // only port 0 in case of DCI 1_0
  /* VRB_TO_PRB_MAPPING */
  dlsch_pdu->vrb_to_prb_mapping =
      (dci->vrb_to_prb_mapping.val == 0) ? vrb_to_prb_mapping_non_interleaved : vrb_to_prb_mapping_interleaved;
  /* MCS TABLE INDEX */
  dlsch_pdu->mcs_table = get_dlsch_mcs_table(NR_DL_DCI_FORMAT_1_0,
                                             rnti_type,
                                             dci_ind->ss_type,
                                             pdsch_config ? pdsch_config->mcs_Table : NULL,
                                             NULL, // SPS not implemented,
                                             false, // as above
                                             NULL); // MCS-C-RNTI not implemented
  /* MCS */
  dlsch_pdu->mcs = dci->mcs;

  NR_UE_HARQ_STATUS_t *current_harq = &mac->dl_harq_info[dci->harq_pid];
  /* NDI (only if CRC scrambled by C-RNTI or CS-RNTI or new-RNTI or TC-RNTI)*/
  if (dl_conf_req->pdu_type == FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH ||
      dl_conf_req->pdu_type == FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH ||
      dci->ndi != current_harq->last_ndi) {
    // new data
    dlsch_pdu->new_data_indicator = true;
    current_harq->R = 0;
    current_harq->TBS = 0;
  }
  else
    dlsch_pdu->new_data_indicator = false;

  if (dl_conf_req->pdu_type != FAPI_NR_DL_CONFIG_TYPE_SI_DLSCH &&
      dl_conf_req->pdu_type != FAPI_NR_DL_CONFIG_TYPE_RA_DLSCH) {
    current_harq->last_ndi = dci->ndi;
  }

  dlsch_pdu->qamModOrder = nr_get_Qm_dl(dlsch_pdu->mcs, dlsch_pdu->mcs_table);
  if (dlsch_pdu->qamModOrder == 0) {
    LOG_W(MAC, "Invalid code rate or Mod order, likely due to unexpected DL DCI.\n");
    return -1;
  }

  int R = nr_get_code_rate_dl(dlsch_pdu->mcs, dlsch_pdu->mcs_table);
  if (R > 0) {
    dlsch_pdu->targetCodeRate = R;
    int nb_rb_oh;
    if (mac->sc_info.xOverhead_PDSCH)
      nb_rb_oh = 6 * (1 + *mac->sc_info.xOverhead_PDSCH);
    else
      nb_rb_oh = 0;
    int nb_re_dmrs = ((dlsch_pdu->dmrsConfigType == NFAPI_NR_DMRS_TYPE1) ? 6 : 4) * dlsch_pdu->n_dmrs_cdm_groups;
    dlsch_pdu->TBS = nr_compute_tbs(dlsch_pdu->qamModOrder,
                                    R,
                                    dlsch_pdu->number_rbs,
                                    dlsch_pdu->number_symbols,
                                    nb_re_dmrs * get_num_dmrs(dlsch_pdu->dlDmrsSymbPos),
                                    nb_rb_oh,
                                    0,
                                    1);
    // storing for possible retransmissions
    current_harq->R = dlsch_pdu->targetCodeRate;
    if (!dlsch_pdu->new_data_indicator && current_harq->TBS != dlsch_pdu->TBS) {
      LOG_W(NR_MAC, "NDI indicates re-transmission but computed TBS %d doesn't match with what previously stored %d\n",
            dlsch_pdu->TBS, current_harq->TBS);
      dlsch_pdu->new_data_indicator = true; // treated as new data
    }
    current_harq->TBS = dlsch_pdu->TBS;
  }
  else {
    dlsch_pdu->targetCodeRate = current_harq->R;
    dlsch_pdu->TBS = current_harq->TBS;
  }

  dlsch_pdu->ldpcBaseGraph = get_BG(dlsch_pdu->TBS, dlsch_pdu->targetCodeRate);

  if (dlsch_pdu->TBS == 0) {
    LOG_E(MAC, "Invalid TBS = 0. Probably caused by missed detection of DCI\n");
    return -1;
  }

  int bw_tbslbrm = current_DL_BWP ? mac->sc_info.dl_bw_tbslbrm : dlsch_pdu->BWPSize;
  dlsch_pdu->tbslbrm = nr_compute_tbslbrm(dlsch_pdu->mcs_table, bw_tbslbrm, 1);

  /* RV (only if CRC scrambled by C-RNTI or CS-RNTI or new-RNTI or TC-RNTI)*/
  dlsch_pdu->rv = dci->rv;
  /* HARQ_PROCESS_NUMBER (only if CRC scrambled by C-RNTI or CS-RNTI or new-RNTI or TC-RNTI)*/
  dlsch_pdu->harq_process_nbr = dci->harq_pid;
  /* TB_SCALING (only if CRC scrambled by P-RNTI or RA-RNTI) */
  // according to TS 38.214 Table 5.1.3.2-3
  if (dci->tb_scaling > 3) {
    LOG_E(MAC, "invalid tb_scaling %d\n", dci->tb_scaling);
    return -1;
  }
  const float factor[] = {1, 0.5, 0.25, 0};
  dlsch_pdu->scaling_factor_S = factor[dci->tb_scaling];
  /* TPC_PUCCH (only if CRC scrambled by C-RNTI or CS-RNTI or new-RNTI or TC-RNTI)*/
  // according to TS 38.213 Table 7.2.1-1
  if (dci->tpc > 3) {
    LOG_E(MAC, "invalid tpc %d\n", dci->tpc);
    return -1;
  }

  // Sanity check for pucch_resource_indicator value received to check for false DCI.
  bool valid = false;
  NR_PUCCH_Config_t *pucch_Config = mac->current_UL_BWP ? mac->current_UL_BWP->pucch_Config : NULL;

  if (pucch_Config && pucch_Config->resourceSetToAddModList) {
    int pucch_res_set_cnt = pucch_Config->resourceSetToAddModList->list.count;
    for (int id = 0; id < pucch_res_set_cnt; id++) {
      if (dci->pucch_resource_indicator < pucch_Config->resourceSetToAddModList->list.array[id]->resourceList.list.count) {
        valid = true;
        break;
      }
    }
  } else
    valid = true;
  if (!valid) {
    LOG_W(MAC,
          "[%d.%d] pucch_resource_indicator value %d is out of bounds. Possibly due to false DCI. Ignoring DCI!\n",
          frame,
          slot,
          dci->pucch_resource_indicator);
    return -1;
  }

  /* PDSCH_TO_HARQ_FEEDBACK_TIME_IND */
  // according to TS 38.213 9.2.3
  const int ntn_ue_koffset = get_NTN_UE_Koffset(mac->sc_info.ntn_Config_r17, dlsch_pdu->SubcarrierSpacing);
  const uint16_t feedback_ti = 1 + dci->pdsch_to_harq_feedback_timing_indicator.val + ntn_ue_koffset;

  if (rnti_type != TYPE_RA_RNTI_ && rnti_type != TYPE_SI_RNTI_) {
    AssertFatal(feedback_ti > DURATION_RX_TO_TX,
                "PDSCH to HARQ feedback time (%d) needs to be higher than DURATION_RX_TO_TX (%d).\n",
                feedback_ti,
                DURATION_RX_TO_TX);
    // set the harq status at MAC for feedback
    const int tpc[] = {-1, 0, 1, 3};
    set_harq_status(mac,
                    dci->pucch_resource_indicator,
                    dci->harq_pid,
                    tpc[dci->tpc],
                    feedback_ti,
                    dci->dai[0].val,
                    dci_ind->n_CCE,
                    dci_ind->N_CCE,
                    frame,
                    slot);
  }

  LOG_D(MAC,
        "(nr_ue_procedures.c) rnti = %x dl_config->number_pdus = %d\n",
        dl_conf_req->dlsch_config_pdu.rnti,
        dl_config->number_pdus);
  LOG_D(MAC,
        "(nr_ue_procedures.c) frequency_domain_resource_assignment=%d \t number_rbs=%d \t start_rb=%d\n",
        dci->frequency_domain_assignment.val,
        dlsch_pdu->number_rbs,
        dlsch_pdu->start_rb);
  LOG_D(MAC,
        "(nr_ue_procedures.c) time_domain_resource_assignment=%d \t number_symbols=%d \t start_symbol=%d\n",
        dci->time_domain_assignment.val,
        dlsch_pdu->number_symbols,
        dlsch_pdu->start_symbol);
  LOG_D(MAC,
        "(nr_ue_procedures.c) vrb_to_prb_mapping=%d \n>>> mcs=%d\n>>> ndi=%d\n>>> rv=%d\n>>> harq_process_nbr=%d\n>>> dai=%d\n>>> "
        "scaling_factor_S=%f\n>>> tpc_pucch=%d\n>>> pucch_res_ind=%d\n>>> pdsch_to_harq_feedback_time_ind=%d\n",
        dlsch_pdu->vrb_to_prb_mapping,
        dlsch_pdu->mcs,
        dlsch_pdu->new_data_indicator,
        dlsch_pdu->rv,
        dlsch_pdu->harq_process_nbr,
        dci->dai[0].val,
        dlsch_pdu->scaling_factor_S,
        dci->tpc,
        dci->pucch_resource_indicator,
        feedback_ti);

  dlsch_pdu->k1_feedback = feedback_ti;

  LOG_D(MAC, "(nr_ue_procedures.c) pdu_type=%d\n\n", dl_conf_req->pdu_type);

  // the prepared dci is valid, we add it in the list
  dl_config->number_pdus++;
  return 0;
}

static inline uint16_t packBits(const uint8_t *toPack, const int nb)
{
  int res = 0;
  for (int i = 0; i < nb; i++)
    res += (*toPack++) << i;
  return res;
}

static int nr_ue_process_dci_dl_11(NR_UE_MAC_INST_t *mac,
                                   frame_t frame,
                                   int slot,
                                   dci_pdu_rel15_t *dci,
                                   fapi_nr_dci_indication_pdu_t *dci_ind)
{
  /*
   *  with CRC scrambled by C-RNTI or CS-RNTI or new-RNTI
   *    0  IDENTIFIER_DCI_FORMATS:
   *    1  CARRIER_IND:
   *    7  BANDWIDTH_PART_IND:
   *    11 FREQ_DOM_RESOURCE_ASSIGNMENT_DL:
   *    12 TIME_DOM_RESOURCE_ASSIGNMENT: 0, 1, 2, 3, or 4 bits as defined in Subclause 5.1.2.1 of [6, TS 38.214]. The bitwidth for this field is determined as log2(I) bits,
   *    13 VRB_TO_PRB_MAPPING: 0 bit if only resource allocation type 0
   *    14 PRB_BUNDLING_SIZE_IND:
   *    15 RATE_MATCHING_IND:
   *    16 ZP_CSI_RS_TRIGGER:
   *    18 TB1_MCS:
   *    19 TB1_NDI:
   *    20 TB1_RV:
   *    21 TB2_MCS:
   *    22 TB2_NDI:
   *    23 TB2_RV:
   *    27 HARQ_PROCESS_NUMBER:
   *    28 DAI_: For format1_1: 4 if more than one serving cell are configured in the DL and the higher layer parameter HARQ-ACK-codebook=dynamic, where the 2 MSB bits are the counter DAI and the 2 LSB bits are the total DAI
   *    33 TPC_PUCCH:
   *    34 PUCCH_RESOURCE_IND:
   *    35 PDSCH_TO_HARQ_FEEDBACK_TIME_IND:
   *    38 ANTENNA_PORTS:
   *    39 TCI:
   *    40 SRS_REQUEST:
   *    43 CBGTI:
   *    44 CBGFI:
   *    47 DMRS_SEQ_INI:
   */

  if (dci->bwp_indicator.val > NR_MAX_NUM_BWP) {
    LOG_W(NR_MAC,
          "[%d.%d] bwp_indicator %d > NR_MAX_NUM_BWP Possibly due to false DCI. Ignoring DCI!\n",
          frame,
          slot,
          dci->bwp_indicator.val);
    return -1;
  }
  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  NR_PDSCH_Config_t *pdsch_Config = current_DL_BWP->pdsch_Config;
  fapi_nr_dl_config_request_t *dl_config = get_dl_config_request(mac, slot);
  fapi_nr_dl_config_request_pdu_t *dl_conf_req = &dl_config->dl_config_list[dl_config->number_pdus];

  dl_conf_req->dlsch_config_pdu.rnti = dci_ind->rnti;

  fapi_nr_dl_config_dlsch_pdu_rel15_t *dlsch_pdu = &dl_conf_req->dlsch_config_pdu.dlsch_config_rel15;

  dlsch_pdu->BWPSize = current_DL_BWP->BWPSize;
  dlsch_pdu->BWPStart = current_DL_BWP->BWPStart;
  dlsch_pdu->SubcarrierSpacing = current_DL_BWP->scs;

  nr_rnti_type_t rnti_type = get_rnti_type(mac, dci_ind->rnti);
  dlsch_pdu->numCsiRsForRateMatching = 0;
  configure_ratematching_csi(dlsch_pdu, dl_config, rnti_type, frame, slot, current_DL_BWP->scs, pdsch_Config);

  /* IDENTIFIER_DCI_FORMATS */
  /* CARRIER_IND */
  /* BANDWIDTH_PART_IND */
  //    dlsch_pdu->bandwidth_part_ind = dci->bandwidth_part_ind;
  /* FREQ_DOM_RESOURCE_ASSIGNMENT_DL */
  if (nr_ue_process_dci_freq_dom_resource_assignment(NULL,
                                                     dlsch_pdu,
                                                     pdsch_Config,
                                                     0,
                                                     current_DL_BWP->BWPSize,
                                                     current_DL_BWP->BWPStart,
                                                     dci->frequency_domain_assignment)
      < 0) {
    LOG_W(MAC, "[%d.%d] Invalid frequency_domain_assignment. Possibly due to false DCI. Ignoring DCI!\n", frame, slot);
    return -1;
  }
  dlsch_pdu->rb_offset = dlsch_pdu->start_rb + dlsch_pdu->BWPStart;
  /* TIME_DOM_RESOURCE_ASSIGNMENT */
  int dmrs_typeA_pos = mac->dmrs_TypeA_Position;
  int mux_pattern = 1;
  const int coreset_type = dci_ind->coreset_type == NFAPI_NR_CSET_CONFIG_PDCCH_CONFIG; // 0 for coreset0, 1 otherwise;

  NR_tda_info_t tda_info = get_dl_tda_info(current_DL_BWP,
                                           dci_ind->ss_type,
                                           dci->time_domain_assignment.val,
                                           dmrs_typeA_pos,
                                           mux_pattern,
                                           rnti_type,
                                           coreset_type,
                                           false);
  if (!tda_info.valid_tda)
    return -1;

  dlsch_pdu->number_symbols = tda_info.nrOfSymbols;
  dlsch_pdu->start_symbol = tda_info.startSymbolIndex;

  struct NR_DMRS_DownlinkConfig *dl_dmrs_config = (tda_info.mapping_type == typeA)
                                                      ? pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup
                                                      : pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeB->choice.setup;

  switch (dci->dmrs_sequence_initialization.val) {
    case 0:
      dlsch_pdu->nscid = 0;
      if (dl_dmrs_config->scramblingID0)
        dlsch_pdu->dlDmrsScramblingId = *dl_dmrs_config->scramblingID0;
      else
        dlsch_pdu->dlDmrsScramblingId = mac->physCellId;
      break;
    case 1:
      dlsch_pdu->nscid = 1;
      if (dl_dmrs_config->scramblingID1)
        dlsch_pdu->dlDmrsScramblingId = *dl_dmrs_config->scramblingID1;
      else
        dlsch_pdu->dlDmrsScramblingId = mac->physCellId;
      break;
    default:
      LOG_E(MAC, "Invalid dmrs sequence initialization value %d\n", dci->dmrs_sequence_initialization.val);
      return -1;
  }

  if (pdsch_Config->dataScramblingIdentityPDSCH)
    dlsch_pdu->dlDataScramblingId = *pdsch_Config->dataScramblingIdentityPDSCH;
  else
    dlsch_pdu->dlDataScramblingId = mac->physCellId;

  dlsch_pdu->dmrsConfigType = dl_dmrs_config->dmrs_Type == NULL ? NFAPI_NR_DMRS_TYPE1 : NFAPI_NR_DMRS_TYPE2;

  /* TODO: fix number of DM-RS CDM groups without data according to subclause 5.1.6.2 of 3GPP TS 38.214,
           using tables 7.3.1.2.2-1, 7.3.1.2.2-2, 7.3.1.2.2-3, 7.3.1.2.2-4 of 3GPP TS 38.212 */
  dlsch_pdu->n_dmrs_cdm_groups = 1;
  /* VRB_TO_PRB_MAPPING */
  if ((pdsch_Config->resourceAllocation == 1) && (pdsch_Config->vrb_ToPRB_Interleaver != NULL))
    dlsch_pdu->vrb_to_prb_mapping =
        (dci->vrb_to_prb_mapping.val == 0) ? vrb_to_prb_mapping_non_interleaved : vrb_to_prb_mapping_interleaved;
  /* PRB_BUNDLING_SIZE_IND */
  dlsch_pdu->prb_bundling_size_ind = dci->prb_bundling_size_indicator.val;
  /* RATE_MATCHING_IND */
  dlsch_pdu->rate_matching_ind = dci->rate_matching_indicator.val;
  /* ZP_CSI_RS_TRIGGER */
  dlsch_pdu->zp_csi_rs_trigger = dci->zp_csi_rs_trigger.val;
  /* MCS (for transport block 1)*/
  dlsch_pdu->mcs = dci->mcs;
  /* NDI (for transport block 1)*/
  NR_UE_HARQ_STATUS_t *current_harq = &mac->dl_harq_info[dci->harq_pid];
  if (dci->ndi != current_harq->last_ndi) {
    // new data
    dlsch_pdu->new_data_indicator = true;
    current_harq->R = 0;
    current_harq->TBS = 0;
  }
  else {
    dlsch_pdu->new_data_indicator = false;
  }
  current_harq->last_ndi = dci->ndi;
  /* RV (for transport block 1)*/
  dlsch_pdu->rv = dci->rv;
  /* MCS (for transport block 2)*/
  dlsch_pdu->tb2_mcs = dci->mcs2.val;
  /* NDI (for transport block 2)*/
  dlsch_pdu->tb2_new_data_indicator = dci->ndi2.val;
  /* RV (for transport block 2)*/
  dlsch_pdu->tb2_rv = dci->rv2.val;
  /* HARQ_PROCESS_NUMBER */
  dlsch_pdu->harq_process_nbr = dci->harq_pid;
  /* TPC_PUCCH */
  // according to TS 38.213 Table 7.2.1-1
  if (dci->tpc > 3) {
    LOG_E(MAC, "invalid tpc %d\n", dci->tpc);
    return -1;
  }

  // Sanity check for pucch_resource_indicator value received to check for false DCI.
  bool valid = false;
  NR_PUCCH_Config_t *pucch_Config = mac->current_UL_BWP->pucch_Config;
  int pucch_res_set_cnt = pucch_Config->resourceSetToAddModList->list.count;
  for (int id = 0; id < pucch_res_set_cnt; id++) {
    if (dci->pucch_resource_indicator < pucch_Config->resourceSetToAddModList->list.array[id]->resourceList.list.count) {
      valid = true;
      break;
    }
  }
  if (!valid) {
    LOG_W(MAC,
          "[%d.%d] pucch_resource_indicator value %d is out of bounds. Possibly due to false DCI. Ignoring DCI!\n",
          frame,
          slot,
          dci->pucch_resource_indicator);
    return -1;
  }

  /* ANTENNA_PORTS */
  uint8_t n_codewords = 1; // FIXME!!!
  long *max_length = dl_dmrs_config->maxLength;
  long *dmrs_type = dl_dmrs_config->dmrs_Type;

  dlsch_pdu->n_front_load_symb = 1; // default value
  const int ant = dci->antenna_ports.val;

  if (dmrs_type == NULL) {
    if (max_length == NULL) {
      // Table 7.3.1.2.2-1: Antenna port(s) (1000 + DMRS port), dmrs-Type=1, maxLength=1
      dlsch_pdu->n_dmrs_cdm_groups = table_7_3_2_3_3_1[ant][0];
      dlsch_pdu->dmrs_ports = packBits(&table_7_3_2_3_3_1[ant][1], 4);
    } else {
      // Table 7.3.1.2.2-2: Antenna port(s) (1000 + DMRS port), dmrs-Type=1, maxLength=2
      if (n_codewords == 1) {
        dlsch_pdu->n_dmrs_cdm_groups = table_7_3_2_3_3_2_oneCodeword[ant][0];
        dlsch_pdu->dmrs_ports = packBits(&table_7_3_2_3_3_2_oneCodeword[ant][1], 8);
        dlsch_pdu->n_front_load_symb = table_7_3_2_3_3_2_oneCodeword[ant][9];
      }
      if (n_codewords == 2) {
        dlsch_pdu->n_dmrs_cdm_groups = table_7_3_2_3_3_2_twoCodeword[ant][0];
        dlsch_pdu->dmrs_ports = packBits(&table_7_3_2_3_3_2_twoCodeword[ant][1], 8);
        dlsch_pdu->n_front_load_symb = table_7_3_2_3_3_2_twoCodeword[ant][9];
      }
    }
  } else {
    if (max_length == NULL) {
      // Table 7.3.1.2.2-3: Antenna port(s) (1000 + DMRS port), dmrs-Type=2, maxLength=1
      if (n_codewords == 1) {
        dlsch_pdu->n_dmrs_cdm_groups = table_7_3_2_3_3_3_oneCodeword[ant][0];
        dlsch_pdu->dmrs_ports = packBits(&table_7_3_2_3_3_3_oneCodeword[ant][1], 6);
      }
      if (n_codewords == 2) {
        dlsch_pdu->n_dmrs_cdm_groups = table_7_3_2_3_3_3_twoCodeword[ant][0];
        dlsch_pdu->dmrs_ports = packBits(&table_7_3_2_3_3_3_twoCodeword[ant][1], 6);
      }
    } else {
      // Table 7.3.1.2.2-4: Antenna port(s) (1000 + DMRS port), dmrs-Type=2, maxLength=2
      if (n_codewords == 1) {
        dlsch_pdu->n_dmrs_cdm_groups = table_7_3_2_3_3_4_oneCodeword[ant][0];
        dlsch_pdu->dmrs_ports = packBits(&table_7_3_2_3_3_4_oneCodeword[ant][1], 12);
        dlsch_pdu->n_front_load_symb = table_7_3_2_3_3_4_oneCodeword[ant][13];
      }
      if (n_codewords == 2) {
        dlsch_pdu->n_dmrs_cdm_groups = table_7_3_2_3_3_4_twoCodeword[ant][0];
        dlsch_pdu->dmrs_ports = packBits(&table_7_3_2_3_3_4_twoCodeword[ant][1], 12);
        dlsch_pdu->n_front_load_symb = table_7_3_2_3_3_4_twoCodeword[ant][13];
      }
    }
  }

    /* dmrs symbol positions*/
  dlsch_pdu->dlDmrsSymbPos = fill_dmrs_mask(pdsch_Config,
                                            NR_DL_DCI_FORMAT_1_1,
                                            mac->dmrs_TypeA_Position,
                                            dlsch_pdu->number_symbols,
                                            dlsch_pdu->start_symbol,
                                            tda_info.mapping_type,
                                            dlsch_pdu->n_front_load_symb);

  /* TCI */
  if (dl_conf_req->dci_config_pdu.dci_config_rel15.coreset.tci_present_in_dci == 1) {
    // 0 bit if higher layer parameter tci-PresentInDCI is not enabled
    // otherwise 3 bits as defined in Subclause 5.1.5 of [6, TS38.214]
    dlsch_pdu->tci_state = dci->transmission_configuration_indication.val;
  }

  /* SRS_REQUEST */
  AssertFatal(dci->srs_request.nbits == 2, "If SUL is supported in the cell, there is an additional bit in SRS request field\n");
  if (dci->srs_request.val > 0)
    nr_ue_aperiodic_srs_scheduling(mac, dci->srs_request.val, frame, slot);
  /* CBGTI */
  dlsch_pdu->cbgti = dci->cbgti.val;
  /* CBGFI */
  dlsch_pdu->codeBlockGroupFlushIndicator = dci->cbgfi.val;
  /* DMRS_SEQ_INI */
  // FIXME!!!

  /* PDSCH_TO_HARQ_FEEDBACK_TIME_IND */
  // according to TS 38.213 Table 9.2.3-1
  const int ntn_ue_koffset = get_NTN_UE_Koffset(mac->sc_info.ntn_Config_r17, dlsch_pdu->SubcarrierSpacing);
  const uint16_t feedback_ti = pucch_Config->dl_DataToUL_ACK->list.array[dci->pdsch_to_harq_feedback_timing_indicator.val][0] + ntn_ue_koffset;

  AssertFatal(feedback_ti > DURATION_RX_TO_TX,
              "PDSCH to HARQ feedback time (%d) needs to be higher than DURATION_RX_TO_TX (%d). Min feedback time set in config "
              "file (min_rxtxtime).\n",
              feedback_ti,
              DURATION_RX_TO_TX);

  // set the harq status at MAC for feedback
  const int tpc[] = {-1, 0, 1, 3};
  set_harq_status(mac,
                  dci->pucch_resource_indicator,
                  dci->harq_pid,
                  tpc[dci->tpc],
                  feedback_ti,
                  dci->dai[0].val,
                  dci_ind->n_CCE,
                  dci_ind->N_CCE,
                  frame,
                  slot);
  // send the ack/nack slot number to phy to indicate tx thread to wait for DLSCH decoding
  dlsch_pdu->k1_feedback = feedback_ti;

  dlsch_pdu->mcs_table = get_dlsch_mcs_table(NR_DL_DCI_FORMAT_1_1,
                                             rnti_type,
                                             dci_ind->ss_type,
                                             pdsch_Config ? pdsch_Config->mcs_Table : NULL,
                                             NULL, // SPS not implemented,
                                             false, // as above
                                             NULL); // MCS-C-RNTI not implemented
  dlsch_pdu->qamModOrder = nr_get_Qm_dl(dlsch_pdu->mcs, dlsch_pdu->mcs_table);
  if (dlsch_pdu->qamModOrder == 0) {
    LOG_W(MAC, "Invalid code rate or Mod order, likely due to unexpected DL DCI.\n");
    return -1;
  }
  uint8_t Nl = 0;
  for (int i = 0; i < 12; i++) { // max 12 ports
    if ((dlsch_pdu->dmrs_ports >> i) & 0x01)
      Nl += 1;
  }

  NR_UE_ServingCell_Info_t *sc_info = &mac->sc_info;
  int nb_rb_oh;
  if (sc_info->xOverhead_PDSCH)
    nb_rb_oh = 6 * (1 + *sc_info->xOverhead_PDSCH);
  else
    nb_rb_oh = 0;
  int nb_re_dmrs = ((dmrs_type == NULL) ? 6 : 4) * dlsch_pdu->n_dmrs_cdm_groups;

  int R = nr_get_code_rate_dl(dlsch_pdu->mcs, dlsch_pdu->mcs_table);
  if (R > 0) {
    dlsch_pdu->targetCodeRate = R;
    dlsch_pdu->TBS = nr_compute_tbs(dlsch_pdu->qamModOrder,
                                    R,
                                    dlsch_pdu->number_rbs,
                                    dlsch_pdu->number_symbols,
                                    nb_re_dmrs * get_num_dmrs(dlsch_pdu->dlDmrsSymbPos),
                                    nb_rb_oh,
                                    0,
                                    Nl);
    // storing for possible retransmissions
    if (!dlsch_pdu->new_data_indicator && current_harq->TBS != dlsch_pdu->TBS) {
      LOG_W(NR_MAC, "NDI indicates re-transmission but computed TBS %d doesn't match with what previously stored %d\n",
            dlsch_pdu->TBS, current_harq->TBS);
      dlsch_pdu->new_data_indicator = true; // treated as new data
    }
    current_harq->R = dlsch_pdu->targetCodeRate;
    current_harq->TBS = dlsch_pdu->TBS;
  }
  else {
    dlsch_pdu->targetCodeRate = current_harq->R;
    dlsch_pdu->TBS = current_harq->TBS;
  }

  dlsch_pdu->ldpcBaseGraph = get_BG(dlsch_pdu->TBS, dlsch_pdu->targetCodeRate);

  if (dlsch_pdu->TBS == 0) {
    LOG_E(MAC, "Invalid TBS = 0. Probably caused by missed detection of DCI\n");
    return -1;
  }

  // TBS_LBRM according to section 5.4.2.1 of 38.212
  int max_mimo_layers = 0;
  if (sc_info->maxMIMO_Layers_PDSCH)
    max_mimo_layers = *sc_info->maxMIMO_Layers_PDSCH;
  else
    max_mimo_layers = mac->uecap_maxMIMO_PDSCH_layers;
  AssertFatal(max_mimo_layers > 0, "Invalid number of max MIMO layers for PDSCH\n");
  int nl_tbslbrm = max_mimo_layers < 4 ? max_mimo_layers : 4;
  dlsch_pdu->tbslbrm = nr_compute_tbslbrm(dlsch_pdu->mcs_table, sc_info->dl_bw_tbslbrm, nl_tbslbrm);
  /*PTRS configuration */
  dlsch_pdu->pduBitmap = 0;
  if (pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->phaseTrackingRS != NULL) {
    bool valid_ptrs_setup =
        set_dl_ptrs_values(pdsch_Config->dmrs_DownlinkForPDSCH_MappingTypeA->choice.setup->phaseTrackingRS->choice.setup,
                           dlsch_pdu->number_rbs,
                           dlsch_pdu->mcs,
                           dlsch_pdu->mcs_table,
                           &dlsch_pdu->PTRSFreqDensity,
                           &dlsch_pdu->PTRSTimeDensity,
                           &dlsch_pdu->PTRSPortIndex,
                           &dlsch_pdu->nEpreRatioOfPDSCHToPTRS,
                           &dlsch_pdu->PTRSReOffset,
                           dlsch_pdu->number_symbols);
    if (valid_ptrs_setup == true) {
      dlsch_pdu->pduBitmap |= 0x1;
      LOG_D(MAC, "DL PTRS values: PTRS time den: %d, PTRS freq den: %d\n", dlsch_pdu->PTRSTimeDensity, dlsch_pdu->PTRSFreqDensity);
    }
  }
  // the prepared dci is valid, we add it in the list
  dl_conf_req->pdu_type = FAPI_NR_DL_CONFIG_TYPE_DLSCH;
  dl_config->number_pdus++; // The DCI configuration is valid, we add it in the list
  return 0;
}

static int8_t nr_ue_process_dci(NR_UE_MAC_INST_t *mac,
                                frame_t frame,
                                int slot,
                                dci_pdu_rel15_t *dci,
                                fapi_nr_dci_indication_pdu_t *dci_ind,
                                const nr_dci_format_t format)
{
  const char *dci_formats[] = {"1_0", "1_1", "2_0", "2_1", "2_2", "2_3", "0_0", "0_1"};
  LOG_D(MAC, "Processing received DCI format %s\n", dci_formats[format]);

  switch (format) {
    case NR_UL_DCI_FORMAT_0_0:
      return nr_ue_process_dci_ul_00(mac, frame, slot, dci, dci_ind);
      break;

    case NR_UL_DCI_FORMAT_0_1:
      return nr_ue_process_dci_ul_01(mac, frame, slot, dci, dci_ind);
      break;

    case NR_DL_DCI_FORMAT_1_0:
      return nr_ue_process_dci_dl_10(mac, frame, slot, dci, dci_ind);
      break;

    case NR_DL_DCI_FORMAT_1_1:
      return nr_ue_process_dci_dl_11(mac, frame, slot, dci, dci_ind);
      break;

    case NR_DL_DCI_FORMAT_2_0:
      AssertFatal(false, "DCI Format 2-0 handling not implemented\n");
      break;

    case NR_DL_DCI_FORMAT_2_1:
      AssertFatal(false, "DCI Format 2-1 handling not implemented\n");
      break;

    case NR_DL_DCI_FORMAT_2_2:
      AssertFatal(false, "DCI Format 2-2 handling not implemented\n");
      break;

    case NR_DL_DCI_FORMAT_2_3:
      AssertFatal(false, "DCI Format 2-3 handling not implemented\n");
      break;

    default:
      break;
  }
  return -1;
}

nr_dci_format_t nr_ue_process_dci_indication_pdu(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, fapi_nr_dci_indication_pdu_t *dci)
{
  LOG_D(NR_MAC,
        "Received dci indication (rnti %x, dci format %d, n_CCE %d, payloadSize %d, payload %llx)\n",
        dci->rnti,
        dci->dci_format,
        dci->n_CCE,
        dci->payloadSize,
        *(unsigned long long *)dci->payloadBits);
  const nr_dci_format_t format =
      nr_extract_dci_info(mac, dci->dci_format, dci->payloadSize, dci->rnti, dci->ss_type, dci->payloadBits, slot);
  if (format == NR_DCI_NONE)
    return NR_DCI_NONE;
  dci_pdu_rel15_t *def_dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
  int ret = nr_ue_process_dci(mac, frame, slot, def_dci_pdu_rel15, dci, format);
  if (ret < 0)
    return NR_DCI_NONE;
  return format;
}

int8_t nr_ue_process_csirs_measurements(NR_UE_MAC_INST_t *mac,
                                        frame_t frame,
                                        int slot,
                                        fapi_nr_csirs_measurements_t *csirs_measurements) {
  LOG_D(NR_MAC,"(%d.%d) Received CSI-RS measurements\n", frame, slot);
  memcpy(&mac->csirs_measurements, csirs_measurements, sizeof(*csirs_measurements));
  return 0;
}

void set_harq_status(NR_UE_MAC_INST_t *mac,
                     uint8_t pucch_id,
                     uint8_t harq_id,
                     int8_t delta_pucch,
                     uint16_t data_toul_fb,
                     uint8_t dai,
                     int n_CCE,
                     int N_CCE,
                     frame_t frame,
                     int slot)
{
  NR_UE_HARQ_STATUS_t *current_harq = &mac->dl_harq_info[harq_id];
  current_harq->active = true;
  current_harq->ack_received = false;
  current_harq->pucch_resource_indicator = pucch_id;
  current_harq->n_CCE = n_CCE;
  current_harq->N_CCE = N_CCE;
  current_harq->dai_cumul = 0;
  current_harq->delta_pucch = delta_pucch;
  // FIXME k0 != 0 currently not taken into consideration
  int scs = mac->current_DL_BWP ? mac->current_DL_BWP->scs : get_softmodem_params()->numerology;
  int slots_per_frame = nr_slots_per_frame[scs];
  current_harq->ul_frame = frame;
  current_harq->ul_slot = slot + data_toul_fb;
  if (current_harq->ul_slot >= slots_per_frame) {
    current_harq->ul_frame = (frame + current_harq->ul_slot / slots_per_frame) % MAX_FRAME_NUMBER;
    current_harq->ul_slot %= slots_per_frame;
  }
  // counter DAI in DCI ranges from 0 to 3
  // we might have more than 4 HARQ processes to report per PUCCH
  // we need to keep track of how many DAI we received in a slot (dai_cumul) despite the modulo operation
  int highest_dai = -1;
  int temp_dai = dai;
  for (int i = 0; i < NR_MAX_HARQ_PROCESSES; i++) {
    // looking for other active HARQ processes with feedback in the same frame/slot
    if (i == harq_id)
      continue;
    NR_UE_HARQ_STATUS_t *harq = &mac->dl_harq_info[i];
    if (harq->active &&
        harq->ul_frame == current_harq->ul_frame &&
        harq->ul_slot == current_harq->ul_slot) {
      // highest_dai is the largest cumulative dai in the set of HARQ allocations for a given slot
      if (harq->dai_cumul > highest_dai)
        highest_dai = harq->dai_cumul - 1;
    }
  }

  current_harq->dai_cumul = temp_dai + 1;  // DAI = 0 (temp_dai) corresponds to 1st assignment and so on
  // if temp_dai is less or equal than cumulative highest dai for given slot
  // it's an indication dai was reset due to modulo 4 operation
  if (temp_dai <= highest_dai) {
    int mod4_count = (highest_dai + 1) / 4; // to take into account how many times dai wrapped up (modulo 4)
    current_harq->dai_cumul += (mod4_count * 4);
  }
  LOG_D(NR_MAC,
        "Setting harq_status for harq_id %d, dl %d.%d, sched ul %d.%d fb time %d total dai %d\n",
        harq_id,
        frame,
        slot,
        current_harq->ul_frame,
        current_harq->ul_slot,
        data_toul_fb,
        current_harq->dai_cumul);
}

int nr_ue_configure_pucch(NR_UE_MAC_INST_t *mac,
                           int slot,
                           frame_t frame,
                           uint16_t rnti,
                           PUCCH_sched_t *pucch,
                           fapi_nr_ul_config_pucch_pdu *pucch_pdu)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_UE_ServingCell_Info_t *sc_info = &mac->sc_info;
  NR_PUCCH_FormatConfig_t *pucchfmt;
  long *pusch_id = NULL;
  long *id0 = NULL;
  const int scs = current_UL_BWP->scs;

  int subframe_number = slot / (nr_slots_per_frame[scs]/10);
  pucch_pdu->rnti = rnti;

  LOG_D(NR_MAC, "initial_pucch_id %d, pucch_resource %p\n", pucch->initial_pucch_id, pucch->pucch_resource);
  // configure pucch from Table 9.2.1-1
  // only for ack/nack
  if (pucch->initial_pucch_id > -1 &&
      pucch->pucch_resource == NULL) {
    const int idx = *current_UL_BWP->pucch_ConfigCommon->pucch_ResourceCommon;
    const initial_pucch_resource_t pucch_resourcecommon = initial_pucch_resource[idx];
    pucch_pdu->format_type = pucch_resourcecommon.format;
    pucch_pdu->start_symbol_index = pucch_resourcecommon.startingSymbolIndex;
    pucch_pdu->nr_of_symbols = pucch_resourcecommon.nrofSymbols;

    pucch_pdu->bwp_size = current_UL_BWP->BWPSize;
    pucch_pdu->bwp_start = current_UL_BWP->BWPStart;

    pucch_pdu->prb_size = 1; // format 0 or 1
    int RB_BWP_offset;
    if (pucch->initial_pucch_id == 15)
      RB_BWP_offset = pucch_pdu->bwp_size >> 2;
    else
      RB_BWP_offset = pucch_resourcecommon.PRB_offset;

    int N_CS = pucch_resourcecommon.nb_CS_indexes;

    if (pucch->initial_pucch_id >> 3 == 0) {
      const int tmp = pucch->initial_pucch_id / N_CS;
      pucch_pdu->prb_start = RB_BWP_offset + tmp;
      pucch_pdu->second_hop_prb = pucch_pdu->bwp_size - 1 - RB_BWP_offset - tmp;
      pucch_pdu->initial_cyclic_shift = pucch_resourcecommon.initial_CS_indexes[pucch->initial_pucch_id % N_CS];
    } else {
      const int tmp = (pucch->initial_pucch_id - 8) / N_CS;
      pucch_pdu->prb_start = pucch_pdu->bwp_size - 1 - RB_BWP_offset - tmp;
      pucch_pdu->second_hop_prb = RB_BWP_offset + tmp;
      pucch_pdu->initial_cyclic_shift = pucch_resourcecommon.initial_CS_indexes[(pucch->initial_pucch_id - 8) % N_CS];
    }
    pucch_pdu->freq_hop_flag = 1;
    pucch_pdu->time_domain_occ_idx = 0;
    // Only HARQ transmitted in default PUCCH
    pucch_pdu->mcs = get_pucch0_mcs(pucch->n_harq, 0, pucch->ack_payload, 0);
    pucch_pdu->payload = pucch->ack_payload;
    pucch_pdu->n_bit = 1;
  }
  else if (pucch->pucch_resource != NULL) {

    NR_PUCCH_Resource_t *pucchres = pucch->pucch_resource;

    if (mac->harq_ACK_SpatialBundlingPUCCH ||
        mac->pdsch_HARQ_ACK_Codebook != NR_PhysicalCellGroupConfig__pdsch_HARQ_ACK_Codebook_dynamic) {
      LOG_E(NR_MAC, "PUCCH Unsupported cell group configuration\n");
      return -1;
    } else if (sc_info->pdsch_CGB_Transmission) {
      LOG_E(NR_MAC, "PUCCH Unsupported code block group for serving cell config\n");
      return -1;
    }

    NR_PUSCH_Config_t *pusch_Config = current_UL_BWP ? current_UL_BWP->pusch_Config : NULL;
    if (pusch_Config) {
      pusch_id = pusch_Config->dataScramblingIdentityPUSCH;
      struct NR_SetupRelease_DMRS_UplinkConfig *tmp = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeA;
      if (tmp && tmp->choice.setup->transformPrecodingDisabled != NULL)
        id0 = tmp->choice.setup->transformPrecodingDisabled->scramblingID0;
      else {
        struct NR_SetupRelease_DMRS_UplinkConfig *tmp = pusch_Config->dmrs_UplinkForPUSCH_MappingTypeB;
        if (tmp && tmp->choice.setup->transformPrecodingDisabled != NULL)
          id0 = tmp->choice.setup->transformPrecodingDisabled->scramblingID0;
      }
    }

    NR_PUCCH_Config_t *pucch_Config = current_UL_BWP->pucch_Config;
    AssertFatal(pucch_Config, "no pucch_Config\n");

    pucch_pdu->bwp_size = current_UL_BWP->BWPSize;
    pucch_pdu->bwp_start = current_UL_BWP->BWPStart;
    pucch_pdu->prb_start = pucchres->startingPRB;
    pucch_pdu->freq_hop_flag = pucchres->intraSlotFrequencyHopping!= NULL ?  1 : 0;
    pucch_pdu->second_hop_prb = pucchres->secondHopPRB!= NULL ?  *pucchres->secondHopPRB : 0;
    pucch_pdu->prb_size = 1; // format 0 or 1

    int n_uci = pucch->n_sr + pucch->n_harq + pucch->n_csi;
    if (n_uci > (sizeof(uint64_t) * 8)) {
      LOG_E(NR_MAC, "PUCCH number of UCI bits exceeds payload size\n");
      return -1;
    }

    switch(pucchres->format.present) {
      case NR_PUCCH_Resource__format_PR_format0 :
        pucch_pdu->format_type = 0;
        pucch_pdu->initial_cyclic_shift = pucchres->format.choice.format0->initialCyclicShift;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format0->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format0->startingSymbolIndex;
        pucch_pdu->mcs = get_pucch0_mcs(pucch->n_harq, pucch->n_sr, pucch->ack_payload, pucch->sr_payload);
        break;
      case NR_PUCCH_Resource__format_PR_format1 :
        pucch_pdu->format_type = 1;
        pucch_pdu->initial_cyclic_shift = pucchres->format.choice.format1->initialCyclicShift;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format1->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format1->startingSymbolIndex;
        pucch_pdu->time_domain_occ_idx = pucchres->format.choice.format1->timeDomainOCC;
        if (pucch->n_harq > 0) {
          // only HARQ bits are transmitted, resource selection depends on SR
          // resource selection handled in function multiplex_pucch_resource
          pucch_pdu->n_bit = pucch->n_harq;
          pucch_pdu->payload = pucch->ack_payload;
        }
        else {
          // For a positive SR transmission using PUCCH format 1,
          // the UE transmits the PUCCH as described in 38.211 by setting b(0) = 0
          pucch_pdu->n_bit = pucch->n_sr;
          pucch_pdu->payload = 0;
        }
        break;
      case NR_PUCCH_Resource__format_PR_format2 :
        pucch_pdu->format_type = 2;
        pucch_pdu->n_bit = n_uci;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format2->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format2->startingSymbolIndex;
        pucch_pdu->data_scrambling_id = pusch_id != NULL ? *pusch_id : mac->physCellId;
        pucch_pdu->dmrs_scrambling_id = id0 != NULL ? *id0 : mac->physCellId;
        pucch_pdu->prb_size = compute_pucch_prb_size(2,
                                                     pucchres->format.choice.format2->nrofPRBs,
                                                     n_uci,
                                                     pucch_Config->format2->choice.setup->maxCodeRate,
                                                     2,
                                                     pucchres->format.choice.format2->nrofSymbols,
                                                     8);
        pucch_pdu->payload = (pucch->csi_part1_payload << (pucch->n_harq + pucch->n_sr)) | (pucch->sr_payload << pucch->n_harq) | pucch->ack_payload;
        break;
      case NR_PUCCH_Resource__format_PR_format3 :
        pucch_pdu->format_type = 3;
        pucch_pdu->n_bit = n_uci;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format3->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format3->startingSymbolIndex;
        pucch_pdu->data_scrambling_id = pusch_id != NULL ? *pusch_id : mac->physCellId;
        if (pucch_Config->format3 == NULL) {
          pucch_pdu->pi_2bpsk = 0;
          pucch_pdu->add_dmrs_flag = 0;
        }
        else {
          pucchfmt = pucch_Config->format3->choice.setup;
          pucch_pdu->pi_2bpsk = pucchfmt->pi2BPSK!= NULL ?  1 : 0;
          pucch_pdu->add_dmrs_flag = pucchfmt->additionalDMRS!= NULL ?  1 : 0;
        }
        int f3_dmrs_symbols;
        if (pucchres->format.choice.format3->nrofSymbols==4)
          f3_dmrs_symbols = 1<<pucch_pdu->freq_hop_flag;
        else {
          if(pucchres->format.choice.format3->nrofSymbols<10)
            f3_dmrs_symbols = 2;
          else
            f3_dmrs_symbols = 2<<pucch_pdu->add_dmrs_flag;
        }
        pucch_pdu->prb_size = compute_pucch_prb_size(3,
                                                     pucchres->format.choice.format3->nrofPRBs,
                                                     n_uci,
                                                     pucch_Config->format3->choice.setup->maxCodeRate,
                                                     2 - pucch_pdu->pi_2bpsk,
                                                     pucchres->format.choice.format3->nrofSymbols - f3_dmrs_symbols,
                                                     12);
        pucch_pdu->payload = (pucch->csi_part1_payload << (pucch->n_harq + pucch->n_sr)) | (pucch->sr_payload << pucch->n_harq) | pucch->ack_payload;
        break;
      case NR_PUCCH_Resource__format_PR_format4 :
        pucch_pdu->format_type = 4;
        pucch_pdu->nr_of_symbols = pucchres->format.choice.format4->nrofSymbols;
        pucch_pdu->start_symbol_index = pucchres->format.choice.format4->startingSymbolIndex;
        pucch_pdu->pre_dft_occ_len = pucchres->format.choice.format4->occ_Length;
        pucch_pdu->pre_dft_occ_idx = pucchres->format.choice.format4->occ_Index;
        pucch_pdu->data_scrambling_id = pusch_id!= NULL ? *pusch_id : mac->physCellId;
        if (pucch_Config->format3 == NULL) {
          pucch_pdu->pi_2bpsk = 0;
          pucch_pdu->add_dmrs_flag = 0;
        }
        else {
          pucchfmt = pucch_Config->format3->choice.setup;
          pucch_pdu->pi_2bpsk = pucchfmt->pi2BPSK!= NULL ?  1 : 0;
          pucch_pdu->add_dmrs_flag = pucchfmt->additionalDMRS!= NULL ?  1 : 0;
        }
        pucch_pdu->payload = (pucch->csi_part1_payload << (pucch->n_harq + pucch->n_sr)) | (pucch->sr_payload << pucch->n_harq) | pucch->ack_payload;
        break;
      default :
        LOG_E(NR_MAC, "Undefined PUCCH format \n");
        return -1;
    }

    int sum_delta_pucch = get_sum_delta_pucch(mac, slot, frame);

    pucch_pdu->pucch_tx_power = get_pucch_tx_power_ue(mac,
                                                      scs,
                                                      pucch_Config,
                                                      sum_delta_pucch,
                                                      pucch_pdu->format_type,
                                                      pucch_pdu->prb_size,
                                                      pucch_pdu->freq_hop_flag,
                                                      pucch_pdu->add_dmrs_flag,
                                                      pucch_pdu->nr_of_symbols,
                                                      subframe_number,
                                                      n_uci,
                                                      pucch_pdu->prb_start);
  } else {
    LOG_E(NR_MAC, "problem with pucch configuration\n");
    return -1;
  }

  NR_PUCCH_ConfigCommon_t *pucch_ConfigCommon = current_UL_BWP->pucch_ConfigCommon;

  if (pucch_ConfigCommon->hoppingId != NULL)
    pucch_pdu->hopping_id = *pucch_ConfigCommon->hoppingId;
  else
    pucch_pdu->hopping_id = mac->physCellId;

  switch (pucch_ConfigCommon->pucch_GroupHopping){
      case 0 :
      // if neither, both disabled
      pucch_pdu->group_hop_flag = 0;
      pucch_pdu->sequence_hop_flag = 0;
      break;
    case 1 :
      // if enable, group enabled
      pucch_pdu->group_hop_flag = 1;
      pucch_pdu->sequence_hop_flag = 0;
      break;
    case 2 :
      // if disable, sequence disabled
      pucch_pdu->group_hop_flag = 0;
      pucch_pdu->sequence_hop_flag = 1;
      break;
    default:
      LOG_E(NR_MAC, "Group hopping flag undefined (0,1,2) \n");
      return -1;
  }
  return 0;
}

static int find_pucch_resource_set(NR_PUCCH_Config_t *pucch_Config, int size)
{
  // Procedure described in 38.213 Section 9.2.1

  AssertFatal(pucch_Config && pucch_Config->resourceSetToAddModList, "pucch-Config NULL, this function shouldn't have been called\n");
  AssertFatal(size <= 1706, "O_UCI cannot be larger that 1706 bits\n");

  // a first set of PUCCH resources with pucch-ResourceSetId = 0 if O UCI ≤ 2 including 1 or 2 HARQ-ACK information bits
  if (size <= 2)
    return 0;

  const int n_set = pucch_Config->resourceSetToAddModList->list.count;

  int N2 = 1706;
  int N3 = 1706;
  for (int i = 0; i < n_set; i++) {
    NR_PUCCH_ResourceSet_t *pucchresset = pucch_Config->resourceSetToAddModList->list.array[i];
    NR_PUCCH_ResourceId_t id = pucchresset->pucch_ResourceSetId;
    if (id == 1)
      N2 = pucchresset->maxPayloadSize ? *pucchresset->maxPayloadSize : 1706;
    if (id == 2)
      N3 = pucchresset->maxPayloadSize ? *pucchresset->maxPayloadSize : 1706;
  }

  // a second set of PUCCH resources with pucch-ResourceSetId = 1, if provided by higher layers, if 2 < O UCI ≤ N 2
  if (size <= N2)
    return 1;
  // a third set of PUCCH resources with pucch-ResourceSetId = 2, if provided by higher layers, if N 2 < O UCI ≤ N 3
  if (size <= N3)
    return 2;
  // a fourth set of PUCCH resources with pucch-ResourceSetId = 3, if provided by higher layers, if N 3 < O UCI ≤ 1706
  return 3;
}

NR_PUCCH_Resource_t *find_pucch_resource_from_list(struct NR_PUCCH_Config__resourceToAddModList *resourceToAddModList,
                                                   long resource_id)
{
  NR_PUCCH_Resource_t *pucch_resource = NULL;
  int n_list = resourceToAddModList->list.count;
  for (int i = 0; i < n_list; i++) {
    if (resourceToAddModList->list.array[i]->pucch_ResourceId == resource_id)
      pucch_resource = resourceToAddModList->list.array[i];
  }
  return pucch_resource;
}

static bool check_mux_acknack_csi(NR_PUCCH_Resource_t *csi_res, NR_PUCCH_Config_t *pucch_Config)
{
  bool ret;
  switch (csi_res->format.present) {
    case NR_PUCCH_Resource__format_PR_format2:
      ret = pucch_Config->format2->choice.setup->simultaneousHARQ_ACK_CSI ? true : false;
      break;
    case NR_PUCCH_Resource__format_PR_format3:
      ret = pucch_Config->format3->choice.setup->simultaneousHARQ_ACK_CSI ? true : false;
      break;
    case NR_PUCCH_Resource__format_PR_format4:
      ret = pucch_Config->format4->choice.setup->simultaneousHARQ_ACK_CSI ? true : false;
      break;
    default:
      AssertFatal(false, "Invalid PUCCH format for CSI\n");
  }
  return ret;
}

void get_pucch_start_symbol_length(NR_PUCCH_Resource_t *pucch_resource, int *start, int *length)
{
  switch (pucch_resource->format.present) {
    case NR_PUCCH_Resource__format_PR_format0:
      *length = pucch_resource->format.choice.format0->nrofSymbols;
      *start = pucch_resource->format.choice.format0->startingSymbolIndex;
      break;
    case NR_PUCCH_Resource__format_PR_format1:
      *length = pucch_resource->format.choice.format1->nrofSymbols;
      *start = pucch_resource->format.choice.format1->startingSymbolIndex;
      break;
    case NR_PUCCH_Resource__format_PR_format2:
      *length = pucch_resource->format.choice.format2->nrofSymbols;
      *start = pucch_resource->format.choice.format2->startingSymbolIndex;
      break;
    case NR_PUCCH_Resource__format_PR_format3:
      *length = pucch_resource->format.choice.format3->nrofSymbols;
      *start = pucch_resource->format.choice.format3->startingSymbolIndex;
      break;
    case NR_PUCCH_Resource__format_PR_format4:
      *length = pucch_resource->format.choice.format4->nrofSymbols;
      *start = pucch_resource->format.choice.format4->startingSymbolIndex;
      break;
    default:
      AssertFatal(false, "Invalid PUCCH format\n");
  }
}

// Ref. 38.213 section 9.2.5 order(Q)
void order_resources(PUCCH_sched_t *pucch, int num_res)
{
  int k = 0;
  while (k < num_res - 1) {
    int l = 0;
    while (l < num_res - 1 - k) {
      NR_PUCCH_Resource_t *pucch_resource = pucch[l].pucch_resource;
      int curr_start, curr_length;
      get_pucch_start_symbol_length(pucch_resource, &curr_start, &curr_length);
      pucch_resource = pucch[l + 1].pucch_resource;
      int next_start, next_length;
      get_pucch_start_symbol_length(pucch_resource, &next_start, &next_length);
      if (curr_start > next_start || (curr_start == next_start && curr_length < next_length)) {
        // swap resources
        PUCCH_sched_t temp_res = pucch[l];
        pucch[l] = pucch[l + 1];
        pucch[l + 1] = temp_res;
      }
      l++;
    }
    k++;
  }
}

bool check_overlapping_resources(PUCCH_sched_t *pucch, int j, int o)
{
  // assuming overlapping means if two resources overlaps in time,
  // ie share a symbol in the slot regardless of PRB
  NR_PUCCH_Resource_t *pucch_resource = pucch[j - o].pucch_resource;
  int curr_start, curr_length;
  get_pucch_start_symbol_length(pucch_resource, &curr_start, &curr_length);
  pucch_resource = pucch[j + 1].pucch_resource;
  int next_start, next_length;
  get_pucch_start_symbol_length(pucch_resource, &next_start, &next_length);

  if (curr_start == next_start)
    return true;
  if (curr_start + curr_length - 1 < next_start)
    return false;
  else
    return true;
}

// 38.213 section 9.2.5
void merge_resources(PUCCH_sched_t *res, int num_res, NR_PUCCH_Config_t *pucch_Config)
{
  PUCCH_sched_t empty = {0};
  for (int i = 0; i < num_res - 1; i++) {
    NR_PUCCH_Resource_t *curr_resource = res[i].pucch_resource;
    NR_PUCCH_Resource_t *next_resource = res[i + 1].pucch_resource;
    switch (curr_resource->format.present) {
      case NR_PUCCH_Resource__format_PR_format0:
        switch (next_resource->format.present) {
          case NR_PUCCH_Resource__format_PR_format0:
            if (res[i].n_sr > 0 && res[i].n_harq == 0 && res[i + 1].n_sr == 0 && res[i + 1].n_harq > 0) {
              // we multiplex SR and HARQ in the HARQ resource
              res[i + 1].n_sr = res[i].n_sr;
              res[i + 1].sr_payload = res[i].sr_payload;
              res[i] = empty;
            } else if (res[i].n_sr == 0 && res[i].n_harq > 0 && res[i + 1].n_sr > 0 && res[i + 1].n_harq == 0) {
              // we multiplex SR and HARQ in the HARQ resource and move it to the i+1 spot
              res[i].n_sr = res[i + 1].n_sr;
              res[i].sr_payload = res[i + 1].sr_payload;
              res[i + 1] = res[i];
              res[i] = empty;
            } else
              AssertFatal(
                  false,
                  "We cannot multiplex more than 1 SR into a PUCCH F0 and we don't expect more than 1 PUCCH with HARQ per slot\n");
            break;
          case NR_PUCCH_Resource__format_PR_format1:
            if (res[i].n_sr > 0 && res[i].n_harq == 0 && res[i + 1].n_sr == 0 && res[i + 1].n_harq > 0)
              // we transmit only HARQ in F1
              res[i] = empty;
            else if (res[i].n_sr == 0 && res[i].n_harq > 0 && res[i + 1].n_sr > 0 && res[i + 1].n_harq == 0) {
              // we multiplex SR and HARQ in the HARQ F0 resource and move it to the i+1 spot
              res[i].n_sr = res[i + 1].n_sr;
              res[i].sr_payload = res[i + 1].sr_payload;
              res[i + 1] = res[i];
              res[i] = empty;
            } else
              AssertFatal(
                  false,
                  "We cannot multiplex more than 1 SR into a PUCCH F0 and we don't expect more than 1 PUCCH with HARQ per slot\n");
            break;
          case NR_PUCCH_Resource__format_PR_format2:
          case NR_PUCCH_Resource__format_PR_format3:
          case NR_PUCCH_Resource__format_PR_format4:
            if (res[i].n_sr > 0 && res[i].n_harq == 0) {
              AssertFatal(res[i + 1].n_sr == 0, "We don't support multiple SR in a slot\n");
              // we multiplex SR in the F2 or above resource
              res[i + 1].n_sr = res[i].n_sr;
              res[i + 1].sr_payload = res[i].sr_payload;
              res[i] = empty;
            } else if (res[i].n_harq > 0) {
              AssertFatal(res[i + 1].n_harq == 0, "The standard doesn't allow for more the 1 PUCCH in a slot with HARQ\n");
              if (check_mux_acknack_csi(next_resource, pucch_Config)) {
                // we multiplex what in F0 to CSI resource
                if (res[i + 1].n_sr == 0) {
                  res[i + 1].n_sr = res[i].n_sr;
                  res[i + 1].sr_payload = res[i].sr_payload;
                } else
                  AssertFatal(res[i].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                res[i + 1].n_harq = res[i].n_harq;
                res[i + 1].ack_payload = res[i].ack_payload;
                res[i] = empty;
              } else {
                // if we can't multiplex HARQ and CSI we discard CSI
                if (res[i + 1].n_sr == 0) {
                  res[i + 1] = res[i];
                  res[i] = empty;
                } else {
                  AssertFatal(res[i].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                  // we take SR from F2 or above into F0 and drop CSI
                  res[i].n_sr = res[i + 1].n_sr;
                  res[i].sr_payload = res[i + 1].sr_payload;
                  res[i + 1] = res[i];
                  res[i] = empty;
                }
              }
            }
            break;
          default:
            AssertFatal(false, "Invalid PUCCH format %d\n", next_resource->format.present);
        }
        break;
      case NR_PUCCH_Resource__format_PR_format1:
        switch (next_resource->format.present) {
          case NR_PUCCH_Resource__format_PR_format0:
            if (res[i].n_sr > 0 && res[i].n_harq == 0 && res[i + 1].n_sr == 0 && res[i + 1].n_harq > 0) {
              // we multiplex SR and HARQ in the HARQ F0 resource and move it to the i+1 spot
              res[i + 1].n_sr = res[i].n_sr;
              res[i + 1].sr_payload = res[i].sr_payload;
              res[i] = empty;
            } else if (res[i].n_sr == 0 && res[i].n_harq > 0 && res[i + 1].n_sr > 0 && res[i + 1].n_harq == 0) {
              // we transmit only HARQ in F1
              res[i + 1] = res[i];
              res[i] = empty;
            } else
              AssertFatal(
                  false,
                  "We cannot multiplex more than 1 SR into a PUCCH F0 and we don't expect more than 1 PUCCH with HARQ per slot\n");
            break;
          case NR_PUCCH_Resource__format_PR_format1:
            if (res[i].n_sr > 0 && res[i].n_harq == 0 && res[i + 1].n_sr == 0 && res[i + 1].n_harq > 0) {
              if (res[i].sr_payload == 0) {
                // negative SR -> transmit HARQ only in HARQ resource
                res[i] = empty;
              } else {
                // positive SR -> transmit HARQ only in SR resource
                res[i].n_harq = res[i + 1].n_harq;
                res[i].ack_payload = res[i + 1].ack_payload;
                res[i].n_sr = 0;
                res[i].sr_payload = 0;
                res[i + 1] = res[i];
                res[i] = empty;
              }
            } else if (res[i].n_sr == 0 && res[i].n_harq > 0 && res[i + 1].n_sr > 0 && res[i + 1].n_harq == 0) {
              if (res[i].sr_payload == 0) {
                // negative SR -> transmit HARQ only in HARQ resource
                res[i + 1] = res[i];
                res[i] = empty;
              } else {
                // positive SR -> transmit HARQ only in SR resource
                res[i + 1].n_harq = res[i].n_harq;
                res[i + 1].ack_payload = res[i].ack_payload;
                res[i + 1].n_sr = 0;
                res[i + 1].sr_payload = 0;
                res[i] = empty;
              }
            } else
              AssertFatal(
                  false,
                  "We cannot multiplex more than 1 SR into a PUCCH F0 and we don't expect more than 1 PUCCH with HARQ per slot\n");
            break;
          case NR_PUCCH_Resource__format_PR_format2:
          case NR_PUCCH_Resource__format_PR_format4:
          case NR_PUCCH_Resource__format_PR_format3:
            if (res[i].n_sr > 0 && res[i].n_harq == 0) {
              AssertFatal(res[i + 1].n_sr == 0, "We don't support multiple SR in a slot\n");
              // we multiplex SR in the F2 or above resource
              res[i + 1].n_sr = res[i].n_sr;
              res[i + 1].sr_payload = res[i].sr_payload;
              res[i] = empty;
            } else if (res[i].n_harq > 0) {
              AssertFatal(res[i + 1].n_harq == 0, "The standard doesn't allow for more the 1 PUCCH in a slot with HARQ\n");
              if (check_mux_acknack_csi(next_resource, pucch_Config)) {
                // we multiplex what in F0 to CSI resource
                if (res[i + 1].n_sr == 0) {
                  res[i + 1].n_sr = res[i].n_sr;
                  res[i + 1].sr_payload = res[i].sr_payload;
                } else
                  AssertFatal(res[i].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                res[i + 1].n_harq = res[i].n_harq;
                res[i + 1].ack_payload = res[i].ack_payload;
                res[i] = empty;
              } else {
                // if we can't multiplex HARQ and CSI we discard CSI
                if (res[i + 1].n_sr == 0) {
                  res[i + 1] = res[i];
                  res[i] = empty;
                } else {
                  AssertFatal(res[i].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                  if (res[i + 1].n_sr > 0)
                    LOG_E(MAC, "Not sure what to do here because you can't easily mux HARQ and SR in F1\n");
                  res[i + 1] = res[i];
                  res[i] = empty;
                }
              }
            }
            break;
          default:
            AssertFatal(false, "Invalid PUCCH format %d\n", next_resource->format.present);
        }
        break;
      case NR_PUCCH_Resource__format_PR_format2:
      case NR_PUCCH_Resource__format_PR_format4:
      case NR_PUCCH_Resource__format_PR_format3:
        switch (next_resource->format.present) {
          case NR_PUCCH_Resource__format_PR_format0:
            if (res[i + 1].n_sr > 0 && res[i + 1].n_harq == 0) {
              AssertFatal(res[i].n_sr == 0, "We don't support multiple SR in a slot\n");
              // we multiplex SR in the F2 or above resource
              res[i].n_sr = res[i + 1].n_sr;
              res[i].sr_payload = res[i + 1].sr_payload;
              res[i + 1] = res[i];
              res[i] = empty;
            } else if (res[i + 1].n_harq > 0) {
              AssertFatal(res[i].n_harq == 0, "The standard doesn't allow for more the 1 PUCCH in a slot with HARQ\n");
              if (check_mux_acknack_csi(curr_resource, pucch_Config)) {
                // we multiplex what in F0 to CSI resource
                if (res[i].n_sr == 0) {
                  res[i].n_sr = res[i + 1].n_sr;
                  res[i].sr_payload = res[i + 1].sr_payload;
                } else
                  AssertFatal(res[i + 1].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                res[i].n_harq = res[i + 1].n_harq;
                res[i].ack_payload = res[i + 1].ack_payload;
                res[i + 1] = res[i];
                res[i] = empty;
              } else {
                // if we can't multiplex HARQ and CSI we discard CSI
                if (res[i].n_sr == 0)
                  res[i] = empty;
                else {
                  AssertFatal(res[i + 1].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                  // we take SR from F2 or above into F0 and drop CSI
                  res[i + 1].n_sr = res[i].n_sr;
                  res[i + 1].sr_payload = res[i].sr_payload;
                  res[i] = empty;
                }
              }
            }
            break;
          case NR_PUCCH_Resource__format_PR_format1:
            if (res[i + 1].n_sr > 0 && res[i + 1].n_harq == 0) {
              AssertFatal(res[i].n_sr == 0, "We don't support multiple SR in a slot\n");
              // we multiplex SR in the F2 or above resource
              res[i].n_sr = res[i + 1].n_sr;
              res[i].sr_payload = res[i + 1].sr_payload;
              res[i + 1] = res[i];
              res[i] = empty;
            } else if (res[i + 1].n_harq > 0) {
              AssertFatal(res[i].n_harq == 0, "The standard doesn't allow for more the 1 PUCCH in a slot with HARQ\n");
              if (check_mux_acknack_csi(curr_resource, pucch_Config)) {
                // we multiplex what in F0 to CSI resource
                if (res[i].n_sr == 0) {
                  res[i].n_sr = res[i + 1].n_sr;
                  res[i].sr_payload = res[i + 1].sr_payload;
                } else
                  AssertFatal(res[i + 1].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                res[i].n_harq = res[i + 1].n_harq;
                res[i].ack_payload = res[i + 1].ack_payload;
                res[i + 1] = res[i];
                res[i] = empty;
              } else {
                // if we can't multiplex HARQ and CSI we discard CSI
                if (res[i].n_sr == 0)
                  res[i] = empty;
                else {
                  AssertFatal(res[i + 1].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                  if (res[i].n_sr > 0)
                    LOG_E(MAC, "Not sure what to do here because you can't easily mux HARQ and SR in F1\n");
                  res[i] = empty;
                }
              }
            }
            break;
          case NR_PUCCH_Resource__format_PR_format2:
          case NR_PUCCH_Resource__format_PR_format4:
          case NR_PUCCH_Resource__format_PR_format3:
            if (res[i + 1].n_csi > 0) {
              AssertFatal(res[i].n_csi == 0, "Multiplexing multiple CSI report in a single PUCCH not supported yet\n");
              AssertFatal(res[i].n_harq > 0 && res[i + 1].n_harq == 0,
                          "There is CSI in next F2 or above resource, since there is no CSI in current one, we expect HARQ in "
                          "there and not in next\n");
              // the UE expects to be provided a same configuration for simultaneousHARQ-ACK-CSI each of PUCCH formats 2, 3, and 4
              // we can check next or current
              if (check_mux_acknack_csi(next_resource, pucch_Config)) {
                // We need to use HARQ resource
                if (res[i + 1].n_sr > 0) {
                  AssertFatal(res[i].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                  res[i].n_sr = res[i + 1].n_sr;
                  res[i].sr_payload = res[i + 1].sr_payload;
                }
                res[i].n_csi = res[i + 1].n_csi;
                res[i].csi_part1_payload = res[i + 1].csi_part1_payload;
                res[i + 1] = res[i];
                res[i] = empty;
              } else {
                if (res[i].n_sr > 0) {
                  AssertFatal(res[i + 1].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                  // if we can't multiplex HARQ and CSI we discard CSI
                  res[i + 1] = res[i];
                  res[i] = empty;
                } else if (res[i + 1].n_sr == 0) {
                  // if we can't multiplex HARQ and CSI we discard CSI
                  res[i + 1] = res[i];
                  res[i] = empty;
                } else {
                  // we move SR (assuming it was previously multiplexed with CSI) into HARQ resource and discard CSI
                  res[i].n_sr = res[i + 1].n_sr;
                  res[i].sr_payload = res[i + 1].sr_payload;
                  res[i + 1] = res[i];
                  res[i] = empty;
                }
              }
            } else if (res[i].n_csi > 0) {
              AssertFatal(res[i + 1].n_csi == 0, "Multiplexing multiple CSI report in a single PUCCH not supported yet\n");
              AssertFatal(res[i + 1].n_harq > 0 && res[i].n_harq == 0,
                          "There is CSI in next F2 or above resource, since there is no CSI in current one, we expect HARQ in "
                          "there and not in next\n");
              // the UE expects to be provided a same configuration for simultaneousHARQ-ACK-CSI each of PUCCH formats 2, 3, and 4
              // we can check next or current
              if (check_mux_acknack_csi(next_resource, pucch_Config)) {
                // We need to use HARQ resource
                if (res[i].n_sr > 0) {
                  AssertFatal(res[i + 1].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                  res[i + 1].n_sr = res[i].n_sr;
                  res[i + 1].sr_payload = res[i].sr_payload;
                }
                res[i + 1].n_csi = res[i].n_csi;
                res[i + 1].csi_part1_payload = res[i].csi_part1_payload;
                res[i] = empty;
              } else {
                if (res[i + 1].n_sr > 0) {
                  AssertFatal(res[i].n_sr == 0, "We don't support more than 1 SR in a slot\n");
                  // if we can't multiplex HARQ and CSI we discard CSI
                  res[i] = empty;
                } else if (res[i].n_sr == 0) {
                  // if we can't multiplex HARQ and CSI we discard CSI
                  res[i] = empty;
                } else {
                  // we move SR (assuming it was previously multiplexed with CSI) into HARQ resource and discard CSI
                  res[i + 1].n_sr = res[i].n_sr;
                  res[i + 1].sr_payload = res[i].sr_payload;
                  res[i] = empty;
                }
              }
            } else
              AssertFatal(false, "We expect at least one of the 2 PUCCH F2 or above resources with CSI\n");
            break;
          default:
            AssertFatal(false, "Invalid PUCCH format %d\n", next_resource->format.present);
        }
        break;
      default:
        AssertFatal(false, "Invalid PUCCH format %d\n", curr_resource->format.present);
    }
  }
}

void multiplex_pucch_resource(NR_UE_MAC_INST_t *mac, PUCCH_sched_t *pucch, int num_res)
{
  NR_PUCCH_Config_t *pucch_Config = mac->current_UL_BWP->pucch_Config;
  order_resources(pucch, num_res);
  // following pseudocode in Ref. 38.213 section 9.2.5 to multiplex resources
  int j = 0;
  int o = 0;
  while (j <= num_res - 1) {
    if ((j < num_res - 1) && check_overlapping_resources(pucch, j, o)) {
      o++;
      j++;
    } else {
      if (o > 0) {
        merge_resources(&pucch[j - o], o + 1, pucch_Config);
        // move the resources to occupy the places left empty
        int num_empty = o;
        for (int i = j; i < num_res; i++)
          pucch[i - num_empty] = pucch[i];
        for (int e = num_res - num_empty; e < num_res; e++)
          memset(&pucch[e], 0, sizeof(pucch[e]));
        j = 0;
        o = 0;
        num_res = num_res - num_empty;
        order_resources(pucch, num_res);
      } else
        j++;
    }
  }
}

void configure_initial_pucch(PUCCH_sched_t *pucch, int res_ind)
{
  /* see TS 38.213 9.2.1  PUCCH Resource Sets */
  int delta_PRI = res_ind;
  int n_CCE_0 = pucch->n_CCE;
  int N_CCE_0 = pucch->N_CCE;
  if (N_CCE_0 == 0)
    AssertFatal(1 == 0, "PUCCH No compatible pucch format found\n");
  int r_PUCCH = ((2 * n_CCE_0) / N_CCE_0) + (2 * delta_PRI);
  pucch->initial_pucch_id = r_PUCCH;
  pucch->pucch_resource = NULL;
}

/*******************************************************************
*
* NAME :         get_downlink_ack
*
* PARAMETERS :   ue context
*                processing slots of reception/transmission
*                gNB_id identifier
*
* RETURN :       o_ACK acknowledgment data
*                o_ACK_number_bits number of bits for acknowledgment
*
* DESCRIPTION :  return acknowledgment value
*                TS 38.213 9.1.3 Type-2 HARQ-ACK codebook determination
*
*          --+--------+-------+--------+-------+---  ---+-------+--
*            | PDCCH1 |       | PDCCH2 |PDCCH3 |        | PUCCH |
*          --+--------+-------+--------+-------+---  ---+-------+--
*    DAI_DL      1                 2       3              ACK for
*                V                 V       V        PDCCH1, PDDCH2 and PCCH3
*                |                 |       |               ^
*                +-----------------+-------+---------------+
*
*                PDCCH1, PDCCH2 and PDCCH3 are PDCCH monitoring occasions
*                M is the total of monitoring occasions
*
*********************************************************************/

bool get_downlink_ack(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, PUCCH_sched_t *pucch)
{
  uint32_t ack_data[NR_DL_MAX_NB_CW][NR_MAX_HARQ_PROCESSES] = {{0},{0}};
  uint32_t dai[NR_DL_MAX_NB_CW][NR_MAX_HARQ_PROCESSES] = {{0},{0}};       /* for serving cell */
  uint32_t dai_total[NR_DL_MAX_NB_CW][NR_MAX_HARQ_PROCESSES] = {{0},{0}}; /* for multiple cells */
  int number_harq_feedback = 0;
  uint32_t dai_max = 0;

  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;

  bool two_transport_blocks = false;
  int number_of_code_word = 1;
  if (current_DL_BWP
      && current_DL_BWP->pdsch_Config
      && current_DL_BWP->pdsch_Config->maxNrofCodeWordsScheduledByDCI
      && current_DL_BWP->pdsch_Config->maxNrofCodeWordsScheduledByDCI[0] == 2) {
    two_transport_blocks = true;
    number_of_code_word = 2;
  }

  int res_ind = -1;

  /* look for dl acknowledgment which should be done on current uplink slot */
  for (int code_word = 0; code_word < number_of_code_word; code_word++) {

    for (int dl_harq_pid = 0; dl_harq_pid < NR_MAX_HARQ_PROCESSES; dl_harq_pid++) {

      NR_UE_HARQ_STATUS_t *current_harq = &mac->dl_harq_info[dl_harq_pid];

      if (current_harq->active) {
        LOG_D(PHY, "HARQ pid %d is active for %d.%d\n",
              dl_harq_pid, current_harq->ul_frame, current_harq->ul_slot);
        /* check if current tx slot should transmit downlink acknowlegment */
        if (current_harq->ul_frame == frame && current_harq->ul_slot == slot) {
          if (get_softmodem_params()->emulate_l1) {
            mac->nr_ue_emul_l1.harq[dl_harq_pid].active = true;
            mac->nr_ue_emul_l1.harq[dl_harq_pid].active_dl_harq_sfn = frame;
            mac->nr_ue_emul_l1.harq[dl_harq_pid].active_dl_harq_slot = slot;
          }
          if (res_ind != -1 && res_ind != current_harq->pucch_resource_indicator)
            LOG_E(NR_MAC,
                  "Value of pucch_resource_indicator %d not matching with what set before %d (Possibly due to a false DCI) \n",
                  current_harq->pucch_resource_indicator,
                  res_ind);
          else{
            if (current_harq->dai_cumul == 0) {
              LOG_E(NR_MAC,"PUCCH Downlink DAI is invalid\n");
              return false;
            } else if (current_harq->dai_cumul > dai_max) {
              dai_max = current_harq->dai_cumul;
            }

            number_harq_feedback++;
            int dai_index = current_harq->dai_cumul - 1;
            if (current_harq->ack_received) {
              ack_data[code_word][dai_index] = current_harq->ack;
              current_harq->active = false;
              current_harq->ack_received = false;
            } else {
              LOG_E(NR_MAC, "DLSCH ACK/NACK reporting initiated for harq pid %d before DLSCH decoding completed\n", dl_harq_pid);
              ack_data[code_word][dai_index] = 0;
            }
            dai[code_word][dai_index] = (dai_index % 4) + 1; // value between 1 and 4
            int temp_ind = current_harq->pucch_resource_indicator;
            AssertFatal(res_ind == -1 || res_ind == temp_ind,
                        "Current resource index %d does not match with previous resource index %d\n",
                        temp_ind,
                        res_ind);
            res_ind = temp_ind;
            pucch->n_CCE = current_harq->n_CCE;
            pucch->N_CCE = current_harq->N_CCE;
            LOG_D(NR_MAC,"%4d.%2d Sent %d ack on harq pid %d\n", frame, slot, current_harq->ack, dl_harq_pid);
          }
        }
      }
    }
  }

  /* no any ack to transmit */
  if (number_harq_feedback == 0) {
    return false;
  }
  else  if (number_harq_feedback > (sizeof(uint32_t)*8)) {
    LOG_E(MAC,"PUCCH number of ack bits exceeds payload size\n");
    return false;
  }

  for (int code_word = 0; code_word < number_of_code_word; code_word++) {
    for (uint32_t i = 0; i < dai_max ; i++ ) {
      if (dai[code_word][i] == 0) {
        dai[code_word][i] = (i % 4) + 1; // it covers case for which PDCCH DCI has not been successfully decoded and so it has been missed
        ack_data[code_word][i] = 0;      // nack data transport block which has been missed
        number_harq_feedback++;
      }
      if (two_transport_blocks == true) {
        dai_total[code_word][i] = dai[code_word][i]; /* for a single cell, dai_total is the same as dai of first cell */
      }
    }
  }

  int M = dai_max;
  int j = 0;
  uint32_t V_temp = 0;
  uint32_t V_temp2 = 0;
  int O_ACK = 0;
  uint8_t o_ACK = 0;
  int O_bit_number_cw0 = 0;
  int O_bit_number_cw1 = 0;

  for (int m = 0; m < M ; m++) {
    if (dai[0][m] <= V_temp) {
      j = j + 1;
    }

    V_temp = dai[0][m]; /* value of the counter DAI for format 1_0 and format 1_1 on serving cell c */

    if (dai_total[0][m] == 0) {
      V_temp2 = dai[0][m];
    } else {
      V_temp2 = dai[1][m];         /* second code word has been received */
      O_bit_number_cw1 = (8 * j) + 2*(V_temp - 1) + 1;
      o_ACK = o_ACK | (ack_data[1][m] << O_bit_number_cw1);
    }

    if (two_transport_blocks == true) {
      O_bit_number_cw0 = (8 * j) + 2*(V_temp - 1);
    }
    else {
      O_bit_number_cw0 = (4 * j) + (V_temp - 1);
    }

    o_ACK = o_ACK | (ack_data[0][m] << O_bit_number_cw0);
    LOG_D(MAC,"m %d bit number %d o_ACK %d\n",m,O_bit_number_cw0,o_ACK);
  }

  if (V_temp2 < V_temp) {
    j = j + 1;
  }

  if (two_transport_blocks == true) {
    O_ACK = 2 * ( 4 * j + V_temp2);  /* for two transport blocks */
  }
  else {
    O_ACK = 4 * j + V_temp2;         /* only one transport block */
  }

  if (number_harq_feedback != O_ACK) {
    LOG_E(MAC,"PUCCH Error for number of bits for acknowledgment\n");
    return false;
  }

  NR_PUCCH_Config_t *pucch_Config = current_UL_BWP ? current_UL_BWP->pucch_Config : NULL;
  if (!(pucch_Config && pucch_Config->resourceSetToAddModList && pucch_Config->resourceSetToAddModList->list.array[0]))
    configure_initial_pucch(pucch, res_ind);
  else {
    int resource_set_id = find_pucch_resource_set(pucch_Config, O_ACK);
    int n_list = pucch_Config->resourceSetToAddModList->list.count;
    AssertFatal(resource_set_id < n_list, "Invalid PUCCH resource set id %d\n", resource_set_id);
    n_list = pucch_Config->resourceSetToAddModList->list.array[resource_set_id]->resourceList.list.count;
    AssertFatal(res_ind < n_list, "Invalid PUCCH resource id %d\n", res_ind);
    long *acknack_resource_id =
        pucch_Config->resourceSetToAddModList->list.array[resource_set_id]->resourceList.list.array[res_ind];
    AssertFatal(acknack_resource_id != NULL, "Couldn't find PUCCH Resource ID in ResourceSet\n");
    NR_PUCCH_Resource_t *acknack_resource = find_pucch_resource_from_list(pucch_Config->resourceToAddModList, *acknack_resource_id);
    AssertFatal(acknack_resource != NULL, "Couldn't find PUCCH Resource ID for ACK/NACK in PUCCH resource list\n");
    pucch->pucch_resource = acknack_resource;
    LOG_D(MAC, "frame %d slot %d pucch acknack payload %d\n", frame, slot, o_ACK);
  }
  pucch->ack_payload = reverse_bits(o_ACK, number_harq_feedback);
  pucch->n_harq = number_harq_feedback;

  return (number_harq_feedback > 0);
}

bool trigger_periodic_scheduling_request(NR_UE_MAC_INST_t *mac, PUCCH_sched_t *pucch, frame_t frame, int slot)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_PUCCH_Config_t *pucch_Config = current_UL_BWP ? current_UL_BWP->pucch_Config : NULL;

  if(!pucch_Config ||
     !pucch_Config->schedulingRequestResourceToAddModList ||
     pucch_Config->schedulingRequestResourceToAddModList->list.count == 0)
    return false; // SR not configured

  int sr_count = 0;
  for (int id = 0; id < pucch_Config->schedulingRequestResourceToAddModList->list.count; id++) {
    NR_SchedulingRequestResourceConfig_t *sr_Config = pucch_Config->schedulingRequestResourceToAddModList->list.array[id];
    int SR_period; int SR_offset;

    find_period_offset_SR(sr_Config, &SR_period, &SR_offset);
    const int n_slots_frame = nr_slots_per_frame[current_UL_BWP->scs];
    int sfn_sf = frame * n_slots_frame + slot;

    if ((sfn_sf - SR_offset) % SR_period == 0) {
      LOG_D(MAC, "Scheduling Request active in frame %d slot %d \n", frame, slot);
      if (!sr_Config->resource) {
        LOG_E(MAC, "No resource associated with SR. SR not scheduled\n");
        break;
      }
      NR_PUCCH_Resource_t *sr_pucch =
          find_pucch_resource_from_list(pucch_Config->resourceToAddModList, *sr_Config->resource);
      AssertFatal(sr_pucch != NULL, "Couldn't find PUCCH Resource ID for SR in PUCCH resource list\n");
      pucch->pucch_resource = sr_pucch;
      pucch->n_sr = 1;
      /* sr_payload = 1 means that this is a positive SR, sr_payload = 0 means that it is a negative SR */
      int ret = nr_ue_get_SR(mac, frame, slot, sr_Config->schedulingRequestID);
      if (ret < 0) {
        memset(pucch, 0, sizeof(*pucch));
        return false;
      }
      pucch->sr_payload = ret;
      sr_count++;
      AssertFatal(sr_count < 2, "Cannot handle more than 1 SR per slot yet\n");
    }
  }
  return sr_count > 0 ? true : false;
}

int8_t nr_ue_get_SR(NR_UE_MAC_INST_t *mac, frame_t frame, slot_t slot, NR_SchedulingRequestId_t sr_id)
{
  // no UL-SCH resources available for this tti && UE has a valid PUCCH resources for SR configuration for this tti
  NR_UE_SCHEDULING_INFO *si = &mac->scheduling_info;
  nr_sr_info_t *sr_info = &si->sr_info[sr_id];
  if (!sr_info->active_SR_ID) {
    LOG_E(NR_MAC, "SR triggered for an inactive SR with ID %ld\n", sr_id);
    return 0;
  }

  LOG_D(NR_MAC, "[UE %d] Frame %d slot %d SR %s for ID %ld\n",
        mac->ue_id,
        frame,
        slot,
        sr_info->pending ? "pending" : "not pending",
        sr_id); // todo

  // TODO check if the PUCCH resource for the SR transmission occasion does not overlap with a UL-SCH resource
  if (!sr_info->pending || is_nr_timer_active(sr_info->prohibitTimer))
    return 0;

  if (sr_info->counter < sr_info->maxTransmissions) {
    sr_info->counter++;
    // start the sr-prohibittimer
    nr_timer_start(&sr_info->prohibitTimer);
    LOG_D(NR_MAC, "[UE %d] Frame %d slot %d instruct the physical layer to signal the SR (counter/sr_TransMax %d/%d)\n",
          mac->ue_id,
          frame,
          slot,
          sr_info->counter,
          sr_info->maxTransmissions);
    return 1;
  }

  LOG_W(NR_MAC,
        "[UE %d] SR not served! SR counter %d reached sr_MaxTransmissions %d\n",
        mac->ue_id,
        sr_info->counter,
        sr_info->maxTransmissions);

  // initiate a Random Access procedure (see clause 5.1) on the SpCell and cancel all pending SRs.
  sr_info->pending = false;
  sr_info->counter = 0;
  nr_timer_stop(&sr_info->prohibitTimer);
  schedule_RA_after_SR_failure(mac);
  return -1;
}

// section 5.2.5 of 38.214
int compute_csi_priority(NR_UE_MAC_INST_t *mac, NR_CSI_ReportConfig_t *csirep)
{
  int y = 4 - csirep->reportConfigType.present;
  int k = csirep->reportQuantity.present == NR_CSI_ReportConfig__reportQuantity_PR_cri_RSRP
                  || csirep->reportQuantity.present == NR_CSI_ReportConfig__reportQuantity_PR_ssb_Index_RSRP
              ? 0
              : 1;
  int Ncells = 32; // maxNrofServingCells
  int c = mac->servCellIndex;
  int s = csirep->reportConfigId;
  int Ms = 48; // maxNrofCSI-ReportConfigurations

  return 2 * Ncells * Ms * y + Ncells * Ms * k + Ms * c + s;
}

int nr_get_csi_measurements(NR_UE_MAC_INST_t *mac, frame_t frame, int slot, PUCCH_sched_t *pucch)
{
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  NR_PUCCH_Config_t *pucch_Config = current_UL_BWP ? current_UL_BWP->pucch_Config : NULL;
  int num_csi = 0;

  if (mac->sc_info.csi_MeasConfig) {
    NR_CSI_MeasConfig_t *csi_measconfig = mac->sc_info.csi_MeasConfig;

    int csi_priority = INT_MAX;
    for (int csi_report_id = 0; csi_report_id < csi_measconfig->csi_ReportConfigToAddModList->list.count; csi_report_id++) {
      NR_CSI_ReportConfig_t *csirep = csi_measconfig->csi_ReportConfigToAddModList->list.array[csi_report_id];

      if(csirep->reportConfigType.present == NR_CSI_ReportConfig__reportConfigType_PR_periodic) {
        int period, offset;
        csi_period_offset(csirep, NULL, &period, &offset);
        const int n_slots_frame = nr_slots_per_frame[current_UL_BWP->scs];
        if (((n_slots_frame*frame + slot - offset)%period) == 0 && pucch_Config) {
          int csi_res_id = -1;
          for (int i = 0; i < csirep->reportConfigType.choice.periodic->pucch_CSI_ResourceList.list.count; i++) {
            const NR_PUCCH_CSI_Resource_t *pucchcsires =
                csirep->reportConfigType.choice.periodic->pucch_CSI_ResourceList.list.array[i];
            if (pucchcsires->uplinkBandwidthPartId == current_UL_BWP->bwp_id) {
              csi_res_id = pucchcsires->pucch_Resource;
              break;
            }
          }
          if (csi_res_id < 0) {
            // This CSI Report ID is not associated with current active BWP
            continue;
          }
          NR_PUCCH_Resource_t *csi_pucch = find_pucch_resource_from_list(pucch_Config->resourceToAddModList, csi_res_id);
          AssertFatal(csi_pucch != NULL, "Couldn't find PUCCH Resource ID for SR in PUCCH resource list\n");
          LOG_D(NR_MAC, "Preparing CSI report in frame %d slot %d CSI report ID %d\n", frame, slot, csi_report_id);
          int temp_priority = compute_csi_priority(mac, csirep);
          if (num_csi > 0) {
            // need to verify if we can multiplex multiple CSI report
            if (pucch_Config->multi_CSI_PUCCH_ResourceList) {
              AssertFatal(false, "Multiplexing multiple CSI report in a single PUCCH not supported yet\n");
            } else if (temp_priority < csi_priority) {
              // we discard previous report
              csi_priority = temp_priority;
              num_csi = 1;
              csi_payload_t csi = nr_get_csi_payload(mac, csi_report_id, WIDEBAND_ON_PUCCH, csi_measconfig);
              pucch->n_csi = csi.p1_bits;
              pucch->csi_part1_payload = csi.part1_payload;
              pucch->pucch_resource = csi_pucch;
            } else
              continue;
          } else {
            num_csi = 1;
            csi_priority = temp_priority;
            csi_payload_t csi = nr_get_csi_payload(mac, csi_report_id, WIDEBAND_ON_PUCCH, csi_measconfig);
            pucch->n_csi = csi.p1_bits;
            pucch->csi_part1_payload = csi.part1_payload;
            pucch->pucch_resource = csi_pucch;
          }
        }
      }
      else
        AssertFatal(csirep->reportConfigType.present == NR_CSI_ReportConfig__reportConfigType_PR_aperiodic,
                    "Not supported CSI report type\n");
    }
  }
  return num_csi;
}

csi_payload_t nr_get_csi_payload(NR_UE_MAC_INST_t *mac,
                                 int csi_report_id,
                                 CSI_mapping_t mapping_type,
                                 NR_CSI_MeasConfig_t *csi_MeasConfig)
{
  AssertFatal(csi_MeasConfig->csi_ReportConfigToAddModList->list.count > 0,"No CSI Report configuration available\n");
  csi_payload_t csi = {0};
  struct NR_CSI_ReportConfig *csi_reportconfig = csi_MeasConfig->csi_ReportConfigToAddModList->list.array[csi_report_id];
  NR_CSI_ResourceConfigId_t csi_ResourceConfigId = csi_reportconfig->resourcesForChannelMeasurement;
  switch(csi_reportconfig->reportQuantity.present) {
    case NR_CSI_ReportConfig__reportQuantity_PR_none:
      break;
    case NR_CSI_ReportConfig__reportQuantity_PR_ssb_Index_RSRP:
      csi = get_ssb_rsrp_payload(mac, csi_reportconfig, csi_ResourceConfigId, csi_MeasConfig);
      break;
    case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_PMI_CQI:
      csi = get_csirs_RI_PMI_CQI_payload(mac, csi_reportconfig, csi_ResourceConfigId, csi_MeasConfig, mapping_type);
      break;
    case NR_CSI_ReportConfig__reportQuantity_PR_cri_RSRP:
      csi = get_csirs_RSRP_payload(mac, csi_reportconfig, csi_ResourceConfigId, csi_MeasConfig);
      break;
    case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_i1:
    case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_i1_CQI:
    case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_CQI:
    case NR_CSI_ReportConfig__reportQuantity_PR_cri_RI_LI_PMI_CQI:
      LOG_E(NR_MAC,"Measurement report %d based on CSI-RS is not available\n", csi_reportconfig->reportQuantity.present);
      break;
    default:
      AssertFatal(1==0,"Invalid CSI report quantity type %d\n",csi_reportconfig->reportQuantity.present);
  }
  return csi;
}


csi_payload_t get_ssb_rsrp_payload(NR_UE_MAC_INST_t *mac,
                                   struct NR_CSI_ReportConfig *csi_reportconfig,
                                   NR_CSI_ResourceConfigId_t csi_ResourceConfigId,
                                   NR_CSI_MeasConfig_t *csi_MeasConfig)
{
  int nb_ssb = 0;  // nb of ssb in the resource
  int nb_meas = 0; // nb of ssb to report measurements on
  int bits = 0;
  uint32_t temp_payload = 0;

  for (int csi_resourceidx = 0; csi_resourceidx < csi_MeasConfig->csi_ResourceConfigToAddModList->list.count; csi_resourceidx++) {
    struct NR_CSI_ResourceConfig *csi_resourceconfig = csi_MeasConfig->csi_ResourceConfigToAddModList->list.array[csi_resourceidx];
    if (csi_resourceconfig->csi_ResourceConfigId == csi_ResourceConfigId) {

      if (csi_reportconfig->groupBasedBeamReporting.present == NR_CSI_ReportConfig__groupBasedBeamReporting_PR_disabled) {
        if (csi_reportconfig->groupBasedBeamReporting.choice.disabled->nrofReportedRS != NULL)
          nb_meas = *(csi_reportconfig->groupBasedBeamReporting.choice.disabled->nrofReportedRS)+1;
        else
          nb_meas = 1;
      } else
        nb_meas = 2;

      struct NR_CSI_SSB_ResourceSet__csi_SSB_ResourceList SSB_resource;
      for (int csi_ssb_idx = 0; csi_ssb_idx < csi_MeasConfig->csi_SSB_ResourceSetToAddModList->list.count; csi_ssb_idx++) {
        if (csi_MeasConfig->csi_SSB_ResourceSetToAddModList->list.array[csi_ssb_idx]->csi_SSB_ResourceSetId ==
            *(csi_resourceconfig->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->csi_SSB_ResourceSetList->list.array[0])){
          SSB_resource = csi_MeasConfig->csi_SSB_ResourceSetToAddModList->list.array[csi_ssb_idx]->csi_SSB_ResourceList;
          ///only one SSB resource set from spec 38.331 IE CSI-ResourceConfig
          nb_ssb = SSB_resource.list.count;
          break;
        }
      }

      AssertFatal(nb_ssb>0,"No SSB found in the resource set\n");
      AssertFatal(nb_meas==1,"PHY currently reports only the strongest SSB to MAC. Can't report more than 1 RSRP\n");
      int ssbri_bits = ceil(log2(nb_ssb));

      int ssb_rsrp[2][nb_meas]; // the array contains index and RSRP of each SSB to be reported (nb_meas highest RSRPs)
      memset(ssb_rsrp, 0, sizeof(ssb_rsrp));

      //TODO replace the following 2 lines with a function to order the nb_meas highest SSB RSRPs
      for (int i=0; i<nb_ssb; i++) {
        if(*SSB_resource.list.array[i] == mac->mib_ssb) {
          ssb_rsrp[0][0] = i;
          break;
        }
      }
      AssertFatal(*SSB_resource.list.array[ssb_rsrp[0][0]] == mac->mib_ssb, "Couldn't find corresponding SSB in csi_SSB_ResourceList\n");
      ssb_rsrp[1][0] = mac->ssb_measurements.ssb_rsrp_dBm;

      uint8_t ssbi;

      if (ssbri_bits > 0) {
        ssbi = ssb_rsrp[0][0];
        temp_payload = reverse_bits(ssbi, ssbri_bits);
        bits += ssbri_bits;
      }

      uint8_t rsrp_idx = get_rsrp_index(ssb_rsrp[1][0]);
      temp_payload |= (reverse_bits(rsrp_idx, 7) << bits);
      bits += 7; // 7 bits for highest RSRP

      // from the second SSB, differential report
      for (int i=1; i<nb_meas; i++){
        ssbi = ssb_rsrp[0][i];
        temp_payload = reverse_bits(ssbi, ssbri_bits);
        bits += ssbri_bits;

        rsrp_idx = get_rsrp_diff_index(ssb_rsrp[1][0],ssb_rsrp[1][i]);
        temp_payload |= (reverse_bits(rsrp_idx, 4) << bits);
        bits += 4; // 7 bits for highest RSRP
      }
      break; // resorce found
    }
  }
  AssertFatal(bits <= 32, "Not supporting CSI report with more than 32 bits\n");
  csi_payload_t csi = {.part1_payload = temp_payload, .p1_bits = bits, csi.p2_bits = 0};
  return csi;
}

csi_payload_t get_csirs_RI_PMI_CQI_payload(NR_UE_MAC_INST_t *mac,
                                           struct NR_CSI_ReportConfig *csi_reportconfig,
                                           NR_CSI_ResourceConfigId_t csi_ResourceConfigId,
                                           NR_CSI_MeasConfig_t *csi_MeasConfig,
                                           CSI_mapping_t mapping_type)
{
  int p1_bits = 0;
  int p2_bits = 0;
  uint64_t temp_payload_1 = 0;
  uint64_t temp_payload_2 = 0;
  AssertFatal(mapping_type != SUBBAND_ON_PUCCH, "CSI mapping for subband PMI and CQI not implemented\n");

  for (int csi_resourceidx = 0; csi_resourceidx < csi_MeasConfig->csi_ResourceConfigToAddModList->list.count; csi_resourceidx++) {

    struct NR_CSI_ResourceConfig *csi_resourceconfig = csi_MeasConfig->csi_ResourceConfigToAddModList->list.array[csi_resourceidx];
    if (csi_resourceconfig->csi_ResourceConfigId == csi_ResourceConfigId) {

      for (int csi_idx = 0; csi_idx < csi_MeasConfig->nzp_CSI_RS_ResourceSetToAddModList->list.count; csi_idx++) {
        if (csi_MeasConfig->nzp_CSI_RS_ResourceSetToAddModList->list.array[csi_idx]->nzp_CSI_ResourceSetId ==
            *(csi_resourceconfig->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.array[0])) {

          nr_csi_report_t *csi_report = NULL;
          for (int i = 0; i < MAX_CSI_REPORTCONFIG; i++) {
            if (mac->csi_report_template[i].reportConfigId == csi_reportconfig->reportConfigId) {
              csi_report = &mac->csi_report_template[i];
              break;
            }
          }
          AssertFatal(csi_report, "Couldn't find CSI report with ID %ld\n", csi_reportconfig->reportConfigId);
          int cri_bitlen = csi_report->csi_meas_bitlen.cri_bitlen;
          int ri_bitlen = csi_report->csi_meas_bitlen.ri_bitlen;
          int pmi_x1_bitlen = csi_report->csi_meas_bitlen.pmi_x1_bitlen[mac->csirs_measurements.rank_indicator];
          int pmi_x2_bitlen = csi_report->csi_meas_bitlen.pmi_x2_bitlen[mac->csirs_measurements.rank_indicator];
          int cqi_bitlen = csi_report->csi_meas_bitlen.cqi_bitlen[mac->csirs_measurements.rank_indicator];

          if (get_softmodem_params()->emulate_l1) {
            static const uint8_t mcs_to_cqi[] = {0, 1, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9,
                                                 10, 10, 11, 11, 12, 12, 13, 13, 14, 14, 15, 15};
            CHECK_INDEX(nr_bler_data, NR_NUM_MCS - 1);
            int mcs = get_mcs_from_sinr(nr_bler_data, (mac->nr_ue_emul_l1.cqi - 640) * 0.1);
            CHECK_INDEX(mcs_to_cqi, mcs);
            mac->csirs_measurements.rank_indicator = mac->nr_ue_emul_l1.ri;
            mac->csirs_measurements.i1 = mac->nr_ue_emul_l1.pmi;
            mac->csirs_measurements.cqi = mcs_to_cqi[mcs];
          }

          int padding_bitlen = 0;
          // TODO: Improvements will be needed to cri_bitlen>0 and pmi_x1_bitlen>0
          if (mapping_type == ON_PUSCH) {
            p1_bits = cri_bitlen + ri_bitlen + cqi_bitlen;
            p2_bits = pmi_x1_bitlen + pmi_x2_bitlen;
            temp_payload_1 = (0/*mac->csi_measurements.cri*/ << (cqi_bitlen + ri_bitlen)) |
                             (mac->csirs_measurements.rank_indicator << cqi_bitlen) |
                             (mac->csirs_measurements.cqi);
            temp_payload_2 = (mac->csirs_measurements.i1 << pmi_x2_bitlen) |
                             mac->csirs_measurements.i2;
          }
          else {
            p1_bits = nr_get_csi_bitlen(csi_report);
            padding_bitlen = p1_bits - (cri_bitlen + ri_bitlen + pmi_x1_bitlen + pmi_x2_bitlen + cqi_bitlen);
            temp_payload_1 = (0/*mac->csi_measurements.cri*/ << (cqi_bitlen + pmi_x2_bitlen + pmi_x1_bitlen + padding_bitlen + ri_bitlen)) |
                             (mac->csirs_measurements.rank_indicator << (cqi_bitlen + pmi_x2_bitlen + pmi_x1_bitlen + padding_bitlen)) |
                             (mac->csirs_measurements.i1 << (cqi_bitlen + pmi_x2_bitlen)) |
                             (mac->csirs_measurements.i2 << (cqi_bitlen)) |
                             (mac->csirs_measurements.cqi);
          }

          temp_payload_1 = reverse_bits(temp_payload_1, p1_bits);
          temp_payload_2 = reverse_bits(temp_payload_2, p2_bits);
          LOG_D(NR_MAC, "cri_bitlen = %d\n", cri_bitlen);
          LOG_D(NR_MAC, "ri_bitlen = %d\n", ri_bitlen);
          LOG_D(NR_MAC, "pmi_x1_bitlen = %d\n", pmi_x1_bitlen);
          LOG_D(NR_MAC, "pmi_x2_bitlen = %d\n", pmi_x2_bitlen);
          LOG_D(NR_MAC, "cqi_bitlen = %d\n", cqi_bitlen);
          LOG_D(NR_MAC, "csi_part1_payload = 0x%lx\n", temp_payload_1);
          LOG_D(NR_MAC, "csi_part2_payload = 0x%lx\n", temp_payload_2);
          LOG_D(NR_MAC, "part1_bits = %d\n", p1_bits);
          LOG_D(NR_MAC, "part2_bits = %d\n", p2_bits);
          break;
        }
      }
    }
  }
  AssertFatal(p1_bits <= 32 && p2_bits <= 32, "Not supporting CSI report with more than 32 bits\n");
  csi_payload_t csi = {.part1_payload = temp_payload_1, .part2_payload = temp_payload_2, .p1_bits = p1_bits, csi.p2_bits = p2_bits};
  return csi;
}

csi_payload_t get_csirs_RSRP_payload(NR_UE_MAC_INST_t *mac,
                                     struct NR_CSI_ReportConfig *csi_reportconfig,
                                     NR_CSI_ResourceConfigId_t csi_ResourceConfigId,
                                     const NR_CSI_MeasConfig_t *csi_MeasConfig)
{
  int n_bits = 0;
  uint64_t temp_payload = 0;

  for (int csi_resourceidx = 0; csi_resourceidx < csi_MeasConfig->csi_ResourceConfigToAddModList->list.count; csi_resourceidx++) {

    struct NR_CSI_ResourceConfig *csi_resourceconfig = csi_MeasConfig->csi_ResourceConfigToAddModList->list.array[csi_resourceidx];
    if (csi_resourceconfig->csi_ResourceConfigId == csi_ResourceConfigId) {

      for (int csi_idx = 0; csi_idx < csi_MeasConfig->nzp_CSI_RS_ResourceSetToAddModList->list.count; csi_idx++) {
        if (csi_MeasConfig->nzp_CSI_RS_ResourceSetToAddModList->list.array[csi_idx]->nzp_CSI_ResourceSetId ==
            *(csi_resourceconfig->csi_RS_ResourceSetList.choice.nzp_CSI_RS_SSB->nzp_CSI_RS_ResourceSetList->list.array[0])) {

          nr_csi_report_t *csi_report = NULL;
          for (int i = 0; i < MAX_CSI_REPORTCONFIG; i++) {
            if (mac->csi_report_template[i].reportConfigId == csi_reportconfig->reportConfigId) {
              csi_report = &mac->csi_report_template[i];
              break;
            }
          }
          AssertFatal(csi_report, "Couldn't find CSI report with ID %ld\n", csi_reportconfig->reportConfigId);
          n_bits = nr_get_csi_bitlen(csi_report);
          int cri_ssbri_bitlen = csi_report->CSI_report_bitlen.cri_ssbri_bitlen;
          int rsrp_bitlen = csi_report->CSI_report_bitlen.rsrp_bitlen;
          int diff_rsrp_bitlen = csi_report->CSI_report_bitlen.diff_rsrp_bitlen;

          if (cri_ssbri_bitlen > 0) {
            LOG_E(NR_MAC, "Implementation for cri_ssbri_bitlen>0 is not supported yet!\n");;
          }

          // TODO: Improvements will be needed to cri_ssbri_bitlen>0
          // TS 38.133 - Table 10.1.6.1-1
          int rsrp_dBm = mac->csirs_measurements.rsrp_dBm;
          if (rsrp_dBm < -140) {
            temp_payload = 16;
          } else if (rsrp_dBm > -44) {
            temp_payload = 113;
          } else {
            temp_payload = mac->csirs_measurements.rsrp_dBm + 157;
          }

          temp_payload = reverse_bits(temp_payload, n_bits);

          LOG_D(NR_MAC, "cri_ssbri_bitlen = %d\n", cri_ssbri_bitlen);
          LOG_D(NR_MAC, "rsrp_bitlen = %d\n", rsrp_bitlen);
          LOG_D(NR_MAC, "diff_rsrp_bitlen = %d\n", diff_rsrp_bitlen);

          LOG_D(NR_MAC, "n_bits = %d\n", n_bits);
          LOG_D(NR_MAC, "csi_part1_payload = 0x%lx\n", temp_payload);

          break;
        }
      }
    }
  }
  AssertFatal(n_bits <= 32, "Not supporting CSI report with more than 32 bits\n");
  csi_payload_t csi = {.part1_payload = temp_payload, .p1_bits = n_bits, csi.p2_bits = 0};
  return csi;
}

// returns index from RSRP
// according to Table 10.1.6.1-1 in 38.133

uint8_t get_rsrp_index(int rsrp) {

  int index = rsrp + 157;
  if (rsrp>-44)
    index = 113;
  if (rsrp<-140)
    index = 16;

  return index;
}


// returns index from differential RSRP
// according to Table 10.1.6.1-2 in 38.133
uint8_t get_rsrp_diff_index(int best_rsrp,int current_rsrp) {

  int diff = best_rsrp-current_rsrp;
  if (diff>30)
    return 15;
  else
    return (diff>>1);

}

void nr_ue_send_sdu(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info, int pdu_id)
{
  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_UE_SEND_SDU, VCD_FUNCTION_IN);

  LOG_D(MAC, "In [%d.%d] Handling DLSCH PDU type %d\n",
        dl_info->frame, dl_info->slot, dl_info->rx_ind->rx_indication_body[pdu_id].pdu_type);

  // Processing MAC PDU
  // it parses MAC CEs subheaders, MAC CEs, SDU subheaderds and SDUs
  switch (dl_info->rx_ind->rx_indication_body[pdu_id].pdu_type){
    case FAPI_NR_RX_PDU_TYPE_DLSCH:
    nr_ue_process_mac_pdu(mac, dl_info, pdu_id);
    break;
    case FAPI_NR_RX_PDU_TYPE_RAR:
      nr_ue_process_rar(mac, dl_info, pdu_id);
    break;
    default:
    break;
  }

  VCD_SIGNAL_DUMPER_DUMP_FUNCTION_BY_NAME(VCD_SIGNAL_DUMPER_FUNCTIONS_UE_SEND_SDU, VCD_FUNCTION_OUT);

}

// #define EXTRACT_DCI_ITEM(val,size) val= readBits(dci_pdu, &pos, size);
#define EXTRACT_DCI_ITEM(val, size)    \
  val = readBits(dci_pdu, &pos, size); \
  LOG_D(NR_MAC_DCI, "     " #val ": %d\n", val);

// Fixme: Intel Endianess only procedure
static inline int readBits(const uint8_t *dci, int *start, int length)
{
  const int mask[] = {0, 1, 3, 7, 0xf, 0x1f, 0x3f, 0x7f, 0xff, 0x1ff, 0x3ff, 0x7ff, 0xfff, 0x1fff, 0x3fff, 0x7fff, 0xffff};
  uint64_t *tmp = (uint64_t *)dci;
  *start -= length;
  return *tmp >> *start & mask[length];
}

static void extract_10_ra_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 RA rnti\n");

  // Freq domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, (int)ceil(log2((N_RB * (N_RB + 1)) >> 1)));
  // Time domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // VRB to PRB mapping
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, 1);
  // MCS
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // TB scaling
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tb_scaling, 2);
}

static void extract_10_si_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 SI rnti\n");

  // Freq domain assignment 0-16 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, (int)ceil(log2((N_RB * (N_RB + 1)) >> 1)));
  // Time domain assignment 4 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // VRB to PRB mapping 1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, 1);
  // MCS 5bit  //bit over 32
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // Redundancy version  2 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // System information indicator 1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->system_info_indicator, 1);
}

static void extract_10_c_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 C rnti\n");

  // Freq domain assignment (275rb >> fsize = 16)
  int fsize = (int)ceil(log2((N_RB * (N_RB + 1)) >> 1));
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, fsize);
  bool pdcch_order = true;
  for (int i = 0; i < fsize; i++) {
    if (!((dci_pdu_rel15->frequency_domain_assignment.val >> i) & 1)) {
      pdcch_order = false;
      break;
    }
  }
  if (pdcch_order) { // Frequency domain resource assignment field are all 1  38.212 section 7.3.1.2.1
    // ra_preamble_index 6 bits
    EXTRACT_DCI_ITEM(dci_pdu_rel15->ra_preamble_index, 6);
    // UL/SUL indicator  1 bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->ul_sul_indicator.val, 1);
    // SS/PBCH index  6 bits
    EXTRACT_DCI_ITEM(dci_pdu_rel15->ss_pbch_index, 6);
    //  prach_mask_index  4 bits
    EXTRACT_DCI_ITEM(dci_pdu_rel15->prach_mask_index, 4);
  } // end if
  else {
    // Time domain assignment 4bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
    // VRB to PRB mapping  1bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, 1);
    // MCS 5bit  //bit over 32, so dci_pdu ++
    EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
    // New data indicator 1bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
    // Redundancy version  2bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
    // HARQ process number  4bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid, 4);
    // Downlink assignment index  2bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[0].val, 2);
    // TPC command for scheduled PUCCH  2bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
    // PUCCH resource indicator  3bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->pucch_resource_indicator, 3);
    // PDSCH-to-HARQ_feedback timing indicator 3bit
    EXTRACT_DCI_ITEM(dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val, 3);
  } // end else
}

static void extract_00_c_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos)
{
  LOG_D(NR_MAC_DCI, "Received dci 0_0 C rnti\n");

  // Frequency domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, dci_pdu_rel15->frequency_domain_assignment.nbits);
  // Time domain assignment 4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // Frequency hopping flag  E1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_hopping_flag.val, 1);
  // MCS  5 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // HARQ process number  4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid, 4);
  // TPC command for scheduled PUSCH  E2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
  // UL/SUL indicator  E1 bit
  /* commented for now (RK): need to get this from BWP descriptor
     if (cfg->pucch_config.pucch_GroupHopping.value)
       dci_pdu->= ((uint64_t)readBits(dci_pdu,>>(dci_size-pos)ul_sul_indicator&1)<<(dci_size-pos++);
  */
}

static void extract_10_tc_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 TC rnti\n");

  // Freq domain assignment 0-16 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, (int)ceil(log2((N_RB * (N_RB + 1)) >> 1)));
  // Time domain assignment - 4 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // VRB to PRB mapping - 1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, 1);
  // MCS 5bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator - 1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version - 2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // HARQ process number - 4 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid, 4);
  // Downlink assignment index - 2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[0].val, 2);
  // TPC command for scheduled PUCCH - 2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
  // PUCCH resource indicator - 3 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pucch_resource_indicator, 3);
  // PDSCH-to-HARQ_feedback timing indicator - 3 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val, 3);
}

static void extract_00_tc_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_0 TC rnti\n");

  // Frequency domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, dci_pdu_rel15->frequency_domain_assignment.nbits);
  // Time domain assignment 4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, 4);
  // Frequency hopping flag  E1 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_hopping_flag.val, 1);
  // MCS  5 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // HARQ process number  4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid, 4);
  // TPC command for scheduled PUSCH  E2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
}

static void extract_11_c_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos)
{
  LOG_D(NR_MAC_DCI, "Received dci 1_1 C rnti\n");

  // Carrier indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->carrier_indicator.val, dci_pdu_rel15->carrier_indicator.nbits);
  // BWP Indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->bwp_indicator.val, dci_pdu_rel15->bwp_indicator.nbits);
  // Frequency domain resource assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, dci_pdu_rel15->frequency_domain_assignment.nbits);
  // Time domain resource assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, dci_pdu_rel15->time_domain_assignment.nbits);
  // VRB-to-PRB mapping
  EXTRACT_DCI_ITEM(dci_pdu_rel15->vrb_to_prb_mapping.val, dci_pdu_rel15->vrb_to_prb_mapping.nbits);
  // PRB bundling size indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->prb_bundling_size_indicator.val, dci_pdu_rel15->prb_bundling_size_indicator.nbits);
  // Rate matching indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rate_matching_indicator.val, dci_pdu_rel15->rate_matching_indicator.nbits);
  // ZP CSI-RS trigger
  EXTRACT_DCI_ITEM(dci_pdu_rel15->zp_csi_rs_trigger.val, dci_pdu_rel15->zp_csi_rs_trigger.nbits);
  // TB1
  //  MCS 5bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  //TB2
  // MCS 5bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs2.val, dci_pdu_rel15->mcs2.nbits);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi2.val, dci_pdu_rel15->ndi2.nbits);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv2.val, dci_pdu_rel15->rv2.nbits);
  // HARQ process number  4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid, 4);
  // Downlink assignment index
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[0].val, dci_pdu_rel15->dai[0].nbits);
  // TPC command for scheduled PUCCH  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
  // PUCCH resource indicator  3bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pucch_resource_indicator, 3);
  // PDSCH-to-HARQ_feedback timing indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.val,
                   dci_pdu_rel15->pdsch_to_harq_feedback_timing_indicator.nbits);
  // Antenna ports
  EXTRACT_DCI_ITEM(dci_pdu_rel15->antenna_ports.val, dci_pdu_rel15->antenna_ports.nbits);
  // TCI
  EXTRACT_DCI_ITEM(dci_pdu_rel15->transmission_configuration_indication.val,
                   dci_pdu_rel15->transmission_configuration_indication.nbits);
  // SRS request
  EXTRACT_DCI_ITEM(dci_pdu_rel15->srs_request.val, dci_pdu_rel15->srs_request.nbits);
  // CBG transmission information
  EXTRACT_DCI_ITEM(dci_pdu_rel15->cbgti.val, dci_pdu_rel15->cbgti.nbits);
  // CBG flushing out information
  EXTRACT_DCI_ITEM(dci_pdu_rel15->cbgfi.val, dci_pdu_rel15->cbgfi.nbits);
  // DMRS sequence init
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dmrs_sequence_initialization.val, 1);
}

static void extract_01_c_rnti(dci_pdu_rel15_t *dci_pdu_rel15, const uint8_t *dci_pdu, int pos, const int N_RB)
{
  LOG_D(NR_MAC_DCI, "Received dci 0_1 C rnti\n");

  // Carrier indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->carrier_indicator.val, dci_pdu_rel15->carrier_indicator.nbits);
  // UL/SUL Indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ul_sul_indicator.val, dci_pdu_rel15->ul_sul_indicator.nbits);
  // BWP Indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->bwp_indicator.val, dci_pdu_rel15->bwp_indicator.nbits);
  // Freq domain assignment  max 16 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_domain_assignment.val, (int)ceil(log2((N_RB * (N_RB + 1)) >> 1)));
  // Time domain assignment
  EXTRACT_DCI_ITEM(dci_pdu_rel15->time_domain_assignment.val, dci_pdu_rel15->time_domain_assignment.nbits);
  // Not supported yet - skip for now
  // Frequency hopping flag – 1 bit
  // EXTRACT_DCI_ITEM(dci_pdu_rel15->frequency_hopping_flag.val, 1);
  // MCS  5 bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->mcs, 5);
  // New data indicator 1bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ndi, 1);
  // Redundancy version  2bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->rv, 2);
  // HARQ process number  4bit
  EXTRACT_DCI_ITEM(dci_pdu_rel15->harq_pid, 4);
  // 1st Downlink assignment index
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[0].val, dci_pdu_rel15->dai[0].nbits);
  // 2nd Downlink assignment index
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dai[1].val, dci_pdu_rel15->dai[1].nbits);
  // TPC command for scheduled PUSCH – 2 bits
  EXTRACT_DCI_ITEM(dci_pdu_rel15->tpc, 2);
  // SRS resource indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->srs_resource_indicator.val, dci_pdu_rel15->srs_resource_indicator.nbits);
  // Precoding info and n. of layers
  EXTRACT_DCI_ITEM(dci_pdu_rel15->precoding_information.val, dci_pdu_rel15->precoding_information.nbits);
  // Antenna ports
  EXTRACT_DCI_ITEM(dci_pdu_rel15->antenna_ports.val, dci_pdu_rel15->antenna_ports.nbits);
  // SRS request
  EXTRACT_DCI_ITEM(dci_pdu_rel15->srs_request.val, dci_pdu_rel15->srs_request.nbits);
  // CSI request
  EXTRACT_DCI_ITEM(dci_pdu_rel15->csi_request.val, dci_pdu_rel15->csi_request.nbits);
  // CBG transmission information
  EXTRACT_DCI_ITEM(dci_pdu_rel15->cbgti.val, dci_pdu_rel15->cbgti.nbits);
  // PTRS DMRS association
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ptrs_dmrs_association.val, dci_pdu_rel15->ptrs_dmrs_association.nbits);
  // Beta offset indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->beta_offset_indicator.val, dci_pdu_rel15->beta_offset_indicator.nbits);
  // DMRS sequence initialization
  EXTRACT_DCI_ITEM(dci_pdu_rel15->dmrs_sequence_initialization.val, dci_pdu_rel15->dmrs_sequence_initialization.nbits);
  // UL-SCH indicator
  EXTRACT_DCI_ITEM(dci_pdu_rel15->ulsch_indicator, 1);
  // UL/SUL indicator – 1 bit
  /* commented for now (RK): need to get this from BWP descriptor
     if (cfg->pucch_config.pucch_GroupHopping.value)
       dci_pdu->= ((uint64_t)*dci_pdu>>(dci_size-pos)ul_sul_indicator&1)<<(dci_size-pos++);
  */
}

static int get_nrb_for_dci(NR_UE_MAC_INST_t *mac, nr_dci_format_t dci_format, int ss_type)
{
  NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
  NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
  int N_RB;
  if(current_DL_BWP)
    N_RB = get_rb_bwp_dci(dci_format,
                          ss_type,
                          mac->type0_PDCCH_CSS_config.num_rbs,
                          current_UL_BWP->BWPSize,
                          current_DL_BWP->BWPSize,
                          mac->sc_info.initial_dl_BWPSize,
                          mac->sc_info.initial_dl_BWPSize);
  else
    N_RB = mac->type0_PDCCH_CSS_config.num_rbs;

  if (N_RB == 0)
    LOG_E(NR_MAC_DCI, "DCI configuration error! N_RB = 0\n");

  return N_RB;
}

static nr_dci_format_t nr_extract_dci_00_10(NR_UE_MAC_INST_t *mac,
                                            int pos,
                                            const int rnti_type,
                                            const uint8_t *dci_pdu,
                                            const int slot,
                                            const int ss_type)
{
  nr_dci_format_t format = NR_DCI_NONE;
  dci_pdu_rel15_t *dci_pdu_rel15 = NULL;
  int format_indicator = -1;
  int n_RB = 0;

  switch (rnti_type) {
    case TYPE_RA_RNTI_ :
      format = NR_DL_DCI_FORMAT_1_0;
      dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
      n_RB = get_nrb_for_dci(mac, format, ss_type);
      if (n_RB == 0)
        return NR_DCI_NONE;
      extract_10_ra_rnti(dci_pdu_rel15, dci_pdu, pos, n_RB);
      break;
    case TYPE_P_RNTI_ :
      AssertFatal(false, "DCI for P-RNTI not handled yet\n");
      break;
    case TYPE_SI_RNTI_ :
      format = NR_DL_DCI_FORMAT_1_0;
      dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
      n_RB = get_nrb_for_dci(mac, format, ss_type);
      if (n_RB == 0)
        return NR_DCI_NONE;
      extract_10_si_rnti(dci_pdu_rel15, dci_pdu, pos, n_RB);
      break;
    case TYPE_C_RNTI_ :
      // Identifier for DCI formats
      EXTRACT_DCI_ITEM(format_indicator, 1);
      if (format_indicator == 1) {
        format = NR_DL_DCI_FORMAT_1_0;
        dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
        int n_RB = get_nrb_for_dci(mac, format, ss_type);
        if (n_RB == 0)
          return NR_DCI_NONE;
        extract_10_c_rnti(dci_pdu_rel15, dci_pdu, pos, n_RB);
      }
      else {
        format = NR_UL_DCI_FORMAT_0_0;
        dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
        extract_00_c_rnti(dci_pdu_rel15, dci_pdu, pos);
      }
      dci_pdu_rel15->format_indicator = format_indicator;
      break;
    case TYPE_TC_RNTI_ :
      // Identifier for DCI formats
      EXTRACT_DCI_ITEM(format_indicator, 1);
      if (format_indicator == 1) {
        format = NR_DL_DCI_FORMAT_1_0;
        dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
        n_RB = get_nrb_for_dci(mac, format, ss_type);
        if (n_RB == 0)
          return NR_DCI_NONE;
        extract_10_tc_rnti(dci_pdu_rel15, dci_pdu, pos, n_RB);
      }
      else {
        format = NR_UL_DCI_FORMAT_0_0;
        dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
        extract_00_tc_rnti(dci_pdu_rel15, dci_pdu, pos);
      }
      dci_pdu_rel15->format_indicator = format_indicator;
      break;
    default :
      AssertFatal(false, "Invalid RNTI type\n");
  }
  return format;
}

static nr_dci_format_t nr_extract_dci_info(NR_UE_MAC_INST_t *mac,
                                           const nfapi_nr_dci_formats_e dci_format,
                                           const uint8_t dci_size,
                                           const uint16_t rnti,
                                           const int ss_type,
                                           const uint8_t *dci_pdu,
                                           const int slot)
{
  LOG_D(NR_MAC_DCI,"nr_extract_dci_info : dci_pdu %lx, size %d, format %d\n", *(uint64_t *)dci_pdu, dci_size, dci_format);
  int rnti_type = get_rnti_type(mac, rnti);
  int pos = dci_size;

  nr_dci_format_t format = NR_DCI_NONE;
  switch(dci_format) {
    case  NFAPI_NR_FORMAT_0_0_AND_1_0 :
      format = nr_extract_dci_00_10(mac, pos, rnti_type, dci_pdu, slot, ss_type);
      break;
    case  NFAPI_NR_FORMAT_0_1_AND_1_1 :
      if (rnti_type == TYPE_C_RNTI_) {
        // Identifier for DCI formats
        int format_indicator = 0;
        EXTRACT_DCI_ITEM(format_indicator, 1);
        if (format_indicator == 1) {
          format = NR_DL_DCI_FORMAT_1_1;
          dci_pdu_rel15_t *def_dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
          def_dci_pdu_rel15->format_indicator = format_indicator;
          extract_11_c_rnti(def_dci_pdu_rel15, dci_pdu, pos);
        }
        else {
          format = NR_UL_DCI_FORMAT_0_1;
          dci_pdu_rel15_t *def_dci_pdu_rel15 = &mac->def_dci_pdu_rel15[slot][format];
          def_dci_pdu_rel15->format_indicator = format_indicator;
          int n_RB = get_nrb_for_dci(mac, format, ss_type);
          if (n_RB == 0)
            return NR_DCI_NONE;
          extract_01_c_rnti(def_dci_pdu_rel15, dci_pdu, pos, n_RB);
        }
      }
      else {
        LOG_E(NR_MAC_DCI, "RNTI type not supported for formats 01 or 11\n");
        return NR_DCI_NONE;
      }
      break;
    default :
      LOG_E(NR_MAC_DCI, "DCI format not supported\n");
  }
  return format;
}

///////////////////////////////////
// brief:     nr_ue_process_mac_pdu
// function:  parsing DL PDU header
///////////////////////////////////
//  Header for DLSCH:
//  Except:
//   - DL-SCH: fixed-size MAC CE(known by LCID)
//   - DL-SCH: padding
//
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|F|   LCID    |
//  |       L       |
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|F|   LCID    |
//  |       L       |
//  |       L       |
////////////////////////////////
//  Header for DLSCH:
//   - DLSCH: fixed-size MAC CE(known by LCID)
//   - DLSCH: padding, for single/multiple 1-oct padding CE(s)
//
//  |0|1|2|3|4|5|6|7|  bit-wise
//  |R|R|   LCID    |
//  LCID: The Logical Channel ID field identifies the logical channel instance of the corresponding MAC SDU or the type of the corresponding MAC CE or padding as described
//         in Tables 6.2.1-1 and 6.2.1-2 for the DL-SCH and UL-SCH respectively. There is one LCID field per MAC subheader. The LCID field size is 6 bits;
//  L:    The Length field indicates the length of the corresponding MAC SDU or variable-sized MAC CE in bytes. There is one L field per MAC subheader except for subheaders
//         corresponding to fixed-sized MAC CEs and padding. The size of the L field is indicated by the F field;
//  F:    lenght of L is 0:8 or 1:16 bits wide
//  R:    Reserved bit, set to zero.
////////////////////////////////
void nr_ue_process_mac_pdu(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info, int pdu_id)
{
  frame_t frameP = dl_info->frame;
  int slot = dl_info->slot;
  uint8_t *pduP = (dl_info->rx_ind->rx_indication_body + pdu_id)->pdsch_pdu.pdu;
  int32_t pdu_len = (int32_t)(dl_info->rx_ind->rx_indication_body + pdu_id)->pdsch_pdu.pdu_length;
  uint8_t gNB_index = dl_info->gNB_index;
  uint8_t CC_id = dl_info->cc_id;
  uint8_t done = 0;
  RA_config_t *ra = &mac->ra;

  if (!pduP){
    return;
  }

  LOG_D(MAC, "[%d.%d]: processing PDU %d (with length %d) of %d total number of PDUs...\n",
        frameP,
        slot,
        pdu_id,
        pdu_len,
        dl_info->rx_ind->number_pdus);

  while (!done && pdu_len > 0){
    uint16_t mac_len = 0x0000;
    uint16_t mac_subheader_len = 0x0001; //  default to fixed-length subheader = 1-oct
    uint8_t rx_lcid = ((NR_MAC_SUBHEADER_FIXED *)pduP)->LCID;

    LOG_D(MAC, "[UE] LCID %d, PDU length %d\n", rx_lcid, pdu_len);
    bool ret;
    switch(rx_lcid){
      //  MAC CE
      case DL_SCH_LCID_CCCH:
        //  MSG4 RRC Setup 38.331
        //  variable length
        ret=get_mac_len(pduP, pdu_len, &mac_len, &mac_subheader_len);
        AssertFatal(ret, "The mac_len (%d) has an invalid size. PDU len = %d! \n", mac_len, pdu_len);

        // Check if it is a valid CCCH message, we get all 00's messages very often
        int i = 0;
        for(i=0; i<(mac_subheader_len+mac_len); i++) {
          if(pduP[i] != 0) {
            break;
          }
        }
        if (i == (mac_subheader_len+mac_len)) {
          LOG_E(NR_MAC, "Invalid CCCH message!, pdu_len: %d\n", pdu_len);
          done = 1;
          break;
        }

        if (mac_len > 0) {
          LOG_DDUMP(NR_MAC, (void *)pduP, mac_subheader_len + mac_len, LOG_DUMP_CHAR, "DL_SCH_LCID_CCCH (e.g. RRCSetup) payload: ");
          mac_rlc_data_ind(mac->ue_id,
                           mac->ue_id,
                           gNB_index,
                           frameP,
                           ENB_FLAG_NO,
                           MBMS_FLAG_NO,
                           0,
                           (char *)(pduP + mac_subheader_len),
                           mac_len,
                           1,
                           NULL);
        }
        break;
      case DL_SCH_LCID_TCI_STATE_ACT_UE_SPEC_PDSCH:
      case DL_SCH_LCID_APERIODIC_CSI_TRI_STATE_SUBSEL:
      case DL_SCH_LCID_SP_CSI_RS_CSI_IM_RES_SET_ACT:
      case DL_SCH_LCID_SP_SRS_ACTIVATION:

        //  38.321 Ch6.1.3.14
        //  varialbe length
        get_mac_len(pduP, pdu_len, &mac_len, &mac_subheader_len);
        break;

      case DL_SCH_LCID_RECOMMENDED_BITRATE:
        //  38.321 Ch6.1.3.20
        mac_len = 2;
        break;
      case DL_SCH_LCID_SP_ZP_CSI_RS_RES_SET_ACT:
        //  38.321 Ch6.1.3.19
        mac_len = 2;
        break;
      case DL_SCH_LCID_PUCCH_SPATIAL_RELATION_ACT:
        //  38.321 Ch6.1.3.18
        mac_len = 3;
        break;
      case DL_SCH_LCID_SP_CSI_REP_PUCCH_ACT:
        //  38.321 Ch6.1.3.16
        mac_len = 2;
        break;
      case DL_SCH_LCID_TCI_STATE_IND_UE_SPEC_PDCCH:
        //  38.321 Ch6.1.3.15
        mac_len = 2;
        break;
      case DL_SCH_LCID_DUPLICATION_ACT:
        //  38.321 Ch6.1.3.11
        mac_len = 1;
        break;
      case DL_SCH_LCID_SCell_ACT_4_OCT:
        //  38.321 Ch6.1.3.10
        mac_len = 4;
        break;
      case DL_SCH_LCID_SCell_ACT_1_OCT:
        //  38.321 Ch6.1.3.10
        mac_len = 1;
        break;
      case DL_SCH_LCID_L_DRX:
        //  38.321 Ch6.1.3.6
        //  fixed length but not yet specify.
        mac_len = 0;
        break;
      case DL_SCH_LCID_DRX:
        //  38.321 Ch6.1.3.5
        //  fixed length but not yet specify.
        mac_len = 0;
        break;
      case DL_SCH_LCID_TA_COMMAND:
        //  38.321 Ch6.1.3.4
        mac_len = 1;

        /*uint8_t ta_command = ((NR_MAC_CE_TA *)pduP)[1].TA_COMMAND;
          uint8_t tag_id = ((NR_MAC_CE_TA *)pduP)[1].TAGID;*/

        const int ta = ((NR_MAC_CE_TA *)pduP)[1].TA_COMMAND;
        const int tag = ((NR_MAC_CE_TA *)pduP)[1].TAGID;

        NR_UL_TIME_ALIGNMENT_t *ul_time_alignment = &mac->ul_time_alignment;
        ul_time_alignment->tag_id = tag;
        ul_time_alignment->ta_command = ta;
        ul_time_alignment->ta_apply = adjustment_ta;

        const int ntn_ue_koffset = get_NTN_UE_Koffset(mac->sc_info.ntn_Config_r17, mac->current_UL_BWP->scs);
        const int n_slots_frame = nr_slots_per_frame[mac->current_UL_BWP->scs];
        ul_time_alignment->frame = (frameP + (slot + ntn_ue_koffset) / n_slots_frame) % MAX_FRAME_NUMBER;
        ul_time_alignment->slot = (slot + ntn_ue_koffset) % n_slots_frame;

        /*
        #ifdef DEBUG_HEADER_PARSING
        LOG_D(MAC, "[UE] CE %d : UE Timing Advance : %d\n", i, pduP[1]);
        #endif
        */

        if (ta == 31)
          LOG_D(NR_MAC, "[%d.%d] Received TA_COMMAND %u TAGID %u CC_id %d \n", frameP, slot, ta, tag, CC_id);
        else
          LOG_I(NR_MAC, "[%d.%d] Received TA_COMMAND %u TAGID %u CC_id %d \n", frameP, slot, ta, tag, CC_id);

        break;
      case DL_SCH_LCID_CON_RES_ID:
        //  Clause 5.1.5 and 6.1.3.3 of 3GPP TS 38.321 version 16.2.1 Release 16
        // MAC Header: 1 byte (R/R/LCID)
        // MAC SDU: 6 bytes (UE Contention Resolution Identity)
        mac_len = 6;

        if (ra->ra_state == nrRA_WAIT_CONTENTION_RESOLUTION) {
          LOG_I(MAC, "[UE %d]Frame %d Contention resolution identity: 0x%02x%02x%02x%02x%02x%02x Terminating RA procedure\n",
                mac->ue_id,
                frameP,
                pduP[1],
                pduP[2],
                pduP[3],
                pduP[4],
                pduP[5],
                pduP[6]);

          bool ra_success = true;
	  if (!IS_SOFTMODEM_IQPLAYER) { // Control is bypassed when replaying IQs (BMC)
	    for(int i = 0; i < mac_len; i++) {
	      if(ra->cont_res_id[i] != pduP[i + 1]) {
		ra_success = false;
		break;
	      }
	    }
	  }

          if (ra->RA_active && ra_success) {
            nr_ra_succeeded(mac, gNB_index, frameP, slot);
          } else if (!ra_success) {
            // TODO: Handle failure of RA procedure @ MAC layer
            //  nr_ra_failed(module_idP, CC_id, prach_resources, frameP, slot); // prach_resources is a PHY structure
            ra->ra_state = nrRA_UE_IDLE;
            ra->RA_active = false;
          }
        }
        break;
      case DL_SCH_LCID_PADDING:
        done = 1;
        //  end of MAC PDU, can ignore the rest.
        break;
        //  MAC SDU
      // From values 1 to 32 it equals to the identity of the logical channel
      case 1 ... 32:
        if (!get_mac_len(pduP, pdu_len, &mac_len, &mac_subheader_len))
          return;
        LOG_D(NR_MAC, "%4d.%2d : DLSCH -> LCID %d %d bytes\n", frameP, slot, rx_lcid, mac_len);

        mac_rlc_data_ind(mac->ue_id,
                         mac->ue_id,
                         gNB_index,
                         frameP,
                         ENB_FLAG_NO,
                         MBMS_FLAG_NO,
                         rx_lcid,
                         (char *)(pduP + mac_subheader_len),
                         mac_len,
                         1,
                         NULL);
        break;
      default:
        LOG_W(MAC, "unknown lcid %02x\n", rx_lcid);
        break;
    }
    pduP += ( mac_subheader_len + mac_len );
    pdu_len -= ( mac_subheader_len + mac_len );
    if (pdu_len < 0)
      LOG_E(MAC, "[UE %d][%d.%d] nr_ue_process_mac_pdu, residual mac pdu length %d < 0!\n", mac->ue_id, frameP, slot, pdu_len);
  }
}

/**
 * Function:      generating MAC CEs (MAC CE and subheader) for the ULSCH PDU
 * Notes:         TODO: PHR and BSR reporting
 * Parameters:
 * @mac_ce        pointer to the MAC sub-PDUs including the MAC CEs
 * @mac           pointer to the MAC instance
 * Return:        number of written bytes
 */
int nr_write_ce_ulsch_pdu(uint8_t *mac_ce,
                          NR_UE_MAC_INST_t *mac,
                          uint8_t power_headroom,  // todo: NR_POWER_HEADROOM_CMD *power_headroom,
                          uint16_t *crnti,
                          NR_BSR_SHORT *truncated_bsr,
                          NR_BSR_SHORT *short_bsr,
                          NR_BSR_LONG  *long_bsr)
{
  int      mac_ce_len = 0;
  uint8_t mac_ce_size = 0;
  uint8_t *pdu = mac_ce;
  if (power_headroom) {

    // MAC CE fixed subheader
    ((NR_MAC_SUBHEADER_FIXED *) mac_ce)->R = 0;
    ((NR_MAC_SUBHEADER_FIXED *) mac_ce)->LCID = UL_SCH_LCID_SINGLE_ENTRY_PHR;
    mac_ce++;

    // PHR MAC CE (1 octet)
    ((NR_SINGLE_ENTRY_PHR_MAC_CE *) mac_ce)->PH = power_headroom;
    ((NR_SINGLE_ENTRY_PHR_MAC_CE *) mac_ce)->R1 = 0;
    ((NR_SINGLE_ENTRY_PHR_MAC_CE *) mac_ce)->PCMAX = 0; // todo
    ((NR_SINGLE_ENTRY_PHR_MAC_CE *) mac_ce)->R2 = 0;

    // update pointer and length
    mac_ce_size = sizeof(NR_SINGLE_ENTRY_PHR_MAC_CE);
    mac_ce += mac_ce_size;
    mac_ce_len += mac_ce_size + sizeof(NR_MAC_SUBHEADER_FIXED);
    LOG_D(NR_MAC, "[UE] Generating ULSCH PDU : power_headroom pdu %p mac_ce %p b\n",
          pdu, mac_ce);
  }

  if (crnti && (!get_softmodem_params()->sa && get_softmodem_params()->do_ra && mac->ra.ra_state != nrRA_SUCCEEDED)) {
    LOG_D(NR_MAC, "In %s: generating C-RNTI MAC CE with C-RNTI %x\n", __FUNCTION__, (*crnti));

    // MAC CE fixed subheader
    ((NR_MAC_SUBHEADER_FIXED *) mac_ce)->R = 0;
    ((NR_MAC_SUBHEADER_FIXED *) mac_ce)->LCID = UL_SCH_LCID_C_RNTI;
    mac_ce++;

    // C-RNTI MAC CE (2 octets)
    memcpy(mac_ce, crnti, sizeof(*crnti));

    // update pointer and length
    mac_ce_size = sizeof(uint16_t);
    mac_ce += mac_ce_size;
    mac_ce_len += mac_ce_size + sizeof(NR_MAC_SUBHEADER_FIXED);
  }

  if (truncated_bsr) {

	// MAC CE fixed subheader
    ((NR_MAC_SUBHEADER_FIXED *) mac_ce)->R = 0;
    ((NR_MAC_SUBHEADER_FIXED *) mac_ce)->LCID = UL_SCH_LCID_S_TRUNCATED_BSR;
    mac_ce++;

    // Short truncated BSR MAC CE (1 octet)
    ((NR_BSR_SHORT_TRUNCATED *) mac_ce)-> Buffer_size = truncated_bsr->Buffer_size;
    ((NR_BSR_SHORT_TRUNCATED *) mac_ce)-> LcgID = truncated_bsr->LcgID;;

    // update pointer and length
    mac_ce_size = sizeof(NR_BSR_SHORT_TRUNCATED);
    mac_ce += mac_ce_size;
    mac_ce_len += mac_ce_size + sizeof(NR_MAC_SUBHEADER_FIXED);
    LOG_D(NR_MAC, "[UE] Generating ULSCH PDU : truncated_bsr Buffer_size %d LcgID %d pdu %p mac_ce %p\n",
          truncated_bsr->Buffer_size, truncated_bsr->LcgID, pdu, mac_ce);

  } else if (short_bsr) {

	// MAC CE fixed subheader
    ((NR_MAC_SUBHEADER_FIXED *) mac_ce)->R = 0;
    ((NR_MAC_SUBHEADER_FIXED *) mac_ce)->LCID = UL_SCH_LCID_S_BSR;
    mac_ce++;

    // Short truncated BSR MAC CE (1 octet)
    ((NR_BSR_SHORT *) mac_ce)->Buffer_size = short_bsr->Buffer_size;
    ((NR_BSR_SHORT *) mac_ce)->LcgID = short_bsr->LcgID;

    // update pointer and length
    mac_ce_size = sizeof(NR_BSR_SHORT);
    mac_ce += mac_ce_size;
    mac_ce_len += mac_ce_size + sizeof(NR_MAC_SUBHEADER_FIXED);
    LOG_D(NR_MAC, "[UE] Generating ULSCH PDU : short_bsr Buffer_size %d LcgID %d pdu %p mac_ce %p\n",
          short_bsr->Buffer_size, short_bsr->LcgID, pdu, mac_ce);
  } else if (long_bsr) {

	// MAC CE variable subheader
    // ch 6.1.3.1. TS 38.321
    ((NR_MAC_SUBHEADER_SHORT *) mac_ce)->R = 0;
    ((NR_MAC_SUBHEADER_SHORT *) mac_ce)->F = 0;
    ((NR_MAC_SUBHEADER_SHORT *) mac_ce)->LCID = UL_SCH_LCID_L_BSR;

    NR_MAC_SUBHEADER_SHORT *mac_pdu_subheader_ptr = (NR_MAC_SUBHEADER_SHORT *) mac_ce;
    mac_ce += 2;

    // Could move to nr_get_sdu()
    uint8_t *Buffer_size_ptr= (uint8_t*) mac_ce + 1;
    //int NR_BSR_LONG_SIZE = 1;
    if (long_bsr->Buffer_size0 == 0) {
      ((NR_BSR_LONG *) mac_ce)->LcgID0 = 0;
    } else {
      ((NR_BSR_LONG *) mac_ce)->LcgID0 = 1;
      *Buffer_size_ptr++ = long_bsr->Buffer_size0;
    }
    if (long_bsr->Buffer_size1 == 0) {
      ((NR_BSR_LONG *) mac_ce)->LcgID1 = 0;
    } else {
      ((NR_BSR_LONG *) mac_ce)->LcgID1 = 1;
      *Buffer_size_ptr++ = long_bsr->Buffer_size1;
    }
    if (long_bsr->Buffer_size2 == 0) {
      ((NR_BSR_LONG *) mac_ce)->LcgID2 = 0;
    } else {
      ((NR_BSR_LONG *) mac_ce)->LcgID2 = 1;
      *Buffer_size_ptr++ = long_bsr->Buffer_size2;
    }
    if (long_bsr->Buffer_size3 == 0) {
      ((NR_BSR_LONG *) mac_ce)->LcgID3 = 0;
    } else {
      ((NR_BSR_LONG *) mac_ce)->LcgID3 = 1;
      *Buffer_size_ptr++ = long_bsr->Buffer_size3;
    }
    if (long_bsr->Buffer_size4 == 0) {
      ((NR_BSR_LONG *) mac_ce)->LcgID4 = 0;
    } else {
      ((NR_BSR_LONG *) mac_ce)->LcgID4 = 1;
      *Buffer_size_ptr++ = long_bsr->Buffer_size4;
    }
    if (long_bsr->Buffer_size5 == 0) {
      ((NR_BSR_LONG *) mac_ce)->LcgID5 = 0;
    } else {
      ((NR_BSR_LONG *) mac_ce)->LcgID5 = 1;
      *Buffer_size_ptr++ = long_bsr->Buffer_size5;
    }
    if (long_bsr->Buffer_size6 == 0) {
      ((NR_BSR_LONG *) mac_ce)->LcgID6 = 0;
    } else {
      ((NR_BSR_LONG *) mac_ce)->LcgID6 = 1;
      *Buffer_size_ptr++ = long_bsr->Buffer_size6;
    }
    if (long_bsr->Buffer_size7 == 0) {
      ((NR_BSR_LONG *) mac_ce)->LcgID7 = 0;
    } else {
      ((NR_BSR_LONG *) mac_ce)->LcgID7 = 1;
      *Buffer_size_ptr++ = long_bsr->Buffer_size7;
    }
    ((NR_MAC_SUBHEADER_SHORT *) mac_pdu_subheader_ptr)->L = mac_ce_size = (uint8_t*) Buffer_size_ptr - (uint8_t*) mac_ce;
    LOG_D(NR_MAC, "[UE] Generating ULSCH PDU : long_bsr size %d Lcgbit 0x%02x Buffer_size %d %d %d %d %d %d %d %d\n", mac_ce_size, *((uint8_t*) mac_ce),
          ((NR_BSR_LONG *) mac_ce)->Buffer_size0, ((NR_BSR_LONG *) mac_ce)->Buffer_size1, ((NR_BSR_LONG *) mac_ce)->Buffer_size2, ((NR_BSR_LONG *) mac_ce)->Buffer_size3,
          ((NR_BSR_LONG *) mac_ce)->Buffer_size4, ((NR_BSR_LONG *) mac_ce)->Buffer_size5, ((NR_BSR_LONG *) mac_ce)->Buffer_size6, ((NR_BSR_LONG *) mac_ce)->Buffer_size7);
    mac_ce_len += mac_ce_size + sizeof(NR_MAC_SUBHEADER_SHORT);
  }

  return mac_ce_len;
}


/////////////////////////////////////
//    Random Access Response PDU   //
//         TS 38.213 ch 8.2        //
//        TS 38.321 ch 6.2.3       //
/////////////////////////////////////
//| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |// bit-wise
//| E | T |       R A P I D       |//
//| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |//
//| R |           T A             |//
//|       T A         |  UL grant |//
//|            UL grant           |//
//|            UL grant           |//
//|            UL grant           |//
//|         T C - R N T I         |//
//|         T C - R N T I         |//
/////////////////////////////////////
//       UL grant  (27 bits)       //
/////////////////////////////////////
//| 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 |// bit-wise
//|-------------------|FHF|F_alloc|//
//|        Freq allocation        |//
//|    F_alloc    |Time allocation|//
//|      MCS      |     TPC   |CSI|//
/////////////////////////////////////
// TbD WIP Msg3 development ongoing
// - apply UL grant freq alloc & time alloc as per 8.2 TS 38.213
// - apply tpc command
// WIP fix:
// - time domain indication hardcoded to 0 for k2 offset
// - extend TS 38.213 ch 8.3 Msg3 PUSCH
// - b buffer
// - ulsch power offset
// - optimize: mu_pusch, j and table_6_1_2_1_1_2_time_dom_res_alloc_A are already defined in nr_ue_procedures
static void nr_ue_process_rar(NR_UE_MAC_INST_t *mac, nr_downlink_indication_t *dl_info, int pdu_id)
{
  frame_t frame = dl_info->frame;
  int slot = dl_info->slot;

  if(dl_info->rx_ind->rx_indication_body[pdu_id].pdsch_pdu.ack_nack == 0) {
    LOG_W(NR_MAC,"[UE %d][RAPROC][%d.%d] CRC check failed on RAR (NAK)\n", mac->ue_id, frame, slot);
    return;
  }

  RA_config_t *ra = &mac->ra;
  uint8_t n_subPDUs  = 0;  // number of RAR payloads
  uint8_t n_subheaders = 0;  // number of MAC RAR subheaders
  uint8_t *dlsch_buffer = dl_info->rx_ind->rx_indication_body[pdu_id].pdsch_pdu.pdu;
  uint8_t is_Msg3 = 1;
  frame_t frame_tx = 0;
  int slot_tx = 0;
  int ret = 0;
  NR_RA_HEADER_RAPID *rarh = (NR_RA_HEADER_RAPID *) dlsch_buffer; // RAR subheader pointer
  NR_MAC_RAR *rar = (NR_MAC_RAR *) (dlsch_buffer + 1);   // RAR subPDU pointer
  uint8_t preamble_index = ra->ra_PreambleIndex;
  uint16_t rnti = mac->ra.ra_rnti;

  LOG_D(NR_MAC, "[%d.%d]: [UE %d][RAPROC] invoking MAC for received RAR (current preamble %d)\n", frame, slot, mac->ue_id, preamble_index);

  while (1) {
    n_subheaders++;
    if (rarh->T == 1) {
      n_subPDUs++;
      LOG_I(NR_MAC, "[UE %d][RAPROC][RA-RNTI %04x] Got RAPID RAR subPDU\n", mac->ue_id, rnti);
    } else {
      ra->RA_backoff_indicator = table_7_2_1[((NR_RA_HEADER_BI *)rarh)->BI];
      ra->RA_BI_found = 1;
      LOG_I(NR_MAC, "[UE %d][RAPROC][RA-RNTI %04x] Got BI RAR subPDU %d ms\n", mac->ue_id, ra->RA_backoff_indicator, rnti);
      if ( ((NR_RA_HEADER_BI *)rarh)->E == 1) {
        rarh += sizeof(NR_RA_HEADER_BI);
        continue;
      } else {
        break;
      }
    }
    if (rarh->RAPID == preamble_index) {
      LOG_A(NR_MAC, "[UE %d][RAPROC][%d.%d] Found RAR with the intended RAPID %d\n", mac->ue_id, frame, slot, rarh->RAPID);
      rar = (NR_MAC_RAR *) (dlsch_buffer + n_subheaders + (n_subPDUs - 1) * sizeof(NR_MAC_RAR));
      ra->RA_RAPID_found = 1;
      if (get_softmodem_params()->emulate_l1) {
        /* When we are emulating L1 with multiple UEs, the rx_indication will have
           multiple RAR PDUs. The code would previously handle each of these PDUs,
           but it should only be handling the single RAR that matches the current
           UE. */
        LOG_I(NR_MAC, "RAR PDU found for our UE with PDU index %d\n", pdu_id);
        dl_info->rx_ind->number_pdus = 1;
        if (pdu_id != 0) {
          memcpy(&dl_info->rx_ind->rx_indication_body[0],
                &dl_info->rx_ind->rx_indication_body[pdu_id],
                sizeof(fapi_nr_rx_indication_body_t));
        }
        mac->nr_ue_emul_l1.expected_rar = false;
        memset(mac->nr_ue_emul_l1.index_has_rar, 0, sizeof(mac->nr_ue_emul_l1.index_has_rar));
      }
      break;
    }
    if (rarh->E == 0) {
      LOG_W(NR_MAC,"[UE %d][RAPROC][%d.%d] Received RAR preamble (%d) doesn't match the intended RAPID (%d)\n",
            mac->ue_id,
            frame,
            slot,
            rarh->RAPID,
            preamble_index);
      break;
    } else {
      rarh += sizeof(NR_MAC_RAR) + 1;
    }
  }

  #ifdef DEBUG_RAR
  LOG_D(MAC, "[DEBUG_RAR] (%d,%d) number of RAR subheader %d; number of RAR pyloads %d\n",
        frame,
        slot,
        n_subheaders,
        n_subPDUs);
  LOG_D(MAC, "[DEBUG_RAR] Received RAR (%02x|%02x.%02x.%02x.%02x.%02x.%02x) for preamble %d/%d\n",
        *(uint8_t *) rarh,
        rar[0],
        rar[1],
        rar[2],
        rar[3],
        rar[4],
        rar[5],
        rarh->RAPID,
        preamble_index);
  #endif

  if (ra->RA_RAPID_found) {
    RAR_grant_t rar_grant;

    unsigned char tpc_command;
#ifdef DEBUG_RAR
    unsigned char csi_req;
#endif

    // TA command
    NR_UL_TIME_ALIGNMENT_t *ul_time_alignment = &mac->ul_time_alignment;
    const int ta = rar->TA2 + (rar->TA1 << 5);
    ul_time_alignment->ta_command = ta;
    ul_time_alignment->ta_apply = rar_ta;

    LOG_W(MAC, "received TA command %d\n", 31 + ta);
#ifdef DEBUG_RAR
    // CSI
    csi_req = (unsigned char) (rar->UL_GRANT_4 & 0x01);
#endif

    // TPC
    tpc_command = (unsigned char) ((rar->UL_GRANT_4 >> 1) & 0x07);
    switch (tpc_command){
      case 0:
        ra->Msg3_TPC = -6;
        break;
      case 1:
        ra->Msg3_TPC = -4;
        break;
      case 2:
        ra->Msg3_TPC = -2;
        break;
      case 3:
        ra->Msg3_TPC = 0;
        break;
      case 4:
        ra->Msg3_TPC = 2;
        break;
      case 5:
        ra->Msg3_TPC = 4;
        break;
      case 6:
        ra->Msg3_TPC = 6;
        break;
      case 7:
        ra->Msg3_TPC = 8;
        break;
      default:
        LOG_E(NR_PHY, "RAR impossible msg3 TPC\n");
    }
    // MCS
    rar_grant.mcs = (unsigned char) (rar->UL_GRANT_4 >> 4);
    // time alloc
    rar_grant.Msg3_t_alloc = (unsigned char) (rar->UL_GRANT_3 & 0x0f);
    // frequency alloc
    rar_grant.Msg3_f_alloc = (uint16_t) ((rar->UL_GRANT_3 >> 4) | (rar->UL_GRANT_2 << 4) | ((rar->UL_GRANT_1 & 0x03) << 12));
    // frequency hopping
    rar_grant.freq_hopping = (unsigned char) (rar->UL_GRANT_1 >> 2);

#ifdef DEBUG_RAR
    LOG_I(NR_MAC, "rarh->E = 0x%x\n", rarh->E);
    LOG_I(NR_MAC, "rarh->T = 0x%x\n", rarh->T);
    LOG_I(NR_MAC, "rarh->RAPID = 0x%x (%i)\n", rarh->RAPID, rarh->RAPID);

    LOG_I(NR_MAC, "rar->R = 0x%x\n", rar->R);
    LOG_I(NR_MAC, "rar->TA1 = 0x%x\n", rar->TA1);

    LOG_I(NR_MAC, "rar->TA2 = 0x%x\n", rar->TA2);
    LOG_I(NR_MAC, "rar->UL_GRANT_1 = 0x%x\n", rar->UL_GRANT_1);

    LOG_I(NR_MAC, "rar->UL_GRANT_2 = 0x%x\n", rar->UL_GRANT_2);
    LOG_I(NR_MAC, "rar->UL_GRANT_3 = 0x%x\n", rar->UL_GRANT_3);
    LOG_I(NR_MAC, "rar->UL_GRANT_4 = 0x%x\n", rar->UL_GRANT_4);

    LOG_I(NR_MAC, "rar->TCRNTI_1 = 0x%x\n", rar->TCRNTI_1);
    LOG_I(NR_MAC, "rar->TCRNTI_2 = 0x%x\n", rar->TCRNTI_2);

    LOG_I(NR_MAC, "[%d.%d]: [UE %d] Received RAR with t_alloc %d f_alloc %d ta_command %d mcs %d freq_hopping %d tpc_command %d\n",
          frame,
          slot,
          mac->ue_id,
          rar_grant.Msg3_t_alloc,
          rar_grant.Msg3_f_alloc,
          ta_command,
          rar_grant.mcs,
          rar_grant.freq_hopping,
          tpc_command);
#endif

    // Schedule Msg3
    const NR_UE_UL_BWP_t *current_UL_BWP = mac->current_UL_BWP;
    const NR_UE_DL_BWP_t *current_DL_BWP = mac->current_DL_BWP;
    const NR_BWP_PDCCH_t *pdcch_config = &mac->config_BWP_PDCCH[current_DL_BWP->bwp_id];
    NR_tda_info_t tda_info = get_ul_tda_info(current_UL_BWP,
                                             *pdcch_config->ra_SS->controlResourceSetId,
                                             pdcch_config->ra_SS->searchSpaceType->present,
                                             TYPE_RA_RNTI_,
                                             rar_grant.Msg3_t_alloc);
    if (!tda_info.valid_tda || tda_info.nrOfSymbols == 0) {
      LOG_E(MAC, "Cannot schedule Msg3. Something wrong in TDA information\n");
      return;
    }
    const int ntn_ue_koffset = get_NTN_UE_Koffset(mac->sc_info.ntn_Config_r17, mac->current_UL_BWP->scs);
    ret = nr_ue_pusch_scheduler(mac, is_Msg3, frame, slot, &frame_tx, &slot_tx, tda_info.k2 + ntn_ue_koffset);

    const int n_slots_frame = nr_slots_per_frame[current_UL_BWP->scs];
    ul_time_alignment->frame = (frame_tx + (slot_tx + ntn_ue_koffset) / n_slots_frame) % MAX_FRAME_NUMBER;
    ul_time_alignment->slot = (slot_tx + ntn_ue_koffset) % n_slots_frame;

    if (ret != -1) {
      uint16_t rnti = mac->crnti;
      // Upon successful reception, set the T-CRNTI to the RAR value if the RA preamble is selected among the contention-based RA Preambles
      if (!ra->cfra) {
        ra->t_crnti = rar->TCRNTI_2 + (rar->TCRNTI_1 << 8);
        rnti = ra->t_crnti;
        if (!ra->msg3_C_RNTI)
          nr_mac_rrc_msg3_ind(mac->ue_id, rnti, dl_info->gNB_index);
      }
      fapi_nr_ul_config_request_pdu_t *pdu = lockGet_ul_config(mac, frame_tx, slot_tx, FAPI_NR_UL_CONFIG_TYPE_PUSCH);
      if (!pdu)
        return;
      // Config Msg3 PDU
      int ret = nr_config_pusch_pdu(mac,
                                    &tda_info,
                                    &pdu->pusch_config_pdu,
                                    NULL,
                                    NULL,
                                    &rar_grant,
                                    rnti,
                                    NR_SearchSpace__searchSpaceType_PR_common,
                                    NR_DCI_NONE);
      if (ret != 0)
        remove_ul_config_last_item(pdu);
      release_ul_config(pdu, false);
    }

  } else {
    ra->t_crnti = 0;
  }
  return;
}

