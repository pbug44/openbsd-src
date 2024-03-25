/*	$OpenBSD$ */
/*
 * Copyright (c) 2024 Mark Kettenis <kettenis@openbsd.org>
 * Copyright (c) 2024 Peter J. Philipp <pjp@delphinusdns.org>
 *
 * Permission to use, copy, modify, and distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/clockintr.h>
#include <sys/device.h>
#include <sys/sensors.h>

#include <machine/intr.h>
#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>
#include <dev/ofw/fdt.h>

/* Registers */

#define BFLB_TMR_TCCR		0x0
#define  BFLB_TMR2_TCCR_SHIFT	0
#define  BFLB_TMR2_TCCR_MASK	0xf
#define  BFLB_TMR3_TCCR_SHIFT	4
#define  BFLB_TMR3_TCCR_MASK	0xf0
#define  BFLB_WDT_TCCR_SHIFT	8
#define  BFLB_WDT_TCCR_MASK	0xf00

#define  BFLB_TMR_FCLK		0
#define  BFLB_TMR_F32K		1	/* reset default for watchdog timer */
#define  BFLB_TMR_1K		2
#define  BFLB_TMR_32M		3
#define  BFLB_TMR_GPIO		4
#define  BFLB_TMR_NOCLOCK	5	/* reset default for timer 2 and 3 */

#define  BFLB_TMR2_F32K		(BFLB_TMR_F32K << BFLB_TMR2_TCCR_SHIFT)
#define  BFLB_WDT_F32K		(BFLB_TMR_F32K << BFLB_WDT_TCCR_SHIFT)

#define  BFLB_TM2_DFLT		(BFLB_TMR_NOCLOCK)
#define  BFLB_TM3_DFLT		(BFLB_TMR_NOCLOCK << BFLB_TMR3_TCCR_SHIFT)
#define  BFLB_RESET_STATE	(BFLB_WDT_F32K | BFLB_TM2_DFLT | BFLB_TM3_DFLT)


#define BFLB_TMR2_WATCH_VALUE	0x10	/* timer 2 watch value 0 */
#define BFLB_TMR3_WATCH_VALUE	0x1c	/* timer 3 watch value 0 */

#define BFLB_TMR2_COUNT_VALUE	0x2c	/* timer 2 counter value (ro) */
#define BFLB_TMR3_COUNT_VALUE	0x30	/* timer 3 counter value (ro) */

#define BFLB_TMR2_TIER_ENA	0x44	/* timer 2 watch value intr enable */
#define BFLB_TMR3_TIER_ENA	0x48	/* timer 3 watch value intr enable */

#define BFLB_TMR2_PLV		0x50	/* timer 2 pre-load value */
#define BFLB_TMR3_PLV		0x54	/* timer 3 pre-load value */

#define BFLB_TMR2_PLCR		0x5c	/* timer 2 pre-load ctrl with match */
#define BFLB_TMR3_PLCR		0x60	/* timer 2 pre-load ctrl with match */


#define BFLB_WDT_INT_ENA	0x64	/* WMER - Watchdog intr ena */

#define BFLB_TMR2_TICR		0x78	/* TICR - timer 2 interrupt clear */
#define BFLB_TMR3_TICR		0x7c	/* TICR - timer 3 interrupt clear */

#define BFLB_TMR_TCER		0x84
#define  BFLB_TMR_TCER_TMR2_ENA (1 << 1)
#define  BFLB_TMR_TCER_TMR3_ENA (1 << 2)
#define  BFLB_TMR_TCER_TMR2_CLR (1 << 5)
#define  BFLB_TMR_TCER_TMR3_CLR (1 << 6)

#define BFLB_TMR_TCMR		0x88	/* pre-load(dflt) vs. free-run mode */

#define BFLB_TMR2_TILR		0x90	/* timer 2 level 1:edge */
#define BFLB_TMR3_TILR		0x94	/* timer 3 level 1:edge */

#define BFLB_TMR2_TCVWR		0xa8	/* timer 2 latch value */
#define BFLB_TMR3_TCVWR		0xac	/* timer 3 latch value */

#define BFLB_TMR2_TCVSYN	0xb4	/* timer2 counter value (sync) */
#define BFLB_TMR3_TCVSYN	0xb8	/* timer3 counter value (sync) */

#define BFLB_TMR_TCDR		0xbc	/* timer clock division value reg */

#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))

