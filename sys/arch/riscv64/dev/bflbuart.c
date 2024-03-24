/*	$OpenBSD$ */
/*
 * Copyright (c) 2024 Peter J. Philipp <pjp@delphinusdns.org>
 * Copyright (c) 2019 Mark Kettenis <kettenis@openbsd.org>
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

/*
 * based on sfuart.c
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/fcntl.h>
#include <sys/proc.h>
#include <sys/systm.h>
#include <sys/tty.h>

#include <machine/bus.h>
#include <machine/fdt.h>

#include <dev/cons.h>

#include <dev/ofw/fdt.h>
#include <dev/ofw/openfirm.h>
#include <dev/ofw/ofw_clock.h>

#define BFLB_UART_TXDATA		0x0
#define  BFLB_UART_TX_ENA		(1 << 0)
#define  BFLB_UART_TX_CTS		(1 << 1)
#define  BFLB_UART_TX_FRM		(1 << 2)
#define  BFLB_UART_TX_PRT		(1 << 4)
#define  BFLB_UART_TX_PRT_SEL		(1 << 5)
#define  BFLB_UART_TX_CNT_D_SHIFT	8
#define  BFLB_UART_TX_CNT_D_MASK	(7 << 8)
#define  BFLB_UART_TX_CNT_P_SHIFT	10
#define  BFLB_UART_TX_CNT_P_MASK	(3 << 10)
#define BFLB_UART_RXDATA		0x4
#define  BFLB_UART_RX_ENA		(1 << 0)
#define  BFLB_UART_RX_PRT		(1 << 4)
#define  BFLB_UART_RX_PRT_SEL		(1 << 5)
#define  BFLB_UART_RX_CNT_D_SHIFT	8
#define  BFLB_UART_RX_CNT_D_MASK	(7 << 8)
#define BFLB_UART_PRD			0x8
#define  BFLB_UART_PRD_TX_MASK		0xffffU
#define  BFLB_UART_PRD_RX_MASK		0xffff0000U
#define BFLB_UART_DATA_CONFIG		0xc
#define  BFLB_UART_UART_BIT_INVERSE	(1 << 0)
#define BFLB_UART_RX_RTO_TIMER		0x18
#define  BFLB_UART_RX_RTO_MASK		0x000000ffU
#define BFLB_UART_SW_MODE		0x1c
#define BFLB_UART_INT_STS		0x20
#define  BFLB_UART_INT_TX_END		(1 << 0)
#define  BFLB_UART_INT_RX_END		(1 << 1)
#define  BFLB_UART_INT_TX_FIFO		(1 << 2)
#define  BFLB_UART_INT_RX_FIFO		(1 << 3)
#define  BFLB_UART_INT_RTO		(1 << 4)
#define  BFLB_UART_INT_PCE		(1 << 5)
#define  BFLB_UART_INT_TX_FER		(1 << 6)
#define  BFLB_UART_INT_RX_FER		(1 << 7)
#define  BFLB_UART_INT_RX_LSE		(1 << 8)
#define BFLB_UART_INT_MASK		0x24
#define BFLB_UART_INT_CLR		0x28
#define BFLB_UART_INT_ENA		0x2c
#define BFLB_UART_STATUS		0x30
#define  BFLB_UART_STATUS_TX_BUSY	(1 << 0)
#define BFLB_UART_FIFO_CONF0		0x80
#define  BFLB_UART_FIFO_DMA_TX_ENA	(1 << 0)
#define  BFLB_UART_FIFO_DMA_RX_ENA	(1 << 1)
#define  BFLB_UART_FIFO_TX_CLR		(1 << 2)
#define  BFLB_UART_FIFO_RX_CLR		(1 << 3)
#define  BFLB_UART_FIFO_TX_OVERFLOW	(1 << 4)
#define  BFLB_UART_FIFO_TX_UNDERFLOW	(1 << 5)
#define  BFLB_UART_FIFO_RX_OVERFLOW	(1 << 6)
#define  BFLB_UART_FIFO_RX_UNDERFLOW	(1 << 7)
#define BFLB_UART_FIFO_CONF1		0x84
#define  BFLB_UART_FIFO_TX_CNT_SHIFT	0
#define  BFLB_UART_FIFO_TX_CNT_MASK	0x3f
#define  BFLB_UART_FIFO_RX_CNT_SHIFT	8
#define  BFLB_UART_FIFO_RX_CNT_MASK	(0x3f << 8)
#define  BFLB_UART_FIFO_TX_TH_SHIFT	16
#define  BFLB_UART_FIFO_TX_TH_MASK	(0x1f << 16)
#define  BFLB_UART_FIFO_RX_TH_SHIFT	24	
#define  BFLB_UART_FIFO_RX_TH_MASK	(0x1f << 24)
#define BFLB_UART_FIFO_WDATA		0x88
#define  BFLB_UART_FIFO_WDATA_MASK	0xff
#define BFLB_UART_FIFO_RDATA		0x8c
#define  BFLB_UART_FIFO_RDATA_MASK	0xff

#define BFLB_UART_MAXPORTS		8
#define BFLB_UART_BAUD			115200
#define BFLB_UART_FIFO_RX_TH		7

#define BFLB_UART_SPACE			100
#define BFLB_UART_FIFO_SIZE		32


#define HREAD4(sc, reg)							\
	(bus_space_read_4((sc)->sc_iot, (sc)->sc_ioh, (reg)))
#define HWRITE4(sc, reg, val)						\
	bus_space_write_4((sc)->sc_iot, (sc)->sc_ioh, (reg), (val))
#define HSET4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) | (bits))
#define HCLR4(sc, reg, bits)						\
	HWRITE4((sc), (reg), HREAD4((sc), (reg)) & ~(bits))

#define BFLB_TXDATA_FULL(sc) \
	((HREAD4(sc, BFLB_UART_FIFO_CONF1) & BFLB_UART_FIFO_TX_CNT_MASK) != 0)

cdev_decl(com);
cdev_decl(bflbuart);

#define DEVUNIT(x)	(minor(x) & 0x7f)
#define DEVCUA(x)	(minor(x) & 0x80)

struct cdevsw bflbuartdev = cdev_tty_init(2, bflbuart);

struct bflbuart_softc {
	struct device		sc_dev;
	bus_space_tag_t		sc_iot;
	bus_space_handle_t	sc_ioh;

	uint32_t		sc_frequency;

	struct soft_intrhand	*sc_si;
	void			*sc_ih;

	struct tty		*sc_tty;
	int			sc_conspeed;
	int			sc_floods;
	int			sc_overflows;
	int			sc_halt;
	int			sc_cua;
	int	 		*sc_ibuf, *sc_ibufp, *sc_ibufhigh, *sc_ibufend;
#define BFLB_UART_IBUFSIZE	128
#define BFLB_UART_IHIGHWATER	100
	int			sc_ibufs[2][BFLB_UART_IBUFSIZE];
};

int	bflbuart_match(struct device *, void *, void *);
void	bflbuart_attach(struct device *, struct device *, void *);

struct cfdriver bflbuart_cd = {
	NULL, "bflbuart", DV_TTY
};

const struct cfattach bflbuart_ca = {
	sizeof(struct bflbuart_softc), bflbuart_match, bflbuart_attach
};

bus_space_tag_t	bflbuartconsiot;
bus_space_handle_t bflbuartconsioh;

struct bflbuart_softc *bflbuart_sc(dev_t);

int	bflbuart_intr(void *);
void	bflbuart_softintr(void *);
void	bflbuart_start(struct tty *);

int	bflbuartcnattach(bus_space_tag_t, bus_addr_t);
int	bflbuartcngetc(dev_t);
void	bflbuartcnputc(dev_t, int);
void	bflbuartcnpollc(dev_t, int);

void
bflbuart_init_cons(void)
{
	struct fdt_reg reg;
	void *node;

	if ((node = fdt_find_cons("bflb,bl808-uart")) == NULL)
		return;
	if (fdt_get_reg(node, 0, &reg))
		return;

	bflbuartcnattach(fdt_cons_bs_tag, reg.addr);
}

int
bflbuart_match(struct device *parent, void *match, void *aux)
{
	struct fdt_attach_args *faa = aux;
	char status[OFMAXPARAM];

	if ((OF_getprop(faa->fa_node, "status", status, sizeof(status)) > 0) && 
		strcmp(status, "disabled")) {
		return 0;
	}

	return (OF_is_compatible(faa->fa_node, "bflb,bl808-uart"));
}

void
bflbuart_attach(struct device *parent, struct device *self, void *aux)
{
	struct bflbuart_softc *sc = (struct bflbuart_softc *)self;
	struct fdt_attach_args *faa = aux;
	int maj;

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

	sc->sc_frequency = clock_get_frequency(faa->fa_node, NULL);
	if (faa->fa_node == stdout_node) {
		/* Locate the major number. */
		for (maj = 0; maj < nchrdev; maj++)
			if (cdevsw[maj].d_open == bflbuartopen)
				break;
		cn_tab->cn_dev = makedev(maj, sc->sc_dev.dv_unit);
		sc->sc_conspeed = stdout_speed;
		printf(": console");
	}

	sc->sc_si = softintr_establish(IPL_TTY, bflbuart_softintr, sc);
	if (sc->sc_si == NULL) {
		printf(": can't establish soft interrupt\n");
		return;
	}

	sc->sc_ih = fdt_intr_establish_idx(faa->fa_node, 0, IPL_TTY,
	    bflbuart_intr, sc, sc->sc_dev.dv_xname);
	if (sc->sc_ih == NULL) {
		printf(": can't establish hard interrupt\n");
		return;
	}

	printf("\n");
}

