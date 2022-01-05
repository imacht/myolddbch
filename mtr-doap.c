#include <dbch.h>


static uint8_t day_id;


// helpers

static void* rezalloc(void *prev, int old, int noo)
{
	void *next = zalloc(noo);
	if (next)
		memcpy(next, prev, old);
	free(prev);
	return next;
}


// calendars

static void cal_out(struct meter *m, struct calendar *c)
{
	uartf("%s.cal={ID:%x,ST:%x,TP:%x,NM:%s,DS:{", m->obj, c->IssuerEventID, c->StartTime, c->Type, c->Name);

	struct day *d = c->Days, *anchor;
	while (anchor = d) {
		uartf("{ID:%x", d->ID);
		do {
			uartf(",{ST:%x,TR:%x}", d->StartTime, d->PriceTier);
			d = d->next;
		} while (d && d->ID == anchor->ID);
		uartf("}");
		if (d)
			uartf(",");
	}

	uartf("},WS:{");
	struct week *w = c->Weeks;
	while (w) {
		uartf("{ID:%x,DS:%7b}", w->ID, w->Days);
		if (w = w->next)
			uartf(",");
	}

	uartf("},SS:{");
	struct speason *s = c->Seasons;
	while (s) {
		uartf("{ST:%x,WK:%x}", s->When.All, s->WeekID);
		if (s = s->next)
			uartf(",");
	}

	uartf("},SP:{");
	struct speason *p = c->Specials;
	while (p) {
		uartf("{DT:%x,DY:%x}", p->When.All, p->DayID);
		if (p = p->next)
			uartf(",");
	}
	uartf("}}\n");
}

static void cal_get(struct meter *m)
{
	struct calendar *c = m->Calendars;
	for (; c; c = c->next)
		cal_out(m, c);
}

static void parse_special(int i, char *v, struct speason *s)
{
	if (i == 0)
		s->When.All = hex2int(v);
	else if (i == 1)
		s->DayID = hex2int(v);
}

static void parse_specials(int i, char *v, struct calendar *c)
{
	struct speason **ss = &c->Specials, *s;
	while (s = *ss)
		ss = &s->next;
	doap_obj_parse(v, (objp_f*)parse_special, *ss = zalloc(sizeof(*s)));
}

static void parse_season(int i, char *v, struct speason *s)
{
	if (i == 0)
		s->When.All = hex2int(v);
	else if (i == 1)
		s->WeekID = hex2int(v);
}

static void parse_seasons(int i, char *v, struct calendar *c)
{
	struct speason **ss = &c->Seasons, *s;
	while (s = *ss)
		ss = &s->next;
	doap_obj_parse(v, (objp_f*)parse_season, *ss = zalloc(sizeof(*s)));
	c->SeasonCount++;
}

static void parse_week(int i, char *v, struct week *w)
{
	if (i == 0)
		w->ID = hex2int(v);
	else if (i == 1)
		hex2bin(v, w->Days);
}

static void parse_weeks(int i, char *v, struct calendar *c)
{
	struct week **ww = &c->Weeks, *w;
	while (w = *ww)
		ww = &w->next;
	doap_obj_parse(v, (objp_f*)parse_week, *ww = zalloc(sizeof(*w)));
	c->WeekCount++;
}

static void parse_day_entry(int i, char *v, struct calendar *c)
{
	struct day **dd = &c->Days, *d;
	while (*dd)
		d = *dd, dd = &d->next;

	if (i)
		d->PriceTier = hex2int(v); // d will be set when i == 0
	else if (d = *dd = zalloc(sizeof(*d))) {
		d->ID = day_id;
		d->StartTime = hex2int(v);
	}
}

static void parse_day(int i, char *v, struct calendar *c)
{
	if (i == 0)
		day_id = hex2int(v);
	else
		doap_obj_parse(v, (objp_f*)parse_day_entry, c);
}

static void parse_days(int i, char *v, struct calendar *c)
{
	doap_obj_parse(v, (objp_f*)parse_day, c);
	c->DayCount++;
}

