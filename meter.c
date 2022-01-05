#include <dbch.h>
#include <util/common.h>
#include <app/framework/util/util.h>


static struct meter *used, *gone, pool[3];


// helpers

static int big_out(int type, uint8_t *out, int64_t num, int bytes)
{
	*out++ = type;
	uint_out(out, num, bytes);
	return 1 + bytes;
}
static int num_out(int type, uint8_t *out, int num, int bytes) { return big_out(type, out, num, bytes); }
static int uint1_out(uint8_t *out, int num) { return num_out(ZCL_INT8U_ATTRIBUTE_TYPE, out, num, 1); }
static int uint2_out(uint8_t *out, int num) { return num_out(ZCL_INT16U_ATTRIBUTE_TYPE, out, num, 2); }
static int uint3_out(uint8_t *out, int num) { return num_out(ZCL_INT24U_ATTRIBUTE_TYPE, out, num, 3); }
static int uint4_out(uint8_t *out, int num) { return num_out(ZCL_INT32U_ATTRIBUTE_TYPE, out, num, 4); }
static int sint4_out(uint8_t *out, int num) { return num_out(ZCL_INT32S_ATTRIBUTE_TYPE, out, num, 4); }
static int uint6_out(uint8_t *out, uint64_t num) { return big_out(ZCL_INT48U_ATTRIBUTE_TYPE, out, num, 6); }
static int utc_out(uint8_t *out, int num) { return num_out(ZCL_UTC_TIME_ATTRIBUTE_TYPE, out, num, 4); }
static int enum1_out(uint8_t *out, int num) { return num_out(ZCL_ENUM8_ATTRIBUTE_TYPE, out, num, 1); }
static int bitmap1_out(uint8_t *out, int num) { return num_out(ZCL_BITMAP8_ATTRIBUTE_TYPE, out, num, 1); }
static int bitmap2_out(uint8_t *out, int num) { return num_out(ZCL_BITMAP16_ATTRIBUTE_TYPE, out, num, 2); }
static int bitmap4_out(uint8_t *out, int num) { return num_out(ZCL_BITMAP32_ATTRIBUTE_TYPE, out, num, 4); }

static int type_pstr_out(uint8_t *out, char *s)
{
	uint8_t *p = out;
	*p++ = ZCL_OCTET_STRING_ATTRIBUTE_TYPE;
	return (uint8_t*)pstrout(p, s) - out;
}

static void* pstrskip(uint8_t *p)
{
	uint8_t len = *p++;
	return p + (len == 255 ? 0 : len);
}

static uint32_t history_value(void *history, int ago)
{
	struct history *h = history;
	if (ago >= h->got)
		return -1;
	return h->val[(h->now + ago) % h->top];
}

static uint32_t alt_history_value(struct meter *m, void *history, int ago)
{
	uint32_t v = history_value(history, ago);
	if (v != 0xFFFFFFFF)
		v = convert(v, m, m->MeterUnitOfMeasure, m->PriceUnitOfMeasure);
	return v;
}

static uint64_t history_cost(void *history, uint64_t *costs, int ago)
{
	struct history *h = history;
	if (ago >= h->got)
		return -1;
	return costs[(h->now + ago) % h->top];
}


// conversion

static uint64_t gcd(uint64_t u, uint64_t v)
{
	uint64_t t;
	uint32_t shift = 0;
	while (1) {
		if (u < v) // make u biggest
			t = u, u = v, v = t;
		if (v == 0) // done
			return u << shift;
		if ((u | v) % 2 == 0) { // both even
			u /= 2;
			v /= 2;
			shift++;
		} else if (u & v & 1) // both odd
			u -= v;
		else if (u % 2 == 0)
			u /= 2;
		else if (v % 2 == 0)
			v /= 2;
	}
}

static void scale(uint32_t *frac, uint32_t mul, int td)
{
	uint64_t num = (uint64_t)mul * frac[0], den = frac[1];
	while (td--)
		den *= 10;

	uint64_t d = gcd(num, den);
	num /= d;
	den /= d;

	while (num >= 16777216 || den >= 16777216)
		num >>= 1, den >>= 1;

	frac[0] = num;
	frac[1] = den;
}

static void mj_factor(struct meter *m, int uom, uint32_t *frac)
{
	uom &= ~128;
	if (uom == 0 || uom == 13) // kWh = 3.6MJ
		scale(frac, 36, 1);
	else if (uom == 6) // BTU = 1055J
		scale(frac, 1055, 6);
	else if (uom == 12); // MJ
	else if (uom != 8 && uom != 9 && uom < 11) { // volume
		if (uom == 1); // m3
		else if (uom == 2) // cubic feet = 28317cc = 0.0283m3
			scale(frac, 283, 4);
		else if (uom == 3) // CCF = 2.8317m3
			scale(frac, 283, 2);
		else if (uom == 4) // US gallon = 231 cubic inch = 3785.4cc = 0.003785m3
			scale(frac, 3785, 6);
		else if (uom == 5) // Imperial gallon = 4.54609l = 0.004546m3
			scale(frac, 4546, 6);
		else if (uom == 7) // litre = 0.001m3
			scale(frac, 1, 3);
		else if (uom == 10) // MCF = 28.317m3
			scale(frac, 283, 1);
		scale(frac, m->ConversionFactor ,m->ConversionFactorTrailingDigit >> 4);
		scale(frac, m->CalorificValue, m->CalorificValueTrailingDigit >> 4);
	}
}


