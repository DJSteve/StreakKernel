/*******************************************************************************
                                                                                
                      Qisda Handset Project                                     
                                                                                
                     Copyright (c) 2009 Qisda Corpration                        
                                                                                
File Description                                                                
----------------                                                                
                                                                                
when        who            what, where, why                                     
                                                                                
----------  ------------   ----------------------------------------------------
2010/08/02  Vincecheng     Stage Main: [Modem][PS] Add event to refresh PBM phonebook; Streak-05588: [US][ATT_AT1_114317][V1.03]GSM-BTR-1-5662: Verify Allowed MT Short Messages: "Contact 10" is not displayed Implement
2010/06/24  NoahYang       Austin Main: [Modem][SYS] Add event to Get/Set reset option of modem crash
2010/06/03  LesterLee      Toucan Main:[Modem][L1] add client and server command for nv backup
2010/3/19   NoahYang       Austin Main: [Modem][Sys] AUSB1A_SCR#2152 ; Align timezone with Android
2009/12/30  JZHou          Austin Main: [Modem][L1] add adb_permission flag
2009/12/22  JimmyLin       Austin Main: Integrate Qualcomm R324012 release
2009/10/7   LesterLee      [RD][FR] Disable DIAG/NMEA/Modem by default. User can configure from log master
2009/9/29   JZHou          refine erase partition, for master reset, refine flags sub partition
2009/9/7    LesterLee      Disable/Enable UART1 log by reading value from Reserved Partition
2009/7/29   JamesYang      Feature Support for GPS test mode in Single SV command for CN0 measurement
2009/6/30   SkyWeng        [FR] secure clock API for DRM
2009/6/26   JZHou          [L1][FR] Implement flash OTP driver
2009/6/23   JSLee          [FR] Add OEMRAPI to read/write NV item
2009/6/17   YanChang       [Protocol][FR] OS Engineering mode - PDP info
2009/6/4    NoahYang       [System][FR] HLOS access protocol logs of modem
2009/6/1    JZHou          [FR] Device must not charge in airplane mode - power off mode
2009/5/6    EllisonYang    [L1][FR] OS Engineer Mode : GPIO & NV & Flash UID & Flash OTP
2009/4/28   BernieHuang    [Protocol][FR] OS Engineering mode - System info
2009/4/21   NoahYang       [System][FR] HLOS access Error log and NPI logs of modem
                           [System][FR] SYS-Debug-2: Exception handler
2009/4/1    NoahYang       [L1][FR] Engineer Mode :RF test
2009/4/1    NoahYang       [APP][FR] Protocol log filter configuration interface
                           [System][FR] HLOS access protocol logs of modem
                           [System][FR] HLOS access Error log and NPI logs of modem
2009/3/25   HankHuang      [Protocol] Add OEMRPC to query modem SW version
2009/3/24   HankHuang      Add EM PS Data events
                           [Protocol][FR] OS Engineering mode - PS data info
2009/3/20   SkyWeng        Add PM log engineering mode control.
2009/3/16   MikeYang       [FR] Add NV for CTS data_connection test
********************************************************************************/ 

#ifndef OEM_RAPI_EVENT_H
#define OEM_RAPI_EVENT_H
/* Define the maximum sizes of the buffers here */
#define OEM_RAPI_MAX_CLIENT_INPUT_BUFFER_SIZE  128
#define OEM_RAPI_MAX_CLIENT_OUTPUT_BUFFER_SIZE 128
#define OEM_RAPI_MAX_SERVER_INPUT_BUFFER_SIZE  128
#define OEM_RAPI_MAX_SERVER_OUTPUT_BUFFER_SIZE 128

/*============================================================================

             TYPE DECLARATIONS

============================================================================*/

