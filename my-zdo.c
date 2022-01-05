#include <dbch.h>
#include <app/framework/util/af-main.h>

this is an abandoned attempt to override some ZDO requests in order to have dynamic endpoints

__PACKED_STRUCT SimpleDescriptorRequest {
	uint8_t TransactionSequenceNumber;
	uint16_t NWKAddrOfInterest;
	uint8_t EndPoint;
};

__PACKED_STRUCT SimpleDescriptorResponse {
	uint8_t TransactionSequenceNumber, Status;
	uint16_t NWKAddrOfInterest;
	uint8_t Length, SimpleDescriptor[0];
};

__PACKED_STRUCT ActiveEndpointsRequest {
	uint8_t TransactionSequenceNumber;
	uint16_t NWKAddrOfInterest;
};

__PACKED_STRUCT ActiveEndpointsResponse {
	uint8_t TransactionSequenceNumber, Status;
	uint16_t NWKAddrOfInterest;
	uint8_t ActiveEPCount, ActiveEPList[0];
};

__PACKED_STRUCT ClusterList {
	uint8_t count;
	uint16_t clusters[0];
};

__PACKED_STRUCT MatchDescriptorRequest {
	uint8_t TransactionSequenceNumber;
	uint16_t NWKAddrOfInterest, ProfileID;
	struct ClusterList InClusters, OutClusters;
};

__PACKED_STRUCT MatchDescriptorResponse {
	uint8_t TransactionSequenceNumber, Status;
	uint16_t NWKAddrOfInterest;
	uint8_t MatchLength, MatchList[0];
};

__PACKED_STRUCT SimpleDescriptor {
	uint8_t endpoint;
	uint16_t profile, device;
	uint8_t version;
};


// helpers

static bool respond(EmberNodeId node, EmberApsFrame *f, void *payload, int bytes)
{
	uint8_t tag;
	EmberApsFrame tx = {EMBER_ZDO_PROFILE_ID, f->clusterId | CLUSTER_ID_RESPONSE_MINIMUM, EMBER_ZDO_ENDPOINT, EMBER_ZDO_ENDPOINT, 0, 0, 0}; // encryption?
	emAfSend(EMBER_OUTGOING_DIRECT, node, &tx, bytes, payload, &tag, 0, 0);

	return true;
}

static void append_endpoint(struct ActiveEndpointsResponse *r, uint8_t ep)
{
	r->ActiveEPList[r->ActiveEPCount++] = ep;
}

static void append_static_endpoints(struct ActiveEndpointsResponse *r)
{
	EmberAfDefinedEndpoint *p = emAfEndpoints, *end = p + FIXED_ENDPOINT_COUNT;
	while (p < end) {
//		if (emberAfIsDeviceEnabled(p->endpoint))
			append_endpoint(r, p->endpoint);
		p++;
	}
}

static void append_meter_endpoints(struct ActiveEndpointsResponse *r)
{
	struct meter *m = meters();
	while (m) {
		append_endpoint(r, m->ep);
		m = m->next;
	}
}

static struct ClusterList* cluster_list_init(void *list)
{
	struct ClusterList *l = list;
	l->count = 0;
	return l;
}

static void cluster_list_add(struct ClusterList *l, uint16_t id)
{
	l->clusters[l->count++] = id;
}

static void* cluster_list_end(struct ClusterList *l)
{
	return l->clusters + l->count;
}

static void cluster_list_add_types(struct ClusterList *l, EmberAfEndpointType *ept, int mask)
{
	EmberAfCluster *c = ept->cluster, *end = c + ept->clusterCount;
	while (c < end) {
		if (c->mask & mask)
			cluster_list_add(l, c->clusterId);
		c++;
	}
}

static void cluster_list_add_array(struct ClusterList *l, const uint16_t *array, int count)
{
	while (count--)
		cluster_list_add(l, *array++);
}

static int static_descriptor_out(uint8_t *out, EmberAfDefinedEndpoint *e)
{
	struct SimpleDescriptor *o = (struct SimpleDescriptor*)out;
	o->endpoint = e->endpoint;
	o->profile = e->profileId;
	o->device = e->deviceId;
	o->version = e->deviceVersion;

	struct ClusterList *in = cluster_list_init(o + 1);
	cluster_list_add_types(in, e->endpointType, CLUSTER_MASK_SERVER);

	struct ClusterList *oc = cluster_list_init(cluster_list_end(in));
	cluster_list_add_types(oc, e->endpointType, CLUSTER_MASK_CLIENT);

	uint8_t *end = cluster_list_end(oc);
	return end - out;
}

