#include <dbch.h>


typedef struct {
	ZigBeeDate_t Date;
	uint8_t Ref;
} DateRef_t;


// helpers

static struct calendar* find_id(struct calendar *p, uint32_t ProviderID, uint32_t IssuerCalendarID)
{
	while (p && p->IssuerEventID != IssuerCalendarID)
		p = p->next;
	return p;
}

static int ZigBeeDateMatch(ZigBeeDate_t *z, EmberAfTimeStruct *tm, uint8_t dow)
{
	if (z->Year != 255 && 1900 + z->Year != tm->year)
		return 0;
	if (z->Month != 255 && z->Month != tm->month)
		return 0;
	if (z->MonthDay != 255 && z->MonthDay != tm->day)
		return 0;
	if (z->WeekDay != 255 && z->WeekDay != 1 + dow)
		return 0;
	return 1;
}

static int ZigBeeDateNotAfter(ZigBeeDate_t *z, EmberAfTimeStruct *tm)
{
	if (z->Year != 255) {
		if (1900 + z->Year < tm->year)
			return 1;
		if (1900 + z->Year > tm->year)
			return 0;
	}
	if (z->Month != 255) {
		if (z->Month < tm->month)
			return 1;
		if (z->Month > tm->month)
			return 0;
	}
	if (z->MonthDay != 255) {
		if (z->MonthDay < tm->day)
			return 1;
		if (z->MonthDay > tm->day)
			return 0;
	}
	return 1;
}

static struct speason* find_special(struct speason *s, EmberAfTimeStruct *tm, uint8_t dow)
{
	while (s && !ZigBeeDateMatch(&s->When, tm, dow))
		s = s->next;
	return s;
}

static struct speason* find_season(struct speason *s, EmberAfTimeStruct *tm, uint8_t dow)
{
	while (s && !ZigBeeDateNotAfter(&s->When, tm))
		s = s->next;
	return s;
}

static struct week* find_week(struct week *w, uint8_t id)
{
	while (w && w->ID != id)
		w = w->next;
	return w;
}

static struct day* find_day(struct calendar *c, uint32_t when)
{
	uint8_t DayIDRef = 1, day = day_of_week(when);
	EmberAfTimeStruct tm;
	emberAfFillTimeStructFromUtc(when, &tm);

	struct speason *s = find_special(c->Specials, &tm, day);
	if (s)
		DayIDRef = s->DayID;
	else if (s = find_season(c->Seasons, &tm, day)) {
		struct week *w = find_week(c->Weeks, s->WeekID);
		if (w)
			DayIDRef = w->Days[day]; // Mon to Sun
	}

	struct day *d = c->Days;
	while (d && d->ID != DayIDRef)
		d = d->next;
	return d;
}

static int walk_specials(struct speason *s, DateRef_t *o, uint32_t StartTime, int NumberOfEvents)
{
	EmberAfTimeStruct tm;
	emberAfFillTimeStructFromUtc(StartTime, &tm);
	uint8_t day = day_of_week(StartTime);

	int count = 0;
	for (; s; s = s->next) {
		if (!ZigBeeDateMatch(&s->When, &tm, day) && ZigBeeDateNotAfter(&s->When, &tm))
			continue;
		if (o) {
			o->Date = s->When;
			o->Ref = s->DayID;
			o++;
		}
		if (++count == NumberOfEvents)
			break;
	}
	return count;
}


// construction

int SendPublishCalendar(cmd_sink *send, struct meter *m, struct calendar *c)
{
	__PACKED_STRUCT PublishCalendar1 {
		uint32_t ProviderID, IssuerEventID, IssuerCalendarID, StartTime;
		uint8_t CalendarType, CalendarTimeReference, CalendarName[13];
	};
	__PACKED_STRUCT PublishCalendar2 {
		uint8_t NumberOfSeasons, NumberOfWeekProfiles, NumberOfDayProfiles;
	};

	uint8_t buf[sizeof(struct PublishCalendar1) + sizeof(struct PublishCalendar2)];
	struct PublishCalendar1 *p = (struct PublishCalendar1*)buf;
	p->ProviderID = ProviderID(m, c->StartTime);
	p->IssuerEventID = p->IssuerCalendarID = c->IssuerEventID;
	p->StartTime = c->StartTime;
	p->CalendarType = c->Type;
	p->CalendarTimeReference = 2; // local time
	struct PublishCalendar2 *q = pstrout(p->CalendarName, c->Name);

	q->NumberOfSeasons = c->SeasonCount;
	q->NumberOfWeekProfiles = c->WeekCount;
	q->NumberOfDayProfiles = c->DayCount;

	return send(m, buf, &q->NumberOfDayProfiles + 1 - buf, 0x00, 0x707);
}

