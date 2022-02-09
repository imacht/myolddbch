#include <dbch.h>


// helpers

static void history_increase(void *history, int delta)
{
	struct history *h = history;
	h->val[h->now] += delta;
	if (h->got == 0)
		h->got = 1;
}

static void history_advance(void *history, uint64_t *cost)
{
	struct history *h = history;
	h->now = (h->now ? h->now : h->top) - 1;
	if (h->got < h->top)
		h->got++;
	h->val[h->now] = 0;
	cost[h->now] = 0;
}

static void update_credit_status(struct meter *m)
{
	uint8_t cs = m->CreditStatus & 16; // save EC selected bit as it cannot be derived
	uint16_t ppas = m->PrepaymentAlarmStatus & ~0x141; // other code sets and clears these

	if (m->EmergencyCreditLimitAllowance) {
		cs |= 4; // EC supported
		if (~cs & 16 && m->CreditRemaining < m->EmergencyCreditThreshold) {
			cs |= 8; // EC offered
			ppas |= 64; // EC Available
		}
	}
	if (m->CreditRemaining > m->CutOffValue) {
		cs |= 1; // credit OK
		if (m->CreditRemaining < m->LowCreditWarning) {
			cs |= 2; // low credit
			ppas |= 1; // Low Credit Warning
		}
	} else if (cs & 16 && m->EmergencyCreditRemaining > 0) // EC selected
		cs |= 33; // EC in use, credit OK
	else {
		cs |= 64; // exhausted
		if (~ppas & 16) { // no FCP
			if (m->SupplyStatus == 2)
				m->SupplyStatus = 0; // off
			ppas |= 256; // Disconnected Supply Due to Credit
		}
	}
	if (~cs & 64 && m->SupplyStatus == 0)
		m->SupplyStatus = 1; // arm

	m->CreditStatus = cs;
	m->PrepaymentAlarmStatus = ppas;
}

static void incur_suspended_cost(struct meter *m, int cost, int suspended)
{
	if (suspended)
		m->AccumulatedDebt += cost;
	else
		m->CreditRemaining -= cost;
}

static void reduce_credit(struct meter *m, int cost, char type)
{
	m->CreditRemainingTimeStamp = emberAfGetCurrentTimeCallback();

	if (m->CreditRemaining > m->CutOffValue) { // use normal credit first
		m->CreditRemaining -= cost;
		update_credit_status(m);
		if (m->CreditRemaining > m->CutOffValue)
			return; // still in credit, done
		cost = m->CreditRemaining - m->CutOffValue;
		m->CreditRemaining = m->CutOffValue;
	}

	if (m->EmergencyCreditRemaining > 0 && m->CreditStatus & 16) { // use EC if available and selected
		if (type == 's' && m->PaymentControl & 1 << 9) {
			incur_suspended_cost(m, cost, m->PaymentControl & 1 << 8);
			return;
		} else if (type == 'd' && m->PaymentControl & 1 << 11) {
			incur_suspended_cost(m, cost, m->PaymentControl & 1 << 10);
			return;
		}
		m->EmergencyCreditRemaining -= cost;
		update_credit_status(m);
		if (m->EmergencyCreditRemaining > 0)
			return; // still got EC, done
		cost = -m->EmergencyCreditRemaining;
		m->EmergencyCreditRemaining = 0;
	}

	if (type == 's')
		incur_suspended_cost(m, cost,m->PaymentControl & 1 << 8);
	else if(type=='d')
		incur_suspended_cost(m, cost,m->PaymentControl & 1 << 10);
	else
		incur_suspended_cost(m, cost,m->PaymentControl & 5 << 8);
}

static void increase_bill(struct meter *m, int delta)
{
	struct bill *b = bill_current(m);
	if (b) {
		b->Amount += delta;
		m->BillToDateTimeStampDelivered = emberAfGetCurrentTimeCallback();
	}
}

static void spend_cash(struct meter *m, int cash, char type)
{
	if (m->PaymentControl & 2)
		reduce_credit(m, cash, type);
	else
		increase_bill(m, cash);
}

static int block(struct meter *m, struct tariff *t, struct price *p)
{
	int64_t usage = 0;
	for (struct price *q = t->Prices; q; q = q->next) {
		if (t->TierBlockMode && q != p) // modes 1 & 2 use sum of block counters in one tier, mode 0 uses sum of all tiers
			continue;
		for (int b = 0; b <= p->Thresholds; b++)
			usage += q->Cells[b].Counter;
	}
	usage = convert(usage, m, m->MeterUnitOfMeasure, m->PriceUnitOfMeasure);

	int block = 0;
	while (block < p->Thresholds && usage >= p->Cells[block].Threshold)
		block++;
	return block;
}