// basic cluster

static int basic_read(struct meter *m, int a, uint8_t *o)
{
	if (a == ZCL_APPLICATION_VERSION_ATTRIBUTE_ID)
		return uint1_out(o, m->ApplicationVersion);
	else if (a == ZCL_STACK_VERSION_ATTRIBUTE_ID)
		return uint1_out(o, m->StackVersion);
	else if (a == ZCL_HW_VERSION_ATTRIBUTE_ID)
		return uint1_out(o, m->HWVersion);
	else if (a == ZCL_MANUFACTURER_NAME_ATTRIBUTE_ID)
		return type_pstr_out(o, m->ManufacturerName);
	else if (a == ZCL_MODEL_IDENTIFIER_ATTRIBUTE_ID)
		return type_pstr_out(o, m->ModelIdentifier);
	else if (a == ZCL_POWER_SOURCE_ATTRIBUTE_ID)
		return uint1_out(o, m->PowerSource);
	return 0;
}


// price cluster

static EmberAfStatus price_read(struct meter *m, int a, uint8_t *o)
{
	if (a == 0x300) // CommodityType
		return enum1_out(o, m->MeteringDeviceType);
	else if (a == 0x301)
		return uint4_out(o, m->Tariffs ? m->Tariffs->StandingCharge : -1);
	else if (a == 0x615)
		return enum1_out(o, m->PriceUnitOfMeasure);
	else if (a == 0x616)
		return uint2_out(o, m->Currency);
	else if (a == 0x617)
		return bitmap1_out(o, m->PriceTrailingDigit);
	else if (a == 0x620)
		return uint4_out(o,m->co2 ? m->co2->Value : -1);
	else if (a == 0x621)
		return enum1_out(o, m->co2 ? m->co2->Unit : -1);
	else if (a == 0x622)
		return bitmap1_out(o, m->co2 ? m->co2->TrailingDigit : -1);

	if (~m->PaymentControl & 2) { // credit account
		struct bill *b = bill_current(m);
		if (a >= 0x702 && b)
			b = b->next;
		if (a == 0x700 || a == 0x702) // CurrentBillingPeriodStart or LastBillingPeriodStart
			return utc_out(o, b ? b->StartTime : -1);
		else if (a == 0x701 || a == 0x703) // CurrentBillingPeriodDuration or LastBillingPeriodDuration
			return uint3_out(o, b ? (b->EndTime - b->StartTime) / 60 : -1);
		else if (a == 0x704) // LastBillingPeriodConsolidatedBill
			return uint4_out(o,b ? b->Amount : -1);
	}

	if (is_gas(m)) {
		if (a == 0x302)
			return uint4_out(o, m->ConversionFactor);
		else if (a == 0x303)
			return bitmap1_out(o, m->ConversionFactorTrailingDigit);
		else if (a == 0x304)
			return uint4_out(o, m->CalorificValue);
		else if (a == 0x305)
			return enum1_out(o, 1);
		else if (a == 0x306)
			return bitmap1_out(o, m->CalorificValueTrailingDigit);
	}

	return 0;
}


// metering cluster

static int64_t multiply_divide(int64_t n, uint32_t num, uint32_t den)
{
	uint32_t q = num / den, r = num % den;
	return n * r / den + n * q;
}