int
bflbuart_intr(void *arg)
{
	struct bflbuart_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	int *p;
	uint32_t val;
	int c, handled = 0;

	if (tp == NULL)
		return 0;

	if (BFLB_TXDATA_FULL(sc) && ISSET(tp->t_state, TS_BUSY)) {
		CLR(tp->t_state, TS_BUSY | TS_FLUSH);
		if (sc->sc_halt > 0)
			wakeup(&tp->t_outq);
		(*linesw[tp->t_line].l_start)(tp);
		handled = 1;
	}

	p = sc->sc_ibufp;
	val = HREAD4(sc, BFLB_UART_FIFO_CONF1);
	while ((val & BFLB_UART_FIFO_RX_CNT_MASK) != 0) {
		c = HREAD4(sc, BFLB_UART_FIFO_RDATA) & 0xff;

		if (p >= sc->sc_ibufend)
			sc->sc_floods++;
		else
			*p++ = c;

		val = HREAD4(sc, BFLB_UART_RXDATA);
		handled = 1;
	}
	if (sc->sc_ibufp != p) {
		sc->sc_ibufp = p;
		softintr_schedule(sc->sc_si);
	}

	return handled;
}

void
bflbuart_softintr(void *arg)
{
	struct bflbuart_softc *sc = arg;
	struct tty *tp = sc->sc_tty;
	int *ibufp, *ibufend;
	int s;

	if (sc->sc_ibufp == sc->sc_ibuf)
		return;

	s = spltty();

	ibufp = sc->sc_ibuf;
	ibufend = sc->sc_ibufp;

	if (ibufp == ibufend) {
		splx(s);
		return;
	}

	sc->sc_ibufp = sc->sc_ibuf = (ibufp == sc->sc_ibufs[0]) ?
	    sc->sc_ibufs[1] : sc->sc_ibufs[0];
	sc->sc_ibufhigh = sc->sc_ibuf + BFLB_UART_IHIGHWATER;
	sc->sc_ibufend = sc->sc_ibuf + BFLB_UART_IBUFSIZE;

	if (tp == NULL || !ISSET(tp->t_state, TS_ISOPEN)) {
		splx(s);
		return;
	}

	splx(s);

	while (ibufp < ibufend) {
		int i = *ibufp++;
#ifdef DDB
		if (tp->t_dev == cn_tab->cn_dev) {
			int j = db_rint(i);

			if (j == 1)	/* Escape received, skip */
				continue;
			if (j == 2)	/* Second char wasn't 'D' */
				(*linesw[tp->t_line].l_rint)(27, tp);
		}
#endif
		(*linesw[tp->t_line].l_rint)(i, tp);
	}
}

