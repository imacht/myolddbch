#include <dbch.h>


__PACKED_STRUCT GetBillingPeriod {
	uint32_t EarliestStartTime, MinIssuerEventID;
	uint8_t NumberOfCommands, TariffType;
};


static uint8_t count; // for GetScheduledPrices


// helpers

static struct tariff* find_tariff_id(struct tariff *t, uint32_t ID)
{
	while (t && t->IssuerEventID != ID)
		t = t->next;
	return t;
}

static int walk_bills(struct meter *m, struct bill *b, void (*got)(struct meter*, struct bill*), struct GetBillingPeriod *q)
{
	if (!b || q->TariffType) // no older bills or early fail
		return 0;
	int sent = walk_bills(m, b->next, got, q); // process older bills first

	if (q->NumberOfCommands && sent == q->NumberOfCommands)
		return sent; // quota met
	if (b->EndTime <= q->EarliestStartTime || q->MinIssuerEventID != 0xFFFFFFFF && b->StartTime < q->MinIssuerEventID)
		return sent; // doesn't match search criteria

	got(m, b);
	return 1 + sent;
}

static int bill_duration(struct bill *b)
{
	int minutes = (b->EndTime - b->StartTime) / 60;
	if (b->DurationType == 0x00)
		return minutes;

	int days = minutes / 1440;
	if (b->DurationType == 0x01)
		return days;

	int weeks = days / 7;
	if (b->DurationType == 0x02)
		return weeks;

	int months = weeks/4; // minimum 28 days for Feb
	return months;
}


// construction

int SendPublishPrice(cmd_sink *send, struct meter *m, uint32_t start, uint32_t end, struct price *p)
{
	__PACKED_STRUCT PublishPrice1 {
		uint32_t ProviderID;
		uint8_t RateLabel[13];
	};
	__PACKED_STRUCT PublishPrice2 {
		uint32_t IssuerEventID, CurrentTime;
		uint8_t UnitOfMeasure;
		uint16_t Currency;
		uint8_t PriceTrailingDigitPriceTier, NumberOfPriceTiersRegisterTier;
		uint32_t StartTime;
		uint16_t DurationInMinutes;
		uint32_t Price;
		// optional
		uint8_t PriceRatio;
		uint32_t GenerationPrice;
		uint8_t GenerationPriceRatio;
		uint32_t AlternateCostDelivered;
		uint8_t AlternateCostUnit, AlternateCostTrailingDigit;
		int8_t NumberOfBlockThresholds;
		uint8_t PriceControl;
		// 1.x
		int8_t NumberOfGenerationTiers;
		uint8_t GenerationTier;
		// 1.2a
		uint8_t ExtendedNumberOfPriceTiers, ExtendedPriceTier;
		// 1.2b
		uint8_t ExtendedRegisterTier;
	};

	if (!p)
		return EMBER_ZCL_STATUS_NOT_FOUND;
	struct tariff *t = p->boss;

	uint8_t buf[sizeof(struct PublishPrice1) + sizeof(struct PublishPrice2)], *tail;
	memset(buf, 255, sizeof(buf));
	struct PublishPrice1 *h = (struct PublishPrice1*)buf;
	h->ProviderID = ProviderID(m,start);
	struct PublishPrice2 *g = pstrout(h->RateLabel, p->Label);

	g->IssuerEventID = t->IssuerEventID;
	g->CurrentTime = emberAfGetCurrentTimeCallback();
	g->UnitOfMeasure = m->PriceUnitOfMeasure;
	g->Currency = m->Currency;
	g->StartTime = start;
	g->DurationInMinutes = end && end != 0xFFFFFFFF ? (end - start) / 60 : 65535;
	g->Price = p->Cells[p->Block].UnitRate;
	g->NumberOfBlockThresholds = t->BlockThresholdsInUse;
	g->PriceTrailingDigitPriceTier = m->PriceTrailingDigit;
	g->NumberOfPriceTiersRegisterTier = t->PriceTiersInUse << 4;

	if (p->Tier < 15) {
		g->PriceTrailingDigitPriceTier |= p->Tier;
		g->NumberOfPriceTiersRegisterTier |= p->Tier;
	} else {
		g->PriceTrailingDigitPriceTier |= 15;
		g->NumberOfPriceTiersRegisterTier |= 15;
		g->ExtendedPriceTier = g->ExtendedRegisterTier = p->Tier - 15;
	}

	if (t->PriceTiersInUse < 16) {
		g->PriceControl = 0x00;
		tail = (uint8_t*)&g->NumberOfGenerationTiers; // short 1.0 form
	} else {
		g->PriceControl = 0x02;
		g->NumberOfPriceTiersRegisterTier |= 15 << 4;
		g->ExtendedNumberOfPriceTiers = t->PriceTiersInUse - 15;
		tail = &g->ExtendedRegisterTier + 1; // long 1.2b form
	}

	return send(m, buf, tail - buf, 0x00, 0x700);
}

