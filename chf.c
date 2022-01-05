#include <dbch.h>
#include <util/util.h>
#include <security/af-security.h>
#include <app/framework/plugin/trust-center-backup/trust-center-backup.h>
#include <app/framework/plugin/tunneling-server/tunneling-server.h>
#include <app/framework/util/af-main.h>


// form

static void form_parse(int i, char *v, EmberNetworkParameters *x)
{
	if (i == 0)
		x->radioChannel = hex2int(v);
	else if (i == 1)
		x->panId = hex2int(v);
	else if (i == 2)
		hex2nib(v, x->extendedPanId);
}

static int form_set(void *ctx, char *val)
{
	EmberNetworkParameters p;
	p.panId = halCommonGetRandom();
	MEMMOVE(p.extendedPanId, emAfExtendedPanId, EXTENDED_PAN_ID_SIZE);
	int got = doap_obj_parse(val, (objp_f*)form_parse, &p);
	if (got < 1)
		return uartf("no, usage: chf.form={page-channel[,PAN-ID[,ePAN-ID]]}\n");

	p.radioTxPower = 0;
	if (got == 3)
		emberAfClearLinkKeyTableUponFormingOrJoining = false;
	return uartf("emberAfFormNetwork said %x\n", emberAfFormNetwork(&p));
}


// auth

struct auth {
	uint8_t mac[8], len, ic[24];
};

static void auth_parse(int i, char *v, struct auth *x)
{
	if (i == 0)
		hex2nib(v, x->mac);
	else if (i == 1)
		x->len = hex2bin(v, x->ic);
}

static int auth_set(void *ctx, char *val)
{
	struct auth a;
	if (doap_obj_parse(val, (objp_f*)auth_parse, &a) < 2)
		return uartf("no, usage: chf.auth={mac-addr,install-code-crc}\n");

	EmberKeyData key;
	EmberStatus s = emAfInstallCodeToKey(a.ic, a.len, &key); // Convert the install code to a key.
	if (EMBER_SUCCESS == s)
		return uartf("emberAddOrUpdateKeyTableEntry said %x\n", emberAddOrUpdateKeyTableEntry(a.mac, true, &key));
	else if (EMBER_SECURITY_DATA_INVALID == s)
		return uartf("ERR: Calculated CRC does not match\n");
	else if (EMBER_BAD_ARGUMENT == s)
		return uartf("ERR: Install Code must be 8, 10, 14, or 18 bytes in length\n");
	else
		return uartf("ERR: AES-MMO hash failed: 0x%x\n", s);
}

static int kuth_set(void *ctx, char *val)
{
	struct auth a;
	if (doap_obj_parse(val, (objp_f*)auth_parse, &a) < 2)
		return uartf("no, usage: chf.kuth={mac-addr,link-key}\n");

	EmberKeyData key;
	memcpy(&key, a.ic, 16);
	return uartf("emberAddOrUpdateKeyTableEntry said %x\n", emberAddOrUpdateKeyTableEntry(a.mac, true, &key));
}


// permit always

static int perm_set(void *ctx, char *val)
{
	int8u pa = hex2int(val);
	emAfPermitJoin(pa ? 255 : 0, false);

	halCommonSetToken(TOKEN_PERMIT_ALWAYS, &pa);
	return uartf("ok, permit always = %x\n", pa);
}


// change channel

static int cchn_set(void *ctx, char *val)
{
	unsigned pchn = hex2int(val), page = pchn >> 5, chn = pchn & 31;
	if (page)
		page += 24;
	pchn = page << 27 | 1 << chn;

	EmberStatus s = emberEnergyScanRequest(EMBER_SLEEPY_BROADCAST_ADDRESS, pchn, 254, 0);
	return uartf("ok, emberEnergyScanRequest(%x) said %x\n", pchn, s);
}


// TCSO
#if 0
static struct tcso {
	EmberNetworkParameters np;
	struct dev {
		struct dev *next;
		uint8_t eui[8];
		EmberKeyData key;
	} *devs;
} *tcso;

static int tcso_get(void *ctx)
{
	if (tcso)
		return uartf("no, TCSO already in progress\n");

	struct tcso *t = tcso = zalloc(sizeof(*t));
	if (!t)
		return uartf("no, not enough memory for TCSO\n");

	EmberNodeType nodeType;
	emberAfGetNetworkParameters(&nodeType, &t->np);

	struct dev **dd = &t->devs, *d;
	for (int i = 0; i < EMBER_KEY_TABLE_SIZE; i++) {
		EmberKeyStruct k;
		if (EMBER_SUCCESS != emberGetKeyTableEntry(i, &k))
			continue;
		if (!(d = *dd = zalloc(sizeof(*d))))
			break;
		memcpy(d->eui, k.partnerEUI64, 8);
		emberAesHashSimple(16, emberKeyContents(&k.key), d->key.contents);
		dd = &d->next;
	}

	emberLeaveNetwork();
	return uartf("ok, waiting on stack stopping...\n");
}