/*
** This enum lists the events that the client can notify the server of.
*/
typedef enum oem_rapi_client_event
{
  /* -------------------------------- */
  OEM_RAPI_CLIENT_EVENT_NONE = 0,
  /* -------------------------------- */
  OEM_RAPI_CLIENT_EVENT_EM_RF_TX_ON,
  OEM_RAPI_CLIENT_EVENT_EM_RF_TX_OFF,
/* Qisda, Mike Yang, 2009/3/18, RF test for engineering mode { */
  OEM_RAPI_CLIENT_EVENT_EM_RF_RX_ON,
  OEM_RAPI_CLIENT_EVENT_EM_RF_RX_OFF,
/* } Qisda, Mike Yang, 2009/3/18 */
/* QISDA Sky Weng 2009/3/20 GAUSB1A-00002, PM Log control { */
  OEM_RAPI_CLIENT_EVENT_EM_PM_LOG_CONTROL,
  /* } QISDA Sky Weng, 2009/3/20 */
  /* Qisda Hank Huang, 2009/3/16 AUSB1A_SCR #37 { */
#ifdef FEATURE_QISDA_ENG_MODE_PSDATA
  OEM_RAPI_CLIENT_EVENT_EM_PS_DATA_GET_CUR_CNT_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_PS_DATA_GET_LT_CNT_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_PS_DATA_RESET_LT_CNT_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_PS_DATA_STOP_LT_CNT_REQ,
#endif
  /* } Qisda Hank Huang, 2009/3/16 */

  /* Qisda Hank Huang, 2009/3/5 AUSB1A_SCR #130 { */
  OEM_RAPI_CLIENT_EVENT_SYSTEM_GET_VER_REQ,
  /* } Qisda Hank Huang, 2009/3/5 */
  /* QISDA  Noah Yang, AUSB1A_SCR #117, 2008/4/1, QXDM msg filter setup { */
  OEM_RAPI_CLIENT_EVENT_EXP_QXDM_FILTER,
  /* } QISDA  Noah Yang, 2008/4/1 */
  /* QISDA  Noah Yang, AUSB1A_SCR #110, 2008/4/1, save QXDM log to HLOS { */
  OEM_RAPI_CLIENT_EVENT_EXP_QXDM_SAVING,
  /* } QISDA  Noah Yang, 2008/4/1 */  
  /* QISDA  Noah Yang, AUSB1A_SCR #109, 2008/4/1, save log in EFS to HLOS { */
  OEM_RAPI_CLIENT_EVENT_EXP_EFS_SAVING,
  /* } QISDA  Noah Yang, 2008/4/1 */

  /* Qisda Eliot Huang, 2009/02/20 Austin_SCR #34 { */
  /* For Austin EM mode */
  #ifdef FEATURE_QISDA_ENG_MODE_REG_MM
  OEM_RAPI_CLIENT_EVENT_EM_NAS_INFO_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_REG_INFO_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_MM_MM_INFO_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_MM_PLMN_LIST_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_MM_GMM_INFO_REQ,
  #endif
  /* } Qisda Eliot Huang, 2008/02/20 */   
  
  /* QISDA  Noah Yang, 2009/4/21, AUSB1A_SCR #001 #109,  HLOS access logs in EFS  { */
  OEM_RAPI_CLIENT_EVENT_EXP_QUERY_ERR_OFFSET,
  OEM_RAPI_CLIENT_EVENT_EXP_TRIGGER_ERR_FATAL,      
  OEM_RAPI_CLIENT_EVENT_EXP_EFS_DIR_LOOKUP,  
  /* } QISDA  Noah Yang, 2009/4/21 */
  
  /* Qisda Bernie Huang, 2009/03/27, GAUSB1A-00033, Sys info for engineering mode { */
  #ifdef FEATURE_QISDA_ENG_MODE_SYS_INFO
  OEM_RAPI_CLIENT_EVENT_EM_GSM_MEAS_INFO_1_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GSM_MEAS_INFO_2_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GSM_SCELL_INFO_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GSM_DED_INFO_1_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GSM_DED_INFO_2_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GSM_PT_INFO_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_MEAS_INFO_1_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_MEAS_INFO_2_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_MEAS_INFO_3_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_SCELL_INFO_1_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_SCELL_INFO_2_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_CONN_INFO_1_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_CONN_INFO_2_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_HSDPA_INFO_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_HSUPA_INFO_1_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_WCDMA_HSUPA_INFO_2_REQ,
  #endif
  /* } Qisda Bernie Huang, 2009/03/27 */

/* Qisda, Ellison Yang, 2009/March/31, GPIO engineering mode { */
  OEM_RAPI_CLIENT_EVENT_EM_GPIO_READ_0_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GPIO_READ_1_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GPIO_READ_2_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GPIO_WRITE_REQ,		        
  /* } Qisda, Ellison Yang, 2009/March/31 */

/* Qisda, JZ Hou 2009/5/26 for Flags partition { */
  OEM_RAPI_CLIENT_EVENT_AIRPLANE_MODE_SETTING, 				
/* Qisda, JZ Hou 2009/5/26 for Flags partition } */

  /* QISDA  Noah Yang, 2009/6/4, AUSB1A_SCR #001 #110,  pass trace_buffer offset to HLOS { */
  OEM_RAPI_CLIENT_EVENT_EXP_QUERY_QXDM_OFFSET,  
  /* } QISDA  Noah Yang, 2009/6/4 */

  /* Qisda Yan Chang, 2009/4/10 AUSB1A_SCR #35 { */
  #ifdef FEATURE_QISDA_ENG_MODE_PDP
  OEM_RAPI_CLIENT_EVENT_EM_PDP_ABSTRACT_INFO_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_PDP_GENERAL_INFO_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_QOS_INFO_REQ,
  #endif
  /* } Qisda Yan Chang, 2009/4/10 */

  /* Qisda JS Lee, 2009/4/24 AUSB1A_SCR #725 { */
  /* OEM RAPI for NV operation */
  OEM_RAPI_CLIENT_EVENT_NV_OP_REQ,
  /* } Qisda JS Lee, 2009/4/24 */

/* Qisda, JZ Hou 2009/6/26 for DRM KEY read { */
  OEM_RAPI_CLIENT_EVENT_GET_DRMKEY_REQ,
/* Qisda, JZ Hou 2009/6/26 for DRM KEY read } */
  /* QISDA Sky Weng 2009/6/30 GAUSB1A-00160, Secure time feature { */
  OEM_RAPI_CLIENT_EVENT_TIME_REMOTE_SECURE_GET,
  OEM_RAPI_CLIENT_EVENT_TIME_REMOTE_SECURE_GET_JULIAN,
  OEM_RAPI_CLIENT_EVENT_TIME_REMOTE_SECURE_SET,
  OEM_RAPI_CLIENT_EVENT_TIME_REMOTE_SECURE_SET_JULIAN,
  /* } QISDA Sky Weng, 2009/6/30 */


/* Qisda, JZ Hou 2009/07/09 for BT address, WLAN address, and Service Tag { */
  OEM_RAPI_CLIENT_EVENT_GET_BT_MAC_ADDR_REQ,
  OEM_RAPI_CLIENT_EVENT_GET_WLAN_MAC_ADDR_REQ,
  OEM_RAPI_CLIENT_EVENT_GET_SERVICE_TAG_REQ,
/* Qisda, JZ Hou 2009/07/0 for BT address, WLAN address, and Service Tag } */

  /*  Qisda James Yang,  2009/06/10, AUSB1A_SCR #673, Add for GPS Testmode for CN0 { */
  #ifdef FEATURE_QISDA_ENG_MODE_GPS_TESTMODE
  OEM_RAPI_CLIENT_EVENT_EM_GPS_SARF_MODE_SWITCH_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GPS_SINGLE_SV_REQ,
  #endif
  /* }Qisda James Yang,  2009/06/10   */
/* Qisda, Lester Lee 2009/09/07, for uart log flag { */
  OEM_RAPI_CLIENT_EVENT_UART_LOG_SETTING,
/* } Qisda, Lester Lee 2009/09/07, for uart log flag */

/* Qisda, JZ Hou 2009/9/30 for master reset { */
  OEM_RAPI_CLIENT_EVENT_DEFAULT_NV_RESTORE_REQ,
  OEM_RAPI_CLIENT_EVENT_ERASE_FLAGS_PARTITION_REQ,
/* Qisda, JZ Hou 2009/9/30 for master reset } */


/* Qisda, JZ Hou 2009/10/02 for Flags partition { */
  OEM_RAPI_CLIENT_EVENT_RECOVERY_FAIL_PAHES1_FLAG_SETTING, 				
  OEM_RAPI_CLIENT_EVENT_RECOVERY_FAIL_PAHES2_FLAG_SETTING, 				
/* Qisda, JZ Hou 2009/10/02 for Flags partition } */

/* Qisda, Lester Lee 2009/10/07 for qct usb driver flag { */
  OEM_RAPI_CLIENT_EVENT_QCT_USB_DRIVER_FLAG_SETTING,
/* } Qisda, Lester Lee 2009/10/07 for qct usb driver flag */  

  /* Qisda, Eliot Huang, 2008/11/14, Austin_SCR #46{ */
  /* In original design, MS will wait recoery timer timeout and try to find network 
   when MS can't find network service before.
   In order to find service more quickly, adding a new API to trigger modem
   to shorten recovery timer.
   Therefore, recovery timer will timeout more quickly and try to find network */
  #ifdef  FEATURE_QISDA_SHORTEN_SLEEP_MODE 
  OEM_RAPI_CLIENT_EVENT_SHORTEN_SEARCH_SLEEP_TIMER,
  #endif
  /* } Qisda Eliot Huang, 2008/11/14 */

/* Qisda, JZ Hou 2009/12/30 for Flags partition { */
  OEM_RAPI_CLIENT_EVENT_ADB_PERMISSION_FLAG_SETTING, 
/* Qisda, JZ Hou 2009/12/30 for Flags partition } */

  /* QISDA  Noah Yang, 2010/3/19, AUSB1A_SCR#2152 , Align timezone with Android { */
  OEM_RAPI_CLIENT_EVENT_SET_TIMEZONE,
  /* } QISDA  Noah Yang, 2010/3/19 */

  /* Qisda, Lester Lee 2010/06/03, add for nv backup { */
  OEM_RAPI_CLIENT_EVENT_NV_BACKUP_REQ,
  /* } Qisda, Lester Lee 2010/06/03, add for nv backup */
  
  /* QISDA  Noah Yang, 2010/6/24, Add event to Get/Set reset option of modem crash { */
  OEM_RAPI_CLIENT_EVENT_EXP_GET_RESET_OPTION,
  OEM_RAPI_CLIENT_EVENT_EXP_SET_RESET_OPTION,
  /* } QISDA  Noah Yang, 2010/6/24 */
/* QISDA Vince, 2010/7/29, Austin SCR# Streak-05588{*/
  OEM_RAPI_CLIENT_EVENT_PBM_SYN_REQ,
/* } QISDA Vince, 2010/7/29 */
  /* Qisda, Lester Lee 2010/09/07, add client event for gpio setting in sleep mode { */
  OEM_RAPI_CLIENT_EVENT_EM_GPIO_READ_IN_SLEEP_0_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GPIO_READ_IN_SLEEP_1_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GPIO_READ_IN_SLEEP_2_REQ,
  OEM_RAPI_CLIENT_EVENT_EM_GPIO_WRITE_IN_SLEEP_REQ,              
  /* } Qisda, Lester Lee 2010/09/07, add client event for gpio setting in sleep mode */
  /* -------------------------------- */
  OEM_RAPI_CLIENT_EVENT_MAX
  /* -------------------------------- */
} oem_rapi_client_event_e_type;