static int SendPublishConversionFactor(cmd_sink *send, struct meter *m)
{
	uint32_t now = emberAfGetCurrentTimeCallback();

	__PACKED_STRUCT {
		uint32_t IssuerEventID, StartTime, Factor;
		uint8_t TrailingDigit;
	} rsp = {now, now, m->ConversionFactor, m->ConversionFactorTrailingDigit};

	return send(m, &rsp, sizeof(rsp), 0x02, 0x700);
}

static int SendPublishCalorificValue(cmd_sink *send, struct meter *m)
{
	uint32_t now = emberAfGetCurrentTimeCallback();

	__PACKED_STRUCT {
		uint32_t IssuerEventID, StartTime, Value;
		uint8_t Unit, TrailingDigit;
	} rsp = {now, now, m->CalorificValue, 1, m->CalorificValueTrailingDigit};

	return send(m, &rsp, sizeof(rsp), 0x03, 0x700);
}

int SendPublishTariffInformation(cmd_sink *send, struct meter *m, struct tariff *t)
{
	__PACKED_STRUCT PublishTariffInformation1 {
		uint32_t ProviderID, IssuerEventID, IssuerTariffID, StartTime;
		uint8_t TariffTypeChargingScheme, TariffLabel[25];
	};
	__PACKED_STRUCT PublishTariffInformation2 {
		uint8_t NumberOfPriceTiersInUse, NumberOfBlockThresholdsInUse, UnitOfMeasure;
		uint16_t Currency;
		uint8_t PriceTrailingDigit;
		uint32_t StandingCharge;
		uint8_t TierBlockMode, BlockThresholdMultiplier[3], BlockThresholdDivisor[3];
	};

	uint8_t buf[sizeof(struct PublishTariffInformation1) + sizeof(struct PublishTariffInformation2)];
	struct PublishTariffInformation1 *h = (struct PublishTariffInformation1*)buf;
	h->ProviderID = ProviderID(m, t->StartTime);
	h->IssuerEventID = h->IssuerTariffID = t->IssuerEventID;
	h->StartTime = t->StartTime;
	if (t->BlockThresholdsInUse == 0)
		h->TariffTypeChargingScheme = 0x00; // TOU Tariff
	else if (t->PriceTiersInUse == 1)
		h->TariffTypeChargingScheme = 0x10; // Block Tariff TODO not convinced, ESI don't support tier 0
	else if (t->TierBlockMode != 2)
		h->TariffTypeChargingScheme = 0x20; // Block/TOU Tariff with common thresholds
	else
		h->TariffTypeChargingScheme = 0x30; // Block/TOU Tariff with individual thresholds per tier
	struct PublishTariffInformation2 *g = pstrout(h->TariffLabel, t->Label);

	g->NumberOfPriceTiersInUse = t->PriceTiersInUse;
	g->NumberOfBlockThresholdsInUse = t->BlockThresholdsInUse;
	if (t->TierBlockMode == 2)
		g->NumberOfBlockThresholdsInUse *= t->PriceTiersInUse;
	g->UnitOfMeasure = m->PriceUnitOfMeasure;
	g->Currency = m->Currency;
	g->PriceTrailingDigit = m->PriceTrailingDigit;
	g->StandingCharge = t->StandingCharge;
	g->TierBlockMode = t->TierBlockMode;
	uint_out(g->BlockThresholdMultiplier, m->Multiplier, 3);
	uint_out(g->BlockThresholdDivisor, m->Divisor, 3);

	return send(m, buf, g->BlockThresholdDivisor + 3 - buf, 0x04, 0x700);
}