static void parse_cal(int i, char *v, struct calendar *c)
{
	if (i == 0)
		c->IssuerEventID = hex2int(v);
	else if (i == 1)
		c->StartTime = hex2time(v);
	else if (i == 2)
		c->Type = hex2int(v);
	else if (i == 3)
		strncpy(c->Name, v, sizeof(c->Name) - 1);
	else if (i == 4)
		doap_obj_parse(v, (objp_f*)parse_days, c);
	else if (i == 5)
		doap_obj_parse(v, (objp_f*)parse_weeks, c);
	else if (i == 6)
		doap_obj_parse(v, (objp_f*)parse_seasons, c);
	else if (i == 7)
		doap_obj_parse(v, (objp_f*)parse_specials, c);
}

static void cal_set(struct meter *m, char *v)
{
	struct calendar *c = zalloc(sizeof(*c)), **pp = &m->Calendars, *p;
	int elements = doap_obj_parse(v, (objp_f*)parse_cal, c);

	if (elements == 1) { // delete
		while (p = *pp) {
			if (p->IssuerEventID == c->IssuerEventID) {
				SendCancelCalendar(push_to_bound, m, p);
				// ought to send SendPublishSpecialDays with StartTime = hFFFFFFFF
				*pp = p->next;
				calendar_free(p);
				uartf("%s.cal={ID:%x}\n",m->obj, c->IssuerEventID);
			} else
				pp = &p->next;
		}
	} else if (elements) { // new
		if (c->StartTime == 0 && (p = *pp))
			c->StartTime = p->StartTime;
		while ((p = *pp) && p->StartTime <= c->StartTime)
			pp = &p->next;
		c->next = *pp, *pp = c;
		cal_out(m, c);
		SendPublishCalendar(push_to_bound, m, c);
		SendPublishSpecialDays(push_to_bound, m, c, c->StartTime ? c->StartTime : emberAfGetCurrentTimeCallback(), 0);
		return;
	}

	calendar_free(c);
}


// bills

static void bll_out(struct meter *m, struct bill *b)
{
	uartf("%s.bill={ID:%x,ST:%x,ET:%x,AM:%x,DT:%x}\n", m->obj, b->IssuerEventID, b->StartTime, b->EndTime, b->Amount, b->DurationType);
}

static void bll_get(struct meter *m)
{
	struct bill *b = m->Bills;
	for (; b; b = b->next)
		bll_out(m, b);
}

static void parse_bll(int i, char *v, struct bill *b)
{
	if (i == 0)
		b->IssuerEventID = hex2int(v);
	else if (i == 1)
		b->StartTime = hex2time(v);
	else if (i == 2)
		b->EndTime = hex2time(v);
	else if (i == 3)
		b->Amount = hex2int(v);
	else if (i == 4)
		b->DurationType = hex2int(v);
}

static void bll_set(struct meter *m, char *v)
{
	struct bill *b = zalloc(sizeof(*b)), **pp = &m->Bills, *p;
	int elements = doap_obj_parse(v, (objp_f*)parse_bll, b);

	if (elements == 1) {
		while (p = *pp) {
			if (p->IssuerEventID == b->IssuerEventID) {
				p->StartTime = -1;
				SendPublishConsolidatedBill(push_to_bound, m, p);
				*pp = p->next;
				free(p);
				uartf("%s.bill={ID:%x}\n", m->obj, b->IssuerEventID);
			} else
				pp = &p->next;
		}
	} else if (elements) {
		while ((p = *pp) && p->StartTime >= b->StartTime)
			pp = &p->next;
		b->next = *pp, *pp = b;
		bll_out(m, b);
		SendPublishConsolidatedBill(push_to_bound, m, b);
		return;
	}

	free(b);
}


// tariffs

static void trf_out(struct meter *m, struct tariff *t)
{
	uartf("%s.trf={ID:%x,ST:%x,SC:%x,NM:%s,BM:%x", m->obj, t->IssuerEventID, t->StartTime, t->StandingCharge, t->Label, t->TierBlockMode);
	struct price *p = t->Prices;
	for (; p; p = p->next) {
		struct pcell *c = p->Cells;
		uartf(",{NM:%s,UR:%x", p->Label, c->UnitRate);
		int blocks = p->Thresholds;
		while (blocks--) {
			uartf(",%x", c->Threshold);
			c++;
			uartf(",%x", c->UnitRate);
		}
		uartf("}");
	}
	uartf("}\n");
}

static void trf_get(struct meter *m)
{
	struct tariff *t = m->Tariffs;
	for (; t; t = t->next)
		trf_out(m, t);
}

