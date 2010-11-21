

#ifndef SMEM_PC_OEM_CMD_H
#define SMEM_PC_OEM_CMD_H

typedef enum
{
SMEM_PC_OEM_FIRST_GEN_CMD = 0,
SMEM_PC_OEM_CHECK_VERSION = SMEM_PC_OEM_FIRST_GEN_CMD,
SMEM_PC_OEM_LAST_GEN_CMD = 100,	

/* Beginning of DT commands */
SMEM_PC_OEM_FIRST_DT_CMD = 101,
SMEM_PC_OEM_FLASH_LED_ON = SMEM_PC_OEM_FIRST_DT_CMD,
SMEM_PC_OEM_FLASH_LED_OFF,

SMEM_PC_OEM_RTC_WR_YEAR,
SMEM_PC_OEM_RTC_WR_MONTH,
SMEM_PC_OEM_RTC_WR_DAY,
SMEM_PC_OEM_RTC_WR_HOUR,
SMEM_PC_OEM_RTC_WR_MINUTE,
SMEM_PC_OEM_RTC_WR_SECOND,
SMEM_PC_OEM_RTC_WR_DOW,
SMEM_PC_OEM_RTC_RD_YEAR,
SMEM_PC_OEM_RTC_RD_MONTH,
SMEM_PC_OEM_RTC_RD_DAY,
SMEM_PC_OEM_RTC_RD_HOUR,
SMEM_PC_OEM_RTC_RD_MINUTE,
SMEM_PC_OEM_RTC_RD_SECOND,
SMEM_PC_OEM_RTC_RD_DOW,
SMEM_PC_OEM_WR_RTC_ALERT, 

SMEM_PC_OEM_USB_OTG_ON,
SMEM_PC_OEM_USB_OTG_OFF,

SMEM_PC_OEM_VIBRATOR_ON,
SMEM_PC_OEM_VIBRATOR_OFF,

SMEM_PC_OEM_KPD_LED_ON,
SMEM_PC_OEM_KPD_LED_OFF,

SMEM_PC_OEM_BACKUP_BATT_CHG_ON,
SMEM_PC_OEM_BACKUP_BATT_CHG_OFF,
SMEM_PC_OEM_BACKUP_BATT_ADC_READ_VOLT,

SMEM_PC_OEM_ALS_ON, /* ambient light sensor */
SMEM_PC_OEM_READ_ALS, 
SMEM_PC_OEM_ALS_OFF, 

SMEM_PC_OEM_CAMERA_ON,/*camera */

SMEM_PC_OEM_SDIO_ON,
SMEM_PC_OEM_SDIO_INIT_GET_MAN_ID,
SMEM_PC_OEM_SDIO_ONLY_INIT,

SMEM_PC_OEM_PROXIMITY_SENSOR_ON,
SMEM_PC_OEM_PROXIMITY_SENSOR_OFF,

SMEM_PC_OEM_AUD_LB_HANDSET, /* Mic1 -> Receiver */
SMEM_PC_OEM_AUD_LB_HEADSET, /* Mic2 -> headset */
SMEM_PC_OEM_AUD_LB_LOUDSPEAKER,   /* Mic1 -> Loudspeaker */

SMEM_PC_OEM_G_SENSOR_ON,
SMEM_PC_OEM_G_SENSOR_OFF,

SMEM_PC_OEM_CHARGING_PRE,
SMEM_PC_OEM_CHARGING_FAST,
SMEM_PC_OEM_CHARGING_STOP,

SMEM_PC_OEM_GPS_TEST,
SMEM_PC_OEM_GSM_TX_ON,
SMEM_PC_OEM_GSM_TX_OFF,

SMEM_PC_OEM_BOARD_ID,

SMEM_PC_OEM_HW_ID,

SMEM_PC_OEM_LCD_TEST,


SMEM_PC_OEM_TP_TEST,


SMEM_PC_OEM_BT_TEST,

SMEM_PC_OEM_FM_TEST,

SMEM_PC_OEM_WLAN_TEST,

SMEM_PC_OEM_SIM_ON,
SMEM_PC_OEM_SIM_OFF,

SMEM_PC_OEM_SIM_INFO,



SMEM_PC_OEM_MODEM_VER,



SMEM_PC_OEM_HDMI_TEST,

SMEM_PC_OEM_GPS_GET_RESULT,


SMEM_PC_OEM_LCD_OFF,


SMEM_PC_OEM_AUD_PLAY_MUSIC_START, /*  */
SMEM_PC_OEM_AUD_PLAY_MUSIC_STOP, /*  */



SMEM_PC_OEM_GET_SECURE_BOOT_STATUS,



SMEM_PC_OEM_GET_DEFAULT_NV_RESTORE_FLAG,


SMEM_PC_OEM_LAST_DT_CMD = 500,
/* End of DT commands */

//============================================================================//
/* Qisda, Eric Liu, 2009/2/19, BSP functions { */
SMEM_PC_OEM_FIRST_BSP_CMD = 501,
SMEM_PC_OEM_BAT_CTL_USB_SWITCH_ON,  //USB transistor
SMEM_PC_OEM_BAT_CTL_USB_SWITCH_OFF,
SMEM_PC_OEM_BAT_CTL_CHG_SWITCH_ON,  //CHG transistor
SMEM_PC_OEM_BAT_CTL_CHG_SWITCH_OFF,
SMEM_PC_OEM_BAT_CTL_BAT_SWITCH_ON,  //BAT MOS_FET
SMEM_PC_OEM_BAT_CTL_BAT_SWITCH_OFF,
SMEM_PC_OEM_BAT_CTL_COIN_CHG_ON,    //coin cell charge
SMEM_PC_OEM_BAT_CTL_COIN_CHG_OFF,
SMEM_PC_OEM_BAT_CTL_TRICKLE_ON,     //trickle charge
SMEM_PC_OEM_BAT_CTL_TRICKLE_OFF,
SMEM_PC_OEM_LED_CTL_VIB_ON,         //vibrator
SMEM_PC_OEM_LED_CTL_VIB_OFF,
SMEM_PC_OEM_LED_CTL_FLASH_LED_ON,   //flash LED
SMEM_PC_OEM_LED_CTL_FLASH_LED_OFF,
SMEM_PC_OEM_LED_CTL_KEYPAD_LED_ON,  //keypad LED
SMEM_PC_OEM_LED_CTL_KEYPAD_LED_OFF,
SMEM_PC_OEM_AUD_CTL_MICBIAS_ON,     //MICBIAS
SMEM_PC_OEM_AUD_CTL_MICBIAS_OFF,
SMEM_PC_OEM_LED_CTL_READ_ALS,
SMEM_PC_OEM_LED_CTL_KEYPAD_LED_READ,



SMEM_PC_OEM_AUD_LB_HANDSET_START, /* Mic1 -> Receiver, Start */
SMEM_PC_OEM_AUD_LB_HANDSET_STOP, /* Mic1 -> Receiver, Stop */

//============================================================================//

SMEM_PC_OEM_CAMERA2_ON,/*camera */

SMEM_PC_OEM_AIRPLANE_MODE_INFO, 
SMEM_PC_OEM_AIRPLANE_MODE_DISABLE,   // not use anymore



SMEM_PC_OEM_GET_FA_HOME_KEY_SENSITIVITY,
SMEM_PC_OEM_GET_FA_MENU_KEY_SENSITIVITY,
SMEM_PC_OEM_GET_FA_BACK_KEY_SENSITIVITY,



SMEM_PC_OEM_GET_QISDA_DIAGPKT_REQ_RSP_ADDR,
SMEM_PC_OEM_GET_QISDA_DIAGPKT_REQ_RSP_ADDR_STATUS,
SMEM_PC_OEM_QISDA_DIAGPKT_REQ_RSP,



SMEM_PC_OEM_GET_OTP_DATA_NV_DATA_CURRENTLY_ADDR,
SMEM_PC_OEM_GET_OTP_DATA_ADDR, 
SMEM_PC_OEM_CLEAR_OTP_DATA, 
SMEM_PC_OEM_CLEAR_OTP_DATA_STATUS,



SMEM_PC_OEM_UART_LOG_STATUS,



SMEM_PC_OEM_GET_PWK_RELEASE_STATUS,



SMEM_PC_OEM_ERASE_EFS2,



SMEM_PC_OEM_RECOVERY_FAIL_PAHES1_FLAG_STATUS, 
SMEM_PC_OEM_RECOVERY_FAIL_PAHES2_FLAG_STATUS, 



SMEM_PC_OEM_QCT_USB_DRIVER_STATUS,



SMEM_PC_OEM_GET_ALS_CAL_NV_DATA,


SMEM_PC_OEM_QUERY_ERR_OFFSET,



SMEM_PC_OEM_PM_GP2_LPM_ON,

SMEM_PC_OEM_PM_GP2_LPM_OFF,



SMEM_PC_OEM_ADB_PERMISSION_STATUS,



SMEM_PC_OEM_LOW_BATTERY_STATUS,
SMEM_PC_OEM_ENABLE_LOW_BATTERY_STATUS,
SMEM_PC_OEM_DISABLE_LOW_BATTERY_STATUS,
SMEM_PC_OEM_QUERY_WRITE_LOW_BATTERY_STATUS,



SMEM_PC_OEM_GET_PROXIMITY_SENSOR_NV_DATA,



SMEM_PC_OEM_FLUSH_ADSP6_CACHE,


SMEM_PC_OEM_MAX_CMDS = 65535 /* This should be large enough. We only need 16bits from VAR1 of smem_proc_comm function */
}smem_pc_oem_cmd_type;
#endif /* SMEM_PCMOD_QISDA_H */

