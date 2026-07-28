#ifndef TC6_CONFIG_H_
#define TC6_CONFIG_H_

/* Build-time configuration for the vendored OPEN Alliance TC6 driver
 * (third_party/oa-tc6-lib). The library #includes "tc6-conf.h" from the
 * include path; this is fauxmote's copy (ESP32 side). Values mirror the
 * library's cfg-example defaults; only TC6_MAX_INSTANCES is pinned
 * (one LAN8651). */

#ifdef __cplusplus
extern "C" {
#endif

#define TC6_ASSERT(condition)

/* One MAC-PHY on the fauxmote side. */
#define TC6_MAX_INSTANCES       (1u)

/* OPEN Alliance fixed protocol values — do not modify. */
#define TC6_HEADER_SIZE         (4u)
#define TC6_CHUNK_SIZE          (64u)
#define TC6_CHUNK_BUF_SIZE      (TC6_CHUNK_SIZE + TC6_HEADER_SIZE)

/* Control-data queue length (power of 2). */
#define REG_OP_ARRAY_SIZE       (4u)

/* Max TC6 chunks per SPI transaction, and concat threshold. */
#define TC6_CHUNKS_XACT         (31u)
#define TC6_CONCAT_THRESHOLD    (1024u)

#define SPI_FULL_BUFFERS        (1u)
#define TC6_TX_ETH_QSIZE        (4u)
#define TC6_TX_ETH_MAX_SEGMENTS (8u)
#define TC6_MAX_CNTRL_VARS      (1u)

#ifdef __cplusplus
}
#endif

#endif /* TC6_CONFIG_H_ */
