#include <dbch.h>
#include <include/mfglib.h>
#include <rail.h>


struct packet {
	struct packet *next;
	uint32_t ts;
	int8_t rssi;
	uint8_t lqi, pkt[1];
};


VAR_AT_SEGMENT(NO_STRIPPING uint8_t _store[16384], __PSSTORE__);


static struct packet *packets;
static char chn, na;


// sniffer

static void heard(uint8_t *pkt, uint8_t lqi, int8_t rssi)
{
	struct packet **pp = &packets, *p;
	while (p = *pp)
		pp = &p->next;

	if (p = *pp = malloc(sizeof(*p) + pkt[0])) {
		p->next = 0;
		p->rssi = rssi;
		p->lqi = lqi;
		memcpy(p->pkt, pkt, 1 + pkt[0]);
		p->ts = RAIL_GetTime();
	}
}

static int go_set(void *ctx, char *val)
{
	EmberStatus s;
	if (chn == 0 && (s = mfglibStart(heard)))
		return uartf("no mfglibStart said %x\n", s);

	if (s = mfglibSetChannel(chn = hex2int(val)))
		return uartf("no mfglibSetChannel(%x) said %x\n", chn, s);

	return uartf("ok on chn %x\n", chn);
}

static int stop_get(void *ctx)
{
	EmberStatus s;
	if (chn && (s = mfglibEnd()))
		return uartf("no mfglibEnd said %x\n", s);

	return chn = 0;
}

static int na_get(void *ctx)
{
	return uartf("snf.na=%d\n", na);
}

static int na_set(void *ctx, char *val)
{
	na = hex2int(val);
	return na_get(0);
}


// exports

bool emberAfMainStartCallback(int *returnCode, int argc, char **argv)
{ // called before starting the stack
	static const struct doap_attr attrs[] = {
		{"go", 0, go_set},
		{"stop", stop_get, 0},
		{"na", na_get, na_set},
		{0, 0, 0}
	};

	uartf("up\n");
	doap_obj_add("snf", attrs, 0);
	time_start();

	return false;
}

void metro(uint32_t now)
{
}

void emberAfMainTickCallback(void)
{
//{{(rxPacket)}{len:96}{timeUs:96992850}{timePos:4}{crc:Pass}{rssi:-81}{lqi:255}{phy:0}{isAck:False}{syncWordId:0}{antenna:0}{channelHopIdx:254}{ed154:42}{lqi154:76}{payload: 0x61 0x61 0x88 0x94 ...}}
// see printPacket in railtest-sv2db::app_main.c
	struct packet *p = packets;
	if (p) {
		packets = p->next;
		if (na)
			uartf("{{(rxPacket)}{len:%d}{timeUs:%d}{rssi:%d}{lqi:%d}{payload: %p}}\n", p->pkt[0], p->ts, p->rssi, p->lqi, p->pkt);
		else
			uartf("snf.rx={%b,%d,%d,%u}\n", p->pkt + 1, p->pkt[0], p->lqi, p->rssi, p->ts);
		free(p);
	}
}

bool emberAfStackStatusCallback(EmberStatus status) { uartf("STK %x %i\n", status, emberAfNetworkState()); return false; }
EmberAfStatus emberAfExternalAttributeReadCallback(uint8_t ep, EmberAfClusterId cid, EmberAfAttributeMetadata *a, uint16_t manu, uint8_t *b, uint16_t maxReadLength) { return EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE; }
EmberAfStatus emberAfExternalAttributeWriteCallback(uint8_t ep, EmberAfClusterId cid, EmberAfAttributeMetadata *a, uint16_t manu, uint8_t *b) { return EMBER_ZCL_STATUS_UNSUPPORTED_ATTRIBUTE; }
void emberAfClusterInitCallback(uint8_t endpoint, EmberAfClusterId clusterId) { }
bool emberAfPluginTunnelingServerIsProtocolSupportedCallback(uint8_t protocolId, uint16_t manufacturerCode) { return false; }
void emberAfPluginTunnelingServerDataReceivedCallback(uint16_t tunnelIndex, uint8_t *data, uint16_t dataLen) { }
bool emberAfPreCommandReceivedCallback(EmberAfClusterCommand *af) { return true; }
bool emberAfOtaServerIncomingMessageRawCallback(EmberAfClusterCommand *m) { return false; }
void emberChildJoinHandler(uint8_t index, bool joining) { }
