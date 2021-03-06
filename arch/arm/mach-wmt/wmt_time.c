/*++
linux/arch/arm/mach-wmt/timer.c

Copyright (c) 2008  WonderMedia Technologies, Inc.

This program is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software Foundation,
either version 2 of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
PARTICULAR PURPOSE.  See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with
this program.  If not, see <http://www.gnu.org/licenses/>.

WonderMedia Technologies, Inc.
10F, 529, Chung-Cheng Road, Hsin-Tien, Taipei 231, R.O.C.
--*/

#include <linux/io.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/clocksource.h>
#include <linux/clockchips.h>
#include <linux/delay.h>

#include <asm/mach/time.h>
#include <asm/sched_clock.h>
#include <mach/hardware.h>

//#define DEBUG
#ifdef  DEBUG
#define fq_dbg(fmt, args...)  printk(KERN_ERR "[%s]_%d: " fmt, __func__ , __LINE__, ## args)
#define fq_trace()  printk(KERN_ERR "trace in %s %d\n", __func__, __LINE__)
#else
#define fq_dbg(fmt, args...)
#define fq_trace()
#endif

#define MIN_OSCR_DELTA  16
#define WMT_CLOCK_TICK_RATE 3000000

/* Clear OS Timer1 irq */
static inline void wmt_os_timer_clear_irq(void)
{
    OSTS_VAL = OSTS_M1;
}

/* disable OS Timer1 irq */
static inline void wmt_os_timer_disable_irq(void)
{
    OSTI_VAL &= ~OSTI_E1;
}

/* Enable OS timer1 irq */
static inline void wmt_os_timer_enable_irq(void)
{
	OSTI_VAL |= OSTI_E1;
}

/* Stop ostimer, counter stop */
static inline void wmt_os_timer_freeze_counter(void)
{
	OSTC_VAL = 0;
}

/* Let OS Timer free run, counter increase now */
static inline void wmt_os_timer_restart_counter(void)
{
	OSTC_VAL = OSTC_ENABLE;
}

static inline void wmt_os_timer_set_counter(u32 new_cnt)
{
    OSCR_VAL = new_cnt;
}

static inline u32 wmt_os_timer_read_counter(void)
{
    OSTC_VAL |= OSTC_RDREQ;
	while (OSTA_VAL & OSTA_RCA)
		;

    return (u32)OSCR_VAL;
}

static inline void wmt_os_timer_set_match(u32 new_match)
{
	/* check if can write OS Timer1 match register nows */
	while (OSTA_VAL & OSTA_MWA1)
		;

	OSM1_VAL = new_match;
}

/* OS timer hardware initializing routine */
/* TODO: Here we let os timer run, but disable interrupt, 
         when clcokevent registed, then enable interrupt
*/
static void __init wmt_os_timer_init(void)
{
    wmt_os_timer_disable_irq();
    wmt_os_timer_clear_irq();
    wmt_os_timer_freeze_counter();
    wmt_os_timer_set_match(0);
    wmt_os_timer_restart_counter();
}

/* for schedule clock */
static DEFINE_CLOCK_DATA(wmt_cd);

unsigned long long notrace sched_clock(void)
{
	u32 cyc = wmt_os_timer_read_counter();
	return cyc_to_sched_clock(&wmt_cd, cyc, (u32)~0);
}

static void notrace wmt_update_sched_clock(void)
{
	u32 cyc = wmt_os_timer_read_counter();
	update_sched_clock(&wmt_cd, cyc, (u32)~0);
}

/* schedule clcok init and register */
static void __init wmt_sched_clock_init(void)
{
	init_sched_clock(&wmt_cd, wmt_update_sched_clock, 32, WMT_CLOCK_TICK_RATE);
}

/* for clocksource */
static cycle_t wmt_timer_read_cycles(struct clocksource *cs)
{
    return (cycle_t)wmt_os_timer_read_counter();
}