static int SendPublishPriceMatrix(cmd_sink *send, struct meter *m, struct tariff *t)
{
	__PACKED_STRUCT PublishPriceMatrix {
		uint32_t ProviderID, IssuerEventID, StartTime, IssuerTariffID;
		uint8_t CommandIndex, TotalNumberOfCommands, SubPayloadControl;
		__PACKED_STRUCT PriceMatrixSubPayload {
			uint8_t TierBlockID;
			uint32_t Price;
		} SubPayload[0];
	};

	char buf[sizeof(struct PublishPriceMatrix) + t->PriceTiersInUse * (t->BlockThresholdsInUse + 1) * sizeof(struct PriceMatrixSubPayload)];
	struct PublishPriceMatrix *r = (struct PublishPriceMatrix*)buf;
	r->ProviderID = ProviderID(m, t->StartTime);
	r->IssuerEventID = r->IssuerTariffID = t->IssuerEventID;
	r->StartTime = t->StartTime;
	r->CommandIndex = 0;
	r->TotalNumberOfCommands = 1;
	r->SubPayloadControl = t->BlockThresholdsInUse ? 0 : 1; // block/mixed based or TOU based

	struct price *p = t->Prices;
	struct PriceMatrixSubPayload *s = r->SubPayload;
	for (; p; p = p->next) {
		int block = 0;
		while (block <= p->Thresholds) {
			s->TierBlockID = t->BlockThresholdsInUse ? p->Tier << 4 | block : p->Tier;
			s->Price = p->Cells[block].UnitRate;
			s++;
			block++;
		}
	}

	return send(m, r,(char*)s - buf, 0x05, 0x700);
}

static int SendPublishBlockThresholds(cmd_sink *send, struct meter *m, struct tariff *t)
{
	__PACKED_STRUCT PublishBlockThresholds {
		uint32_t ProviderID, IssuerEventID, StartTime, IssuerTariffID;
		uint8_t CommandIndex, TotalNumberOfCommands, SubPayloadControl, SubPayload[0];
	};

	int size = sizeof(struct PublishBlockThresholds), block;
	struct price *p = t->Prices;
	for (; p; p = p->next)
		size += 1 + 6 * p->Thresholds;

	uint8_t buf[size];
	struct PublishBlockThresholds *r = (struct PublishBlockThresholds*)buf;
	r->ProviderID = ProviderID(m, t->StartTime);
	r->IssuerEventID = r->IssuerTariffID = t->IssuerEventID;
	r->StartTime = t->StartTime;
	r->CommandIndex = 0;
	r->TotalNumberOfCommands = 1;
	r->SubPayloadControl = t->TierBlockMode < 2 ? 1 : 0; // thresholds apply to all tiers or thresholds apply to specific tiers

	uint8_t *s = r->SubPayload;
	for (p = t->Prices; p; p = p->next) {
		if (r->SubPayloadControl)
			*s++ = p->Thresholds;
		else
			*s++ = p->Thresholds | p->Tier << 4;
		for (block = 0; block < p->Thresholds; block++)
			s = uint_out(s, p->Cells[block].Threshold, 6);
		if (r->SubPayloadControl)
			break;
	}

	return send(m, r,s - buf, 0x06, 0x700);
}