static void parse_prc(int i, char *v, struct price **pp)
{
	struct price *p = *pp;
	if (i == 0) {
		p = *pp = zalloc(sizeof(*p));
		strncpy(p->Label, v, sizeof(p->Label) - 1);
	} else if (i == 1)
		p->Cells[0].UnitRate = hex2int(v);
	else if (i % 2 == 0) {
		int size = sizeof(*p) + sizeof(struct pcell) * p->Thresholds;
		p = *pp = rezalloc(p, size, size + sizeof(struct pcell));
		p->Cells[p->Thresholds++].Threshold = hex2int(v);
	} else
		p->Cells[p->Thresholds].UnitRate = hex2int(v);
}

static void parse_trf(int i, char *v, struct tariff *t)
{
	static struct price **pp;
	if (i == 0)
		t->IssuerEventID = hex2int(v);
	else if (i == 1)
		t->StartTime = hex2time(v);
	else if (i == 2)
		t->StandingCharge = hex2int(v);
	else if (i == 3)
		strncpy(t->Label, v, sizeof(t->Label) - 1);
	else if (i == 4) {
		t->TierBlockMode = hex2int(v);
		pp = &t->Prices;
	} else {
		struct price *p;
		doap_obj_parse(v, (objp_f*)parse_prc, pp);
		if (p = *pp) {
			p->boss = t;
			p->Tier = ++t->PriceTiersInUse;
			if (t->BlockThresholdsInUse < p->Thresholds)
				t->BlockThresholdsInUse = p->Thresholds;
			pp = &p->next;
		}
	}
}

static void trf_set(struct meter *m, char *v)
{
	struct tariff *t = zalloc(sizeof(*t)), **pp = &m->Tariffs, *p;
	int elements = doap_obj_parse(v, (objp_f*)parse_trf, t);

	if (elements == 1) {
		while (p = *pp) {
			if (p->IssuerEventID == t->IssuerEventID) {
				SendCancelTariff(push_to_bound, m, p);
				*pp = p->next;
				tariff_free(p);
				uartf("%s.trf={ID:%x}\n", m->obj, t->IssuerEventID);
			} else
				pp = &p->next;
		}
	} else if (elements) {
		if (t->StartTime == 0 && (p = *pp))
			t->StartTime = p->StartTime;
		while ((p = *pp) && p->StartTime <= t->StartTime)
			pp = &p->next;
		t->next = *pp, *pp = t;
		trf_out(m, t);
		SendPublishTariffInformation(push_to_bound, m, t);
		return;
	}

	tariff_free(t);
}


// topups

static void tpp_out(struct meter *m, struct topup *t)
{
	uartf("%s.tpp={DT:%x,AM:%x,OD:%x,TN:%s}\n", m->obj, t->When, t->Amount, t->OriginatingDevice, t->Code);
}

static void tpp_get(struct meter *m)
{
	struct topup *t = m->Topups;
	for (; t; t = t->next)
		tpp_out(m, t);
}

static void parse_tpp(int i, char *v, struct topup *t)
{
	if (i == 0)
		t->When = hex2time(v);
	else if (i == 1)
		t->Amount = hex2int(v);
	else if (i == 2)
		t->OriginatingDevice = hex2int(v);
	else if (i == 3)
		strncpy(t->Code, v, sizeof(t->Code) - 1);
}

static void tpp_set(struct meter *m, char *v)
{
	struct topup *t = zalloc(sizeof(*t)), **pp = &m->Topups, *p;
	int elements = doap_obj_parse(v, (objp_f*)parse_tpp, t);

	if (elements == 1) {
		while (p = *pp) {
			if (p->When == t->When) {
				*pp = p->next;
				free(p);
				uartf("%s.tpp={DT:%x}\n", m->obj, t->When);
			} else
				pp = &p->next;
		}
	} else if (elements) {
		while ((p = *pp) && p->When >= t->When)
			pp = &p->next;
		t->next = *pp, *pp = t;
		tpp_out(m, t);
		return;
	}

	free(t);
}


// repays

static void rpy_out(struct meter *m, struct repay *r)
{
	uartf("%s.rpy={CT:%x,TP:%x,AM:%x,OD:%x}\n", m->obj, r->CollectionTime, r->DebtType, r->AmountCollected, r->OutstandingDebt);
}

static void rpy_get(struct meter *m)
{
	struct repay *r = m->Repays;
	for (; r; r = r->next)
		rpy_out(m, r);
}

