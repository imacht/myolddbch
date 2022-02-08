#ifndef DBCH_H
#define DBCH_H


#include EMBER_AF_API_AF_HEADER


// TODO
// * add fake meter and mechanism to store persistently
// * replace time cluster server
// * better linker script for malloc
// * boot-loader for serial updates?

// CORE
// esi-dongle does update the wrong blocksummation attribute: instead of CurrentTier2Block2SummationDelivered it fills CurrentTier3Block1SummationDelivered
// open issue on GitHub: #5 Proper GSME support

// CTRL INTERFACE
// Ability to set OTA jitter notification
// Ability to mimic weak signal conditions by manipulating radio signal or noise (+/-)  --maybe one option to combine multiple dongles to create a noisy channel--: will add command to set TX power
// Ability to set Supplier target
// invoke channel change
// invoke NWK key update

// GUI APP
// esi-app crash.  it seems not able to push 260 characters message on HAN (GBCS requirement is 116 characters)
// CoT and CoS don't work properly
// FCP Calendar type=3 support for all meters type / Simulate a cut off period on CH (already possible via term interface)
// open issue on Trac: #95 ESI-APP: topup from esi-app option to call add_credit

// GBCS SUPPORT
// Ability to inject GBCS messages
// Support for GBCS join between ppmid and meters/GPF
// Support for CS08


// malloc

void* qalloc(size_t bytes);
void* malloc(size_t bytes);
void* zalloc(size_t bytes);
void free(void *ptr);
void heap_dump(void);



// time

void time_start(void);
uint32_t utc_to_local(uint32_t utc);
uint32_t local_to_utc(uint32_t t);
int day_of_week(uint32_t utc);
extern void metro(uint32_t now);


// meters

struct history {
	uint8_t top, got, now;
	uint32_t val[0];
};

struct pcell {
	uint32_t UnitRate, Threshold, Counter; // runtime
};

struct tariff {
	struct tariff *next;
	uint32_t StartTime, StandingCharge, IssuerEventID;
	uint8_t PriceTiersInUse, BlockThresholdsInUse, TierBlockMode;
	char Label[25];
	struct price {
		struct price *next;
		struct tariff *boss;
		char Label[13];
		uint8_t Tier, Thresholds, Block; // runtime
		struct pcell Cells[1];
	} *Prices;
};

typedef union {
	struct {
		uint8_t Year;		///< [0,254] -> [1900,2154], 255 = don't care
		uint8_t Month;		///< [1,12] -> [January,December], 255 = don't care
		uint8_t MonthDay;	///< [1,31]. [29,31] may be invalid depending on the month and year, 255 = don't care
		uint8_t WeekDay;	///< [1,7] -> [Monday,Sunday], 255 = don't care
	};
	uint32_t All;
} ZigBeeDate_t;

struct day {
	struct day *next;
	uint16_t StartTime;
	uint8_t ID, PriceTier; // or FriendlyCreditEnable or AuxiliaryLoadSwitchState;
};

struct week {
	struct week *next;
	uint8_t ID, Days[7]; // Monday - Sunday
};

struct speason {
	struct speason *next;
	ZigBeeDate_t When;
	union {
		uint8_t WeekID;
		uint8_t DayID;
	};
};

struct calendar {
	struct calendar *next;
	uint32_t StartTime, IssuerEventID;
	struct day *Days;
	struct week *Weeks;
	struct speason *Seasons, *Specials;
	uint8_t Type, DayCount, WeekCount, SeasonCount;
	char Name[13];
};

struct bill {
	struct bill *next;
	uint32_t StartTime, EndTime, IssuerEventID, Amount;
	uint8_t DurationType;
};

struct topup {
	struct topup *next;
	uint32_t When;
	int32_t Amount;
	uint8_t OriginatingDevice;
	char Code[26];
};

struct repay {
	struct repay *next;
	uint32_t CollectionTime, AmountCollected, OutstandingDebt;
	uint8_t DebtType;
};

struct message {
	struct message *next;
	uint32_t StartTime, ID;
	uint16_t Minutes;
	uint8_t Control; // as per spec
	char Message[1];
};

struct cot {
	struct cot *next;
	uint32_t When, IssuerEventID, ChangeControl;
};

struct cos {
	struct cos *next;
	uint32_t When, IssuerEventID, ChangeControl, SupplierID;
	char SupplierName[17];
	char ContactDetails[20];
};