int SendPublishCO2Value(cmd_sink *send, struct meter *m, struct co2 *c)
{
	__PACKED_STRUCT {
		uint32_t ProviderID, IssuerEventID, StartTime;
		uint8_t TariffType;
		uint32_t Value;
		uint8_t Unit, TrailingDigit;
	} rsp = {m->ProviderID, c->IssuerEventID, c->StartTime, 0, c->Value, c->Unit, c->TrailingDigit};
	return send(m, &rsp, sizeof(rsp), 0x07, 0x700);
}

static int SendPublishTierLabels(cmd_sink *send, struct meter *m, struct tariff *t)
{
	__PACKED_STRUCT PublishTierLabels {
		uint32_t ProviderID, IssuerEventID, IssuerTariffID;
		uint8_t CommandIndex, TotalNumberOfCommands, NumberOfLabels, TierIDLabels[0];
	};

	uint8_t buf[sizeof(struct PublishTierLabels) + 14 * t->PriceTiersInUse];
	struct PublishTierLabels *r = (struct PublishTierLabels*)buf;
	r->ProviderID = ProviderID(m, t->StartTime);
	r->IssuerEventID = r->IssuerTariffID = t->IssuerEventID;
	r->CommandIndex = 0;
	r->TotalNumberOfCommands = 1;
	r->NumberOfLabels = t->PriceTiersInUse;

	struct price *p = t->Prices;
	uint8_t *o = r->TierIDLabels, tier = 0;
	for (; p; p = p->next) {
		*o++ = ++tier;
		o = pstrout(o, p->Label);
	}

	return send(m, p, o - buf, 0x08, 0x700);
}

int SendPublishConsolidatedBill(cmd_sink *send, struct meter *m, struct bill *b)
{
	__PACKED_STRUCT {
		uint32_t ProviderID, IssuerEventID, StartTime;
		uint8_t Duration[3], DurationType, TariffType;
		uint32_t ConsolidatedBill;
		uint16_t Currency;
		uint8_t BillTrailingDigit;
	} rsp;

	rsp.ProviderID = m->ProviderID;
	rsp.IssuerEventID = rsp.StartTime = b->StartTime;
	uint_out(rsp.Duration, bill_duration(b), 3);
	rsp.DurationType = b->DurationType;
	rsp.TariffType = 0;
	rsp.ConsolidatedBill = b->Amount;
	rsp.Currency = m->Currency;
	rsp.BillTrailingDigit = m->PriceTrailingDigit;
	return send(m, &rsp, sizeof(rsp), 0x0A, 0x700);
}

int SendCancelTariff(cmd_sink *send, struct meter *m, struct tariff *t)
{
	__PACKED_STRUCT {
		uint32_t ProviderID, IssuerTariffID;
		uint8_t TariffType;
	} r = {ProviderID(m, t->StartTime), t->IssuerEventID, 0};

	return send(m, &r, sizeof(r), 0x0E, 0x700);
}


// command requests

static int got_price(struct meter *m, struct calendar *c, uint32_t start, uint32_t end, uint8_t tier)
{
	return SendPublishPrice(send_response, m, start, end, price_find_tier(m->Tariffs->Prices, tier));
}
static int GetCurrentPrice(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint8_t CommandOptions;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	struct tariff *t = m->Tariffs;
	if (!t)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	if (t->PriceTiersInUse < 2)
		return SendPublishPrice(send_response, m, 0, 0, t->Prices);

	return walk_calendar(m, emberAfGetCurrentTimeCallback(), 0x00, got_price);
}

