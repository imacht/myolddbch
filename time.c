#include <dbch.h>


struct dst {
	uint32_t year, start, end;
};


EmberEventControl metroData;

static uint32_t time, ticks, msec;
static int wait;


// placeholders

static void null_metro(uint32_t now) { }
#pragma weak metro = null_metro


// helpers

static int leap_year(int y)
{
	return y % 400 == 0 || (y % 4 == 0 && y % 100) ? 1 : 0;
}

static int month_days(int m, int y)
{
	if (m == 2)
		return leap_year(y) ? 29 : 28;
	if (m >= 8)
		m--;
	return 30 + (m & 1);
}

static uint32_t last_sunday(int day)
{
	do
		day--;
	while (day % 7 != 1);
	return day * 86400;
}

static struct dst get_dst(uint32_t utc)
{
	struct dst r;
	r.year = 2000;
	uint32_t day = utc / 86400, month = 1, t;
	for (t = 0; ; ) { // creep t up on day to find year
		int days = 365 + leap_year(r.year);
		if (day < t + days)
			break;
		t += days;
		r.year++;
	}

	while (month < 4) // 1st April
		t += month_days(month++, r.year);
	r.start = last_sunday(t) + 3600;

	while (month < 11) // 1st November
		t += month_days(month++, r.year);
	r.end = last_sunday(t) + 3600;

	return r;
}


// exports

void time_start(void)
{
	emberEventControlSetActive(metroData);
}

void metroHandler(void)
{
//	utc_t now = time_run(&metroEvent), ok = time_ok();
	uint32_t now = emberAfGetCurrentTimeCallback();
	uint32_t last = metroData.timeToExecute, delay = MILLISECOND_TICKS_PER_SECOND;

	int adjust = wait; // shorten/extend tick until wait is 0
	if (adjust < -4) // we're too slow
		adjust = -4; // limit shorten to ~4mS
	else if (adjust > 4) // we're too fast
		adjust = 4; // limit stretch to ~4mS
	wait -= adjust;
	delay += adjust;

	emEventControlSetDelayMS(&metroData, delay);
	if (last)
		metroData.timeToExecute = last + delay;

	metro(now);
}

void emberAfSetTimeCallback(uint32_t t)
{
	ticks = halCommonGetInt32uMillisecondTick();
	msec = 0;

	if (time == 0 || time - t > 60 || t - time > 60)
		time = t;
	else
		wait = (time - t) * MILLISECOND_TICKS_PER_SECOND; // +ve: we're too fast
}

uint32_t emberAfGetCurrentTimeCallback(void)
{
	uint32_t now = halCommonGetInt32uMillisecondTick(), since = now - ticks + msec;
	time += since / MILLISECOND_TICKS_PER_SECOND;
	msec = since % MILLISECOND_TICKS_PER_SECOND;
	ticks = now;
	return time;
}

uint32_t utc_to_local(uint32_t utc)
{
	struct dst dst = get_dst(utc);
	return utc >= dst.start && utc < dst.end ? utc + 3600 : utc;
}

uint32_t local_to_utc(uint32_t t)
{
	struct dst dst = get_dst(t);
	return t >= dst.start + 3600 && t < dst.end + 3600 ? t - 3600 : t;
}

int day_of_week(uint32_t utc)
{
	return (utc / 86400 + 5) % 7; // day of week (1st Jan 2000 = Saturday, 0=Monday)
}