static int meter_descriptor_out(uint8_t *out, struct meter *m)
{
	static const uint16_t servers[] = {0x000, 0x800, 0x00A, 0x700, 0x701, 0x702, 0x703, 0x705, 0x707, 0x708}, clients[] = {0x800};

	struct SimpleDescriptor *o = (struct SimpleDescriptor*)out;
	o->endpoint = m->ep;
	o->profile = SE_PROFILE_ID;
	o->device = 0x0500;
	o->version = 0;

	struct ClusterList *in = cluster_list_init(o + 1);
	cluster_list_add_array(in, servers, sizeof(servers) / sizeof(uint16_t));

	struct ClusterList *oc = cluster_list_init(cluster_list_end(in));
	cluster_list_add_array(oc, clients, sizeof(clients) / sizeof(uint16_t));

	uint8_t *end = cluster_list_end(oc);
	return end - out;
}


// handlers

static bool process_sdr(EmberNodeId node, EmberApsFrame *f, void *msg, uint16_t len)
{
	struct SimpleDescriptorRequest *q = msg;
	struct SimpleDescriptorResponse rsp[20], *r = rsp; // 100 byte buf

	r->TransactionSequenceNumber = q->TransactionSequenceNumber;
	r->Status = EMBER_ZDP_SUCCESS;
	r->NWKAddrOfInterest = q->NWKAddrOfInterest;
	r->Length = 0;

	uint8_t i;
	struct meter *m;
	if (q->NWKAddrOfInterest != emberAfGetNodeId())
		r->Status = EMBER_ZDP_DEVICE_NOT_FOUND;
	else if ((i = emberAfIndexFromEndpoint(q->EndPoint)) < FIXED_ENDPOINT_COUNT)
		r->Length = static_descriptor_out(r->SimpleDescriptor, emAfEndpoints + i);
	else if (m = meter_find(q->EndPoint))
		r->Length = meter_descriptor_out(r->SimpleDescriptor, m);
	else
		r->Status = EMBER_ZDP_INVALID_ENDPOINT;

	return respond(node, f, r, sizeof(*r) + r->Length);
}

static bool process_aer(EmberNodeId node, EmberApsFrame *f, void *msg, uint16_t len)
{
	struct ActiveEndpointsRequest *q = msg;
	struct ActiveEndpointsResponse rsp[20], *r = rsp; // 100 byte buf

	r->TransactionSequenceNumber = q->TransactionSequenceNumber;
	r->Status = EMBER_ZDP_SUCCESS;
	r->NWKAddrOfInterest = q->NWKAddrOfInterest;
	r->ActiveEPCount = 0;

	if (q->NWKAddrOfInterest != emberAfGetNodeId())
		r->Status = EMBER_ZDP_INVALID_REQUEST_TYPE;
	else {
		append_static_endpoints(r);
		append_meter_endpoints(r);
	}

	return respond(node, f, r, sizeof(*r) + r->ActiveEPCount);
}