/*
** This enum lists the events that the server can notify the client of.
*/
typedef enum oem_rapi_server_event
{
  /* -------------------------------- */
  OEM_RAPI_SERVER_EVENT_NONE = 0,
  /* -------------------------------- */
  /* Qisda Hank Huang, 2009/3/16 AUSB1A_SCR #37 { */
#ifdef FEATURE_QISDA_ENG_MODE_PSDATA
  OEM_RAPI_SERVER_EVENT_EM_PS_DATA_GET_CUR_CNT_IND,
  OEM_RAPI_SERVER_EVENT_EM_PS_DATA_GET_LT_CNT_IND,
#endif
  /* } Qisda Hank Huang, 2009/3/16 */

  /* QISDA  Noah Yang, AUSB1A_SCR #110, 2008/4/1, save QXDM log to HLOS { */
  OEM_RAPI_SERVER_EVENT_EXP_QXDM_SAVING,
  /* } QISDA  Noah Yang, 2008/4/1 */  
  /* QISDA  Noah Yang, AUSB1A_SCR #109, 2008/4/1, save log in EFS to HLOS { */
  OEM_RAPI_SERVER_EVENT_EXP_EFS_SAVING,
  /* } QISDA  Noah Yang, 2008/4/1 */

  /* Qisda Eliot Huang, 2009/02/20 Austin_SCR #34 { */
  /* For Austin EM mode */
  #ifdef FEATURE_QISDA_ENG_MODE_REG_MM
  OEM_RAPI_SERVER_EVENT_EM_NAS_INFO_IND,
  OEM_RAPI_SERVER_EVENT_EM_REG_INFO_IND,
  OEM_RAPI_SERVER_EVENT_EM_MM_MM_INFO_IND,
  OEM_RAPI_SERVER_EVENT_EM_MM_PLMN_LIST_IND,
  OEM_RAPI_SERVER_EVENT_EM_MM_GMMINFO_IND,
  #endif
  /* } Qisda Eliot Huang, 2008/02/20 */
  
  /* QISDA  Noah Yang, 2009/4/21, AUSB1A_SCR #001 #109,  HLOS access logs in EFS  { */
  OEM_RAPI_SERVER_EVENT_EXP_QUERY_ERR_OFFSET,
  OEM_RAPI_SERVER_EVENT_EXP_TRIGGER_ERR_FATAL,      
  OEM_RAPI_SERVER_EVENT_EXP_EFS_DIR_LOOKUP,  
  /* } QISDA  Noah Yang, 2009/4/21 */ 
  
  /* Qisda Bernie Huang, 2009/03/27, GAUSB1A-00033, Sys info for engineering mode { */
  #ifdef FEATURE_QISDA_ENG_MODE_SYS_INFO
  OEM_RAPI_SERVER_EVENT_EM_GSM_MEAS_INFO_1_IND,
  OEM_RAPI_SERVER_EVENT_EM_GSM_MEAS_INFO_2_IND,
  OEM_RAPI_SERVER_EVENT_EM_GSM_SCELL_INFO_IND,
  OEM_RAPI_SERVER_EVENT_EM_GSM_DED_INFO_1_IND,
  OEM_RAPI_SERVER_EVENT_EM_GSM_DED_INFO_2_IND,
  OEM_RAPI_SERVER_EVENT_EM_GSM_PT_INFO_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_MEAS_INFO_1_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_MEAS_INFO_2_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_MEAS_INFO_3_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_SCELL_INFO_1_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_SCELL_INFO_2_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_CONN_INFO_1_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_CONN_INFO_2_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_HSDPA_INFO_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_HSUPA_INFO_1_IND,
  OEM_RAPI_SERVER_EVENT_EM_WCDMA_HSUPA_INFO_2_IND,
  #endif
  /* } Qisda Bernie Huang, 2009/03/27 */

 /* Qisda, Ellison Yang, 2009/March/31, GPIO engineering mode { */
  OEM_RAPI_SERVER_EVENT_EM_GPIO_READ_0_IND,
  OEM_RAPI_SERVER_EVENT_EM_GPIO_READ_1_IND,
  OEM_RAPI_SERVER_EVENT_EM_GPIO_READ_2_IND,
  /* } Qisda, Ellison Yang, 2009/March/31 */
  
  /* QISDA  Noah Yang, 2009/6/4, AUSB1A_SCR #001 #110,  pass trace_buffer offset to HLOS { */
  OEM_RAPI_SERVER_EVENT_EXP_QUERY_QXDM_OFFSET,  
  /* } QISDA  Noah Yang, 2009/6/4 */

  /* Qisda Yan Chang, 2009/4/10 AUSB1A_SCR #35 { */
  #ifdef FEATURE_QISDA_ENG_MODE_PDP
  OEM_RAPI_SERVER_EVENT_EM_PDP_ABSTRACT_INFO_IND,
  OEM_RAPI_SERVER_EVENT_EM_PDP_GENERAL_INFO_IND,
  OEM_RAPI_SERVER_EVENT_EM_QOS_INFO_IND,
  #endif
  /* } Qisda Yan Chang, 2009/4/10 */

  /*  Qisda James Yang,  2009/06/10, AUSB1A_SCR #673, Add for GPS Testmode for CN0 { */
  #ifdef FEATURE_QISDA_ENG_MODE_GPS_TESTMODE
  OEM_RAPI_SERVER_EVENT_EM_GPS_SARF_MODE_SWITCH_IND,
  OEM_RAPI_SERVER_EVENT_EM_GPS_SINGLE_SV_IND,
  #endif
  /* }Qisda James Yang,  2009/06/10   */

/* Qisda, JZ Hou 2009/9/30 for master reset { */
  OEM_RAPI_CLIENT_EVENT_DEFAULT_NV_RESTORE_IND,
/* Qisda, JZ Hou 2009/9/30 for master reset } */
  
  /* QISDA  Noah Yang, 2010/3/19, AUSB1A_SCR#2152 , Align timezone with Android { */
  OEM_RAPI_SERVER_EVENT_SET_TIMEZONE,
  /* } QISDA  Noah Yang, 2010/3/19 */

  /* Qisda, Lester Lee 2010/06/03, add for nv backup { */
  OEM_RAPI_SERVER_EVENT_NV_BACKUP_IND,
  /* } Qisda, Lester Lee 2010/06/03, add for nv backup */
  
  /* QISDA  Noah Yang, 2010/6/24, Add event to Get/Set reset option of modem crash { */
  OEM_RAPI_SERVER_EVENT_EXP_GET_RESET_OPTION,
  OEM_RAPI_SERVER_EVENT_EXP_SET_RESET_OPTION,
  /* } QISDA  Noah Yang, 2010/6/24 */

  /* Qisda, Lester Lee 2010/09/07, add client event for gpio setting in sleep mode { */
  OEM_RAPI_SERVER_EVENT_EM_GPIO_READ_IN_SLEEP_0_IND,
  OEM_RAPI_SERVER_EVENT_EM_GPIO_READ_IN_SLEEP_1_IND,
  OEM_RAPI_SERVER_EVENT_EM_GPIO_READ_IN_SLEEP_2_IND,
  /* } Qisda, Lester Lee 2010/09/07, add client event for gpio setting in sleep mode */
  
  /* -------------------------------- */
  OEM_RAPI_SERVER_EVENT_MAX
  /* -------------------------------- */
} oem_rapi_server_event_e_type;
#endif /* ! OEM_RAPI_EVENT_H */
