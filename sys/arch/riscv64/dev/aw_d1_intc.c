/*
 * Copyright (c) 2020, Mars Li <mengshi.li.mars@gmail.com>
 * Copyright (c) 2020, Brian Bamsch <bbamsch@google.com>
 * Copyright (c) 2023, Peter J. Philipp <pjp@delphinusdns.org>
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
#include <sys/queue.h>
#include <sys/malloc.h>
#include <sys/device.h>
#include <sys/evcount.h>

#include <uvm/uvm.h>

#include <machine/bus.h>
#include <machine/fdt.h>
#include <machine/cpu.h>
#include <machine/sbi.h>
#include "riscv64/dev/riscv_cpu_intc.h"

#include <dev/ofw/openfirm.h>
#include <dev/ofw/fdt.h>

static int	aw_d1_intc_match(struct device *, void *, void *);
void		aw_d1_intc_attach(struct device *, struct device *, void *);
void *		aw_d1_intc_intr_establish(uint8_t, int, int (*)(void *), void *, char *);
void		aw_d1_intc_intr_disestablish(void *);
int		aw_d1_intc_irq_handler(void *);
uint32_t	aw_d1_intc_intr_get_parent(int);
void		aw_d1_intc_wake_irq(void *, int);
void		aw_d1_intc_reset(void *);

int     	plic_irq_dispatch(uint32_t, void *);



#define AW_D1_IRQ			160

#define AW_D1_WAKEUP_MASK_ENA		0x20
#define	AW_D1_WAKEUP_MASK_OFFSET	0x24

#define AW_D1_IRQ_MODE_OFFSET		0x60

#define AW_D1_RISCV_RESET_REG		0xd0c
#define AW_D1_RISCV_RESET_METHOD_1	1
#define AW_D1_RISCV_RESET_METHOD_2	65536
#define AW_D1_RISCV_RESET_METHOD_3	65537
#define AW_D1_RISCV_RESET_METHOD_4	0

/* remove debug!!!!!  */
#define AW_D1_DEBUG_INTC 1

struct aw_d1_intrhand {
        int (*ih_func)(void *);		/* handler */
        void *ih_arg;			/* arg for handler */
        uint8_t ih_irq;			/* IRQ number */
        char *ih_name;			/* driver name */
};

struct aw_d1_intc_softc {
	struct device *		sc_dev;
	struct device *		sc_parent;

	int			sc_node;

	bus_space_tag_t         sc_iot;
	bus_space_handle_t      sc_ioh;

	void *			sc_ccu_node;
	bus_space_handle_t	sc_ccu_ioh;
	struct fdt_reg 		sc_ccu_reg;
};

struct aw_d1_intc_softc *aw_d1 = NULL;

struct aw_d1_intrhand * aw_d1_intc_handler[AW_D1_IRQ] = {NULL};
struct interrupt_controller aw_d1_intc_ic;

const struct cfattach aw_d1_intc_ca = {
	sizeof (struct device), aw_d1_intc_match, aw_d1_intc_attach
};

struct cfdriver aw_d1_intc_cd = {
	NULL, "aw_d1_intc", DV_DULL
};

int aw_d1_attached = 0;


static int
aw_d1_intc_match(struct device *parent, void *cfdata, void *aux)
{
        struct fdt_attach_args *faa = aux;
	int node = faa->fa_node;

	if (aw_d1_attached)
		return 0;

        return (OF_is_compatible(node, "allwinner,sun20i-d1-intc"));
}