int SendPublishSpecialDays(cmd_sink *send, struct meter *m, struct calendar *c, uint32_t StartTime, int NumberOfEvents)
{
	__PACKED_STRUCT PublishSpecialDays {
		uint32_t ProviderID, IssuerEventID, IssuerCalendarID, StartTime;
		uint8_t CalendarType, TotalNumberofSpecialDays, CommandIndex, TotalNumberofCommands;
		DateRef_t SpecialDayEntry[0]; // DayDate, DayIDRef
	};

	int count = walk_specials(c->Specials, 0, StartTime, NumberOfEvents);
	uint8_t buf[sizeof(struct PublishSpecialDays) + count * sizeof(DateRef_t)];
	struct PublishSpecialDays *p = (struct PublishSpecialDays*)buf;

	p->ProviderID = ProviderID(m, c->StartTime);
	p->IssuerEventID = p->IssuerCalendarID = c->IssuerEventID;
	p->StartTime = c->StartTime;
	p->CalendarType = c->Type;
	p->TotalNumberofSpecialDays = count;
	p->CommandIndex = 0;
	p->TotalNumberofCommands = 1;
	walk_specials(c->Specials, p->SpecialDayEntry, StartTime, NumberOfEvents);

	return send(m, p, sizeof(buf), 0x04, 0x707);
}

int SendCancelCalendar(cmd_sink *send, struct meter *m, struct calendar *c)
{
	__PACKED_STRUCT {
		uint32_t ProviderID, IssuerCalendarID;
		uint8_t CalendarType;
	} r = {ProviderID(m, c->StartTime), c->IssuerEventID, c->Type};

	return send(m, &r, sizeof(r), 0x05, 0x707);
}


// command requests

static int GetCalendars(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t EarliestStartTime, MinIssuerEventID;
		uint8_t NumberOfCalendars, CalendarType;
		uint32_t ProviderID;
	} *c = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*c))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	int sent = 0;
	struct calendar *p = m->Calendars;
	for (; p; p = p->next) {
		struct calendar *next = calendar_next_type(p->next, p->Type);
		if (next && next->StartTime <= c->EarliestStartTime) // The first returned PublishCalendar command shall be the instance which is active or becomes active at or after the stated Earliest Start Time.
			continue; // If more than one instance is requested, the active and scheduled instances shall be sent with ascending ordered Start Time.
		if (c->MinIssuerEventID != 0xFFFFFFFF && c->MinIssuerEventID < p->IssuerEventID) // Min. Issuer Event ID (mandatory): A 32-bit integer representing the minimum Issuer Event ID of calendars to be returned by the corresponding PublishCalendar command.
			continue; // A value of 0xFFFFFFFF means not specified; the server shall return calendars irrespective of the value of the Issuer Event ID.
		if (c->CalendarType != 0xFF && c->CalendarType != p->Type) // Generation Meters shall use the 'Received' Calendar. See Table D-159.
			continue; // A value of 0xFF means not specified. If the CalendarType is not specified, the server shall return calendars regardless of its type.
		if (c->ProviderID != 0xFFFFFFFF && c->ProviderID != ProviderID(m, p->StartTime)) // This field allows differentiation in deregulated markets where multiple commodity providers may be available.
			continue; // A value of 0xFFFFFFFF means not specified; the server shall return calendars irrespective of the value of the Provider Id.

		SendPublishCalendar(send_response, m, p);
		if (++sent == c->NumberOfCalendars) // Number of Calendars (mandatory): An 8-bit integer which represents the maximum number of PublishCalendar commands that the client is willing to receive in response to this command.
			break; //  A value of 0 would indicate all available PublishCalendar commands shall be returned.
	}

	return sent ? 'R' : EMBER_ZCL_STATUS_NOT_FOUND;
}