static void price_cell_add(struct meter *m, int delta, struct price *p)
{
	struct pcell *c = p->Cells + p->Block;
	c->Counter += delta;

	int old = p->Block;
	p->Block = block(m, p->boss, p);
	if (p->Block != old)
		SendPublishPrice(push_to_bound, m, 0, 0, p);
}

static int check_current_price(struct meter *m, struct calendar *c, uint32_t start, uint32_t end, uint8_t tier)
{
	m->PriceNow = 0;
	struct tariff *t = m->Tariffs;
	struct price *p = price_find_tier(t->Prices, tier);

	if (p) {
		p->Block = block(m, t, m->PriceNow = p);
		if (!m->PriceEnd || start == emberAfGetCurrentTimeCallback())
			SendPublishPrice(push_to_bound, m, start, end, p);
	}
	m->PriceEnd = end;

	return 0;
}

static void AnnounceCalendar(struct meter *m, struct calendar *c)
{
	SendPublishCalendar(push_to_bound, m, c);
	if (c->Specials)
		SendPublishSpecialDays(push_to_bound, m, c, 0, 0);
}

static int update_friendly_credit(struct meter *m, struct calendar *c, uint32_t start, uint32_t end, uint8_t enabled)
{
	if (enabled)
		m->PrepaymentAlarmStatus |= 16;
	else
		m->PrepaymentAlarmStatus &= ~16;
	m->NextFriendlyCreditSwitch = end;
	update_credit_status(m);
	return 0;
}

static struct repay* record_repayment(struct meter *m, uint32_t CollectionTime, uint32_t AmountCollected, uint8_t DebtType)
{
	struct repay *r = malloc(sizeof(*r));
	if (r) {
		r->next = m->Repays, m->Repays = r;
		r->CollectionTime = CollectionTime;
		r->AmountCollected = AmountCollected;
		r->DebtType = DebtType;

		struct debt *d = m->Debts;
		for (r->OutstandingDebt = 0; d; d = d->next)
			r->OutstandingDebt += d->Amount;
	}
	return r;
}

static void change(struct meter *m, uint32_t ChangeControl)
{
	if (ChangeControl & 1 << 2) { // All Credit Registers shall be reset to their default value
		bills_free(m);
		m->BillToDateTimeStampDelivered = 0;
	}
	if (ChangeControl & 1 << 3) // All Debt Registers shall be reset to their default value
		debts_free(m);
//	if (ChangeControl & 1 << 4) // All Billing periods shall be reset to their default value
//		got no default billing period
//	if (ChangeControl & 1 << 5) // The tariff shall be reset to its default value
//		got no default tariffs
//	if (ChangeControl & 1 << 6) // The Standing Charge shall be reset to its default value
//		got no default standing charge
//	if (ChangeControl & 1 << 7) // Historical LP information shall no longer be available to be published to the HAN. With regards to a meter that is mirrored, this information may be available to the HES but not to the HAN. Any historical LP shall be cleared from the IHD.
//		TODO
	if (ChangeControl & 1 << 8) { // Historical LP information shall be cleared from all devices
		m->MonthConsumption.got = 0;
		m->WeekConsumption.got = 0;
		m->DayConsumption.got = 0;
		m->HourConsumption.got = 0;
	}
	if (ChangeControl & 1 << 9) // All consumer data shall be removed
		m->CustomerIDNumber[0] = 0;
	if (ChangeControl & 1 << 10) { // All supplier data shall be removed
		m->ProviderID = m->LowMediumThreshold = m->MediumHighThreshold = 0xFFFFFFFF;
		m->ProviderName[0] = m->ProviderContactDetails[0] = 0;
	}
//	if (ChangeControl & 1 << 13) { // All transaction logs shall be cleared from all devices
//	}
	if (ChangeControl & 1 << 14) { // All Prepayment Registers shall be reset to their default state
		topups_free(m);
		m->CreditRemaining = m->EmergencyCreditRemaining = m->AccumulatedDebt = m->OverallDebtCap = m->CutOffValue = 0;
		m->CreditRemainingTimeStamp = m->NextFriendlyCreditSwitch = 0;
		m->PrepaymentAlarmStatus = 0;
		m->CreditStatus = m->TokenCarrierID[0] = 0;
	}
}

static struct topup* find_topup_code(struct topup *t, uint8_t *TopUpCode)
{
	while (t && strpcmp(t->Code, TopUpCode))
		t = t->next;
	return t;
}