static void parse_rpy(int i, char *v, struct repay *r)
{
	if (i == 0)
		r->CollectionTime = hex2time(v);
	else if (i == 1)
		r->DebtType = hex2int(v);
	else if (i == 2)
		r->AmountCollected = hex2int(v);
	else if (i == 3)
		r->OutstandingDebt = hex2int(v);
}

static void rpy_set(struct meter *m, char *v)
{
	struct repay *r = zalloc(sizeof(*r)), **pp = &m->Repays, *p;
	int elements = doap_obj_parse(v, (objp_f*)parse_rpy, r);

	if (elements == 2) {
		while (p = *pp) {
			if (p->CollectionTime == r->CollectionTime && p->DebtType == r->DebtType) {
				*pp = p->next;
				free(p);
				uartf("%s.rpay={CT:%x,TP:%x}\n", m->obj, r->CollectionTime, r->DebtType);
			} else
				pp = &p->next;
		}
	} else if (elements) {
		while ((p = *pp) && p->CollectionTime >= r->CollectionTime)
			pp = &p->next;
		r->next = *pp, *pp = r;
		rpy_out(m, r);
		return;
	}

	free(r);
}


// msgs

static void msg_out(struct meter *m, struct message *g)
{
	uartf("%s.msg={ID:%x,ST:%x,DM:%x,CL:%x,BD:%s}\n", m->obj, g->ID, g->StartTime, g->Minutes, g->Control, g->Message);
}

static void msg_get(struct meter *m)
{
	struct message *g = m->Messages;
	for (; g; g = g->next)
		msg_out(m, g);
}

static void parse_msg(int i, char *v,struct message *g)
{
	if (i == 0)
		g->ID = hex2int(v);
	else if (i == 1)
		g->StartTime = hex2time(v);
	else if (i == 2)
		g->Minutes = hex2int(v);
	else if (i == 3)
		g->Control = hex2int(v);
	else if (i == 4)
		strcpy(g->Message, v);
}

static void measure_msg(int i, char *v, int *len)
{
	if (i == 4)
		*len = strlen(v);
}

static void msg_set(struct meter *m, char *v)
{
	int len = 0, elements = doap_obj_parse(v, (objp_f*)measure_msg, &len);
	struct message *g = zalloc(sizeof(*g) + len), **pp = &m->Messages, *p;
	doap_obj_parse(v, (objp_f*)parse_msg, g);

	if (elements == 1) {
		while (p = *pp) {
			if (p->ID == g->ID) {
				*pp = p->next;
				free(p);
				uartf("%s.msg={ID:%x}\n", m->obj, g->ID);
			} else
				pp = &p->next;
		}
		SendCancelMessage(push_to_bound, m, g);
	} else if (elements) {
		if (g->StartTime == 0 && (p = *pp))
			g->StartTime = p->StartTime;
		while ((p = *pp) && p->StartTime <= g->StartTime)
			pp = &p->next;
		g->next = *pp, *pp = g;
		msg_out(m, g);
		SendDisplayMessage(push_to_bound, m, g);
		return;
	}

	free(g);
}


// cot

static void cot_out(struct meter* m, struct cot *c)
{
	uartf("%s.cot={ID:%x,DT:%x,CC:%x}\n", m->obj, c->IssuerEventID, c->When, c->ChangeControl);
}

static void cot_get(struct meter *m)
{
	struct cot *c = m->Cots;
	for (; c; c = c->next)
		cot_out(m, c);
}

static void parse_cot(int i, char *v, struct cot *c)
{
	if (i == 0)
		c->IssuerEventID = hex2int(v);
	else if (i == 1)
		c->When = hex2time(v);
	else if (i == 2)
		c->ChangeControl = hex2int(v);
}

static void cot_set(struct meter *m, char *v)
{
	struct cot *c = zalloc(sizeof(*c)), **pp = &m->Cots, *p;
	int elements = doap_obj_parse(v, (objp_f*)parse_cot, c);

	if (elements == 1) {
		while (p = *pp) {
			if (p->IssuerEventID == c->IssuerEventID) {
				p->When = -1;
				SendPublishChangeOfTenancy(push_to_bound, m, c);
				*pp = p->next;
				free(p);
				uartf("%s.cot={ID:%x}\n", m->obj, c->IssuerEventID);
			} else
				pp = &p->next;
		}
	} else if (elements) {
		if (c->When == 0 && (p = *pp))
			c->When = p->When;
		while ((p = *pp) && p->When <= c->When)
			pp = &p->next;
		c->next = *pp, *pp = c;
		cot_out(m, c);
		SendPublishChangeOfTenancy(push_to_bound, m, c);
		return;
	}

	free(c);
}


