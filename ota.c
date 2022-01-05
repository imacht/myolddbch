#include <dbch.h>
#include <flash.h>


#define OTA_MAX_MTU		32


__PACKED_STRUCT ota_header {
	uint32_t UpgradeFileIdentifier;
	uint16_t HeaderVersion, HeaderLength, HeaderFieldControl, ManufacturerCode, ImageType;
	uint32_t FileVersion;
	uint16_t ZigBeeStackVersion;
	char HeaderString[32];
	uint32_t TotalImageSize;
	uint8_t SecurityCredentialVersion, UpgradeFileDestination[8];
	uint16_t MinimumHardwareVersion, MaximumHardwareVersion;
};


static uint32_t FileVersion, TotalImageSize, ftail;
static uint16_t ManufacturerCode, ImageType;
static EmberNodeId last;
static uint8_t deny, fuckoff, corrupt, abort, delay, ep;


// storage. SIMEE starts at FE000, starting FUP at 60000

#define FUP			(uint8_t*)0x60000
#define END			(uint8_t*)_SIMEE_SEGMENT_BEGIN

__WEAK int ota_store(int offset, uint8_t *data, int bytes)
{
	uint8_t *base = FUP + offset, *p = base;
	while (bytes && p < END) {
		int seg = FLASH_PAGE_SIZE - (unsigned)p % FLASH_PAGE_SIZE;
		if (seg == FLASH_PAGE_SIZE)
			halInternalFlashErase(MFB_PAGE_ERASE, (unsigned)p);
		if (seg > bytes)
			seg = bytes;
		halInternalFlashWriteWord((unsigned)p, (uint32_t*)data, (seg + 3) / 4);
		p += seg;
		data += seg;
		bytes -= seg;
	}
	return p - base;
}

__WEAK void ota_fetch(void *data, int offset, int bytes)
{
	memcpy(data, FUP + offset, bytes);
}

__WEAK int ota_capacity(void)
{
	return END - FUP;
}

//static void null_init(void) { }
//#pragma weak spi_init = null_init

static void get_image_details(void)
{
	struct ota_header h;
	ota_fetch(&h, 0, sizeof(h));
	if (h.UpgradeFileIdentifier == 0x0BEEF11E) {
		ManufacturerCode = h.ManufacturerCode;
		ImageType = h.ImageType;
		FileVersion = h.FileVersion;
		TotalImageSize = h.TotalImageSize;
	}
}


// helpers

static int respond(void *pay, int bytes, uint8_t cmd)
{
	return send_response(0, pay, bytes, cmd, 0x019);
}

static int fuck_off(void)
{
	fuckoff--;

	__PACKED_STRUCT {
		uint8_t Status;
		uint32_t CurrentTime, RequestTime;
	} r;

	r.Status = EMBER_ZCL_STATUS_WAIT_FOR_DATA;
	r.CurrentTime = emberAfGetCurrentTimeCallback();
	r.RequestTime = r.CurrentTime + 120;
	return respond(&r, sizeof(r), 0x05);
}

static int SendImageBlockRsp(int status, int offset, int bytes)
{
	__PACKED_STRUCT {
		uint8_t Status;
		uint16_t ManufacturerCode, ImageType;
		uint32_t FileVersion, FileOffset;
		uint8_t DataSize, ImageData[OTA_MAX_MTU];
	} r;

	r.Status = status;
	if (status == 0) {
		r.ManufacturerCode = ManufacturerCode;
		r.ImageType = ImageType;
		r.FileVersion = FileVersion;
		r.FileOffset = offset;
		ota_fetch(r.ImageData, offset, r.DataSize = bytes);

		if (corrupt && offset <= 20 && offset + bytes > 20)
			r.ImageData[20 - offset] ^= 255;
	}

	return respond(&r, r.Status ? 1 : 14 + bytes, 0x05);
}


// incoming commands

static int ProcessQueryNextImageReq(EmberAfClusterCommand *m, void *req)
{
	if (m->bufLen - m->payloadStartIndex != 11 && m->bufLen - m->payloadStartIndex != 9)
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	__PACKED_STRUCT {
		uint8_t FieldControl;
		uint16_t ManufacturerCode, ImageType;
		uint32_t CurrentFileVersion;
		uint16_t HardwareVersion;
	} *q = req;

	__PACKED_STRUCT {
		uint8_t Status;
		uint16_t ManufacturerCode, ImageType;
		uint32_t FileVersion, ImageSize;
	} r;

	if (TotalImageSize && !deny && ManufacturerCode == q->ManufacturerCode && ImageType == q->ImageType) {
		r.Status = EMBER_ZCL_STATUS_SUCCESS;
		r.ManufacturerCode = ManufacturerCode;
		r.ImageType = ImageType;
		r.FileVersion = FileVersion;
		r.ImageSize = TotalImageSize;
	} else
		r.Status = EMBER_ZCL_STATUS_NO_IMAGE_AVAILABLE;

	return respond(&r, r.Status ? 1 : sizeof(r), 0x02);
}