static void add_credit(struct meter *m, int credit)
{
	m->TotalCreditAdded += credit;

	uint32_t RepaidThisWeek = 0, now = emberAfGetCurrentTimeCallback(), AWeekAgo = now - 604800, repay;
	for (struct repay *r = m->Repays; r; r = r->next) {
		if (r->CollectionTime >= AWeekAgo)
			RepaidThisWeek += r->AmountCollected;
	}

	// xii.	recovery of payment-based debt of an amount defined by Debt Recovery per Payment(4.6.4.8) from the Payment Debt Register(4.6.5.13) subject to the Debt Recovery Rate Cap(4.6.4.10); 
	for (struct debt *d = m->Debts; d; d = d->next) {
		if (d->RecoveryMethod != 1)
			continue;
		uint32_t RecoveryAmount = credit * d->RecoveryTopUpPercentage / 10000;
		if (RecoveryAmount > d->Amount)
			RecoveryAmount = d->Amount;
		if (m->OverallDebtCap && RecoveryAmount > m->OverallDebtCap - RepaidThisWeek)
			RecoveryAmount = m->OverallDebtCap - RepaidThisWeek;

		record_repayment(m, now, RecoveryAmount, d->Ordinal - 1);
		RepaidThisWeek += RecoveryAmount;
		d->Amount -= RecoveryAmount;
		credit -= RecoveryAmount;
	}

	// xiii. recovery of debt accumulated in the Accumulated Debt Register(4.6.5.1);
	repay = m->AccumulatedDebt < credit ? m->AccumulatedDebt : credit;
	m->AccumulatedDebt -= repay;
	credit -= repay;

	// xiv.	repayment of Emergency Credit activated and used by the Consumer; and
	uint32_t dues = m->EmergencyCreditLimitAllowance - m->EmergencyCreditRemaining;
	repay = dues < credit ? dues : credit;
	m->EmergencyCreditRemaining += repay;
	credit -= repay;

	// xv.	adding remaining credit (the credit after deduction of (xii), (xiii) and (xiv) above) to the Meter Balance(4.6.5.11).
	m->CreditRemaining += credit;
	if (m->CreditRemaining >= m->EmergencyCreditThreshold)
		m->CreditStatus = 0; // EC not selected
	update_credit_status(m);
}

static struct topup* top_up(struct meter *m, uint8_t OriginatingDevice, uint8_t *TopUpCode)
{
	struct topup *t = malloc(sizeof(*t));
	if (t) {
		t->next = m->Topups;
		t->When = emberAfGetCurrentTimeCallback();
		t->Amount = m->NextTopup;
		t->OriginatingDevice = OriginatingDevice;
		pstrcpy(t->Code, TopUpCode, sizeof(t->Code));

		m->NextTopup = 0;
		add_credit(m, t->Amount);
	}
	return t;
}


// ticks

static void energy_tick(struct meter *m)
{
	m->Joules += m->InstantaneousDemand;
	if (m->Joules >= 3600) { // 1/1000th of a kWh
		int delta = m->Joules / 3600;
		m->Joules %= 3600;
		m->CurrentSummationDelivered += delta;
		history_increase(&m->HourConsumption, delta);
		history_increase(&m->DayConsumption, delta);
		history_increase(&m->WeekConsumption, delta);
		history_increase(&m->MonthConsumption, delta);
		if (m->PriceNow)
			price_cell_add(m, delta, m->PriceNow);
	}

	if (m->random_demand)
		m->InstantaneousDemand = 550 + halCommonGetRandom() % 1024 - 512;
	if (is_elec(m)) {
		if (m->MediumHighThreshold == 0)
			m->AmbientConsumptionIndicator = 0;
		else if (m->InstantaneousDemand < m->LowMediumThreshold)
			m->AmbientConsumptionIndicator = 1;
		else if (m->InstantaneousDemand < m->MediumHighThreshold)
			m->AmbientConsumptionIndicator = 2;
		else
			m->AmbientConsumptionIndicator = 3;
	}
}

static void spend_tick(struct meter *m)
{
	struct price *p = m->PriceNow;
	if (p) {
		int64_t spend = m->InstantaneousDemand * p->Cells[p->Block].UnitRate;
		if (m->MeterUnitOfMeasure != m->PriceUnitOfMeasure) // TODO fuck knows if this is right, check at runtime
			spend = convert(spend, m, m->MeterUnitOfMeasure, m->PriceUnitOfMeasure);
		m->Spend += spend;
	}

	if (m->Spend >= 3600000) {
		int delta = m->Spend / 3600000;
		m->Spend %= 3600000;
		m->DayCost[m->DayConsumption.now] += delta;
		m->WeekCost[m->WeekConsumption.now] += delta;
		m->MonthCost[m->MonthConsumption.now] += delta;
		spend_cash(m, delta, 'u');
	}
}