// cos

static void cos_out(struct meter *m, struct cos *c)
{
	uartf("%s.cos={ID:%x,DT:%x,CC:%x,NS:%x,NM:%s,CD:%s}\n", m->obj, c->IssuerEventID, c->When, c->ChangeControl, c->SupplierID, c->SupplierName, c->ContactDetails);
}

static void cos_get(struct meter *m)
{
	struct cos *c = m->Coss;
	for (; c; c = c->next)
		cos_out(m, c);
}

static void parse_cos(int i, char *v, struct cos *c)
{
	if (i == 0)
		c->IssuerEventID = hex2int(v);
	else if (i == 1)
		c->When = hex2time(v);
	else if (i == 2)
		c->ChangeControl = hex2int(v);
	else if (i == 3)
		c->SupplierID = hex2int(v);
	else if (i == 4)
		strncpy(c->SupplierName, v, sizeof(c->SupplierName) - 1);
	else if (i == 5)
		strncpy(c->ContactDetails, v, sizeof(c->ContactDetails) - 1);
}

static void cos_set(struct meter *m, char *v)
{
	struct cos *c = zalloc(sizeof(*c)), **pp = &m->Coss, *p;
	int elements = doap_obj_parse(v, (objp_f*)parse_cos, c);

	if (elements == 1) {
		while (p = *pp) {
			if (p->IssuerEventID == c->IssuerEventID) {
				p->When = -1;
				SendPublishChangeOfSupplier(push_to_bound, m, c);
				*pp = p->next;
				free(p);
				uartf("%s.cos={ID:%x}\n", m->obj, c->IssuerEventID);
			} else
				pp = &p->next;
		}
	} else if (elements) {
		if (c->When == 0 && (p = *pp))
			c->When = p->When;
		while ((p = *pp) && p->When <= c->When)
			pp = &p->next;
		c->next = *pp, *pp = c;
		cos_out(m, c);
		SendPublishChangeOfSupplier(push_to_bound, m, c);
		return;
	}

	free(c);
}


// debt

static void dbt_out(struct meter *m, struct debt *d)
{
	uartf("%s.debt={ID:%x,AM:%x,ST:%x,NM:%s,RM:%x,", m->obj, d->Ordinal, d->Amount, d->RecoveryStartTime, d->Label, d->RecoveryMethod);
	if (d->RecoveryMethod == 0) // time based
		uartf("RA:%x,NC:%x,RF:%x}\n", d->RecoveryAmount, d->NextCollection, d->RecoveryFrequency);
	else // assume payment based
		uartf("RP:%x}\n", d->RecoveryTopUpPercentage);
}

static void dbt_get(struct meter *m)
{
	struct debt *d = m->Debts;
	for (; d; d = d->next)
		dbt_out(m, d);
}

static void parse_dbt(int i, char *v, struct debt *d)
{
	if (i == 0)
		d->Ordinal = hex2int(v);
	else if (i == 1)
		d->Amount = hex2int(v);
	else if (i == 2)
		d->RecoveryStartTime = hex2time(v);
	else if (i == 3)
		strncpy(d->Label, v, sizeof(d->Label) - 1);
	else if (i == 4)
		d->RecoveryMethod = hex2int(v);
	else if (d->RecoveryMethod)
		d->RecoveryTopUpPercentage = hex2int(v);
	else if (i == 5)
		d->RecoveryAmount = hex2int(v);
	else if (i == 6)
		d->NextCollection = hex2time(v);
	else if (i == 7)
		d->RecoveryFrequency = hex2int(v);
}

static void dbt_set(struct meter *m, char *v)
{
	struct debt *d = zalloc(sizeof(*d)), **pp = &m->Debts, *p;
	int elements = doap_obj_parse(v, (objp_f*)parse_dbt, d);

	if (elements == 1) {
		while (p = *pp) {
			if (p->Ordinal == d->Ordinal) {
				*pp = p->next;
				free(p);
				uartf("%s.debt={ID:%x}\n", m->obj, d->Ordinal);
			} else
				pp = &p->next;
		}
	} else if (elements) {
		d->next = m->Debts, m->Debts = d;
		dbt_out(m, d);
		return;
	}

	free(d);
}