static void tcso_finish(void)
{
	emLocalEui64[0] += 1; // can't do this, new IEEE doesn't match CBKE certs

	for (int i = 0; i < EMBER_KEY_TABLE_SIZE; i++) {
		struct dev *d = tcso->devs;
		if (d) {
			emberSetKeyTableEntry(i, d->eui, true, &d->key);
			tcso->devs = d->next;
			free(d);
		} else
			emberEraseKeyTableEntry(i);
	}

	emberAfClearLinkKeyTableUponFormingOrJoining = false;
	EmberStatus s = emberAfFormNetwork(&tcso->np);
	if (s)
		uartf("no, emberAfFormNetwork said %x\n", s);
	else
		uartf("ok, new IEEE %a\n", emLocalEui64);

	free(tcso);
	tcso = 0;
}
#endif

// show child table

static int kids_get(void *ctx)
{
	int kids = emberChildCount(), i = 0;
	while (i < kids) {
		EmberChildData cd;
		if (EMBER_SUCCESS == emberGetChildData(i, &cd))
			uartf("%d/%d %a/%4x %x\n", i, kids, cd.eui64, cd.id, cd.type);
		i++;
	}
	return i;
}


// meter administration

static int mtrs_out(struct meter *m)
{
	return uartf("%s={DT:%x,PC:%x}\n", m->obj, m->MeteringDeviceType, m->PaymentControl);
}

static int mtrs_get(void *ctx)
{
	struct meter *m = meters();
	for (; m; m = m->next)
		mtrs_out(m);
	return 0;
}

static void mtrs_parse(int i, char *v, struct meter *m)
{
	if (i == 0)
		m->MeteringDeviceType = hex2int(v);
	else if (i == 1)
		m->PaymentControl = hex2int(v);
	// TODO Marco wants to be able to create meters from a config file, add more arguments for calendar and tariff
	// then maybe store in NV as the same string?
}

static int mtrs_set(void *ctx, char *val)
{
	struct meter *m = meter_new();
	if (!m)
		return uartf("no more meters\n");

	doap_obj_parse(val, (objp_f*)mtrs_parse, m);
	emberAfEndpointEnableDisable(m->ep, true);
	doap_mtr_add(m);
	return mtrs_out(m);
}


// tx power

static int tpwr_get(void *ctx)
{
	return uartf("chf.tpwr=%d\n", emberGetRadioPower());
}

static int tpwr_set(void *ctx, char *val)
{
	int pwr = hex2int(val);
	return uartf("ok, emberSetRadioPower(%d) said %x\n", pwr, emberSetRadioPower(pwr));
}


// placeholders

void app_init(void), ota_init(void);
static void null_init(void) { }
#pragma weak app_init = null_init
#pragma weak ota_init = null_init


// exports

bool emberAfMainStartCallback(int *returnCode, int argc, char **argv)
{ // called before starting the stack
	static const struct doap_attr attrs[] = {
		{"form", 0, form_set, "Form network ={pg-chn[,panid[,epid]]}"},
		{"auth", 0, auth_set, "Add device ={mac,ic}"},
		{"kuth", 0, kuth_set, "Add device ={mac,pclk}"},
		{"perm", 0, perm_set, "Set permit always =hex"},
		{"cchn", 0, cchn_set, "Change channel =hex"},
//		{"tcso", tcso_get, 0},
		{"kids", kids_get, 0},
		{"mtrs", mtrs_get, mtrs_set},
		{"tpwr", tpwr_get, tpwr_set},
		{0, 0, 0}
	};

	halSetLed(BOARDLED0);

	time_start();

	doap_obj_add("chf", attrs, 0);
	ota_init();
	app_init();

	return false;  // exit?
}

void emberAfClusterInitCallback(uint8_t endpoint, EmberAfClusterId clusterId)
{
	if (clusterId == 0x702)
		emberAfEndpointEnableDisable(endpoint, meter_find(endpoint) ? true : false);
//emberAfSetDeviceEnabled(endpoint, false); ?
}

void emberChildJoinHandler(uint8_t index, bool joining)
{
	EmberChildData cd;
	if (EMBER_SUCCESS == emberGetChildData(index, &cd))
		uartf("Child %a/%4x %s\n", cd.eui64, cd.id, joining ? "joined" : "left");
}

bool emberAfStackStatusCallback(EmberStatus status)
{
uartf("STK %x %x\n", status, emberAfNetworkState());
	if (status == EMBER_NETWORK_UP) {
		int8u pa;
		halCommonGetToken(&pa, TOKEN_PERMIT_ALWAYS);
		if (pa)
			emAfPermitJoin(255, false);
	}
#if 0
	else if (tcso)
		tcso_finish();
#endif
	return false; // This value is ignored by the framework.
}

bool emberAfPluginTunnelingServerIsProtocolSupportedCallback(uint8_t protocolId, uint16_t manufacturerCode)
{
	return protocolId == EMBER_ZCL_TUNNELING_PROTOCOL_ID_GB_HRGP && manufacturerCode == ZCL_TUNNELING_CLUSTER_UNUSED_MANUFACTURER_CODE;
}

void emberAfPluginTunnelingServerDataReceivedCallback(uint16_t tunnelIndex, uint8_t *data, uint16_t dataLen)
{
	// TODO
}

void emberAfMainTickCallback(void)
{
}
