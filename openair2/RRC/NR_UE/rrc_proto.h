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

/* \file proto.h
 * \brief RRC functions prototypes for eNB and UE
 * \author R. Knopp, K.H. HSU
 * \date 2018
 * \version 0.1
 * \company Eurecom / NTUST
 * \email: knopp@eurecom.fr, kai-hsiang.hsu@eurecom.fr
 * \note
 * \warning
 */

#ifndef _RRC_PROTO_H_
#define _RRC_PROTO_H_


#include "rrc_defs.h"
#include "NR_RRCReconfiguration.h"
#include "NR_MeasConfig.h"
#include "NR_CellGroupConfig.h"
#include "NR_RadioBearerConfig.h"
#include "openair2/PHY_INTERFACE/queue_t.h"
#include "common/utils/ocp_itti/intertask_interface.h"

extern queue_t nr_rach_ind_queue;
extern queue_t nr_rx_ind_queue;
extern queue_t nr_crc_ind_queue;
extern queue_t nr_uci_ind_queue;
extern queue_t nr_sfn_slot_queue;
extern queue_t nr_chan_param_queue;
extern queue_t nr_dl_tti_req_queue;
extern queue_t nr_tx_req_queue;
extern queue_t nr_ul_dci_req_queue;
extern queue_t nr_ul_tti_req_queue;
//
//  main_rrc.c
//
/**\brief Layer 3 initialization*/
NR_UE_RRC_INST_t *nr_l3_init_ue(char *);

//
//  UE_rrc.c
//

/**\brief Initial the top level RRC structure instance*/
NR_UE_RRC_INST_t *openair_rrc_top_init_ue_nr(char *);
void init_nsa_message (NR_UE_RRC_INST_t *rrc, char* reconfig_file, char* rbconfig_file);

void process_nsa_message(NR_UE_RRC_INST_t *rrc, nsa_message_t nsa_message_type, void *message, int msg_len);

void nr_rrc_cellgroup_configuration(rrcPerNB_t *rrcNB,
                                    instance_t instance,
                                    NR_CellGroupConfig_t *cellGroupConfig);

/**\brief interface between MAC and RRC thru SRB0 (RLC TM/no PDCP)
   \param module_id  module id
   \param CC_id      component carrier id
   \param gNB_index  gNB index
   \param channel    indicator for channel of the pdu
   \param pduP       pointer to pdu
   \param pdu_len    data length of pdu*/
int8_t nr_mac_rrc_data_ind_ue(const module_id_t module_id,
                              const int CC_id,
                              const uint8_t gNB_index,
                              const frame_t frame,
                              const int slot,
                              const rnti_t rnti,
                              const channel_t channel,
                              const uint8_t* pduP,
                              const sdu_size_t pdu_len);

void nr_mac_rrc_sync_ind(const module_id_t module_id,
                         const frame_t frame,
                         const bool in_sync);

void nr_rrc_going_to_IDLE(instance_t instance,
                          NR_Release_Cause_t release_cause,
                          NR_RRCRelease_t *RRCRelease);

void nr_mac_rrc_ra_ind(const module_id_t mod_id, int frame, bool success);
void nr_mac_rrc_msg3_ind(const module_id_t mod_id, int rnti);

/**\brief RRC UE task.
   \param void *args_p Pointer on arguments to start the task. */
void *rrc_nrue_task(void *args_p);
void *rrc_nrue(void *args_p);

void nr_rrc_handle_timers(NR_UE_RRC_INST_t *rrc, instance_t instance);

/**\brief RRC NSA UE task.
   \param void *args_p Pointer on arguments to start the task. */
void *recv_msgs_from_lte_ue(void *args_p);

void init_connections_with_lte_ue(void);

void nsa_sendmsg_to_lte_ue(const void *message, size_t msg_len, Rrc_Msg_Type_t msg_type);

extern void start_oai_nrue_threads(void);

int get_from_lte_ue_fd();

void nr_rrc_SI_timers(NR_UE_RRC_SI_INFO *SInfo);

void nr_ue_rrc_timer_trigger(int module_id, int frame, int gnb_id);
void handle_t300_expiry(instance_t instance);

void reset_rlf_timers_and_constants(NR_UE_Timers_Constants_t *tac);
void set_default_timers_and_constants(NR_UE_Timers_Constants_t *tac);
void nr_rrc_set_sib1_timers_and_constants(NR_UE_Timers_Constants_t *tac, NR_SIB1_t *sib1);
void nr_rrc_set_T304(NR_UE_Timers_Constants_t *tac, NR_ReconfigurationWithSync_t *reconfigurationWithSync);
void handle_rlf_sync(NR_UE_Timers_Constants_t *tac,
                     nr_sync_msg_t sync_msg);
void nr_rrc_handle_SetupRelease_RLF_TimersAndConstants(NR_UE_RRC_INST_t *rrc,
                                                       struct NR_SetupRelease_RLF_TimersAndConstants *rlf_TimersAndConstants);

int configure_NR_SL_Preconfig(int sync_source);

/** @}*/
#endif