static int meter_read(struct meter *m, int a, uint8_t *o)
{
	if (a == 0x000)
		return uint6_out(o, m->CurrentSummationDelivered);
	if (a == 0x005)
		return uint2_out(o, m->DailyFreezeTime);
	if (a == 0x007)
		return utc_out(o, m->ReadingSnapShotTime);
	if (a == 0x00D)
		return uint3_out(o, m->DailyConsumptionTarget);
	if (a == 0x00F) // ProfileIntervalPeriod
		return enum1_out(o, 1);
	if (a == 0x014)
		return enum1_out(o, m->SupplyStatus);
	if (a == 0x300)
		return enum1_out(o, m->MeterUnitOfMeasure);
	if (a == 0x301)
		return uint3_out(o, m->Multiplier);
	if (a == 0x302)
		return uint3_out(o, m->Divisor);
	if (a == 0x303)
		return bitmap1_out(o, m->SummationFormatting);
	if (a == 0x306)
		return bitmap1_out(o, m->MeteringDeviceType);
	if (a == 0x307)
		return type_pstr_out(o, m->SiteID);
	if (a == 0x308)
		return type_pstr_out(o, m->MeterSerialNumber);
	if (a == 0x311)
		return type_pstr_out(o, m->CustomerIDNumber);
	if (a == 0x401)
		return uint3_out(o, history_value(&m->DayConsumption, 0));
	if (a == 0x403)
		return uint3_out(o, history_value(&m->DayConsumption, 1));
	if (a % 2 == 0) {
		if (a >= 0x420 && a <= 0x42C)
			return uint3_out(o, history_value(&m->DayConsumption, 2 + (a - 0x420) / 2));
		if (a >= 0x430 && a <= 0x43A)
			return uint3_out(o, history_value(&m->WeekConsumption, (a - 0x430) / 2));
		if (a >= 0x440 && a <= 0x45A)
			return uint4_out(o, history_value(&m->MonthConsumption, (a - 0x440) / 2));
	}
	if (a == 0x45C)
		return uint2_out(o, m->HistoricalFreezeTime);
	if (a == 0x500)
		return uint1_out(o, m->MaxNumberOfPeriodsDelivered);
	if ((a & ~255) == 0x700) {
		struct pcell *c = price_cell(m, a / 16 % 16, a % 16);
		return uint6_out(o, c ? c->Counter : -1);
	}

	if (m->MeterUnitOfMeasure != m->PriceUnitOfMeasure) {
		if (a == 0x312) // AlternativeUnitOfMeasure
			return enum1_out(o, m->PriceUnitOfMeasure);
		if (a == 0xC01) // CurrentDayConsumptionDelivered
			return uint3_out(o, alt_history_value(m, &m->DayConsumption, 0));
		if (a == 0xC03) // PreviousDayConsumptionDelivered
			return uint3_out(o, alt_history_value(m, &m->DayConsumption, 1));
		if (a >= 0xC20 && a <= 0xC2C) // PreviousDayNConsumptionDelivered
			return uint3_out(o, alt_history_value(m, &m->DayConsumption, 2 + (a - 0x420) / 2));
		if (a >= 0xC30 && a <= 0xC3A) // WeekNConsumptionDelivered
			return uint3_out(o, alt_history_value(m, &m->WeekConsumption, (a - 0x430) / 2));
		if (a >= 0xC40 && a <= 0xC5A) // MonthNConsumptionDelivered
			return uint4_out(o, alt_history_value(m, &m->MonthConsumption, (a - 0x440) / 2));
	}

	if (~m->PaymentControl & 2) { // credit account
		struct bill *b = bill_current(m);
		if (a == 0xA00) // BillToDateDelivered
			return uint4_out(o, b ? b->Amount : -1);
		if (a == 0xA01 || a == 0xA03) // BillToDateTimeStampDelivered or ProjectedBillTimeStampDelivered
			return utc_out(o, m->BillToDateTimeStampDelivered);
		if (a == 0xA02) { // ProjectedBillDelivered
			if (!b)
				return uint4_out(o, -1);
			return uint4_out(o, multiply_divide(b->Amount, b->EndTime - b->StartTime, emberAfGetCurrentTimeCallback() - b->StartTime));
		}
		if (a == 0xA04) // BillDeliveredTrailingDigit
			return bitmap1_out(o, m->PriceTrailingDigit);
	}

	if (is_elec(m)) {
		if (a == 0x400)
			return num_out(ZCL_INT24S_ATTRIBUTE_TYPE, o, m->InstantaneousDemand, 3);
		if (a == 0x207)
			return enum1_out(o, m->AmbientConsumptionIndicator - 1);
	}

	return 0;
}

static int GetProfile(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint8_t IntervalChannel;
		uint32_t EndTime;
		uint8_t NumberOfPeriods;
	} *q = cmd;

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	__PACKED_STRUCT GetProfileResponse {
		uint32_t EndTime;
		uint8_t Status, ProfileIntervalPeriod, NumberOfPeriods, Intervals[0];
	};

	uint8_t buf[sizeof(struct GetProfileResponse) + 3 * m->MaxNumberOfPeriodsDelivered];
	struct GetProfileResponse *r = (struct GetProfileResponse*)buf;

	uint32_t now = emberAfGetCurrentTimeCallback() / 3600 * 3600;
	r->EndTime = q->EndTime ? q->EndTime / 3600 * 3600 : now;
	r->NumberOfPeriods = 0;
	r->ProfileIntervalPeriod = 1;
	uint32_t ago = (now - r->EndTime) / 3600 + 1;

	if (q->IntervalChannel > 1)
		r->Status=1; // Undefined Interval Channel requested
	else if (q->IntervalChannel)
		r->Status = 2; // Interval Channel not supported
	else if (q->EndTime && q->EndTime != r->EndTime)
		r->Status = 3; // Invalid End Time
	else if (m->HourConsumption.got <= ago)
		r->Status = 5; // No intervals available for the requested time
	else {
		while (r->NumberOfPeriods < q->NumberOfPeriods && r->NumberOfPeriods < m->MaxNumberOfPeriodsDelivered)
			uint_out(r->Intervals + 3 * r->NumberOfPeriods++, history_value(&m->HourConsumption, ago++), 3);
		if (r->NumberOfPeriods == q->NumberOfPeriods)
			r->Status = 0; // Success
		else
			r->Status = 4; // More periods requested than can be returned
	}

	return send_response(m, r, sizeof(*r) + 3 * r->NumberOfPeriods, 0x00, 0x702);
}