struct debt {
	struct debt *next;
	uint32_t Amount, RecoveryStartTime, RecoveryAmount, NextCollection;
	uint16_t RecoveryTopUpPercentage;
	uint8_t RecoveryMethod, RecoveryFrequency, Ordinal;
	char Label[13];
};

struct co2 {
	struct co2 *next;
	uint32_t StartTime, IssuerEventID, Value;
	uint8_t Unit, TrailingDigit;
};

struct meter {
	// book-keeping & runtime
	struct meter *next;
	uint32_t Joules, Spend, PriceEnd, NextTopup;
	struct price *PriceNow;
	uint8_t ep;
	char obj[5];
	char random_demand : 1;

	// general basic/time
	uint8_t ApplicationVersion, StackVersion, HWVersion, PowerSource;
	char ManufacturerName[33], ModelIdentifier[33];

	// price
	struct tariff *Tariffs; // first is current
	struct bill *Bills; // descending time
	struct co2 *co2;
	uint32_t ConversionFactor, CalorificValue;
	uint8_t ConversionFactorTrailingDigit, CalorificValueTrailingDigit;
	uint8_t PriceTrailingDigit, MeterUnitOfMeasure, PriceUnitOfMeasure;

	// metering
	struct { uint8_t top, got, now; uint32_t val[14]; } MonthConsumption;
	struct { uint8_t top, got, now; uint32_t val[6]; } WeekConsumption;
	struct { uint8_t top, got, now; uint32_t val[9]; } DayConsumption;
	struct { uint8_t top, got, now; uint32_t val[25]; } HourConsumption;
	uint64_t CurrentSummationDelivered;
	uint32_t ReadingSnapShotTime, BillToDateTimeStampDelivered;
	int32_t DailyConsumptionTarget, Multiplier, Divisor, InstantaneousDemand;
	uint16_t DailyFreezeTime, HistoricalFreezeTime;
	uint8_t MeteringDeviceType, SupplyStatus, AmbientConsumptionIndicator, SummationFormatting;
	uint8_t MaxNumberOfPeriodsDelivered;
	char SiteID[33], MeterSerialNumber[25], CustomerIDNumber[25];

	// messages
	struct message *Messages;

	// prepay
	struct topup *Topups; // descending time
	struct repay *Repays; // descending time
	struct debt *Debts;
	uint64_t TotalCreditAdded, DayCost[9], WeekCost[6], MonthCost[14];
	int32_t CreditRemaining, EmergencyCreditRemaining, AccumulatedDebt, OverallDebtCap, EmergencyCreditLimitAllowance, CutOffValue;
	uint32_t CreditRemainingTimeStamp, MaxCreditLimit, MaxCreditPerTopUp, EmergencyCreditThreshold, LowCreditWarning, NextFriendlyCreditSwitch;
	uint16_t PaymentControl, Currency, PrepaymentAlarmStatus;
	uint8_t CreditStatus, FriendlyCreditWarning, HistoricalCostConsumptionFormatting, CurrencyScalingFactor;
	char TokenCarrierID[21];

	// calendar
	struct calendar *Calendars;

	// device management
	struct cot *Cots;
	struct cos *Coss;
	uint32_t ProviderID, LowMediumThreshold, MediumHighThreshold;
	char ProviderName[17], ProviderContactDetails[20];
};

struct meter* meters(void);
struct meter* meter_new(void);
struct meter* meter_find(uint8_t endpoint);
void meter_free(struct meter *m);

int call_mtrs_set(void *ctx, char *val);

struct debt* debt_find(struct debt *list, uint8_t Ordinal);
void debts_free(struct meter *m);
void topups_free(struct meter *m);
int64_t convert(int64_t v, struct meter *m, int from, int to);
uint32_t ProviderID(struct meter *m, uint32_t when);
uint8_t* uint_out(uint8_t *out, int64_t num, int bytes);
void* pstrout(uint8_t *out, char *s);
char* pstrcpy(char *d, uint8_t *s, int roof);
int strpcmp(char *a, uint8_t *b);
int is_gas(struct meter *m);
int is_elec(struct meter *m);

typedef int cmd_sink(struct meter *m, void *payload, int bytes, uint8_t cmd, uint16_t clusterID);
cmd_sink send_response, push_to_bound;

int SendDisplayMessage(cmd_sink *send, struct meter *m, struct message *g);
int SendCancelMessage(cmd_sink *send, struct meter *m, struct message *g);

int SendPublishChangeOfTenancy(cmd_sink *send, struct meter *m, struct cot *c);
int SendPublishChangeOfSupplier(cmd_sink *send, struct meter *m, struct cos *c);


