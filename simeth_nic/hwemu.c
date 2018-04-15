
#define _GNU_SOURCE

#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <syslog.h>
#include <pthread.h>
#include <semaphore.h>
#include <limits.h>
#include <time.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/mman.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/syscall.h>
#include <sys/utsname.h>
#include <math.h>
#include <dirent.h>
#include <sched.h>

volatile uint32_t we_live = 1;

struct desc_csr { /* Update this acc to FPGA desc design -TODO */
	uint32_t              status:4;
	uint32_t              valid:1;
	uint32_t              rsv1:26;
	volatile uint32_t     ownedByFpga:1;
} __attribute__ ((packed));
struct xpmd_dma_desc {
	uint64_t              src;
	uint64_t              dest;
	uint32_t              len;
	uint32_t              st_err;
	uint32_t              packet_type;
	struct desc_csr       dcsr;
} __attribute__ ((packed));

struct xpmd_desc_node {
	/*Not actual descriptors, but buffers with usage/validity header*/
	uint8_t               *mbuf;
	struct xpmd_dma_desc  desc;
	uint8_t               bytes[4052]; //4K - sizeof(desc) - sizeof ptrmbuf - sizeof idx
	uint32_t              idx;
};

void sighandler (int signum)
{
	we_live = 0;
}

struct xpmd_desc_node *txd_base;
struct xpmd_desc_node *rxd_base;
struct xpmd_desc_node *txd_last;
struct xpmd_desc_node *rxd_last;

pthread_t rxtid;

void *loop_txonly (void *arg)
{
	struct xpmd_desc_node *txd = txd_base;
	uint64_t txpkts_cnt = 0;

	printf ("starting txonly on thread%s\n", arg ? "1" : "0");

	while (we_live) {
		while (!txd->desc.dcsr.ownedByFpga && we_live) usleep (1);
		if (!we_live) break;
		txd->desc.dcsr.ownedByFpga = 0;
		txpkts_cnt++;
		txd = (++txd <= txd_last) ? txd : txd_base;
	}

	printf ("\ntxpkts: %lu\n", txpkts_cnt);

	return arg;
}

void *loop_rxonly (void *arg)
{
	struct xpmd_desc_node *rxd = rxd_base;
	struct xpmd_desc_node *txd = txd_base;
	uint64_t rxpkts_cnt = 0;
	int burst32rx_done = 0;

	printf ("starting rxonly on thread%s\n", arg ? "1" : "0");

	while (we_live) {
		if (burst32rx_done == 32)
			while (!rxd->desc.dcsr.ownedByFpga && we_live) usleep (1);
		else
			burst32rx_done++;
		if (!we_live) break;
		memcpy (rxd->bytes, txd->bytes, txd->desc.len);
		rxd->desc.len = txd->desc.len;
		rxd->desc.packet_type = txd->desc.packet_type;
		rxd->desc.dcsr.ownedByFpga = 0;
		if (burst32rx_done == 32) rxpkts_cnt++;
		rxd = (++rxd <= rxd_last) ? rxd : rxd_base;
		txd = (++txd <= txd_last) ? txd : txd_base;
	}
	rxpkts_cnt--; /*1 unwanted increment in above logic.. so decrement*/

	printf ("\nrxpkts: %lu\n", rxpkts_cnt);

	return arg;
}

void *loop_rxtx (void *arg)
{
	struct xpmd_desc_node *rxd = rxd_base;
	struct xpmd_desc_node *txd = txd_base;
	int burst32_done = 0;
	int burst32rx_done = 0;
	uint64_t rxpkts_cnt = 0;
	uint64_t txpkts_cnt = 0;

	printf ("starting rxtx loop%s\n", \
			(arg) ? " with tx first" : "");

	if (arg) { /*tx first*/
		for (burst32_done = 0; we_live && burst32_done < 32; \
				burst32_done++) {
			while (!txd->desc.dcsr.ownedByFpga && we_live) usleep (1);
			if (!we_live) break;
			txd->desc.dcsr.ownedByFpga = 0;
			txpkts_cnt++;
			txd = (++txd <= txd_last) ? txd : txd_base;
		}
	}

	if (!we_live) goto ENDL;

	while (we_live) {
		for (burst32_done = 0; \
				we_live && burst32_done < 32; \
				burst32_done++) { /*rx burst*/
			if (burst32rx_done == 32)
				while (!rxd->desc.dcsr.ownedByFpga && we_live) usleep (1);
			else
				burst32rx_done++;
			if (!we_live) break;
			memcpy (rxd->bytes, txd->bytes, txd->desc.len);
			rxd->desc.len = txd->desc.len;
			rxd->desc.packet_type = txd->desc.packet_type;
			rxd->desc.dcsr.ownedByFpga = 0;
			if (burst32rx_done == 32) rxpkts_cnt++;
			rxd = (++rxd <= rxd_last) ? rxd : rxd_base;
			txd = (++txd <= txd_last) ? txd : txd_base;
		}
		for (burst32_done = 0; we_live && burst32_done < 32; \
				burst32_done++) { /*tx burst*/
			while (!txd->desc.dcsr.ownedByFpga && we_live) usleep (1);
			if (!we_live) break;
			txd->desc.dcsr.ownedByFpga = 0;
			txpkts_cnt++;
			txd = (++txd <= txd_last) ? txd : txd_base;
		}
	}
	rxpkts_cnt--; /*1 unwanted increment in above logic.. so decrement*/

ENDL:
	printf ("\ntxpkts: %lu\n", txpkts_cnt);
	printf ("rxpkts: %lu\n", rxpkts_cnt);

	return arg;
}