static int GetSampledData(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint16_t SampleID;
		uint32_t EarliestSampleTime;
		uint8_t SampleType;
		uint16_t NumberOfSamples;
	} *q = cmd;

	__PACKED_STRUCT GetSampledDataResponse {
		uint16_t SampleID;
		uint32_t SampleStartTime;
		uint8_t SampleType;
		uint16_t SampleRequestInterval, NumberOfSamples;
		uint8_t Samples[0];
	};

	if (af->bufLen - af->payloadStartIndex < sizeof(*q))
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;
	if (q->SampleID || q->SampleType)
		return EMBER_ZCL_STATUS_INVALID_FIELD;

	uint32_t now = emberAfGetCurrentTimeCallback() / 3600 * 3600;
	int ago = (now - q->EarliestSampleTime) / 3600;
	if (ago < 0 || m->HourConsumption.got == 0)
		return EMBER_ZCL_STATUS_NOT_FOUND;
	if (ago > m->HourConsumption.got - 1)
		ago = m->HourConsumption.got - 1;

	uint8_t buf[sizeof(struct GetSampledDataResponse) + 3 * m->MaxNumberOfPeriodsDelivered];
	struct GetSampledDataResponse *r = (struct GetSampledDataResponse*)buf;
	r->SampleID = q->SampleID;
	r->SampleStartTime = now - ago * 3600;
	r->SampleType = q->SampleType;
	r->SampleRequestInterval = 3600;
	r->NumberOfSamples = 0;

	uint8_t *s = r->Samples;
	do
		s = uint_out(s, history_value(&m->HourConsumption, ago--), 3);
	while (++r->NumberOfSamples < q->NumberOfSamples && r->NumberOfSamples < m->MaxNumberOfPeriodsDelivered && ago >= 0);

	return send_response(m, r, s - buf, 0x07, 0x702);
}

static int LocalChangeSupply(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT {
		uint8_t ProposedSupplyStatus;
	} *q = cmd;

	if (q->ProposedSupplyStatus != 1 && q->ProposedSupplyStatus != 2)
		return EMBER_ZCL_STATUS_INVALID_VALUE;
	else if (q->ProposedSupplyStatus == 2 && m->CreditStatus & 64)
		return EMBER_ZCL_STATUS_ACTION_DENIED;

	m->SupplyStatus = q->ProposedSupplyStatus;
	return EMBER_ZCL_STATUS_SUCCESS;
}

static int meter_cmd(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	if (af->commandId == 0x00)
		return GetProfile(m, af, cmd);
	if (af->commandId == 0x08)
		return GetSampledData(m, af, cmd);
	if (af->commandId == 0x0C)
		return LocalChangeSupply(m, af, cmd);
	return EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND;
}


// messaging cluster

int SendDisplayMessage(cmd_sink *send, struct meter *m, struct message *g)
{
	__PACKED_STRUCT DisplayMessage {
		uint32_t MessageID;
		uint8_t MessageControl;
		uint32_t StartTime;
		uint16_t DurationInMinutes;
		uint8_t Message[1];
		//uint8 ExtendedMessageControl;
	};

	uint8_t buf[sizeof(struct DisplayMessage) + strlen(g->Message) + 1];
	struct DisplayMessage *p = (struct DisplayMessage*)buf;

	p->MessageID = g->ID;
	p->MessageControl = g->Control;
	p->StartTime = g->StartTime;
	p->DurationInMinutes = g->Minutes ? g->Minutes : 65535;
	uint8_t *end = pstrout(p->Message, g->Message);
	if (g->Control & 32) // enhanced confirmation
		*end++ = 0;

	return send(m, p, end - buf, 0x00, 0x703);
}

int SendCancelMessage(cmd_sink *send, struct meter *m, struct message *g)
{
	__PACKED_STRUCT {
		uint32_t MessageID;
		uint8_t MessageControl;
	} r = {g->ID, 0};

	return send(m, &r, sizeof(r), 0x01, 0x703);
}

static int GetLastMessage(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	if (!m->Messages)
		return EMBER_ZCL_STATUS_NOT_FOUND;
	return SendDisplayMessage(send_response, m, m->Messages);
}

static int MessageConfirmation(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	__PACKED_STRUCT MessageConfirmation {
		uint32_t MessageID, ConfirmationTime;
		// optional
		uint8_t MessageConfirmationControl, MessageConfirmationResponse[0];
	} *c = cmd;

	if (af->bufLen - af->payloadStartIndex < 8)
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	struct message *g = m->Messages;
	while (g && g->ID != c->MessageID)
		g = g->next;
	if (!g)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	g->Control &= ~128; // confirmed
	if (af->bufLen - af->payloadStartIndex >= 9)
		g->Control &= ~32; // enhanced confirm

	return EMBER_ZCL_STATUS_SUCCESS;
}

static int message_cmd(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	if (af->commandId == 0x00)
		return GetLastMessage(m, af, cmd);
	if (af->commandId == 0x01)
		return MessageConfirmation(m, af, cmd);
	return EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND;
}


// prepay cluster