struct clocksource wmt_clocksource = {
	.name           = "wmt_clocksource",
	.rating         = 200,
	.read           = wmt_timer_read_cycles,
	.mask           = CLOCKSOURCE_MASK(32),
	.flags          = CLOCK_SOURCE_IS_CONTINUOUS,
};

static void __init wmt_clocksource_init(struct clocksource *cs)
{
	clocksource_register_hz(cs, WMT_CLOCK_TICK_RATE);
    fq_dbg("%s mult:%u, shift:%u, max_idle_ns:%llu\n\n", cs->name,
                                cs->mult, cs->shift, cs->max_idle_ns);
}

static int
wmt_timer_set_next_event(unsigned long cycles, struct clock_event_device *evt)
{
    unsigned long next = 0;
    unsigned long oscr = 0;

	oscr = wmt_os_timer_read_counter();
	next = oscr + cycles;
	/* set new value to os time1 match register   */
    wmt_os_timer_set_match(next);
    /* Enable match on timer 1 to cause interrupts. */
	wmt_os_timer_enable_irq();

	return 0;
}

static void
wmt_timer_set_mode(enum clock_event_mode mode, struct clock_event_device *evt)
{
    switch (mode) {
        case CLOCK_EVT_MODE_UNUSED:
	    case CLOCK_EVT_MODE_SHUTDOWN:
	    case CLOCK_EVT_MODE_ONESHOT:
            /* disable OS Timer irq here */
            wmt_os_timer_disable_irq();
            /* Clear match on OS Timer 1 */
            wmt_os_timer_clear_irq();
            break;
	    case CLOCK_EVT_MODE_RESUME:
        case CLOCK_EVT_MODE_PERIODIC:    
		break;
	}

    return ;
}

struct clock_event_device wmt_clockevent = {
	.name           = "wmt_clockevent",
	.features       = CLOCK_EVT_FEAT_ONESHOT,
	.rating         = 200,
	.set_next_event = wmt_timer_set_next_event,
	.set_mode       = wmt_timer_set_mode,
    .shift          = 32,
};

static irqreturn_t wmt_timer_interrupt(int irq, void *dev_id)
{
	struct clock_event_device *evt = dev_id;

    /* Clear match on OS Timer 1 irq */
    wmt_os_timer_clear_irq();
	evt->event_handler(evt);

	return IRQ_HANDLED;
}

struct irqaction wmt_timer_irq = {
	.name    = "wmt_timer",
	.flags   = IRQF_DISABLED | IRQF_TIMER | IRQF_IRQPOLL,
	.handler = wmt_timer_interrupt,
	.dev_id  = &wmt_clockevent,
};

static void __init wmt_clockevent_init(struct clock_event_device *evt,
                                    unsigned int irq, struct irqaction *act)
{
    /* reset some elements for clockevent */
    evt->cpumask = cpumask_of(smp_processor_id());
    evt->mult = div_sc(WMT_CLOCK_TICK_RATE, NSEC_PER_SEC, evt->shift);
	evt->max_delta_ns = clockevent_delta2ns(0x7fffffff, evt);
	evt->min_delta_ns = clockevent_delta2ns(MIN_OSCR_DELTA, evt);
    fq_dbg("%s, mult:%u, shift:%d, max_delta:%llu, min_delta:%llu\n\n\n",
       evt->name, evt->mult, evt->shift, evt->max_delta_ns, evt->min_delta_ns);

    /* setup os timer1 irq */
    if (setup_irq(irq, act))
        printk(KERN_ERR "setup clockevent %s irq %u, failed!\n\n", evt->name, irq);

    /* register clockevent */
	clockevents_register_device(evt);

    return ;
}

static void __init wmt_timer_init(void)
{
    /* prepare OS timer hardware, irq disabled  */
    wmt_os_timer_init();
    /* os timer1 as clocksourece                */
    wmt_clocksource_init(&wmt_clocksource);
    /* sched_clock for timestamp, as printk.... */
    wmt_sched_clock_init();
    /* os timer1 as clockevent device           */
    wmt_clockevent_init(&wmt_clockevent, IRQ_OST1, &wmt_timer_irq);

    /* this is a MUST operation */    
    wmt_os_timer_enable_irq();

    return ;
}

