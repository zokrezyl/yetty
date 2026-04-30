/*
 * yplatform/time.h - Cross-platform time abstraction
 */

#ifndef YETTY_YPLATFORM_TIME_H
#define YETTY_YPLATFORM_TIME_H

#ifdef __cplusplus
extern "C" {
#endif

/* Monotonic clock in seconds (steady, epoch unspecified — differences only). */
double yetty_yplatform_ytime_monotonic_sec(void);

/* Sleep for at least the given number of milliseconds. */
void yetty_yplatform_ytime_sleep_ms(unsigned ms);

#ifdef __cplusplus
}
#endif

#endif /* YETTY_YPLATFORM_TIME_H */