static void tariff_tick(struct meter *m, uint32_t now)
{
	struct tariff *t = m->Tariffs, *n;
	if (t && (n = t->next) && now >= n->StartTime) {
		tariff_free(t);
		SendPublishTariffInformation(push_to_bound, m, t = n);
		m->PriceEnd = 0; // force PublishPrice
	}

	if (t && now >= m->PriceEnd) {
		if (t->PriceTiersInUse < 2)
			check_current_price(m, 0, 0, -1, 1);
		else
			walk_calendar(m, now, 0x00, check_current_price);
	}
}

static void bill_tick(struct meter *m, uint32_t now)
{
	struct bill *c = bill_current(m), *b;
	if (c && now >= c->EndTime && (b = malloc(sizeof(*b)))) {
		b->StartTime = c->EndTime;
		b->DurationType = c->DurationType;
		if (b->DurationType == 0x03) {
			EmberAfTimeStruct tm;
			emberAfFillTimeStructFromUtc(now, &tm);
			tm.seconds = tm.minutes = tm.hours = 0;
			tm.day = 1;
			b->StartTime = emberAfGetUtcFromTimeStruct(&tm);
			if (++tm.month == 13)
				tm.month = 1, tm.year++;
			b->EndTime = emberAfGetUtcFromTimeStruct(&tm);
		} else
			b->EndTime = 2 * b->StartTime - c->StartTime;
		b->Amount = 0;
	}
}

static void co2_tick(struct meter *m, uint32_t now)
{
	struct co2 *c, *n;
	if ((c = m->co2) && (n = c->next) && now >= n->StartTime) {
		m->co2 = n, free(c);
		n->StartTime = 0;
		SendPublishCO2Value(push_to_bound, m, n);
	}
}

static void msg_tick(struct meter *m, uint32_t now)
{
	struct message *g = m->Messages;
	if (g && (g->Minutes && now >= g->StartTime + 60 * g->Minutes || g->next && now >= g->next->StartTime)) {
		m->Messages = g->next, free(g);
		if (g = m->Messages)
			SendDisplayMessage(push_to_bound, m, g);
	}
}

static void debts_tick(struct meter *m, uint32_t now)
{
	struct debt *d = m->Debts;
	for (; d; d = d->next) {
		// Fuck RecoveryCollectionTime, it makes no fucking sense for anything but RecoveryFrequency=per day, use non-spec NextCollection field
		if (d->Amount == 0 || now < d->RecoveryStartTime || d->RecoveryMethod || now < d->NextCollection)
			continue;
		if (d->NextCollection == 0 && now % 60)
			continue;
		d->NextCollection = now;

		if (d->RecoveryFrequency == 0) // hourly
			d->NextCollection += 3600;
		else if (d->RecoveryFrequency == 1) // daily
			d->NextCollection += 86400;
		else if (d->RecoveryFrequency == 2) // weekly
			d->NextCollection += 604800;
		else {
			EmberAfTimeStruct tm;
			emberAfFillTimeStructFromUtc(d->NextCollection, &tm);
			if (d->RecoveryFrequency == 3) // monthly
				tm.month += 1;
			else if (d->RecoveryFrequency == 4) // quarterly
				tm.month += 3;
			if (tm.month > 12)
				tm.month -= 12, tm.year++;
			d->NextCollection = emberAfGetUtcFromTimeStruct(&tm);
		}

		int debit = d->Amount < d->RecoveryAmount ? d->Amount : d->RecoveryAmount;
		reduce_credit(m, debit, 'd');
		d->Amount -= debit;

		record_repayment(m, now, debit, d->Ordinal - 1);
	}
}

static void calendar_tick(struct meter *m, uint32_t now)
{
	struct calendar **cc = &m->Calendars, *c, *next;
	while (c = *cc) {
		if ((next = calendar_next_type(c->next, c->Type)) && now >= next->StartTime) {
			*cc = c->next;
			calendar_free(c);
			AnnounceCalendar(m, next);
			m->PriceEnd = 0; // force PublishPrice
		} else
			cc = &c->next;
	}

	if (now >= m->NextFriendlyCreditSwitch)
		walk_calendar(m, now, 3, update_friendly_credit);

	if (m->PrepaymentAlarmStatus & 16 && now >= m->NextFriendlyCreditSwitch - m->FriendlyCreditWarning * 60)
		m->PrepaymentAlarmStatus |= 32; // Friendly Credit Period End Warning
	else
		m->PrepaymentAlarmStatus &= ~32;
}