static int prepay_read(struct meter *m, int a, uint8_t *o)
{
	if (a == 0x000)
		return bitmap2_out(o, m->PaymentControl);
	if (a == 0x500)
		return bitmap1_out(o, m->HistoricalCostConsumptionFormatting);
	if (a == 0x502)
		return enum1_out(o, m->CurrencyScalingFactor);
	if (a == 0x503)
		return uint2_out(o, m->Currency);
	if (a % 2 == 0) {
		if (a >= 0x51C && a <= 0x52C) // DayNCostConsumptionDelivered
			return uint6_out(o, history_cost(&m->DayConsumption, m->DayCost, (a - 0x51C) / 2));
		if (a >= 0x530 && a <= 0x53A) // WeekNCostConsumptionDelivered
			return uint6_out(o, history_cost(&m->WeekConsumption, m->WeekCost, (a - 0x530) / 2));
		if (a >= 0x540 && a <= 0x55A) // MonthNCostConsumptionDelivered
			return uint6_out(o, history_cost(&m->MonthConsumption, m->MonthCost, (a - 0x540) / 2));
	}
	if (a == 0x55C)
		return uint2_out(o, m->HistoricalFreezeTime);

	if (m->PaymentControl & 2) { // prepay account
		if (a == 0x001)
			return sint4_out(o, m->CreditRemaining);
		if (a == 0x002)
			return sint4_out(o, m->EmergencyCreditRemaining);
		if (a == 0x003)
			return bitmap1_out(o, m->CreditStatus);
		if (a == 0x004)
			return utc_out(o, m->CreditRemainingTimeStamp);
		if (a == 0x005)
			return sint4_out(o, m->AccumulatedDebt);
		if (a == 0x006)
			return sint4_out(o, m->OverallDebtCap);
		if (a == 0x010)
			return uint4_out(o, m->EmergencyCreditLimitAllowance);
		if (a == 0x011)
			return uint4_out(o, m->EmergencyCreditThreshold);
		if (a == 0x020)
			return uint6_out(o, m->TotalCreditAdded);
		if (a == 0x021)
			return uint4_out(o, m->MaxCreditLimit);
		if (a == 0x022)
			return uint4_out(o, m->MaxCreditPerTopUp);
		if (a == 0x030)
			return uint1_out(o, m->FriendlyCreditWarning);
		if (a == 0x040)
			return sint4_out(o, m->CutOffValue);
		if (a == 0x080)
			return type_pstr_out(o, m->TokenCarrierID);
		if (a == 0x400)
			return bitmap2_out(o, m->PrepaymentAlarmStatus);

		if (a >= 0x100 && a < 0x150) {
			struct topup *p = m->Topups;
			while (p && a >= 0x110)
				p = p->next, a -= 16;
			if (!p)
				return 0;
			if (a == 0x100)
				return utc_out(o, p->When);
			if (a == 0x101)
				return sint4_out(o, p->Amount);
			if (a == 0x102)
				return enum1_out(o, p->OriginatingDevice);
			if (a == 0x103)
				return type_pstr_out(o, p->Code);
		} else if (a >= 0x210 && a < 0x240) {
			struct debt *p = debt_find(m->Debts, (a - 0x200) / 16);
			if (!p)
				return 0;
			a %= 16;
			if (a == 0x0)
				return type_pstr_out(o, p->Label);
			if (a == 0x1)
				return uint4_out(o, p->Amount);
			if (a == 0x2)
				return enum1_out(o, p->RecoveryMethod);
			if (a == 0x3)
				return utc_out(o, p->RecoveryStartTime);
			if (a == 0x4)
				return uint2_out(o, p->NextCollection / 60 % 1440);
			if (a == 0x6)
				return enum1_out(o, p->RecoveryFrequency);
			if (a == 0x7)
				return uint4_out(o, p->RecoveryAmount);
			if (a == 0x9)
				return uint2_out(o, p->RecoveryTopUpPercentage);
		}
	}

	return 0;
}

__WEAK int SelectAvailableEmergencyCredit(struct meter *m, uint32_t CommandIssueDateTime, uint8_t OriginatingDevice) { return EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND; }
__WEAK int ConsumerTopUp(struct meter *m, uint8_t OriginatingDevice, uint8_t *TopUpCode) { return EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND; }

static int walk_topups(struct topup *t, uint32_t LatestEndTime, int NumberOfRecords, uint8_t *out)
{
	int bytes = 0;
	for (; t; t = t->next) {
		if (t->When > LatestEndTime)
			continue;
		if (out) {
			out = pstrout(out, t->Code);
			out = uint_out(out, t->Amount, 4);
			out = uint_out(out, t->When, 4);
		} else
			bytes += 9 + strlen(t->Code);
		if (--NumberOfRecords == 0)
			break;
	}
	return bytes;
}

static int SendPublishTopUpLog(cmd_sink *send, struct meter *m, uint32_t LatestEndTime, int NumberOfRecords)
{
	int bytes = walk_topups(m->Topups, LatestEndTime, NumberOfRecords, 0);
	if (bytes == 0)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	bytes += 2;
	uint8_t tmp[bytes];
	tmp[0] = 0, tmp[1] = 1;
	walk_topups(m->Topups, LatestEndTime, NumberOfRecords, tmp + 2);

	return send(m, tmp, bytes, 0x05, 0x705);
}

static int walk_repays(struct repay *r, uint32_t LatestEndTime, uint8_t NumberOfDebts, uint8_t DebtType, uint8_t *out)
{
	int did = 0;
	for (; r; r = r->next) {
		if (r->CollectionTime > LatestEndTime || DebtType < 3 && DebtType != r->DebtType)
			continue;
		if (out) {
			out = uint_out(out, r->CollectionTime, 4);
			out = uint_out(out, r->AmountCollected, 4);
			*out++ = r->DebtType;
			out = uint_out(out, r->OutstandingDebt, 4);
		}
		did++;
		if (--NumberOfDebts == 0)
			break;
	}
	return did;
}

static int SendPublishDebtLog(cmd_sink *send, struct meter *m, uint32_t LatestEndTime, uint8_t NumberOfDebts, uint8_t DebtType)
{
	int count = walk_repays(m->Repays, LatestEndTime, NumberOfDebts, DebtType, 0);
	if (count == 0)
		return EMBER_ZCL_STATUS_NOT_FOUND;

	int bytes = 2 + count * 13;
	uint8_t tmp[bytes];
	tmp[0] = 0, tmp[1] = 1;
	walk_repays(m->Repays, LatestEndTime, NumberOfDebts, DebtType, tmp + 2);

	return send(m, tmp, bytes, 0x06, 0x705);
}

