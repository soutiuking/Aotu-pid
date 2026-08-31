/**
 * @file    protocol_def.h
 * @brief   DOME 涓€鏈熷伐绋?鍙岀缁熶竴閫氫俊鍗忚瀹氫箟 (ESP32-S3 <-> STM32F103C8T6)
 *
 * 甯ф牸寮?(鍏ㄩ儴澶氬瓧鑺傛暟鎹皬绔?:
 *   [0] 0xAA        甯уご1
 *   [1] 0x55        甯уご2
 *   [2] 0x01        鍗忚鐗堟湰
 *   [3] CMD         鍛戒护瀛?(鍝嶅簲 = 璇锋眰 | 0x80)
 *   [4] SEQ         搴忓彿 (鍝嶅簲鍥炲～璇锋眰搴忓彿)
 *   [5] LEN_L       鏁版嵁闀垮害浣庡瓧鑺? *   [6] LEN_H       鏁版嵁闀垮害楂樺瓧鑺? *   [7..]           鏁版嵁鍖?N 瀛楄妭 (N = LEN, 鍙负 0)
 *   [7+N] CRC_L     CRC16 浣庡瓧鑺? (瑕嗙洊 [2] .. 6+N, 鍗崇増鏈瑍鏁版嵁鍖?
 *   [8+N] CRC_H     CRC16 楂樺瓧鑺? *   [9+N] 0x0D      甯у熬1
 *   [10+N] 0x0A     甯у熬2
 *
 * CRC16: Modbus, 澶氶」寮?0x8005 (鍙嶅皠瀹炵幇 0xA001), 鍒濆€?0xFFFF,
 *        鏃犺緭鍑哄紓鎴? 杈撳叆鑼冨洿 = 鍗忚鐗堟湰瀛楄妭璧疯嚦鏁版嵁鍖烘渶鍚庝竴瀛楄妭, 浣庡瓧鑺傚湪鍓嶃€? *
 * 鎵€鏈夊懡浠ょ殑鍝嶅簲鏁版嵁鍖虹粺涓€鍓?2 瀛楄妭: [0]=STATUS, [1]=ERROR_CODE, 鍏跺悗涓烘暟鎹€? * 瀛楄妭搴?搴忓垪鍖? 涓€寰嬩娇鐢ㄦ樉寮忕殑灏忕鎵撳寘鍑芥暟, 涓嶇洿鎺ヤ紶杈撶粨鏋勪綋銆? *
 * 鏈枃浠跺湪 ESP32 涓?STM32 涓ょ鍚勫瓨涓€浠? 蹇呴』淇濇寔瀛楄妭绾т竴鑷?
 *   aotu pid stm32/Protocol/protocol_def.h
 *   aotu pid esp32/main/protocol/protocol_def.h
 */
#ifndef PROTOCOL_DEF_H
#define PROTOCOL_DEF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ================================ 甯у父閲?================================ */
#define PROTO_HEAD1              0xAAu
#define PROTO_HEAD2              0x55u
#define PROTO_VERSION            0x01u
#define PROTO_TAIL1              0x0Du
#define PROTO_TAIL2              0x0Au

#define PROTO_HEAD_LEN           7u   /* 甯уご2 + 鐗堟湰1 + 鍛戒护1 + 搴忓彿1 + 闀垮害2 */
#define PROTO_CRC_LEN            2u
#define PROTO_TAIL_LEN           2u
#define PROTO_OVERHEAD           (PROTO_HEAD_LEN + PROTO_CRC_LEN + PROTO_TAIL_LEN) /* 11 */

#define PROTO_MAX_PAYLOAD        192u
#define PROTO_MAX_FRAME          (PROTO_MAX_PAYLOAD + PROTO_OVERHEAD) /* 203 */

#define PROTO_RESP_BIT           0x80u

/* ================================ 鍛戒护瀛?================================ */
/* 鍩虹閫氫俊绫?*/
#define CMD_DEVICE_INFO_READ     0x01u
#define CMD_DEVICE_STATUS_READ   0x02u
#define CMD_HEARTBEAT            0x03u
#define CMD_RESET                0x04u
#define CMD_TIME_SYNC            0x05u

/* PID 鍙傛暟绫?*/
#define CMD_PID_PARAM_READ       0x10u
#define CMD_PID_PARAM_WRITE      0x11u
#define CMD_PID_PARAM_SAVE       0x12u
#define CMD_PID_PARAM_LOAD       0x13u
#define CMD_PID_PARAM_DEFAULT    0x14u
#define CMD_PID_PARAM_LIST_READ  0x15u
#define CMD_PID_PARAM_RANGE_READ 0x16u