static void cos_tick(struct meter *m, uint32_t now)
{
	struct cos *s = m->Coss;
	if (s && now >= s->When) {
		change(m, s->ChangeControl);
		m->ProviderID = s->SupplierID;
		strcpy(m->ProviderName, s->SupplierName);
		strcpy(m->ProviderContactDetails, s->ContactDetails);
		m->Coss = s->next, free(s);
	}
}

static void cot_tick(struct meter *m, uint32_t now)
{
	struct cot *t = m->Cots;
	if (t && now >= t->When) {
		change(m, t->ChangeControl);
		m->Cots = t->next, free(t);
	}
}

static void day_tick(struct meter *m, uint32_t local, EmberAfTimeStruct *tm)
{
	if (local == 0 || (tm->hours << 8 | tm->minutes) != m->HistoricalFreezeTime)
		return;

	history_advance(&m->DayConsumption, m->DayCost);
	if (local / 86400 % 7 == 2) // Monday
		history_advance(&m->WeekConsumption, m->WeekCost);
	if (tm->day == 0) // 1st
		history_advance(&m->MonthConsumption, m->MonthCost);

	struct tariff *t = m->Tariffs;
	if (t)
		spend_cash(m, m->DayCost[m->DayConsumption.now] = t->StandingCharge, 's');
}


// exports

void metro(uint32_t now)
{
	static uint32_t last;

	uint32_t local = 0, hour = now / 3600;
	EmberAfTimeStruct tm;
	if (last + 1 == hour) {
		local = utc_to_local(now);
		emberAfFillTimeStructFromUtc(local, &tm);
	}
	last = hour;

	struct meter *m = meters();
	for (; m; m = m->next) {
		m->ReadingSnapShotTime = now;
		energy_tick(m);
		spend_tick(m);
		calendar_tick(m, now);
		tariff_tick(m, now);
		bill_tick(m, now);
		co2_tick(m, now);
		msg_tick(m, now);
		debts_tick(m, now);
		cos_tick(m, now);
		cot_tick(m, now);
		day_tick(m, local, &tm);
	}
}

void app_init(void)
{
	nv_init();
	// TODO add / load fakey meters and register DOAP objects
//	int mp = call_mtrs_set(NULL, "{0x0E,0x0300}");
	int mp = call_mtrs_set(NULL, "{0x0E, 0x0300}");
	uartf("--- Meter added: %d\n", mp);
}

int SelectAvailableEmergencyCredit(struct meter *m, uint32_t CommandIssueDateTime, uint8_t OriginatingDevice)
{
	if (m->EmergencyCreditLimitAllowance == 0 || m->CreditRemaining >= m->EmergencyCreditThreshold)
		return EMBER_ZCL_STATUS_ACTION_DENIED;

	m->CreditStatus = 16; // EC selected
	update_credit_status(m);

	return EMBER_ZCL_STATUS_SUCCESS;
}

int ConsumerTopUp(struct meter *m, uint8_t OriginatingDevice, uint8_t *TopUpCode)
{
	__PACKED_STRUCT {
		uint8_t ResultType;
		int32_t TopUpValue;
		uint8_t SourceOfTopUp;
		int32_t CreditRemaining;
	} r;

	struct topup *t;
	if (TopUpCode[0] != 20) {
		r.ResultType = EMBER_ZCL_RESULT_TYPE_REJECTED_INVALID_TOP_UP;
		m->PrepaymentAlarmStatus |= 2; // Top Up Code Error
	} else if (find_topup_code(m->Topups, TopUpCode)) {
		r.ResultType = EMBER_ZCL_RESULT_TYPE_REJECTED_DUPLICATE_TOP_UP;
		m->PrepaymentAlarmStatus |= 4; // Top Up Code Already Used
	} else if (!m->NextTopup) {
		r.ResultType = EMBER_ZCL_RESULT_TYPE_REJECTED_DUPLICATE_TOP_UP;
		m->PrepaymentAlarmStatus |= 8; // Top Up Code Invalid
	} else if (t = top_up(m, OriginatingDevice, TopUpCode)) {
		r.ResultType = EMBER_ZCL_RESULT_TYPE_ACCEPTED;
		r.TopUpValue = t->Amount;
		r.SourceOfTopUp = t->OriginatingDevice;
		r.CreditRemaining = m->CreditRemaining;
		m->PrepaymentAlarmStatus &= ~14;
	} else
		return EMBER_ZCL_STATUS_INSUFFICIENT_SPACE;

	return send_response(m, &r, r.ResultType ? 1 : sizeof(r), 0x03, 0x705);
}