static bool process_mdr(EmberNodeId node, EmberApsFrame *f, void *msg, uint16_t len)
{
	struct MatchDescriptorRequest *q = msg;
	struct MatchDescriptorResponse rsp[20], *r = rsp; // 100 byte buf

	r->TransactionSequenceNumber = q->TransactionSequenceNumber;
	r->Status = EMBER_ZDP_SUCCESS;
	r->NWKAddrOfInterest = q->NWKAddrOfInterest;

// 1. Set MatchLength to 0 and create an empty list MatchList.
	r->MatchLength = 0;

// 2. If the receiving device is an End Device and the NWKAddrOfInterest within the Match_Desc_req message does not match the nwkNetworkAddress of the NIB and is not a broadcast address, the following shall be performed. Otherwise it shall proceed to step 3.
	//if (EMBER_END_DEVICE == emAfCurrentZigbeeProNetwork->nodeType || EMBER_SLEEPY_END_DEVICE)
//  a. If the NWK destination of the message is a broadcast address, no further processing shall be done.
//  b. If the NWK destination is a unicast address, the following shall be performed.
//   i. Set the Status value to INV_REQUESTTYPE.
//   ii. Set the MatchLength to 0.
//   iii. Construct a Match_Desc_Rsp with only Status and MatchLength fields.
//   iv. Send the message as a unicast to the source of the Match_Desc_req.
//   v. No further processing shall be done.

// 3. If the NWKAddrOfInterest is equal to the nwkNetworkAddress of the NIB, or is a broadcast address, perform the following procedure. Otherwise proceed to step 4.
	if (q->NWKAddrOfInterest == emberAfGetNodeId() || emberIsZigbeeBroadcastAddress(q->NWKAddrOfInterest)) {
//  a. Apply the match criteria in section 2.4.4.2.7.1.1 for all local Simple Descriptors.
		EmberAfDefinedEndpoint *p = emAfEndpoints, *end = p + MAX_ENDPOINT_COUNT;
		while (p < end) {
			if (match(q, r, p->?))
				mdr_add_ep(r, p->endpoint);
			p++;
		}
//  b. For each Simple Descriptor that matches with at least one cluster, add the endpoint once to MatchList and increment MatchLength.
	}

// 4. If the NWKAddrOfInterest is not a broadcast address, the NWKAddressOfInterest is not equal to the nwkNetworkAddress of the local NIB, and the device is a coordinator or router, then the following shall be performed.
//  Otherwise proceed to step 5.
//  a. Examine each entry in the nwkNeighborTable and perform the following procedure.
//   i. If the Network Address of the entry does not match the NWKAddrOfInterest or the Device Type is not equal to 0x02 (ZigBee End Device), do not process this entry. Continue to the next entry in the nwkNeighborTable.
//   ii. If no cached Simple Descriptors for the device are available, skip this device and proceed to the next entry in the nwkNeighborTable.
//   iii. Apply the match criteria in section 2.4.4.2.7.1.1 for each cached Simple Descriptor.
//   iv. For each endpoint that matches with at least once cluster, add that endpoint once to the MatchList and increment MatchLength.
//   v. Proceed to step 7.
//  b. If the NWKAddrOfInterest does not match any entry in the nwkNeighborTable, perform the following:
//   i. Set the Status to DEVICE_NOT_FOUND.
//   ii. Construct a Match_Desc_Rsp with Status and MatchLength fields only.
//   iii. Unicast the message to the source of the Match_Desc_req.
//   iv. No further processing shall take place.

// 5. If the MatchLength is 0 and the NWK destination of the Match_Desc_Req was a broadcast address, no further processing shall be done. Otherwise proceed to step 6.
	if (r->MatchLength == 0 && emberIsZigbeeBroadcastAddress(?))
		return true;

// 6. If the MatchLength is 0 and the NWKAddrOfInterest matched an entry in the nwkNeighborTable, the following shall be performed. Otherwise proceed to step 7.
//  a. Set the Status to NO_DESCRIPTOR
//  b. Construct a Match_Desc_Rsp with Status and MatchLength only.
//  c. Unicast the Match_Desc_Rsp to the source of the Match_Desc_Req.
//  d. No further processing shall be done.

// 7. The following shall be performed. This is the case for both MatchLength > 0 and MatchLength == 0.
//  a. Set the Status to SUCCESS.
//  b. Construct a Match_Desc_Rsp with Status, NWKAddrOfInterest, MatchLength, and MatchList.
//  c. Unicast the response to the NWK source of the Match_Desc_Req.

// 2.4.4.2.7.1.1 Simple Descriptor Matching Rules
// These rules will examine a ProfileID, InputClusterList, OutputClusterList, and a SimpleDescriptor. The following shall be performed:
// 1. The device shall first check if the ProfileID field matches using the Profile ID of the SimpleDescriptor. If the profile identifiers do not match and the ProfileID is not 0xffff, the device shall note the match as unsuccessful and perform no further processing.
// 2. Examine the InputClusterList and compare each item to the Application Input Cluster List of the SimpleDescriptor.
//  a. If a cluster ID matches exactly, then the device shall note the match as successful and perform no further matching. Processing is complete.
// 3. Examine the OutputClusterList and compare each item to the Application Output Cluster List of the SimpleDescriptor.
//  a. If a cluster ID matches exactly, then the device shall note the match as successful and perform no further matching. Processing is complete.
// 4. The device shall note the match as unsuccessful. Processing is complete.

	return false; // TODO
}


// exports

bool emberAfPreZDOMessageReceivedCallback(EmberNodeId node, EmberApsFrame *f, uint8_t *msg, uint16_t len)
{
	// need to process SIMPLE_DESCRIPTOR_REQUEST, MATCH_DESCRIPTORS_REQUEST and ACTIVE_ENDPOINTS_REQUEST because EMBER_APPLICATION_HANDLES_ENDPOINT_ZDO_REQUESTS is defined
	// this is completely shite, stack should allow app to return false for it to handle the request
	if (f->clusterId == SIMPLE_DESCRIPTOR_REQUEST)
		return process_sdr(node, f, msg, len);
	else if (f->clusterId == ACTIVE_ENDPOINTS_REQUEST)
		return process_aer(node, f, msg, len);
	else if (f->clusterId == MATCH_DESCRIPTORS_REQUEST)
		return process_mdr(node, f, msg, len);

	return false;
}