static int ProcessImageBlockReq(EmberAfClusterCommand *m, void *req)
{
	if (m->bufLen - m->payloadStartIndex != 22 && m->bufLen - m->payloadStartIndex != 14)
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;

	if (fuckoff)
		return fuck_off();

	__PACKED_STRUCT {
		uint8_t FieldControl;
		uint16_t ManufacturerCode, ImageType;
		uint32_t FileVersion, FileOffset;
		uint8_t MaximumDataSize, RequestNodeAddress[8];
		uint16_t BlockRequestDelay;
	} *q = req;

	if (TotalImageSize == 0 || q->FileVersion != FileVersion)
		return EMBER_ZCL_STATUS_NO_IMAGE_AVAILABLE;

	if (deny)
		return EMBER_ZCL_STATUS_FAILURE;

	if (q->FileOffset > TotalImageSize || abort == 1 && q->FileOffset)
		return SendImageBlockRsp(EMBER_ZCL_STATUS_ABORT, 0, 0);

	uint8_t len = q->MaximumDataSize;
	if (len > OTA_MAX_MTU)
		len = OTA_MAX_MTU;
	if (q->FileOffset + len > TotalImageSize)
		len = TotalImageSize - q->FileOffset;
	return SendImageBlockRsp(EMBER_ZCL_STATUS_SUCCESS, q->FileOffset, len);
}

static int ProcessUpgradeEndReq(EmberAfClusterCommand *m, void *req)
{
	if (abort == 2)
		return EMBER_ZCL_STATUS_ABORT;
	if (m->bufLen - m->payloadStartIndex != 9 && m->bufLen - m->payloadStartIndex != 1)
		return EMBER_ZCL_STATUS_MALFORMED_COMMAND;
	if (deny)
		return EMBER_ZCL_STATUS_FAILURE;

	__PACKED_STRUCT {
		uint8_t Status;
		uint16_t ManufacturerCode, ImageType;
		uint32_t FileVersion;
	} *q = req;

	if (q->Status != EMBER_ZCL_STATUS_SUCCESS)
		return EMBER_ZCL_STATUS_SUCCESS;

	__PACKED_STRUCT {
		uint16_t ManufacturerCode, ImageType;
		uint32_t FileVersion, CurrentTime, UpgradeTime;
	} r;

	r.ManufacturerCode = ManufacturerCode;
	r.ImageType = ImageType;
	r.FileVersion = FileVersion;
	r.CurrentTime = emberAfGetCurrentTimeCallback();
	r.UpgradeTime = delay == 255 ? 0xFFFFFFFF : r.CurrentTime + delay;
	return respond(&r, sizeof(r), 0x07);
}


// methods

__PACKED_STRUCT image_notify {
	uint8_t PayloadType, QueryJitter;
	uint16_t ManufacturerCode, ImageType;
	uint32_t NewFileVersion;
};

static void parse_image_notify(int i, char *v, struct image_notify *x)
{
	if (i == 0)
		x->QueryJitter = hex2int(v);
	else if (i == 1)
		x->ManufacturerCode = hex2int(v);
	else if (i == 2)
		x->ImageType = hex2int(v);
	else if (i == 3)
		x->NewFileVersion = hex2int(v);
	else
		return;
	x->PayloadType = i;
}

static int image_notify(char *args, uint16_t dst, uint8_t ep)
{
	struct image_notify p = {0, 0, 0, 0, 0};
	doap_obj_parse(args, (objp_f*)parse_image_notify, &p);

	static const uint8_t sizes[] = {2, 4, 6, 10};
	int len = emberAfFillExternalBuffer(ZCL_CLUSTER_SPECIFIC_COMMAND | ZCL_FRAME_CONTROL_SERVER_TO_CLIENT, 0x019, 0x00, "b", &p, sizes[p.PayloadType]);

	EmberApsFrame *f = emberAfGetCommandApsFrame();
	f->sourceEndpoint = 1;
	if (dst == EMBER_SLEEPY_BROADCAST_ADDRESS)
		return emberAfSendBroadcast(dst, f, len, emAfZclBuffer);
	else {
		f->destinationEndpoint = ep;
		return emberAfSendUnicast(EMBER_OUTGOING_DIRECT, dst, f, len, emAfZclBuffer);
	}
}