static int got_scheduled_price(struct meter *m, struct calendar *c, uint32_t start, uint32_t end, uint8_t tier)
{
	struct price *p;
	struct tariff *t = m->Tariffs;
	while (t && t->next && start >= t->next->StartTime)
		t = t->next;
	if (!t || !(p = price_find_tier(t->Prices, tier)))
		return EMBER_ZCL_STATUS_NOT_FOUND;

	int r = SendPublishPrice(send_response, m, start, end, p);
	if (r == 'R' && --count)
		return 'C'; // continue search
	return r;
}
static int GetScheduledPrices(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t StartTime;
		uint8_t NumberofEvents;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	count = q->NumberofEvents == 0 || q->NumberofEvents > 5 ? 5 : q->NumberofEvents;
	return walk_calendar(m, q->StartTime ? q->StartTime : emberAfGetCurrentTimeCallback(), 0x00, got_scheduled_price);
}

static int PriceAcknowledgement(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	return EMBER_ZCL_STATUS_SUCCESS; // What? Want a fucking medal?
}

static int GetConversionFactor(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t EarliestStartTime, MinIssuerEventID;
		uint8_t NumberOfCommands;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	if (m->ConversionFactor == 0)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	return SendPublishConversionFactor(send_response, m);
}

static int GetCalorificValue(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t EarliestStartTime, MinIssuerEventID;
		uint8_t NumberOfCommands;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	if (m->CalorificValue == 0)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	return SendPublishCalorificValue(send_response, m);
}

static int GetTariffInformation(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t EarliestStartTime, MinIssuerEventID;
		uint8_t NumberOfCommands, TariffType;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	int sent = 0;
	struct tariff *t = m->Tariffs;
	if (q->TariffType == 0x00) for (; t; t = t->next) {
		if (t->next && q->EarliestStartTime >= t->next->StartTime) // EarliestStartTime spec is gibberish, used test spec
			continue;
		if (q->MinIssuerEventID != 0xFFFFFFFF && t->IssuerEventID < q->MinIssuerEventID)
			continue;

		SendPublishTariffInformation(send_response, m, t);
		if (++sent == q->NumberOfCommands)
			break;
	}

	return sent ? 'R' : EMBER_ZCL_STATUS_NOT_FOUND;
}

static int GetPriceMatrix(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t IssuerTariffID;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	struct tariff *t = find_tariff_id(m->Tariffs, q->IssuerTariffID);
	return t ? SendPublishPriceMatrix(send_response, m, t) : EMBER_ZCL_STATUS_NOT_FOUND;
}

static int GetBlockThresholds(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t IssuerTariffID;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	struct tariff *t = find_tariff_id(m->Tariffs, q->IssuerTariffID);
	return t ? SendPublishBlockThresholds(send_response, m, t) : EMBER_ZCL_STATUS_NOT_FOUND;
}

static int GetCO2Value(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t EarliestStartTime, MinIssuerEventID;
		uint8_t NumberOfCommands, TariffType;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	int sent = 0;
	struct co2 *p = m->co2;
	if (q->TariffType == 0x00) for (; p; p = p->next) {
		if (p->StartTime < q->EarliestStartTime && p->next)
			continue;
		if (q->MinIssuerEventID != 0xFFFFFFFF && p->IssuerEventID < q->MinIssuerEventID)
			continue;

		SendPublishCO2Value(send_response, m, p);
		if (++sent == q->NumberOfCommands)
			break;
	}

	return sent ? 'R' : EMBER_ZCL_STATUS_NOT_FOUND;
}

static int GetTierLabels(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint32_t IssuerTariffID;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	struct tariff *t = find_tariff_id(m->Tariffs, q->IssuerTariffID);
	return t ? SendPublishTierLabels(send_response, m, t) : EMBER_ZCL_STATUS_NOT_FOUND;
}

static void got_billing_period(struct meter *m, struct bill *b)
{
	__PACKED_STRUCT {
		uint32_t ProviderID, IssuerEventID, StartTime;
		uint8_t Duration[3], DurationType, TariffType;
	} rsp;

	rsp.ProviderID = m->ProviderID;
	rsp.IssuerEventID = rsp.StartTime = b->StartTime;
	uint_out(rsp.Duration, bill_duration(b), 3);
	rsp.DurationType = b->DurationType;
	rsp.TariffType = 0;
	send_response(m, &rsp, sizeof(rsp), 0x09, 0x700);
}
static int GetBillingPeriod(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	struct GetBillingPeriod *q = cmd;
	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	return walk_bills(m, m->Bills, got_billing_period, q) ? 'R' : EMBER_ZCL_STATUS_NOT_FOUND;
}