struct bflbtimer_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_ticks_per_second;
	uint64_t		sc_nsec_cycle_ratio;
	uint64_t		sc_nsec_max;
	void			*sc_ih;
};

int	bflbtimer_match(struct device *, void *, void *);
void	bflbtimer_attach(struct device *, struct device *, void *);

const struct cfattach bflbtimer_ca = {
	sizeof (struct bflbtimer_softc), bflbtimer_match, bflbtimer_attach
};

struct cfdriver bflbtimer_cd = {
	NULL, "bflbtimer", DV_DULL
};

void	bflbtimer_startclock(void);
int	bflbtimer_intr(void *);
void	bflbtimer_rearm(void *, uint64_t);
void	bflbtimer_trigger(void *);

struct intrclock bflbtimer_intrclock = {
	.ic_rearm = bflbtimer_rearm,
	.ic_trigger = bflbtimer_trigger
};

int bflbtimer_attached = 0;

int
bflbtimer_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;

	if (bflbtimer_attached != 0)
		return 0;

	return OF_is_compatible(faa->fa_node, "bflb,bl808-timer");
}

void
bflbtimer_attach(struct device *parent, struct device *self, void *aux)
{
	struct bflbtimer_softc *sc = (struct bflbtimer_softc *)self;
	struct fdt_attach_args *faa = aux;

	if (faa->fa_nreg < 1) {
		printf(": no registers\n");
		return;
	}

	sc->sc_iot = faa->fa_iot;
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
	    faa->fa_reg[0].size, 0, &sc->sc_ioh)) {
		printf(": can't map registers\n");
		return;
	}

	/* reset and clear interrupts */
	HWRITE4(sc, BFLB_TMR_TCCR, BFLB_RESET_STATE);
	HWRITE4(sc, BFLB_TMR2_TICR, 0x0);
	HWRITE4(sc, BFLB_TMR3_TICR, 0x0);

	sc->sc_ticks_per_second = 32000;	/* 32 KHz */
	sc->sc_nsec_cycle_ratio =
	    sc->sc_ticks_per_second * (1ULL << 32) / 1000000000;
	sc->sc_nsec_max = UINT64_MAX / sc->sc_nsec_cycle_ratio;

	bflbtimer_intrclock.ic_cookie = sc;
	cpu_startclock_fcn = bflbtimer_startclock;

	sc->sc_ih = fdt_intr_establish_idx(faa->fa_node, 0, IPL_CLOCK,
	    bflbtimer_intr, NULL, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf("can't establish interrupt\n");
		return;
	}

	HWRITE4(sc, BFLB_TMR_TCCR, (BFLB_TMR_F32K | BFLB_WDT_F32K));
	HWRITE4(sc, BFLB_TMR2_TIER_ENA, 1);

	printf(": %u kHz\n", sc->sc_ticks_per_second / 1000);
	
	bflbtimer_attached = 1;
}

void
bflbtimer_startclock(void)
{
	clockintr_cpu_init(&bflbtimer_intrclock);
	clockintr_trigger();
}

int
bflbtimer_intr(void *frame)
{
	struct bflbtimer_softc *sc = bflbtimer_intrclock.ic_cookie;

	HWRITE4(sc, BFLB_TMR2_TICR, 0x0);
	HWRITE4(sc, BFLB_TMR_TCCR, (BFLB_TMR_F32K | BFLB_WDT_F32K));

	return clockintr_dispatch(frame);
}

void
bflbtimer_rearm(void *cookie, uint64_t nsecs)
{
	struct bflbtimer_softc *sc = cookie;
	uint32_t cycles;

	if (nsecs > sc->sc_nsec_max)
		nsecs = sc->sc_nsec_max;
	cycles = (nsecs * sc->sc_nsec_cycle_ratio) >> 32;
	if (cycles > UINT32_MAX)
		cycles = UINT32_MAX;
	if (cycles < 1)
		cycles = 1;

	HWRITE4(sc, BFLB_TMR2_TICR, 0x0);
	HWRITE4(sc, BFLB_TMR2_WATCH_VALUE, cycles);
	HWRITE4(sc, BFLB_TMR_TCCR, (BFLB_TMR_F32K | BFLB_WDT_F32K));
}

void
bflbtimer_trigger(void *cookie)
{
	struct bflbtimer_softc *sc = cookie;

	HWRITE4(sc, BFLB_TMR2_WATCH_VALUE, 1);
	HWRITE4(sc, BFLB_TMR_TCCR, (BFLB_TMR_F32K | BFLB_WDT_F32K));
}