// co2

static void co2_out(struct meter *m, struct co2 *c)
{
	uartf("%s.co2={ID:%x,DT:%x,VL:%x,UN:%x,TD:%x}\n", m->obj, c->IssuerEventID, c->StartTime, c->Value, c->Unit, c->TrailingDigit);
}

static void co2_get(struct meter *m)
{
	struct co2 *c = m->co2;
	for (; c; c = c->next)
		co2_out(m, c);
}

static void parse_co2(int i, char *v, struct co2 *c)
{
	if (i == 0)
		c->IssuerEventID = hex2int(v);
	else if (i == 1)
		c->StartTime = hex2time(v);
	else if (i == 2)
		c->Value = hex2int(v);
	else if (i == 3)
		c->Unit = hex2int(v);
	else if (i == 4)
		c->TrailingDigit = hex2int(v);
}

static void co2_set(struct meter *m, char *v)
{
	struct co2 *c = zalloc(sizeof(*c)), **pp = &m->co2, *p;
	int elements = doap_obj_parse(v, (objp_f*)parse_co2, c);

	if (elements == 1) {
		while (p = *pp) {
			if (p->IssuerEventID == c->IssuerEventID) {
				p->StartTime = -1;
				SendPublishCO2Value(push_to_bound, m, p);
				*pp = p->next;
				free(p);
				uartf("%s.co2={ID:%x}\n", m->obj, c->IssuerEventID);
			} else
				pp = &p->next;
		}
	} else if (elements) {
		if (c->StartTime == 0 && (p = *pp))
			c->StartTime = p->StartTime;
		while ((p = *pp) && p->StartTime <= c->StartTime)
			pp = &p->next;
		c->next = *pp, *pp = c;
		co2_out(m, c);
		SendPublishCO2Value(push_to_bound, m, c);
		return;
	}

	free(c);
}


// next topup

static void ntu_get(struct meter *m)
{
	uartf("%s.ntu=%x\n", m->obj, m->NextTopup);
}

static void ntu_set(struct meter *m, char *v)
{
	m->NextTopup = hex2int(v);
	ntu_get(m);
}


// set by attribute

static void set_basic_attr(struct meter *m, int id, char *v)
{
	if (id == ZCL_APPLICATION_VERSION_ATTRIBUTE_ID)
		m->ApplicationVersion = hex2int(v);
	else if (id == ZCL_STACK_VERSION_ATTRIBUTE_ID)
		m->StackVersion = hex2int(v);
	else if (id == ZCL_HW_VERSION_ATTRIBUTE_ID)
		m->HWVersion = hex2int(v);
	else if (id == ZCL_MANUFACTURER_NAME_ATTRIBUTE_ID)
		strncpy(m->ManufacturerName, v, sizeof(m->ManufacturerName) - 1);
	else if (id == ZCL_MODEL_IDENTIFIER_ATTRIBUTE_ID)
		strncpy(m->ModelIdentifier, v, sizeof(m->ModelIdentifier) - 1);
	else if (id == ZCL_POWER_SOURCE_ATTRIBUTE_ID)
		m->PowerSource = hex2int(v);
}

static void set_price_attr(struct meter *m, int id, char *v)
{
	if (id == 0x301 && m->Tariffs)
		m->Tariffs->StandingCharge = hex2int(v);
	else if (id == 0x302)
		m->ConversionFactor = hex2int(v);
	else if (id == 0x303)
		m->ConversionFactorTrailingDigit = hex2int(v);
	else if (id == 0x304)
		m->CalorificValue = hex2int(v);
	else if (id == 0x306)
		m->CalorificValueTrailingDigit = hex2int(v);
	else if (id == 0x615)
		m->PriceUnitOfMeasure = hex2int(v);
	else if (id == 0x616)
		m->Currency = hex2int(v);
	else if (id == 0x617)
		m->PriceTrailingDigit = hex2int(v);
	else if (id == 0x620 && m->co2)
		m->co2->Value = hex2int(v);
	else if (id == 0x621 && m->co2)
		m->co2->Unit = hex2int(v);
	else if (id == 0x622 && m->co2)
		m->co2->TrailingDigit = hex2int(v);
}