int
bflbuart_param(struct tty *tp, struct termios *t)
{
	struct bflbuart_softc *sc = bflbuart_sc(tp->t_dev);
	int ospeed = t->c_ospeed;
#if 0
	uint32_t div;
#endif

	/* Check requested parameters. */
	if (ospeed < 0 || (t->c_ispeed && t->c_ispeed != t->c_ospeed))
		return EINVAL;

	switch (ISSET(t->c_cflag, CSIZE)) {
	case CS5:
	case CS6:
	case CS7:
		return EINVAL;
	case CS8:
		break;
	}

	if (ospeed != 0) {
		while (ISSET(tp->t_state, TS_BUSY)) {
			int error;

			sc->sc_halt++;
			error = ttysleep(tp, &tp->t_outq,
			    TTOPRI | PCATCH, "bflbuprm");	 /* pjp ??? */
			sc->sc_halt--;
			if (error) {
				bflbuart_start(tp);
				return error;
			}
		}

#if LETITAUTOBAUD 
		div = (sc->sc_frequency + ospeed / 2) / ospeed;
		if (div < 16 || div > 65536)
			return EINVAL;
		HWRITE4(sc, UART_DIV, div - 1);
#endif
	}

	tp->t_ispeed = t->c_ispeed;
	tp->t_ospeed = t->c_ospeed;
	tp->t_cflag = t->c_cflag;

	/* Just to be sure... */
	bflbuart_start(tp);
	return 0;
}

