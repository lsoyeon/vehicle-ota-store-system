#ifndef SOTA_UCB_H
#define SOTA_UCB_H

#include "Ifx_Types.h"

/* UCB_SWAP 주소 (매뉴얼 확인 완료) */
#define UCB_SWAP_ORIG    0xAF402E00UL   /* UCB23 */
#define UCB_SWAP_COPY    0xAF403E00UL   /* UCB31 */
#define UCB_OTP0_ORIG    0xAF404000UL   /* UCB32 — SWAPEN 비트 포함 */
#define UCB_CONFIRM_CODE 0x57B5327FUL
#define SWAP_MARKER_A    0x00000055UL   /* Group A (표준 맵) */
#define SWAP_MARKER_B    0x000000AAUL   /* Group B (교대 맵) */

/* DFLASH 8바이트 쓰기 (공유) */
void WriteDFlash8(uint32 addr, uint32 lo, uint32 hi);

/* 공개 인터페이스 */
void    SOTA_EnableSwapen  (void);
void    SOTA_InitialSetup  (void);
void    SOTA_SwapToGroupB  (void);
void    SOTA_SwapToGroupA  (void);
boolean SOTA_IsInitialized (void);
boolean SOTA_IsGroupBActive(void);

#endif /* SOTA_UCB_H */