/* PID 杩愯鎺у埗绫?*/
#define CMD_PID_START            0x20u
#define CMD_PID_STOP             0x21u
#define CMD_PID_PAUSE            0x22u
#define CMD_PID_RESUME           0x23u
#define CMD_PID_TARGET_SET       0x24u
#define CMD_PID_OUTPUT_SET       0x25u
#define CMD_PID_MODE_SET         0x26u
#define CMD_PID_RUNTIME_READ     0x27u

/* 鑷姩鏁村畾绫?*/
#define CMD_AUTOTUNE_START       0x30u
#define CMD_AUTOTUNE_STOP        0x31u
#define CMD_AUTOTUNE_PAUSE       0x32u
#define CMD_AUTOTUNE_RESUME      0x33u
#define CMD_AUTOTUNE_STATUS      0x34u
#define CMD_AUTOTUNE_RESULT      0x35u
#define CMD_AUTOTUNE_APPLY       0x36u

/* Flash 鍙傛暟瀛樺偍绫?*/
#define CMD_FLASH_PARAM_SAVE     0x40u
#define CMD_FLASH_PARAM_LOAD     0x41u
#define CMD_FLASH_PARAM_ERASE    0x42u
#define CMD_FLASH_PARAM_VERIFY   0x43u
#define CMD_FLASH_PARAM_VERSION  0x44u

/* 鎵╁睍 (涓€鏈?dome 鏂板, 瑙佸崗璁枃妗? */
#define CMD_DISPLAY_PAGE_SET     0x50u

/* ================================ 鐘舵€?閿欒鐮?================================ */
#define STATUS_OK                0x00u
#define STATUS_ERROR             0x01u

#define ERR_OK                   0x00u
#define ERR_UNKNOWN_COMMAND      0x01u
#define ERR_INVALID_LENGTH       0x02u
#define ERR_CRC_ERROR            0x03u
#define ERR_FRAME_TIMEOUT        0x04u
#define ERR_PARAM_OUT_OF_RANGE   0x05u
#define ERR_DEVICE_BUSY          0x06u
#define ERR_FLASH_ERROR          0x07u
#define ERR_FLASH_DATA_INVALID   0x08u
#define ERR_PID_NOT_RUNNING      0x09u
#define ERR_AUTOTUNE_RUNNING     0x0Au
#define ERR_AUTOTUNE_FAILED      0x0Bu
#define ERR_AUTOTUNE_TIMEOUT     0x0Cu
#define ERR_SAFETY_LIMIT         0x0Du
#define ERR_COMMUNICATION_ERROR  0x0Eu
#define ERR_UNSUPPORTED_VERSION  0x0Fu

/* ================================ PID 鍥炶矾 ================================ */
#define PID_LOOP_MAX             3u   /* 0=娓╁害 1=閫熷害 2=浣嶇疆 */

/* PID 鍙傛暟/妯″紡鐨勬灇涓惧彇鍊?*/
#define PID_DIR_DIRECT           0x00u  /* 姝ｄ綔鐢? 杈撳嚭闅忚宸澶ц€屽澶?*/
#define PID_DIR_REVERSE          0x01u  /* 鍙嶄綔鐢?*/
#define PID_MODE_MANUAL          0x00u  /* 鎵嬪姩妯″紡, 杈撳嚭鐢卞閮ㄧ洿鎺ヨ瀹?*/
#define PID_MODE_AUTO            0x01u  /* 鑷姩妯″紡, 杈撳嚭鐢?PID 璁＄畻 */

/* PID 杩愯鐘舵€?(runtime/state 瀛楁) */
#define PID_STATE_IDLE           0x00u  /* 鏈繍琛?*/
#define PID_STATE_RUNNING        0x01u  /* 鑷姩妯″紡杩愯涓?*/
#define PID_STATE_PAUSED         0x02u  /* 鏆傚仠, 淇濇寔褰撳墠杈撳嚭 */
#define PID_STATE_MANUAL         0x03u  /* 鎵嬪姩妯″紡 */
#define PID_STATE_SAFE           0x04u  /* 瀹夊叏鐘舵€? 杈撳嚭鍏抽棴 */
#define PID_STATE_FAULT          0x05u  /* 鏁呴殰 */

/* 鏁呴殰鏍囧織浣?(runtime/fault 瀛楁, 浣嶆帺鐮? */
#define PID_FAULT_NONE           0x00u
#define PID_FAULT_FEEDBACK_ABNORMAL  0x01u  /* 鍙嶉鍊艰秺闄?鏃犳晥 */
#define PID_FAULT_OUTPUT_LIMIT   0x02u      /* 杈撳嚭闀挎椂闂撮ケ鍜?*/
#define PID_FAULT_COMM_TIMEOUT   0x04u      /* 涓婁綅鏈洪€氫俊瓒呮椂杩涘叆瀹夊叏鐘舵€?*/