int main (int argc, char **argv)
{
	int n;
	char *mbuf;
	struct xpmd_desc_node *txd;
	struct xpmd_desc_node *rxd;
	int i = 0;
	int burst32_done = 0;
	int burst32rx_done = 0;
	uint64_t rxpkts_cnt = 0;
	uint64_t txpkts_cnt = 0;
	int fd = open ("/dev/shm/ctanan", O_RDWR);
	if (fd < 0) {
		perror ("/dev/shm/ctanan");
		return fd;
	}

	signal (SIGINT, sighandler);

	mbuf = (char *)mmap (0, 8*1024*1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (mbuf == (char *)-1) {
		perror ("mmap ivshmem: ");
		return errno;
	}

	if (0) {
		/*verify simple strcpy operations on rx and tx qs*/
		char *rxlane = mbuf;
		char *txlane = mbuf + 0x8000;
		char tempbuf[64] = "";
		char c;
		printf ("Proceed? (y|n): ");
		scanf (" %c", &c);
		if (c != 'y') goto END;
		strncpy (tempbuf, rxlane, sizeof (tempbuf));
		printf ("incoming: %s\n", tempbuf);
		strcpy (tempbuf, "Applauding to my best for what you have done for me!");
		strncpy (txlane, tempbuf, strlen (tempbuf));
		goto END;
	}

	txd = txd_base = (struct xpmd_desc_node *)mbuf;
	rxd = rxd_base = txd + 32;
	txd_last = txd_base + 31;
	rxd_last = rxd_base + 31;

	do {
		rxd->desc.dcsr.ownedByFpga = 1;
		rxd = (++rxd <= rxd_last) ? rxd : rxd_base;
		txd->desc.dcsr.ownedByFpga = 0;
		txd = (++txd <= txd_last) ? txd : txd_base;
	} while (rxd != rxd_base);

	if (argc == 2) { /*multithreaded?*/
		int ret = 0;
		printf ("Each of the 2 threads can do either rx & tx exclusively!\n");
		printf ("Main Thread0 takes tx and new Thread1 takes rx\n");
		ret = pthread_create (&rxtid, NULL, loop_rxonly, &rxtid);
		if (ret < 0) {
			perror ("loop-thread\n");
			goto END;
		}
		n = 1;
	} else {
		printf ("Select a test-sequence:\n");
		printf ("0. none\n");
		printf ("1. txonly\n");
		printf ("2. rxonly \n");
		printf ("3. loop with tx first\n");
		printf ("4. loop with rx first\n");
		printf ("choice: ");
		scanf ("%d", &n);
		if (n == 0)
			goto END;
	}

#if 0
	if (!(n & 1)) {
		for (i = 0; i < 32; i++) { /*setting a pattern in txd bytes (pktbuf)*/
			memset (txd[i].bytes, 0, 32);
			txd[i].bytes[0] = i;
		}
	}
#endif

	switch (n) { /*test's as per testpmd, test-pmd-perf, pktgen apps*/
		case 1:
			loop_txonly (NULL);
			break;
		case 2:
			loop_rxonly (NULL);
			break;
		case 3:
		case 4:
			loop_rxtx ((n == 3) ? &n : NULL);
			break;
	}

	if (rxtid) {
		pthread_join (rxtid, NULL);
	}

END:
	/*reset ownership flags*/
	rxd = rxd_base;
	txd = txd_base;
	do {
		rxd->desc.dcsr.ownedByFpga = 1;
		rxd = (++rxd <= rxd_last) ? rxd : rxd_base;
		txd->desc.dcsr.ownedByFpga = 0;
		txd = (++txd <= txd_last) ? txd : txd_base;
	} while (rxd != rxd_base);

	printf ("Releasing mapped resource..\n");
	munmap ((void *)mbuf, 8*1024*1024);

	close (fd);

	return 0;
}