static int prepay_cmd(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	if (af->commandId == 0x00) {
		if (af->bufLen - af->payloadStartIndex < 5)
			return EMBER_ZCL_STATUS_MALFORMED_COMMAND;
		__PACKED_STRUCT { uint32_t CommandIssueDateTime; uint8_t OriginatingDevice; } *p = cmd;
		return SelectAvailableEmergencyCredit(m, p->CommandIssueDateTime, p->OriginatingDevice);
	}

	if (af->commandId == 0x04) {
		__PACKED_STRUCT { uint8_t OriginatingDevice; uint8_t TopUpCode[1]; } *p = cmd;
		uint8_t *end = pstrskip(p->TopUpCode);
		if (end > af->buffer + af->bufLen)
			return EMBER_ZCL_STATUS_MALFORMED_COMMAND;
		return ConsumerTopUp(m, p->OriginatingDevice, p->TopUpCode);
	}

	if (af->commandId == 0x08) {
		__PACKED_STRUCT { uint32_t LatestEndTime; uint8_t NumberOfRecords; } *p = cmd;
		if (af->bufLen - af->payloadStartIndex < 5)
			return EMBER_ZCL_STATUS_MALFORMED_COMMAND;
		return SendPublishTopUpLog(send_response, m, p->LatestEndTime, p->NumberOfRecords);
	}

	if (af->commandId == 0x0A) {
		__PACKED_STRUCT { uint32_t LatestEndTime; uint8_t NumberOfDebts, DebtType; } *p = cmd;
		if (af->bufLen - af->payloadStartIndex < 6)
			return EMBER_ZCL_STATUS_MALFORMED_COMMAND;
		return SendPublishDebtLog(send_response, m, p->LatestEndTime, p->NumberOfDebts, p->DebtType);
	}

	return EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND;
}


// device management cluster

static int devman_read(struct meter *m, int a, uint8_t *o)
{
	struct cos *s = m->Coss;
	struct cot *t = m->Cots;
	if (a == 0x100)
		return uint4_out(o, m->ProviderID);
	else if (a == 0x101)
		return type_pstr_out(o, m->ProviderName);
	else if (a == 0x102)
		return type_pstr_out(o, m->ProviderContactDetails);
	else if (a == 0x110)
		return uint4_out(o, s ? s->SupplierID : -1);
	else if (a == 0x111)
		return type_pstr_out(o, s ? s->SupplierName : 0);
	else if (a == 0x112)
		return utc_out(o, s ? s->When : 0);
	else if (a == 0x113)
		return bitmap4_out(o, s ? s->ChangeControl : 0);
	else if (a == 0x114)
		return type_pstr_out(o, s ? s->ContactDetails : 0);
	else if (a == 0x200)
		return utc_out(o, t ? t->When : 0);
	else if (a == 0x201)
		return bitmap4_out(o, t ? t->ChangeControl : 0);
	else if (a == 0x400)
		return uint4_out(o, m->LowMediumThreshold);
	else if (a == 0x401)
		return uint4_out(o, m->MediumHighThreshold);
	return 0;
}

int SendPublishChangeOfTenancy(cmd_sink *send, struct meter *m, struct cot *c)
{
	__PACKED_STRUCT {
		uint32_t ProviderID, IssuerEventID;
		uint8_t TariffType;
		uint32_t ImplementationTime;
		uint32_t ChangeControl;
	} pay = {m->ProviderID, c->IssuerEventID, 0, c->When, c->ChangeControl};
	return send(m, &pay, sizeof(pay), 0x00, 0x708);
}

int SendPublishChangeOfSupplier(cmd_sink *send, struct meter *m, struct cos *c)
{
	__PACKED_STRUCT {
		uint32_t CurrentProviderID, IssuerEventID;
		uint8_t TariffType;
		uint32_t ProposedProviderID, ImplementationTime, ChangeControl;
		uint8_t ProposedProviderName[17], ProposedProviderContactDetails[20];
	} pay = {m->ProviderID, c->IssuerEventID, 0, c->SupplierID, c->When, c->ChangeControl};

	uint8_t *end = pstrout(pay.ProposedProviderName, c->SupplierName);
	end = pstrout(end, c->ContactDetails);

	return send(m, &pay, end - (uint8_t*)&pay, 0x01, 0x708);
}

static int SendUpdateSiteID(cmd_sink *send,struct meter *m)
{
	__PACKED_STRUCT {
		uint32_t IssuerEventID, SiteIDTime, ProviderID;
		uint8_t SiteID[33];
	} pay = {emberAfGetCurrentTimeCallback(), 0, m->ProviderID};

	uint8_t *end = pstrout(pay.SiteID, m->SiteID);

	return send(m, &pay, end - (uint8_t*)&pay, 0x03, 0x708);
}

static int SendUpdateCIN(cmd_sink *send, struct meter *m)
{
	__PACKED_STRUCT {
		uint32_t IssuerEventID, ImplementationTime, ProviderID;
		uint8_t CustomerIDNumber[25];
	} pay = {emberAfGetCurrentTimeCallback(), 0, m->ProviderID};

	uint8_t *end = pstrout(pay.CustomerIDNumber, m->CustomerIDNumber);

	return send(m, &pay, end - (uint8_t*)&pay, 0x06, 0x708);
}