static void got_consolidated_bill(struct meter *m, struct bill *b)
{
	SendPublishConsolidatedBill(send_response, m, b);
}
static int GetConsolidatedBill(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	struct GetBillingPeriod *q = cmd; // same command format
	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	return walk_bills(m, m->Bills, got_consolidated_bill, q) ? 'R' : EMBER_ZCL_STATUS_NOT_FOUND;
}


// exports

int price_cmd(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	if (af->commandId == 0x00)
		return GetCurrentPrice(m, af, cmd);
	if (af->commandId == 0x01)
		return GetScheduledPrices(m, af, cmd);
	if (af->commandId == 0x02)
		return PriceAcknowledgement(m, af, cmd);
//	if (af->commandId == 0x03) return GetBlockPeriods(m, af, cmd);
	if(m->MeteringDeviceType) {
		if (af->commandId == 0x04)
			return GetConversionFactor(m, af, cmd);
		if (af->commandId == 0x05)
			return GetCalorificValue(m, af, cmd);
	}
	if (af->commandId == 0x06)
		return GetTariffInformation(m, af, cmd);
	if (af->commandId == 0x07)
		return GetPriceMatrix(m, af, cmd);
	if (af->commandId == 0x08)
		return GetBlockThresholds(m, af, cmd);
	if (af->commandId == 0x09)
		return GetCO2Value(m, af, cmd);
	if (af->commandId == 0x0A)
		return GetTierLabels(m, af, cmd);
	if (af->commandId == 0x0B)
		return GetBillingPeriod(m, af, cmd);
	if (af->commandId == 0x0C)
		return GetConsolidatedBill(m, af, cmd);
//	if (af->commandId == 0x0D) return CPPEventResponse(m, af, cmd);
//	if (af->commandId == 0x0E) return GetCreditPayment(m, af, cmd);
//	if (af->commandId == 0x0F) return GetCurrencyConversion(m, af);
//	if (af->commandId == 0x10) return GetTariffCancellation(m, af);
	return EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND;
}

void bills_free(struct meter *m)
{
	struct bill *b;
	while (b = m->Bills)
		m->Bills = b->next, free(b);
}

void price_free(struct meter *m)
{
	bills_free(m);

	struct tariff *t;
	while (t = m->Tariffs)
		m->Tariffs = t->next, tariff_free(t);

	struct co2 *c;
	while (c = m->co2)
		m->co2 = c->next, free(c);
}

struct bill* bill_new(struct meter *m, uint32_t StartTime)
{
	struct bill **pp = &m->Bills, *p;
	while ((p = *pp) && p->StartTime >= StartTime)
		pp = &p->next;

	if (p = zalloc(sizeof(*p))) {
		p->next = *pp, *pp = p;
		p->StartTime = StartTime;
	}
	return p;
}

struct bill* bill_current(struct meter *m)
{
	uint32_t time = emberAfGetCurrentTimeCallback();
	struct bill *p = m->Bills;
	while (p && time < p->StartTime)
		p = p->next;
	return p;
}

struct pcell* price_cell(struct meter *m, int tier, int block)
{
	struct tariff *t = m->Tariffs;
	if (t && tier) {
		struct price *p = t->Prices;
		while (p && --tier)
			p = p->next;
		if (block <= p->Thresholds)
			return p->Cells + block;
	}
	return 0;
}

struct price* price_find_tier(struct price *p, int tier)
{
	while (p && --tier)
		p = p->next;
	return p;
}

void tariff_free(struct tariff *t)
{
	struct price *p;
	while (p = t->Prices)
		t->Prices = p->next, free(p);
	free(t);
}
