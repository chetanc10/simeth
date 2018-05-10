
#ifndef __SIMETH_COMMON_H
#define __SIMETH_COMMON_H

#include <linux/types.h>

/* Resource realeasing and nullify pointer */
#define simeth_release(rel_fn, ptr) \
	if ((ptr)) { rel_fn ((ptr)); (ptr) = NULL; }

/* Resolve gcc -Wunused-variable compiler issues */
#define simeth_sym_void(x) (void)(x)

#ifdef __KERNEL__ /* identifies kernel land; is it safe forever? -FIXME*/

#define simeth_r8(addr)            readb (addr)
#define simeth_r16(addr)           readw (addr)
#define simeth_r32(addr)           readl (addr)

#define simeth_w8(addr, value)     writeb (value, addr)
#define simeth_w16(addr, value)    writew (value, addr)
#define simeth_w32(addr, value)    writel (value, addr)

#ifdef CONFIG_64BIT
#define simeth_r64(addr)           readq (addr)
#define simeth_w64(addr, value)    writeq (value, addr)
#else
#define simeth_r64(addr) \
	({uint32_t *_addr = (uint32_t *)addr; \
	 simeth_r32 (_addr) | (simeth_r32 (_addr + 1) << 32)})
#define simeth_w64(addr, value) \
	({uint32_t *_addr = (uint32_t *)addr; \
	 simeth_w32 (_addr, value); \
	 simeth_w32 (_addr + 1, (value >> 32))})
#endif

#define simeth_r8_rep(addr, buf, cnt) ioread8_rep (addr, buf, cnt)
#define simeth_r16_rep(addr, buf, cnt) ioread16_rep (addr, buf, cnt)
#define simeth_r32_rep(addr, buf, cnt) ioread32_rep (addr, buf, cnt)

#define simeth_w8_rep(addr, buf, cnt) iowrite8_rep (addr, buf, cnt)
#define simeth_w16_rep(addr, buf, cnt) iowrite16_rep (addr, buf, cnt)
#define simeth_w32_rep(addr, buf, cnt) iowrite32_rep (addr, buf, cnt)

#ifdef CONFIG_64BIT
#define simeth_r64_rep(addr, buf, cnt) ioread64_rep (addr, buf, cnt)
#define simeth_w64_rep(addr, buf, cnt) iowrite64_rep (addr, buf, cnt)
#else
#define simeth_r64_rep(addr, buf, cnt) \
{ \
	int i = 0; \
	uint64_t _addr = (uint64_t *)addr; \
	uint64_t _buf = (uint64_t *)buf; \
	for (i = 0; i < cnt; i++) { \
		_buf[i] = simeth_r64 (_addr + i); \
	} \
}
#define simeth_w64_rep(addr, buf, cnt) \
{ \
	int i = 0; \
	uint64_t _addr = (uint64_t *)addr; \
	uint64_t _buf = (uint64_t *)buf; \
	for (i = 0; i < cnt; i++) { \
		simeth_w64 (_addr + i, _buf[i]); \
	} \
}
#endif

#else
/*__KERNEL__ not defined i.e. userspace! */

/*bwlq - byte/word/long/quad-word*/
#define simeth_uio_rd(addr, bwlq) \
	(*(volatile uint##bwlq##t *)(addr))
#define simeth_uio_wr(addr, value, bwlq) \
	(*(volatile uint##bwlq##_t *)(addr) = (uint##bwlq##_t)(value))

#define simeth_r8(addr)            simeth_uio_rd (addr, b)
#define simeth_r16(addr)           simeth_uio_rd (addr, w)
#define simeth_r32(addr)           simeth_uio_rd (addr, l)
#define simeth_r64(addr)           simeth_uio_rd (addr, q)

#define simeth_w8(addr, value)     simeth_uio_wr (addr, b, value)
#define simeth_w16(addr, value)    simeth_uio_wr (addr, w, value)
#define simeth_w32(addr, value)    simeth_uio_wr (addr, l, value)
#define simeth_w64(addr, value)    simeth_uio_wr (addr, q, value)

#define simeth_uio_rd_rep(addr, buf, cnt, bwlq) \
	memcpy ((void *)(buf), \
			(const volatile void *)(addr), \
			(cnt) * sizeof (uint##bwlq##_t))
#define simeth_uio_wr_rep(addr, buf, cnt, bwlq) \
	memcpy ((volatile void *)(addr), \
			(const void *)(buf), \
			(cnt) * sizeof (uint##bwlq##_t))

#define simeth_r8_rep(addr, buf, cnt)  simeth_uio_rd_rep (addr, buf, cnt, b)
#define simeth_r16_rep(addr, buf, cnt) simeth_uio_rd_rep (addr, buf, cnt, w)
#define simeth_r32_rep(addr, buf, cnt) simeth_uio_rd_rep (addr, buf, cnt, l)
#define simeth_r64_rep(addr, buf, cnt) simeth_uio_rd_rep (addr, buf, cnt, q)

#define simeth_w8_rep(addr, buf, cnt)  simeth_uio_wr_rep (addr, buf, cnt, b)
#define simeth_w16_rep(addr, buf, cnt) simeth_uio_wr_rep (addr, buf, cnt, w)
#define simeth_w32_rep(addr, buf, cnt) simeth_uio_wr_rep (addr, buf, cnt, l)
#define simeth_w64_rep(addr, buf, cnt) simeth_uio_wr_rep (addr, buf, cnt, q)

#endif /*#ifdef __KERNEL__*/

#endif /*__SIMETH_COMMON_H*/