static int devman_cmd(struct meter *m, EmberAfClusterCommand *af, void *cmd)
{
	if (af->commandId == 0x00) {
		if (!m->Cots)
			return EMBER_ZCL_STATUS_NOT_FOUND;
		return SendPublishChangeOfTenancy(send_response, m, m->Cots);
	}

	if (af->commandId == 0x01) {
		if (!m->Coss)
			return EMBER_ZCL_STATUS_NOT_FOUND;
		return SendPublishChangeOfSupplier(send_response, m, m->Coss);
	}

	if (af->commandId == 0x03)
		return SendUpdateSiteID(send_response, m);

	if (af->commandId == 0x05)
		return SendUpdateCIN(send_response, m);

	return EMBER_ZCL_STATUS_UNSUP_CLUSTER_COMMAND;
}


// dispatch

static bool run_command(struct meter *m, EmberAfClusterCommand *af)
{
	int r;
	if (af->apsFrame->clusterId == 0x700)
		r = price_cmd(m, af, af->buffer + af->payloadStartIndex);
	else if (af->apsFrame->clusterId == 0x702)
		r = meter_cmd(m, af, af->buffer + af->payloadStartIndex);
	else if (af->apsFrame->clusterId == 0x703)
		r = message_cmd(m, af, af->buffer + af->payloadStartIndex);
	else if (af->apsFrame->clusterId == 0x705)
		r = prepay_cmd(m, af, af->buffer + af->payloadStartIndex);
	else if (af->apsFrame->clusterId == 0x707)
		r = calendar_cmd(m, af, af->buffer + af->payloadStartIndex);
	else if (af->apsFrame->clusterId == 0x708)
		r = devman_cmd(m, af, af->buffer + af->payloadStartIndex);
	else
		return false;

	if (r != 'R') { // handler sent a (non-default) response
		if (r || ~af->buffer[0] & ZCL_DISABLE_DEFAULT_RESPONSE_MASK)
			emberAfSendDefaultResponse(af, (EmberAfStatus)r);
	}
	return true;
}

static int read_attr_type_value(struct meter *m, int id, uint8_t *out, int cluster)
{
	if (cluster == 0x000)
		return basic_read(m, id, out);
	else if (cluster == 0x700)
		return price_read(m, id, out);
	else if (cluster == 0x702)
		return meter_read(m, id, out);
	else if (cluster == 0x705)
		return prepay_read(m, id, out);
//	else if (cluster == 0x707) return tou_read(m, id, out);
	else if (cluster == 0x708)
		return devman_read(m, id, out);
	return 0;
}

static bool read_attrs(struct meter *m, EmberAfClusterCommand *af)
{
	emberAfPutInt8uInResp(ZCL_FRAME_CONTROL_SERVER_TO_CLIENT | EMBER_AF_DEFAULT_RESPONSE_POLICY_RESPONSES);
	emberAfPutInt8uInResp(af->seqNum);
	emberAfPutInt8uInResp(ZCL_READ_ATTRIBUTES_RESPONSE_COMMAND_ID);

	uint8_t *p = af->buffer + af->payloadStartIndex, *end = af->buffer + af->bufLen, tmp[96];
	while (p + 2 <= end) {
		uint16_t id = *p++;
		id |= *p++ << 8;
		// TODO unauthorised?
		int len = read_attr_type_value(m, id, tmp, af->apsFrame->clusterId);

		if (appResponseLength + 3 + len > EMBER_AF_RESPONSE_BUFFER_LEN)
			break;

		emberAfPutInt16uInResp(id);
		if (len) {
			emberAfPutInt8uInResp(EMBER_ZCL_STATUS_SUCCESS);
			emberAfPutBlockInResp(tmp, len);
		} else
			emberAfPutInt8uInResp(EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE);
	}
	emberAfSendResponse();

	return false;
}


// exports

struct meter* meters(void)
{
	return used;
}

struct meter* meter_new(void)
{
	if (!used && !gone) {
		gone = pool;
		pool[0].next = pool + 1;
		pool[1].next = pool + 2;
	}

	struct meter *m = gone;
	if (m) {
		gone = m->next;
		memset(m, 0, sizeof(*m));
		m->next = used, used = m;
		m->ep = m - pool + 2;
		m->obj[0] = 'm', m->obj[1] = 't', m->obj[2] = 'r', m->obj[3] = '0' + m->ep, 

		m->StackVersion = EMBER_MAJOR_VERSION << 4 | EMBER_MINOR_VERSION;
		m->MonthConsumption.top = 14;
		m->WeekConsumption.top = 6;
		m->DayConsumption.top = 9;
		m->HourConsumption.top = 25;
		m->FriendlyCreditWarning = 10;
		m->Currency = 826;
		m->PriceTrailingDigit = 0x30;
		m->Multiplier = 1;
		m->Divisor = 1000;
		m->MaxNumberOfPeriodsDelivered = 12;
	}

	return m;
}

struct meter* meter_find(uint8_t endpoint)
{
	struct meter *m = used;
	while (m && m->ep != endpoint)
		m = m->next;
	return m;
}