static void set_cons(void *history, int ago, char *v)
{
	struct history *h = history;
	if (ago < h->top && h->got < ago + 1)
		h->got = ago + 1;
	h->val[(h->now + ago) % h->top] = hex2int(v);
}

static void set_meter_attr(struct meter *m, int id, char *v)
{
	struct bill *b;
	struct pcell *c;

	if (id == 0x000)
		m->CurrentSummationDelivered = hex2int(v);
	else if (id == 0x005)
		m->DailyFreezeTime = hex2int(v);
	else if (id == 0x00D)
		m->DailyConsumptionTarget = hex2int(v);
	else if (id == 0x014)
		m->SupplyStatus = hex2int(v);
	else if (id == 0x207)
		m->AmbientConsumptionIndicator = hex2int(v);
	else if (id == 0x300)
		m->MeterUnitOfMeasure = hex2int(v);
	else if (id == 0x301)
		m->Multiplier = hex2int(v);
	else if (id == 0x302)
		m->Divisor = hex2int(v);
	else if (id == 0x303)
		m->SummationFormatting = hex2int(v);
	else if (id == 0x307)
		strncpy(m->SiteID, v, sizeof(m->SiteID) - 1); //todo SendUpdateSiteID(push_to_bound, m, 0);
	else if (id == 0x308)
		strncpy(m->MeterSerialNumber, v, sizeof(m->MeterSerialNumber) - 1);
	else if (id == 0x311)
		strncpy(m->CustomerIDNumber, v, sizeof(m->CustomerIDNumber) - 1); //todo SendUpdateCIN(push_to_bound, m, 0);
	else if (id == 0x400)
		m->InstantaneousDemand = hex2int(v), m->random_demand = 0;
	else if (id == 0x401) // CurrentDayConsumptionDelivered
		set_cons(&m->DayConsumption, 0, v);
	else if (id == 0x403) // PreviousDayConsumptionDelivered
		set_cons(&m->DayConsumption, 1, v);
	else if (id >= 0x420 && id <= 0x42C) // PreviousDayNConsumptionDelivered
		set_cons(&m->DayConsumption, 2 + (id - 0x420) / 2, v);
	else if (id >= 0x430 && id <= 0x43A) // WeekNConsumptionDelivered
		set_cons(&m->WeekConsumption, (id - 0x430) / 2, v);
	else if (id >= 0x440 && id <= 0x45A) // MonthNConsumptionDelivered
		set_cons(&m->MonthConsumption, (id - 0x440) / 2, v);
	else if (id == 0x45C)
		m->HistoricalFreezeTime = hex2int(v);
	else if (id == 0x500)
		m->MaxNumberOfPeriodsDelivered = hex2int(v);
	else if (id == 0xA00 && (b = bill_current(m))) // BillToDateDelivered
		b->Amount = hex2int(v);
	else if (id == 0xA01 || id == 0xA03) // BillToDateTimeStampDelivered or ProjectedBillTimeStampDelivered
		m->BillToDateTimeStampDelivered = hex2int(v);
	else if (id / 256 == 0x700 && (c = price_cell(m, id / 16 % 16, id % 16)))
		c->Counter = hex2int(v);
}

static void set_cost(void *history, uint64_t *costs, int ago, char *v)
{
	struct history *h = history;
	costs[(h->now + ago) % h->top] = hex2int(v);
}

