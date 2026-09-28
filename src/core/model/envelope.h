#ifndef TR_ENVELOPE_H
#define TR_ENVELOPE_H

/* 이벤트 봉투 계약 (계획서 §6)
 *
 * - 원본 이벤트 시각(event_time_us)과 엔진 처리 순서(sequence)를 따로 기록한다.
 * - 단일 증권사 스트림을 넘는 전역 시간 순서는 보장하지 않는다.
 * - 구조체 메모리를 그대로 IPC·파일에 쓰지 않는다.
 */

#include <stdint.h>

#include "core/model/time_us.h"

typedef enum {
    TR_EVENT_TICK = 0,
    TR_EVENT_ORDERBOOK,
    TR_EVENT_CANDLE_UPDATE,   /* 진행 봉 갱신 */
    TR_EVENT_CANDLE_CLOSED,   /* 봉 확정 */
    TR_EVENT_ORDER_EVENT,     /* 주문 접수/거절/취소 등 정규화 이벤트 */
    TR_EVENT_FILL,            /* 체결 */
    TR_EVENT_COMMAND_RESULT,
    TR_EVENT_SESSION          /* 세션 시작/종료 등 */
} tr_event_kind_t;

/* 데이터 품질 플래그. 조용히 버린 뒤 정상으로 표시하지 않기 위한 명시적 표시다. */
typedef uint32_t tr_quality_flags_t;
#define TR_QUALITY_NONE      ((tr_quality_flags_t)0u)
#define TR_QUALITY_LATE      ((tr_quality_flags_t)1u << 0) /* 늦게 도착 */
#define TR_QUALITY_DUPLICATE ((tr_quality_flags_t)1u << 1) /* 중복 의심 */
#define TR_QUALITY_SUSPECT   ((tr_quality_flags_t)1u << 2) /* 값 자체 의심 */
#define TR_QUALITY_GAP       ((tr_quality_flags_t)1u << 3) /* 앞 입력 누락 감지 */
#define TR_QUALITY_CORRECTED ((tr_quality_flags_t)1u << 4) /* 과거 데이터 정정분 */
#define TR_QUALITY_FILLED_EMPTY ((tr_quality_flags_t)1u << 5) /* 무거래 채움 봉 (수신 누락과 구분) */

typedef struct {
    tr_event_kind_t kind;
    uint32_t schema_version;      /* 메시지 스키마 버전 */
    uint64_t engine_instance_id;  /* 엔진 재시작을 식별하는 실행 ID */
    uint64_t sequence;            /* 엔진 처리 순번 (단조 증가) */
    uint64_t source_id;           /* 입력 출처 ID */
    uint64_t source_sequence;     /* 출처 스트림 내 순번 (없으면 0) */
    tr_time_us_t event_time_us;   /* 원본 이벤트 시각 (UTC) */
    tr_time_us_t received_time_us;/* 엔진 수신 시각 (UTC) */
    tr_quality_flags_t quality;
} tr_event_envelope_t;

#endif