// price cluster

int SendPublishTariffInformation(cmd_sink *send, struct meter *m, struct tariff *t);
int SendPublishPrice(cmd_sink *send, struct meter *m, uint32_t start, uint32_t end, struct price *p);
int SendPublishCO2Value(cmd_sink *send, struct meter *m, struct co2 *c);
int SendPublishConsolidatedBill(cmd_sink *send, struct meter *m, struct bill *b);
int SendCancelTariff(cmd_sink *send, struct meter *m, struct tariff *t);

int price_cmd(struct meter *m, EmberAfClusterCommand *af, void *cmd);
void price_free(struct meter *m);

struct bill* bill_new(struct meter *m, uint32_t StartTime);
struct bill* bill_current(struct meter *m);
void bills_free(struct meter *m);
struct pcell* price_cell(struct meter *m, int tier, int block);
struct price* price_find_tier(struct price *p, int tier);

void tariff_free(struct tariff *t);


// calendar cluster

int SendPublishCalendar(cmd_sink *send, struct meter *m, struct calendar *c);
int SendPublishSpecialDays(cmd_sink *send, struct meter *m, struct calendar *c, uint32_t StartTime, int NumberOfEvents);
int SendCancelCalendar(cmd_sink *send, struct meter *m, struct calendar *c);

int calendar_cmd(struct meter *m, EmberAfClusterCommand *af, void *cmd);
struct calendar* calendar_next_type(struct calendar *p, int type);
int walk_calendar(struct meter *m, uint32_t when, uint8_t type, int (*got)(struct meter*, struct calendar*, uint32_t start, uint32_t end, uint8_t val));
void calendar_free(struct calendar *c);
void calendars_free(struct meter *m);


// DOAP

typedef int dget_f(void *ctx);
typedef int dset_f(void *ctx, char *val);
typedef void objp_f(int index, char *value, void *ctx);

struct doap_attr {
	const char *name;
	dget_f *get;
	dset_f *set;
	const char *desc;
};

void doap_obj_add(const char *name, const struct doap_attr *attr, void *ctx);
void doap_mtr_add(struct meter *m);
void doap_obj_del(const char *name);
int doap_obj_parse(char *text, objp_f *got, void *ctx);
int doap_run(char *cmd);

int64_t hex2int(char *h);
int hex2bin(char *h, uint8_t *b);
int hex2nib(char *h, uint8_t *b); // ass-backward
uint32_t hex2time(char *hex);

int uartf(const char *fmt, ...);


// persistent storage that's not shite

/** Initialise NV store. */
void nv_init(void);

/** If the NV item does not already exist, it is created and initialized with the data passed to the function, if any.
This function must be called before calling nv_read() or nv_write().
 \param id Valid NV item Id
 \param len Item length
 \param buf Pointer to item initalization data. Set to NULL if none
 \return 'u': Id did not exist and was created successfully. 0: Id already existed, no action taken. 'f': Failure to find or create Id. */
int nv_item_init(uint16_t id, uint16_t len, void *buf);

/** Write a data item to NV.
Function can write an entire item to NV or an element of an item by indexing into the item with an offset.
 \param id Valid NV item Id
 \param ndx Index offset into item
 \param len Length of data to write
 \param buf Data to write
 \return 0 if successful, 'u' if item did not exist in NV and offset is non-zero, 'f' if failure. */
int nv_write(uint16_t id, uint16_t ndx, uint16_t len, void *buf);

/** Delete item from NV.
 \param id Valid NV item Id
 \return 0 if item was deleted, 'u' if item did not exist in NV, 'f' if attempted deletion failed. */
int nv_drop(uint16_t id);

/** Find item in NV.
 \param id Valid NV item Id
 \param size Optional size output
 \return Item data address or 0 if not found. */
const void* nv_find(uint16_t id);

/** Get NV length from location.
 \note Not checked for validity
 \param p Address of item in flash
 \return Length in bytes */
int nv_size(const void *p);

/** Get NV ID from location.
 \note Not checked for validity
 \param p Address of item in flash
 \return ID */
int nv_id(const void *p);

/** nv_item_init and nv_write combined.
 \note Will write new data or update old
 \param id Valid NV item Id
 \param len Item length
 \param buf Pointer to item initalization data. Set to NULL if none
 \return 'u': Id did not exist and was created successfully. 0: Id updated. 'f': Failure to create or update Id. */
int nv_save(uint16_t id, uint16_t len, void *buf);

void mtr_save(struct meter *m);


#endif