void
aw_d1_intc_attach(struct device *parent, struct device *dev, void *aux)
{
	struct fdt_attach_args *faa = aux;
	struct aw_d1_intc_softc *sc = (struct aw_d1_intc_softc *)dev;
	uint32_t reg;
	int i;
	int (*sta_add)();
	//uint64_t sta_add;
	paddr_t tmp;

	if (aw_d1_attached)
		return;

	/* init smask */
	//riscv_init_smask();

	printf("\n");

	sc->sc_node = faa->fa_node;
	sc->sc_iot = faa->fa_iot;

	sc->sc_parent = fdt_find_phandle(aw_d1_intc_intr_get_parent(sc->sc_node));
	sc->sc_dev = dev;

	aw_d1_intc_ic.ic_node = faa->fa_node;
	aw_d1_intc_ic.ic_cookie = &aw_d1_intc_ic;

        aw_d1_intc_ic.ic_establish = NULL;
        aw_d1_intc_ic.ic_disestablish = NULL;

	/* map bus space of interrupt controller at 0x6010000 */
	if (bus_space_map(sc->sc_iot, faa->fa_reg[0].addr,
		faa->fa_reg[0].size, 0, &sc->sc_ioh))
		panic("%s: bus_space_map failed!", __func__);

	/* worry about a MULTIPROCESSOR yet?  look up riscv_cpu_intc.c */

	/* enable wakeup mode */

	bus_space_write_4(sc->sc_iot, sc->sc_ioh, AW_D1_WAKEUP_MASK_ENA, 1);

	reg = AW_D1_WAKEUP_MASK_OFFSET;
	for (i = 0; i < 5; i++) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg + (i << 2), 0);	
	}

	/* zero all interrupts on the rising edge */
	reg = AW_D1_IRQ_MODE_OFFSET;
	for (i = 0; i < 5; i++) {
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg + (i << 2), 0);	
	}

	/* reset register on ccu */
	sc->sc_ccu_node = fdt_find_node("/soc/clock-controller");
	if (!fdt_is_compatible(sc->sc_ccu_node, "allwinner,sun20i-d1-ccu")) {
		panic("am I the only clock-controller around here?");
	}

	if (fdt_get_reg(sc->sc_ccu_node, 0, &sc->sc_ccu_reg) < 0) {
		panic("%s: can't register ccu node", __func__);
	}

	if (bus_space_map(sc->sc_iot, sc->sc_ccu_reg.addr, sc->sc_ccu_reg.size,\
		0, &sc->sc_ccu_ioh))
		panic("%s: ccu bus_space_map failed!", __func__);

	/* set our softc to aw_d1 */
	aw_d1 = sc;
		
	/* reset */
	reg = bus_space_read_4(sc->sc_iot, sc->sc_ccu_ioh, AW_D1_RISCV_RESET_REG);
	printf("before reset %u\n", reg);

	/* assert reset */
	reg = AW_D1_RISCV_RESET_METHOD_1; 
	bus_space_write_4(sc->sc_iot, sc->sc_ccu_ioh, AW_D1_RISCV_RESET_REG, reg);

	/* configure the start address register */
	reg = 0x0;

	//__asm volatile ("csrr %0, sepc" : "=r" (sta_add));
	sta_add = aw_d1_intc_irq_handler;
	pmap_extract(pmap_kernel(), (vaddr_t)sta_add, &tmp);

#if 0
	/* high 8 bit */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg + 0x8, \
		(uint32_t)(((uint64_t)0 & 0xff00000000ULL) >> 32));
#endif

	/* low 32 bits, last bit must be 0 */
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg + 0x4, \
		(uint32_t)((uint64_t)0ULL & 0xfffffffeULL));
	printf("hi %X\n", bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg + 0x8));
	printf("lo %X\n", bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg + 0x4));


	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg + 0x4, \
		(uint32_t)((uint64_t)tmp & 0xfffffffeULL));
	printf("hi %X\n", bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg + 0x8));
	printf("lo %X\n", bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg + 0x4));

	printf("2 ccu reset register: %u\n", 
		bus_space_read_4(sc->sc_iot, sc->sc_ccu_ioh, AW_D1_RISCV_RESET_REG));


	/* reset release */
	reg = AW_D1_RISCV_RESET_METHOD_3;
	bus_space_write_4(sc->sc_iot, sc->sc_ccu_ioh, AW_D1_RISCV_RESET_REG \
		, (reg));

	printf("3 ccu reset register: %u\n", 
		bus_space_read_4(sc->sc_iot, sc->sc_ccu_ioh, AW_D1_RISCV_RESET_REG));

	/* hook itself */
	aw_d1_intc_intr_establish(IRQ_EXTERNAL_SUPERVISOR, 0, \
		aw_d1_intc_irq_handler, NULL, "aw_d1_intc");

	csr_set(sie, SIE_SEIE);

}
	