/* ================================ 鑷姩鏁村畾 ================================ */
typedef enum
{
    AT_STATE_IDLE = 0,        /* 绌洪棽 */
    AT_STATE_PREPARE,         /* 鍑嗗: 淇濆瓨褰撳墠鍙傛暟, 杈撳嚭鍒版渶灏?*/
    AT_STATE_WAIT_STABLE,     /* 绛夊緟鍙嶉绋冲畾 */
    AT_STATE_EXCITATION,      /* 缁х數婵€鍔? 杈撳嚭鍦?min/max 闂村垏鎹?*/
    AT_STATE_COLLECT_DATA,    /* 閲囬泦鎸崱鏁版嵁 (涓庢縺鍔遍樁娈靛苟琛岃鏁? */
    AT_STATE_CALCULATE,       /* 璁＄畻 Ku/Tu 涓?Kp Ki Kd */
    AT_STATE_RESULT_READY,    /* 缁撴灉灏辩华, 绛夊緟鐢ㄦ埛搴旂敤 */
    AT_STATE_APPLIED,         /* 缁撴灉宸插簲鐢ㄥ埌 RAM 鍙傛暟 */
    AT_STATE_STOPPED,         /* 琚墜鍔ㄥ仠姝?*/
    AT_STATE_TIMEOUT,         /* 瓒呮椂閫€鍑?*/
    AT_STATE_ERROR            /* 寮傚父閫€鍑?(鍙嶉瓒婇檺绛? */
} autotune_state_t;

#define AUTOTUNE_MODE_RELAY      0x01u  /* Reserved for a future ESP32 algorithm */

/* 鏁村畾缁撴灉鏈夋晥鎬?*/
#define AUTOTUNE_RESULT_NONE     0x00u
#define AUTOTUNE_RESULT_VALID    0x01u

/* ================================ 璁惧鐘舵€?================================ */
typedef enum
{
    DEV_STATE_INIT = 0,       /* 鍒濆鍖栦腑 */
    DEV_STATE_READY,          /* 灏辩华 */
    DEV_STATE_RUNNING,        /* 鑷冲皯涓€涓洖璺湪鑷姩杩愯 */
    DEV_STATE_AUTOTUNE,       /* Compatibility value; STM32 never enters it */
    DEV_STATE_SAFE,           /* 瀹夊叏鐘舵€?(閫氫俊瓒呮椂/鏁呴殰) */
    DEV_STATE_FAULT           /* 鏁呴殰 */
} device_state_t;

/* DEVICE_INFO 鏀寔鐨勫姛鑳戒綅 */
#define FEAT_BIT_PID             (1u << 0)
#define FEAT_BIT_AUTOTUNE        (1u << 1)  /* ESP32-local capability; STM32 does not advertise it */
#define FEAT_BIT_FLASH_STORAGE   (1u << 2)
#define FEAT_BIT_DISPLAY         (1u << 3)

/* 鏄剧ず椤甸潰 (CMD_DISPLAY_PAGE_SET) */
#define DISP_PAGE_MAIN           0x00u
#define DISP_PAGE_PARAM          0x01u
#define DISP_PAGE_AUTOTUNE       0x02u  /* Compatibility value; ignored by STM32 */
#define DISP_PAGE_ERROR          0x03u
#define DISP_PAGE_AUTO_CYCLE     0xFFu

/* ============================ PID 鍙傛暟鑼冨洿琛?============================ */
/* 鍙傛暟绱㈠紩, 鐢ㄤ簬鍙傛暟鑼冨洿琛?(CMD_PID_PARAM_RANGE_READ) */
enum
{
    PIDR_KP = 0,
    PIDR_KI,
    PIDR_KD,
    PIDR_TARGET,
    PIDR_OUTPUT_MIN,
    PIDR_OUTPUT_MAX,
    PIDR_INTEGRAL_LIMIT,
    PIDR_SAMPLE_TIME,
    PIDR_DEADBAND,
    PIDR_FILTER,
    PIDR_COUNT
};

/* CMD_PID_PARAM_WRITE atomic payload:
 * [loop_id:1][10 x float32_le:40][direction:1][mode:1][reserved:2]
 * Total payload is 45 bytes and is validated before any live PID state changes. */
/* ============================ 绾挎牸寮忚緟鍔╁畯 ============================ */
/* 鍚勫懡浠ゅ搷搴旀暟鎹尯闀垮害 (鍚?STATUS+ERROR 2 瀛楄妭), 0 琛ㄧず鏃犲浐瀹氶暱搴?*/
#define RESP_LEN_DEVICE_INFO     (2u + 16u + 8u + 1u + 8u + 12u + 20u + 8u + 4u)

#ifdef __cplusplus
}
#endif

#endif /* PROTOCOL_DEF_H */