void
bflbuart_start(struct tty *tp)
{
	struct bflbuart_softc *sc = bflbuart_sc(tp->t_dev);
	int s;

	s = spltty();
	if (ISSET(tp->t_state, TS_BUSY))
		goto out;
	if (ISSET(tp->t_state, TS_TIMEOUT | TS_TTSTOP) || sc->sc_halt > 0)
		goto out;
	ttwakeupwr(tp);
	if (tp->t_outq.c_cc == 0) {
		HCLR4(sc, BFLB_UART_INT_STS, BFLB_UART_INT_TX_FIFO);
		goto out;
	}
	SET(tp->t_state, TS_BUSY);

	while (! BFLB_TXDATA_FULL(sc)  && tp->t_outq.c_cc != 0) {
		HWRITE4(sc, BFLB_UART_FIFO_WDATA, getc(&tp->t_outq));
	}
	HSET4(sc, BFLB_UART_INT_STS, BFLB_UART_INT_TX_FIFO);
out:
	splx(s);
}

int
bflbuartopen(dev_t dev, int flag, int mode, struct proc *p)
{
	struct bflbuart_softc *sc = bflbuart_sc(dev);
	struct tty *tp;
	int error;
	int s;

	if (sc == NULL)
		return ENXIO;

	s = spltty();
	if (sc->sc_tty == NULL)
		tp = sc->sc_tty = ttymalloc(0);
	else
		tp = sc->sc_tty;
	splx(s);

	tp->t_oproc = bflbuart_start;
	tp->t_param = bflbuart_param;
	tp->t_dev = dev;

	if (!ISSET(tp->t_state, TS_ISOPEN)) {
		SET(tp->t_state, TS_WOPEN);
		ttychars(tp);
		tp->t_iflag = TTYDEF_IFLAG;
		tp->t_oflag = TTYDEF_OFLAG;
		tp->t_cflag = TTYDEF_CFLAG;
		tp->t_lflag = TTYDEF_LFLAG;
		tp->t_ispeed = tp->t_ospeed =
		    sc->sc_conspeed ? sc->sc_conspeed : B115200;

		s = spltty();

		bflbuart_param(tp, &tp->t_termios);
		ttsetwater(tp);

		sc->sc_ibufp = sc->sc_ibuf = sc->sc_ibufs[0];
		sc->sc_ibufhigh = sc->sc_ibuf + BFLB_UART_IHIGHWATER;
		sc->sc_ibufend = sc->sc_ibuf + BFLB_UART_IBUFSIZE;

		/* Enable transmit. */
		HSET4(sc, BFLB_UART_TXDATA, BFLB_UART_TX_ENA);

		/* Enable receive. */
		HSET4(sc, BFLB_UART_RXDATA, BFLB_UART_RX_ENA);

		/* Enable interrupts. */
		HSET4(sc, BFLB_UART_INT_STS, BFLB_UART_INT_RX_FIFO);

		/* No carrier detect support. */
		SET(tp->t_state, TS_CARR_ON);
	} else if (ISSET(tp->t_state, TS_XCLUDE) && suser(p) != 0)
		return EBUSY;
	else
		s = spltty();

	if (DEVCUA(dev)) {
		if (ISSET(tp->t_state, TS_ISOPEN)) {
			/* Ah, but someone already is dialed in... */
			splx(s);
			return EBUSY;
		}
		sc->sc_cua = 1;		/* We go into CUA mode. */
	} else {
		if (ISSET(flag, O_NONBLOCK) && sc->sc_cua) {
			/* Opening TTY non-blocking... but the CUA is busy. */
			splx(s);
			return EBUSY;
		} else {
			while (sc->sc_cua) {
				SET(tp->t_state, TS_WOPEN);
				error = ttysleep(tp, &tp->t_rawq,
				    TTIPRI | PCATCH, ttopen);
				/*
				 * If TS_WOPEN has been reset, that means the
				 * cua device has been closed.
				 * We don't want to fail in that case,
				 * so just go around again.
				 */
				if (error && ISSET(tp->t_state, TS_WOPEN)) {
					CLR(tp->t_state, TS_WOPEN);
					splx(s);
					return error;
				}
			}
		}
	}
	splx(s);

	return (*linesw[tp->t_line].l_open)(dev, tp, p);
}

