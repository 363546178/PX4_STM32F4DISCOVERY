/**
 * @file hc_sr04.cpp
 *
 * HC-SR04 driver.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <stm32.h>
#include <semaphore.h>
#include <nuttx/config.h>
#include <drivers/device/device.h>
#include <drivers/drv_hc_sr04.h>

#define GPIO_TRIG (GPIO_OUTPUT|GPIO_PUSHPULL|GPIO_SPEED_2MHz|GPIO_OUTPUT_SET|GPIO_PORTC|GPIO_PIN6)
#define GPIO_ECHO (GPIO_INPUT|GPIO_FLOAT|GPIO_EXTI|GPIO_PORTC|GPIO_PIN7)
#define SELECT_LISTENER_DELAY 1000000
#define DEMCR_TRCENA    0x01000000

/* Core Debug registers */
#define DEMCR           (*((volatile uint32_t *)0xE000EDFC))
#define DWT_CTRL        (*(volatile uint32_t *)0xe0001000)
#define CYCCNTENA       (1<<0)
#define DWT_CYCCNT      ((volatile uint32_t *)0xE0001004)
#define CPU_CYCLES      *DWT_CYCCNT
__BEGIN_DECLS

__END_DECLS
static inline uint32_t getticks() {
	return CPU_CYCLES;
}
class HC_SR04: device::CDev {
public:
	HC_SR04();
	virtual ~HC_SR04();

	virtual int init();
	virtual ssize_t read(struct file *filp, char *buffer, size_t buflen);
	virtual int ioctl(struct file *filp, int cmd, unsigned long arg);
	void irqeEcho();
	void print_info();
private:

	sem_t sem_isr;
	unsigned int timerend;
	static int irq_handler(int irq, FAR void *context);
};

HC_SR04::HC_SR04() :
		CDev("hc_sr04", HC_SR04_DEVICE_PATH), sem_isr(SEM_INITIALIZER(0)) {
	init();
	/* Enable DWT */
	DEMCR |= DEMCR_TRCENA;
	*DWT_CYCCNT = 0;
	/* Enable CPU cycle counter */
	DWT_CTRL |= CYCCNTENA;
	sem_init(&sem_isr, 0, 0);
}

HC_SR04::~HC_SR04() {
}

int HC_SR04::init() {
	CDev::init();
	stm32_configgpio(GPIO_TRIG);
	stm32_configgpio(GPIO_ECHO);
	stm32_gpiosetevent(GPIO_ECHO, false, true, false, irq_handler);
	stm32_gpiowrite(GPIO_TRIG, true);
	return 0;
}
ssize_t HC_SR04::read(struct file *filp, char *buffer, size_t buflen) {
	struct timespec abstime;
	unsigned int timestart;
	for (;;) {
		stm32_gpiowrite(GPIO_TRIG, true);
		usleep(50000);
		stm32_gpiowrite(GPIO_TRIG, false);

		clock_gettime(CLOCK_REALTIME, &abstime);
		abstime.tv_nsec += 10000000;
		//clock_gettime(CLOCK_REALTIME, &timestart);
		timestart = getticks();
		//sem_init(&sem_isr, 0, 0);
		int ret = sem_timedwait(&sem_isr, &abstime);

		if (ret == ETIMEDOUT) {
			return snprintf(buffer, buflen, "TimeOut\n");
		}
		if (ret == OK) {
			long long timediff = timerend - timestart;

			if (timerend < timestart) {
				timediff += timerend + (INT_MAX - timestart);
			}
			int pv = (int) timediff / 1000;
			if (pv > 12000) {
				usleep(100000);
				continue;
			}
			return snprintf(buffer, buflen, "ALT %u\n", pv);
		}
	}
//return snprintf(buffer, buflen, "Error %u\n", ret);
}

int HC_SR04::ioctl(struct file *filp, int cmd, unsigned long arg) {
	int result = OK;

	switch (cmd) {

	default:
		result = CDev::ioctl(filp, cmd, arg);
	}
	return result;
}

void HC_SR04::print_info() {
}

void HC_SR04::irqeEcho() {
	int svalue;
	timerend = getticks();
	sem_getvalue(&sem_isr, &svalue);
	if (svalue < 0) {
		sem_post(&sem_isr);
	}
}

namespace {
HC_SR04 *gHC_SR04;
}

int HC_SR04::irq_handler(int irq, FAR void *context) {
	irqstate_t flags = irqsave();

	if (gHC_SR04 != nullptr) {
		gHC_SR04->irqeEcho();
	}
	irqrestore(flags);
	return 0;
}

void drv_hc_sr04_start(void) {
	if (gHC_SR04 == nullptr) {
		gHC_SR04 = new HC_SR04;
		if (gHC_SR04 != nullptr)
			gHC_SR04->init();
	}
}

int hc_sr04_main(int argc, char *argv[]) {
	drv_hc_sr04_start();
	return 0;
}