static int img_get(void *ctx)
{
	if (TotalImageSize == 0)
		get_image_details();

	uartf("ota.img={");
	if (TotalImageSize)
		uartf("Manu:%x,Type:%x,Version:%x,Size:%x", ManufacturerCode, ImageType, FileVersion, TotalImageSize);
	return uartf("}\n");
}

static int img_set(void *ctx, char *val)
{
	uint8_t *hex = (uint8_t*)val;
	int bytes = hex2bin(val, hex);
	int did = ota_store(ftail, hex, bytes);
	uartf("ota.wrote=%x\n", did);
	return ftail += did;
}

static int max_get(void *ctx)
{
	return uartf("ota.max=%x\n", ota_capacity());
}

static int img_reset(void *ctx)
{
	ftail = TotalImageSize = 0;
	return img_get(ctx);
}

static int img_done(void *ctx)
{
	get_image_details();
	return img_get(ctx);
}

static int attr_set(void *ctx, char *val)
{
	uint16_t id = hex2int(val);
	uartf("Requesting OTA attr %x\n", id);

	EmberApsFrame *f = emberAfGetCommandApsFrame();
	f->sourceEndpoint = 1;
	f->destinationEndpoint = ep;
	int len = emberAfFillCommandGlobalServerToClientReadAttributes(0x019, &id, 2);
	return emberAfSendUnicast(EMBER_OUTGOING_DIRECT, last, f, len, emAfZclBuffer);
}

static int delay_set(void *ctx, char *val)
{
	return uartf("ota.delay=%x\n", delay = hex2int(val));
}

static int now_get(void *ctx)
{
	uartf("Sending update now\n");

	__PACKED_STRUCT {
		uint16_t ManufacturerCode, ImageType;
		uint32_t FileVersion, CurrentTime, UpgradeTime;
	} r;

	r.ManufacturerCode = ManufacturerCode;
	r.ImageType = ImageType;
	r.FileVersion = FileVersion;
	r.CurrentTime = r.UpgradeTime = emberAfGetCurrentTimeCallback();

	EmberApsFrame *f = emberAfGetCommandApsFrame();
	f->sourceEndpoint = 1;
	f->destinationEndpoint = ep;
	int len = emberAfFillExternalBuffer(ZCL_CLUSTER_SPECIFIC_COMMAND | ZCL_FRAME_CONTROL_SERVER_TO_CLIENT, 0x019, 0x07, "b", &r, sizeof(r));
	return emberAfSendUnicast(EMBER_OUTGOING_DIRECT, last, f, len, emAfZclBuffer);
}

static int corrupt_set(void *ctx, char *val)
{
	return uartf("ota.corrupt=%x\n", corrupt = hex2int(val));
}

static int wait_set(void *ctx, char *val)
{
	return uartf("ota.wait=%x\n", fuckoff = hex2int(val));
}

static int abort_set(void *ctx, char *val)
{
	return uartf("ota.abort=%x\n", abort = hex2int(val));
}

static int bnot_set(void *ctx, char *val)
{
	return image_notify(val, EMBER_SLEEPY_BROADCAST_ADDRESS, 1);
}

static int unot_set(void *ctx, char *val)
{
	return image_notify(val, last, ep);
}


// exports

bool emberAfOtaServerIncomingMessageRawCallback(EmberAfClusterCommand *m)
{
	if (m->mfgSpecific)
		return false;

	last = m->source;
	ep = m->apsFrame->sourceEndpoint;

	int r;
	void *req = m->buffer + m->payloadStartIndex;
	if (m->commandId == 0x01)
		r = ProcessQueryNextImageReq(m, req);
	else if (m->commandId == 0x03)
		r = ProcessImageBlockReq(m, req);
	else if (m->commandId == 0x06)
		r = ProcessUpgradeEndReq(m, req);
	else
		return false;

	if (r != 'R') { // handler sent a (non-default) response
		if (r || ~m->buffer[0] & ZCL_DISABLE_DEFAULT_RESPONSE_MASK)
			emberAfSendDefaultResponse(m, (EmberAfStatus)r);
	}
	return true;

}

void ota_init(void)
{
	static const struct doap_attr attrs[] = {
		{"img", img_get, img_set},
		{"max", max_get, 0},
		{"reset", img_reset, 0},
		{"done", img_done, 0},
		{"attr", 0, attr_set},
		{"delay", 0, delay_set},
		{"now", now_get, 0},
		{"corrupt", 0, corrupt_set},
		{"wait", 0, wait_set},
		{"abort", 0, abort_set},
		{"bnot", 0, bnot_set},
		{"unot", 0, unot_set},
		{0, 0, 0}
	};

	doap_obj_add("ota", attrs, 0);
}