static int GetDayProfiles(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t ProviderID, IssuerCalendarID;
		uint8_t StartDayId, NumberOfDays;
	} *c = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*c))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	struct calendar *p = find_id(m->Calendars, c->ProviderID, c->IssuerCalendarID);
	if (!p)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	struct day *d;
	int sent = 0, total;
	do {
		for (d = p->Days, total = 0; d; d = d->next) {
			if (d->ID == c->StartDayId)
				total++;
		}
		if (total == 0)
			break;

		__PACKED_STRUCT DayScheduleEntry {
			uint16_t StartTime;
			uint8_t PriceTier; // or FriendlyCreditEnable or AuxiliaryLoadSwitchState
		};
		__PACKED_STRUCT PublishDayProfile {
			uint32_t ProviderID, IssuerEventID, IssuerCalendarID;
			uint8_t DayID, TotalNumberofScheduleEntries, CommandIndex, TotalNumberofCommands, CalendarType;
			struct DayScheduleEntry DayScheduleEntries[0];
		};
		uint8_t buf[sizeof(struct PublishDayProfile) + total * sizeof(struct DayScheduleEntry)];
		struct PublishDayProfile *r = (struct PublishDayProfile*)buf;

		r->ProviderID = ProviderID(m, p->StartTime);
		r->IssuerEventID = r->IssuerCalendarID = p->IssuerEventID;
		r->DayID = c->StartDayId;
		r->TotalNumberofScheduleEntries = total;
		r->CommandIndex = 0;
		r->TotalNumberofCommands = 1;
		r->CalendarType = p->Type;

		struct DayScheduleEntry *e = r->DayScheduleEntries;
		for (d = p->Days; d; d = d->next) {
			if (d->ID == c->StartDayId) {
				e->StartTime = d->StartTime;
				e->PriceTier = d->PriceTier;
				e++;
			}
		}

		send_response(m, buf, (uint8_t*)e - buf, 0x01, 0x707);
		c->StartDayId++;
	} while (++sent != c->NumberOfDays);

	return sent ? 'R' : EMBER_ZCL_STATUS_NOT_FOUND;
}

static int GetWeekProfiles(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t ProviderID, IssuerCalendarID;
		uint8_t StartWeekId, NumberOfWeeks;
	} *c = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*c))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	struct calendar *p = find_id(m->Calendars, c->ProviderID, c->IssuerCalendarID);
	if (!p)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	int sent = 0;
	do {
		struct week *w = find_week(p->Weeks, c->StartWeekId);
		if (!w)
			break;

		__PACKED_STRUCT PublishWeekProfile {
			uint32_t ProviderID, IssuerEventID, IssuerCalendarID;
			uint8_t WeekID, DayIDRefs[7]; ///< Monday - Sunday
		} rsp, *r = &rsp;

		r->ProviderID = ProviderID(m, p->StartTime);
		r->IssuerEventID = r->IssuerCalendarID = p->IssuerEventID;
		r->WeekID = c->StartWeekId++;
		memcpy(r->DayIDRefs, w->Days, 7);

		send_response(m, r, sizeof(*r), 0x02, 0x707);
	} while (++sent != c->NumberOfWeeks);

	return sent ? 'R' : EMBER_ZCL_STATUS_NOT_FOUND;
}