struct sys_timer wmt_timer = {
	.init = wmt_timer_init,
};


/* below code is for old design compatibility */
inline unsigned int wmt_read_oscr(void)
{
    return wmt_os_timer_read_counter();
}
EXPORT_SYMBOL(wmt_read_oscr);

int wmt_rtc_on = 1;
static int __init no_rtc(char *str)
{
	wmt_rtc_on = 0;
	return 1;
}
__setup("nortc", no_rtc);


static void wmt_rtc_init(void)
{
    fq_dbg("Enter\n");

	RTCC_VAL = (RTCC_ENA|RTCC_INTTYPE);
	if (!(RTSR_VAL&RTSR_VAILD))
		while (!(RTSR_VAL&RTSR_VAILD))
			;
    /* Reset RTC alarm settings */
	//RTAS_VAL = 0;
    /* Reset RTC test mode. */
	RTTM_VAL = 0;

	/* Patch 1, RTCD default value isn't 0x41 and it will not sync with RTDS. */
	if (RTCD_VAL == 0) {
		while (RTWS_VAL & RTWS_DATESET)
			;
		RTDS_VAL = RTDS_VAL;
	}

	/*
	 * Disable all RTC control functions.
	 * Set to 24-hr format and update type to each second.
	 * Disable sec/min update interrupt.
	 * Let RTC free run without interrupts.
	 */
	/* RTCC_VAL &= ~(RTCC_12HR | RTCC_INTENA); */
	/* RTCC_VAL |= RTCC_ENA | RTCC_INTTYPE; */
	RTCC_VAL = (RTCC_ENA|RTCC_INTTYPE);

	if (RTWS_VAL & RTWS_CONTROL) {
		while (RTWS_VAL & RTWS_CONTROL)
			;
	}

    fq_dbg("Exit\n\n\n");
    return ;
}

void wmt_read_rtc(unsigned int *date, unsigned int *time)
{
    /* before read rtc, we should check if rtc already be initialized */
    if (((RTCC_VAL & RTCC_ENA) == 0) || ((RTSR_VAL & RTSR_VAILD) == 0)) {
        wmt_rtc_init();
    }

	if (!(RTSR_VAL & RTSR_VAILD))
		while (!(RTSR_VAL & RTSR_VAILD))
			;

	if (RTWS_VAL & RTWS_DATESET)
		while (RTWS_VAL & RTWS_DATESET)
			;

	*date = RTCD_VAL;

	if (RTWS_VAL & RTWS_TIMESET)
		while (RTWS_VAL & RTWS_TIMESET)
			;

	*time = RTCT_VAL;
}
EXPORT_SYMBOL(wmt_read_rtc);

/*
 * get current Gregorian date from RTC, 
 * coverts to seconds since 1970-01-01 00:00:00
 */
static unsigned long wmt_get_rtc_time(void)
{
	unsigned int date = 0;
    unsigned int time = 0;

    /* if no rtc or rtc disabled, return 0,
     * means timekeeping is 1970-01-01 00:00:00 when system booting */
    if (!wmt_rtc_on)
        return 0;

    wmt_read_rtc(&date, &time);
	return mktime(RTCD_YEAR(date) + ((RTCD_CENT(date) * 100) + 100),
				  RTCD_MON(date), RTCD_MDAY(date),RTCT_HOUR(time),
				  RTCT_MIN(time), RTCT_SEC(time));
}

void read_persistent_clock(struct timespec *ts)
{
    ts->tv_nsec = 0;
    ts->tv_sec  = (__kernel_time_t)wmt_get_rtc_time;

    fq_dbg("seconds read from RTC is %lu\n\n\n", ts->tv_sec);
    return ;  
}