static void set_prepay_attr(struct meter *m, int id, char *v)
{
	if (id == 0x000)
		m->PaymentControl = hex2int(v); // TODO esi-dongle calls set_account
	else if (id == 0x001)
		m->CreditRemaining = hex2int(v);
	else if (id == 0x002)
		m->EmergencyCreditRemaining = hex2int(v);
	else if (id == 0x003)
		m->CreditStatus = hex2int(v);
	else if (id == 0x004)
		m->CreditRemainingTimeStamp = hex2int(v);
	else if (id == 0x005)
		m->AccumulatedDebt = hex2int(v);
	else if (id == 0x006)
		m->OverallDebtCap = hex2int(v);
	else if (id == 0x010)
		m->EmergencyCreditLimitAllowance = hex2int(v);
	else if (id == 0x011)
		m->EmergencyCreditThreshold = hex2int(v);
	else if (id == 0x020)
		m->TotalCreditAdded = hex2int(v);
	else if (id == 0x021)
		m->MaxCreditLimit = hex2int(v);
	else if (id == 0x022)
		m->MaxCreditPerTopUp = hex2int(v);
	else if (id == 0x030)
		m->FriendlyCreditWarning = hex2int(v);
	else if (id == 0x040)
		m->CutOffValue = hex2int(v);
	else if (id == 0x080)
		strncpy(m->TokenCarrierID, v, sizeof(m->TokenCarrierID) - 1);
	else if (id == 0x500)
		m->HistoricalCostConsumptionFormatting = hex2int(v);
	else if (id == 0x502)
		m->CurrencyScalingFactor = hex2int(v);
	else if (id == 0x503)
		m->Currency = hex2int(v);
	else if (id >= 0x51C && id <= 0x52C) // DayNCostConsumptionDelivered
		set_cost(&m->DayConsumption, m->DayCost, (id - 0x51C) / 2, v);
	else if (id >= 0x530 && id <= 0x53A) // WeekNCostConsumptionDelivered
		set_cost(&m->WeekConsumption, m->WeekCost, (id - 0x530) / 2, v);
	else if (id >= 0x540 && id <= 0x55A) // MonthNCostConsumptionDelivered
		set_cost(&m->MonthConsumption, m->MonthCost, (id - 0x540) / 2, v);
	else if (id == 0x55C)
		m->HistoricalFreezeTime = hex2int(v);
}

static void set_devman_attr(struct meter *m, int id, char *v)
{
	if (id == 0x100)
		m->ProviderID = hex2int(v);
	else if (id == 0x101)
		strncpy(m->ProviderName, v, sizeof(m->ProviderName) - 1);
	else if (id == 0x102)
		strncpy(m->ProviderContactDetails, v, sizeof(m->ProviderContactDetails) - 1);
	else if (id == 0x400)
		m->LowMediumThreshold = hex2int(v);
	else if (id == 0x401)
		m->MediumHighThreshold = hex2int(v);
}

static void set_meter_by_attr(struct meter *m, int cluster, int id, char *v)
{
	if (cluster == 0x000)
		set_basic_attr(m, id, v);
	else if (cluster == 0x700)
		set_price_attr(m, id, v);
	else if (cluster == 0x702)
		set_meter_attr(m, id, v);
	else if (cluster == 0x705)
		set_prepay_attr(m, id, v);
	else if (cluster == 0x708)
		set_devman_attr(m, id, v);
}

static void parse_att(int i, char *v, struct meter *m)
{
	static uint16_t cluster, id;

	if (i == 0)
		cluster = hex2int(v);
	else if (i == 1)
		id = hex2int(v);
	else if (i == 2)
		set_meter_by_attr(m, cluster, id, v);
}

static void att_set(struct meter *m, char *v)
{
	doap_obj_parse(v, (objp_f*)parse_att, m);
}


// tear down

static void kill(struct meter *m)
{
	meter_free(m);
	doap_obj_del(m->obj);
	emberAfEndpointEnableDisable(m->ep, false);
	uartf("ok %s gone\n", m->obj);
}

// exports

void doap_mtr_add(struct meter *m)
{
	static const struct doap_attr attrs[] = {
		{"kill", (dget_f*)kill, 0},
		{"cal", (dget_f*)cal_get, (dset_f*)cal_set},
		{"trf", (dget_f*)trf_get, (dset_f*)trf_set},
		{"bll", (dget_f*)bll_get, (dset_f*)bll_set},
		{"tpp", (dget_f*)tpp_get, (dset_f*)tpp_set},
		{"rpy", (dget_f*)rpy_get, (dset_f*)rpy_set},
		{"msg", (dget_f*)msg_get, (dset_f*)msg_set},
		{"cot", (dget_f*)cot_get, (dset_f*)cot_set},
		{"cos", (dget_f*)cos_get, (dset_f*)cos_set},
		{"dbt", (dget_f*)dbt_get, (dset_f*)dbt_set},
		{"co2", (dget_f*)co2_get, (dset_f*)co2_set},
		{"ntu", (dget_f*)ntu_get, (dset_f*)ntu_set},
		{"att", 0, (dset_f*)att_set},
		{0, 0, 0}
	};

	doap_obj_add(m->obj, attrs, m);
}
