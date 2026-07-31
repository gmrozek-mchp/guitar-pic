#ifndef TC6_CONFIG_H_
#define TC6_CONFIG_H_

/* Build-time configuration for the vendored OPEN Alliance TC6 driver
 * (third_party/oa-tc6-lib) on the beatbox node. The library #includes
 * "tc6-conf.h" from the include path; this is beatbox's copy.
 *
 * This node exchanges only small command/heartbeat frames, so the SPI burst is
 * capped at a few chunks instead of the library's default 31. The dsPIC33AK has
 * ample RAM if these ever need to grow. */

#ifdef __cplusplus
extern "C" {
#endif

#define TC6_ASSERT(condition)

#define TC6_MAX_INSTANCES       (1u)

/* OPEN Alliance fixed protocol values — do not modify. */
#define TC6_HEADER_SIZE         (4u)
#define TC6_CHUNK_SIZE          (64u)
#define TC6_CHUNK_BUF_SIZE      (TC6_CHUNK_SIZE + TC6_HEADER_SIZE)

#define REG_OP_ARRAY_SIZE       (4u)

/* Single command frame fits in one chunk; 4 leaves margin. Caps SPI-buffer SRAM. */
#define TC6_CHUNKS_XACT         (4u)
#define TC6_CONCAT_THRESHOLD    (1024u)

#define SPI_FULL_BUFFERS        (1u)
#define TC6_TX_ETH_QSIZE        (2u)
#define TC6_TX_ETH_MAX_SEGMENTS (1u)
#define TC6_MAX_CNTRL_VARS      (1u)

#ifdef __cplusplus
}
#endif

#endif /* TC6_CONFIG_H_ */