static int GetSeasons(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t ProviderID, IssuerCalendarID;
	} *c = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*c))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	struct calendar *p = find_id(m->Calendars, c->ProviderID, c->IssuerCalendarID);
	if (!p)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	int count = 0;
	struct speason *s;
	for (s = p->Seasons; s; s = s->next)
		count++;

	__PACKED_STRUCT PublishSeasons {
		uint32_t ProviderID, IssuerEventID, IssuerCalendarID;
		uint8_t CommandIndex, TotalNumberofCommands;
		DateRef_t SeasonEntry[0]; // StartDate, WeekIDRef;
	};
	uint8_t buf[sizeof(struct PublishSeasons) + count * sizeof(DateRef_t)];
	struct PublishSeasons *r = (struct PublishSeasons*)buf;

	r->ProviderID = ProviderID(m, p->StartTime);
	r->IssuerEventID = r->IssuerCalendarID = p->IssuerEventID;
	r->CommandIndex = 0;
	r->TotalNumberofCommands = 1;

	DateRef_t *e = r->SeasonEntry;
	for (s = p->Seasons; s; s = s->next, e++) {
		e->Date = s->When;
		e->Ref = s->WeekID;
	}

	send_response(m, r,(uint8_t*)e - buf, 0x03, 0x707);
	return 'R';
}

static int GetSpecialDays(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT GetSpecialDays {
		uint32_t StartTime;
		uint8_t NumberOfEvents, CalendarType;
		uint32_t ProviderID, IssuerCalendarID;
	} *c = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*c))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	struct calendar *p = find_id(m->Calendars, c->ProviderID, c->IssuerCalendarID);
	if (!p || !p->Specials)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	return SendPublishSpecialDays(send_response, m, p, c->StartTime ? c->StartTime : emberAfGetCurrentTimeCallback(), c->NumberOfEvents);
}


// exports

int calendar_cmd(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	if (af->commandId == 0x00)
		return GetCalendars(m, af, cmd);
	if (af->commandId == 0x01)
		return GetDayProfiles(m,af, cmd);
	if (af->commandId == 0x02)
		return GetWeekProfiles(m, af, cmd);
	if (af->commandId == 0x03)
		return GetSeasons(m, af, cmd);
	if (af->commandId == 0x04)
		return GetSpecialDays(m, af, cmd);
	return EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND;
}

struct calendar* calendar_next_type(struct calendar *p, int type)
{
	while (p && p->Type != type)
		p = p->next;
	return p;
}

int walk_calendar(struct meter *m, uint32_t when, uint8_t type, int (*got)(struct meter*, struct calendar*, uint32_t start, uint32_t end, uint8_t val))
{
	// find calendar that contains when and the next of same type
	struct calendar *cnext = calendar_next_type(m->Calendars, type), *c;
	do {
		c = cnext;
		if (!c || when < c->StartTime)
			return EMBER_ZCL_STATUS_NOT_FOUND;
		cnext = calendar_next_type(c->next, type);
	} while (cnext && when >= cnext->StartTime);

	while (!cnext || when < cnext->StartTime) {
		// adjust when to calendar time reference
		when = utc_to_local(when);

		// find day reference
		struct day *d = find_day(c, when), *e;
		if (!d)
			break;

		// find entry
		int ssm = when % 86400, midnight = when - ssm, msm = ssm / 60;
		while (d->next && d->next->ID == d->ID && d->next->StartTime <= msm)
			d = d->next;
		uint32_t start = midnight + d->StartTime * 60, next;

		// find next entry
		if (d->next && d->next->ID == d->ID)
			next = midnight + d->next->StartTime * 60;
		else if (e = find_day(c, midnight += 86400))
			next = midnight + e->StartTime * 60;
		else
			break;

		// adjust times back to UTC
		start = local_to_utc(start);
		next = local_to_utc(next);

		// crop to end of calendar if new one is before end of entry
		if (cnext && next >= cnext->StartTime)
			next = cnext->StartTime;

		int r = got(m, c, start, next, d->PriceTier);
		if (r != 'C')
			return r;
		when = next;
	}

	return EMBER_ZCL_STATUS_NOT_FOUND;
}

void calendar_free(struct calendar *c)
{
	struct day *d;
	while (d = c->Days)
		c->Days = d->next, free(d);

	struct week *w;
	while (w = c->Weeks)
		c->Weeks = w->next, free(w);

	struct speason *s;
	while (s = c->Seasons)
		c->Seasons = s->next, free(s);
	while (s = c->Specials)
		c->Specials = s->next, free(s);

	free(c);
}

void calendars_free(struct meter *m)
{
	struct calendar *c;
	while (c = m->Calendars)
		m->Calendars = c->next, calendar_free(c);
}