int
aw_d1_intc_irq_handler(void *frame)
{
	int i, j;
	uint32_t getirq, reg;
	uint8_t irq;
	struct aw_d1_intrhand *ih;
	struct aw_d1_intc_softc *sc = aw_d1;

/*	KASSERTMSG(_frame->tf_scause & EXCP_INTR, 
			"aw_d1_cpu_intr: wrong frame passed"); */

#if AW_D1_DEBUG_INTC
	printf("\n%s: in handler\n", __func__);
#endif

	reg = AW_D1_IRQ_MODE_OFFSET;
	for (i = 0; i < 5; i++) {
		getirq = bus_space_read_4(sc->sc_iot, sc->sc_ioh, reg + (i << 2));	
		if (! getirq)
			continue;

		for (j = 0; j < 32; j++) {
			if ((getirq >> j) & 0x1) {
				irq = (i << 6) + j;	 /* i * 32 + j */

#if AW_D1_DEBUG_INTC
				printf("%s: irq %d fired (%s)\n", __func__, irq,
					(ih->ih_name != NULL) ? 
					ih->ih_name : "unknown");
#endif
				ih = aw_d1_intc_handler[irq];
				if (plic_irq_dispatch(irq, frame) < 0) {
#if AW_D1_DEBUG_INTC
				if (ih->ih_name != NULL)
                			printf("%s: failed handling irq %d for "
						"%s\n", __func__, irq, 
						ih->ih_name);
				else
                			printf("%s: failed handling irq %d for "
						"(unknown)\n", __func__, irq);
#endif

				}

			}
		}

		/* reset 32 irq bits all at once after handling 32 bits */
		bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg + (i << 2), 0);	
	}

	return 0;
}


void *
aw_d1_intc_intr_establish(uint8_t irqno, int dummy_level, int (*func)(void *),
	void *arg, char *name)
{
	struct aw_d1_intc_softc *sc = aw_d1;
	struct aw_d1_intrhand *ih;
	u_long sie;

	if (irqno > AW_D1_IRQ)
		panic("%s:  bogus irqnumber%d: %s\n", __func__, irqno, name);
	
	ih = malloc(sizeof(*ih), M_DEVBUF, M_WAITOK);
	ih->ih_func = func;
	ih->ih_arg = arg;
	ih->ih_irq = irqno;
	ih->ih_name = name;

	sie = intr_disable();
	aw_d1_intc_handler[irqno] = ih;
	printf("\n%s: irq: %u established\n", __func__, irqno);
	intr_restore(sie);
	aw_d1_intc_wake_irq(sc, irqno);

	return (ih);
}

void
aw_d1_intc_intr_disestablish(void *cookie)
{
	struct aw_d1_intrhand *ih = cookie;
	uint8_t irqno = ih->ih_irq;
	u_long sie;

	sie = intr_disable();
	aw_d1_intc_handler[irqno] = NULL;
	intr_restore(sie);

	free(ih, M_DEVBUF, 0);
}

uint32_t
aw_d1_intc_intr_get_parent(int node)
{
	uint32_t phandle = 0;

	while (node && !phandle) {
		phandle = OF_getpropint(node, "interrupt-parent", 0);
		node = OF_parent(node);
	}

	return phandle;
}

void
aw_d1_intc_wake_irq(void *cookie, int irq)
{
	struct aw_d1_intc_softc *sc = (struct aw_d1_intc_softc *)cookie;
	uint32_t reg;

	reg = AW_D1_WAKEUP_MASK_OFFSET;
	bus_space_write_4(sc->sc_iot, sc->sc_ioh, reg + (irq / 32), (irq % 32));	
}