int
bflbuartclose(dev_t dev, int flag, int mode, struct proc *p)
{
	struct bflbuart_softc *sc = bflbuart_sc(dev);
	struct tty *tp = sc->sc_tty;
	int s;

	if (!ISSET(tp->t_state, TS_ISOPEN))
		return 0;

	(*linesw[tp->t_line].l_close)(tp, flag, p);
	s = spltty();
	if (!ISSET(tp->t_state, TS_WOPEN)) {
		/* Disable interrupts */
		HCLR4(sc, BFLB_UART_INT_STS, BFLB_UART_INT_RX_FIFO | \
			BFLB_UART_INT_TX_FIFO);
	}
	CLR(tp->t_state, TS_BUSY | TS_FLUSH);
	sc->sc_cua = 0;
	splx(s);
	ttyclose(tp);

	return 0;
}

int
bflbuartread(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = bflbuarttty(dev);

	if (tp == NULL)
		return ENODEV;

	return (*linesw[tp->t_line].l_read)(tp, uio, flag);
}

int
bflbuartwrite(dev_t dev, struct uio *uio, int flag)
{
	struct tty *tp = bflbuarttty(dev);

	if (tp == NULL)
		return ENODEV;

	return (*linesw[tp->t_line].l_write)(tp, uio, flag);
}

int
bflbuartioctl(dev_t dev, u_long cmd, caddr_t data, int flag, struct proc *p)
{
	struct bflbuart_softc *sc = bflbuart_sc(dev);
	struct tty *tp;
	int error;

	if (sc == NULL)
		return ENODEV;

	tp = sc->sc_tty;
	if (tp == NULL)
		return ENXIO;

	error = (*linesw[tp->t_line].l_ioctl)(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;

	error = ttioctl(tp, cmd, data, flag, p);
	if (error >= 0)
		return error;

	switch(cmd) {
	case TIOCSBRK:
	case TIOCCBRK:
	case TIOCSDTR:
	case TIOCCDTR:
	case TIOCMSET:
	case TIOCMBIS:
	case TIOCMBIC:
	case TIOCMGET:
	case TIOCGFLAGS:
		break;
	case TIOCSFLAGS:
		error = suser(p);
		if (error != 0)
			return EPERM;
		break;
	default:
		return ENOTTY;
	}

	return 0;
}

int
bflbuartstop(struct tty *tp, int flag)
{
	return 0;
}

struct tty *
bflbuarttty(dev_t dev)
{
	struct bflbuart_softc *sc = bflbuart_sc(dev);

	if (sc == NULL)
		return NULL;
	return sc->sc_tty;
}

struct bflbuart_softc *
bflbuart_sc(dev_t dev)
{
	int unit = DEVUNIT(dev);

	if (unit >= bflbuart_cd.cd_ndevs)
		return NULL;
	return (struct bflbuart_softc *)bflbuart_cd.cd_devs[unit];
}

int
bflbuartcnattach(bus_space_tag_t iot, bus_addr_t iobase)
{
	static struct consdev bflbuartcons = {
		NULL, NULL, bflbuartcngetc, bflbuartcnputc, bflbuartcnpollc, 
		NULL, NODEV, CN_MIDPRI
	};
	int maj;

	bflbuartconsiot = iot;
	if (bus_space_map(iot, iobase, BFLB_UART_SPACE, 0, &bflbuartconsioh))
		return ENOMEM;

	/* Look for major of com(4) to replace. */
	for (maj = 0; maj < nchrdev; maj++)
		if (cdevsw[maj].d_open == comopen)
			break;
	if (maj == nchrdev)
		return ENXIO;

	cn_tab = &bflbuartcons;
	cn_tab->cn_dev = makedev(maj, 0);
	cdevsw[maj] = bflbuartdev; 	/* KLUDGE */

	return 0;
}

int
bflbuartcngetc(dev_t dev)
{
	uint32_t val;

	while (! ((bus_space_read_4(bflbuartconsiot, bflbuartconsioh, \
		BFLB_UART_FIFO_CONF1) & BFLB_UART_FIFO_RX_CNT_MASK)))
		CPU_BUSY_CYCLE();

	val = bus_space_read_4(bflbuartconsiot, bflbuartconsioh, \
				BFLB_UART_FIFO_RDATA);

	return (val & 0xff);
}

void
bflbuartcnputc(dev_t dev, int c)
{
	while (! ((bus_space_read_4(bflbuartconsiot, bflbuartconsioh, \
		BFLB_UART_FIFO_CONF1) & BFLB_UART_FIFO_TX_CNT_MASK)))
		CPU_BUSY_CYCLE();

	bus_space_write_4(bflbuartconsiot, bflbuartconsioh, BFLB_UART_FIFO_WDATA, c);
}

void
bflbuartcnpollc(dev_t dev, int on)
{
}