void meter_free(struct meter *m)
{
	struct meter **pp = &used, *p;
	while (p = *pp) {
		if (p != m) {
			pp = &p->next;
			continue;
		}

		price_free(p);
		debts_free(p);
		calendars_free(p);
		topups_free(p);

		struct message *m;
		while (m = p->Messages)
			p->Messages = m->next, free(m);

		struct cot *t;
		while (t = p->Cots)
			p->Cots = t->next, free(t);

		struct cos *s;
		while (s = p->Coss)
			p->Coss = s->next, free(s);

		*pp = p->next;
		p->next = gone, gone = p;
		return;
	}
}

bool emberAfPreCommandReceivedCallback(EmberAfClusterCommand *af)
{
	// handle meter attributes and commands here to avoid the znet AF
	// TODO need to work out how to add meter endpoints to discovery processing
	struct meter *m = meter_find(af->apsFrame->destinationEndpoint);
	if (af->mfgSpecific || !m);
	else if (af->clusterSpecific)
		return run_command(m, af);
	else if (af->commandId == ZCL_READ_ATTRIBUTES_COMMAND_ID && af->direction == ZCL_DIRECTION_CLIENT_TO_SERVER)
		return read_attrs(m, af);
	return false;
}

EmberAfStatus emberAfExternalAttributeReadCallback(uint8_t endpoint, EmberAfClusterId clusterId, EmberAfAttributeMetadata *attributeMetadata, uint16_t manufacturerCode, uint8_t *buffer, uint16_t maxReadLength)
{
	// emberAfPreCommandReceivedCallback ought to already have dealt with this
	return EMBER_ZCL_STATUS_SUCCESS;
}

EmberAfStatus emberAfExternalAttributeWriteCallback(uint8_t endpoint, EmberAfClusterId clusterId, EmberAfAttributeMetadata *attributeMetadata, uint16_t manufacturerCode, uint8_t *buffer)
{
	return EMBER_ZCL_STATUS_SUCCESS;
}

struct debt* debt_find(struct debt *list, uint8_t Ordinal)
{
	while (list && list->Ordinal != Ordinal)
		list = list->next;
	return list;
}

void debts_free(struct meter *m)
{
	struct debt *d;
	while (d = m->Debts)
		m->Debts = d->next, free(d);

	struct repay *r;
	while (r = m->Repays)
		m->Repays = r->next, free(r);
}

void topups_free(struct meter *m)
{
	struct topup *t;
	while (t = m->Topups)
		m->Topups = t->next, free(t);
}

int64_t convert(int64_t v, struct meter *m, int from, int to)
{
	uint32_t frac[3] = {1, 1};
	mj_factor(m, to, frac);
	frac[2] = frac[0];
	mj_factor(m, from, frac + 1);
	uint32_t q = frac[1] / frac[2], r = frac[1] % frac[2];
	return v * r / frac[2] + v * q;
}

int send_response(struct meter *m, void *pay, int bytes, uint8_t cmd, uint16_t clusterID)
{
	emberAfPutInt8uInResp(ZCL_FRAME_CONTROL_SERVER_TO_CLIENT | ZCL_CLUSTER_SPECIFIC_COMMAND | EMBER_AF_DEFAULT_RESPONSE_POLICY_RESPONSES);
	emberAfPutInt8uInResp(emberAfCurrentCommand()->seqNum);
	emberAfPutInt8uInResp(cmd);
	emberAfPutBlockInResp(pay, bytes);
	emberAfSendResponse();
	appResponseLength = 0; // in case another response, not sure this'll cut it, might need to do something similar to prepareForResponse

	return 'R';
}

int push_to_bound(struct meter *m, void *pay, int bytes, uint8_t cmd, uint16_t clusterID)
{
	EmberApsFrame *f = emberAfGetCommandApsFrame();
	f->sourceEndpoint = m->ep;
	int len = emberAfFillExternalBuffer(ZCL_CLUSTER_SPECIFIC_COMMAND | ZCL_FRAME_CONTROL_SERVER_TO_CLIENT, clusterID, cmd, "b", pay, bytes);
	emberAfSendUnicastToBindings(f, len, emAfZclBuffer);

	return 0;
}

uint32_t ProviderID(struct meter *m, uint32_t when)
{
	struct cos *s = m->Coss;
	return s && when >= s->When ? s->SupplierID : m->ProviderID;
}

uint8_t* uint_out(uint8_t *out, int64_t num, int bytes)
{
	while (bytes--) {
		*out++ = num;
		num >>= 8;
	}
	return out;
}

void* pstrout(uint8_t *out, char *s)
{
	if (!s) {
		*out++ = 255;
		return out;
	}

	uint8_t *p = out;
	while (*s)
		*++p = *s++;
	*out = p - out;

	return ++p;
}

char* pstrcpy(char *d, uint8_t *s, int roof)
{
	uint8_t *end = pstrskip(s++);
	while (s < end && --roof && *s >= 32)
		*d++ = *s++;
	*d++ = 0;
	return d;
}

int strpcmp(char *a, uint8_t *b)
{
	uint8_t *end = pstrskip(b++);
	int left = end - b;
	while (left && *a++ == *b++)
		left--;
	return left ? left : *a;
}

int is_gas(struct meter *m)
{
	return m->MeteringDeviceType == 1 || m->MeteringDeviceType == 128;
}

int is_elec(struct meter *m)
{
	int mdt = m->MeteringDeviceType % 127;
	return mdt == 0 || mdt == 13 || mdt == 14 || mdt == 15; 
}
