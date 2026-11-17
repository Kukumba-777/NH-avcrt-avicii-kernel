/*
 * Focused userspace regression fixtures, not a kernel/hardware test.
 * The included file is a byte-for-byte copy of the production source, prepared
 * by test_frame_validate.py. Including it exposes its actual static helpers;
 * this file contains expectations and wire fixtures, not replacement validators.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "wlan_hdd_frame_validate.c"

_Static_assert(sizeof(struct ieee80211_frame) == 24, "wire header size");
_Static_assert(offsetof(struct ieee80211_frame, i_dur) == 2, "duration offset");
_Static_assert(offsetof(struct ieee80211_frame, i_addr1) == 4, "RA offset");
_Static_assert(offsetof(struct ieee80211_frame, i_seq) == 22, "sequence offset");

static unsigned int checks;

#define CHECK(condition, ...) do { \
	++checks; \
	if (!(condition)) { \
		fprintf(stderr, "FAIL: "); \
		fprintf(stderr, __VA_ARGS__); \
		fputc('\n', stderr); \
		exit(EXIT_FAILURE); \
	} \
} while (0)

struct wire_fixture {
	const char *name;
	const uint8_t *bytes;
	size_t length;
};

typedef QDF_STATUS (*frame_function)(uint8_t *, uint32_t);

/* Every buffer is allocated at the exact supplied frame length, not padded to
 * a maximum size. malloc alignment avoids inventing unaligned-access failures
 * unrelated to the fixtures. UBSan is not an exhaustive memory-safety proof.
 */
static void check_call(const char *label, frame_function function,
		       const uint8_t *bytes, size_t length, QDF_STATUS expected)
{
	uint8_t *frame = malloc(length);
	QDF_STATUS actual;

	CHECK(frame != NULL, "%s: allocation", label);
	memcpy(frame, bytes, length);
	actual = function(frame, (uint32_t)length);
	CHECK(actual == expected, "%s: status %d, expected %d (length %zu)",
	      label, actual, expected, length);
	CHECK(memcmp(frame, bytes, length) == 0,
	      "%s: input mutated (including frame control/Protected)", label);
	free(frame);
}

/* Wire bytes are little-endian, exclude radiotap and FCS, and use locally
 * administered unicast addresses. Fixed association fields need no IEs for
 * these minimum-length checks; acceptance does not assert protocol completeness.
 */
static const uint8_t assoc_request[] = {
	0x00, 0x00, 0x00, 0x00,
	0x02, 0, 0, 0, 0, 1, 0x02, 0, 0, 0, 0, 2,
	0x02, 0, 0, 0, 0, 3, 0x00, 0x00,
	0x01, 0x00, 0x01, 0x00
};

static const uint8_t reassoc_request[] = {
	0x20, 0x00, 0x00, 0x00,
	0x02, 0, 0, 0, 0, 1, 0x02, 0, 0, 0, 0, 2,
	0x02, 0, 0, 0, 0, 3, 0x00, 0x00,
	0x01, 0x00, 0x01, 0x00, 0x02, 0, 0, 0, 0, 4
};

static const uint8_t ack[] = {
	0xd4, 0x00, 0x00, 0x00, 0x02, 0, 0, 0, 0, 1
};

/* Space for Addr4 + QoS + HT Control, even with every FC1 flag set. */
static const uint8_t qos_data[36] = {
	0x88, 0x00, 0x00, 0x00,
	0x02, 0, 0, 0, 0, 1, 0x02, 0, 0, 0, 0, 2,
	0x02, 0, 0, 0, 0, 3, 0x00, 0x00,
	0x02, 0, 0, 0, 0, 4, 0, 0, 0, 0, 0, 0
};

static const struct wire_fixture flag_layouts[] = {
	{ "assoc", assoc_request, sizeof(assoc_request) },
	{ "reassoc", reassoc_request, sizeof(reassoc_request) },
	{ "ACK", ack, sizeof(ack) },
	{ "QoS data", qos_data, sizeof(qos_data) }
};

static void test_versions(void)
{
	size_t index;
	unsigned int version;

	for (index = 0; index < sizeof(flag_layouts) / sizeof(flag_layouts[0]); ++index) {
		const struct wire_fixture *fixture = &flag_layouts[index];

		for (version = 0; version < 4; ++version) {
			uint8_t frame[sizeof(qos_data)];
			char label[96];
			QDF_STATUS expected = version ? QDF_STATUS_E_INVAL : QDF_STATUS_SUCCESS;

			memcpy(frame, fixture->bytes, fixture->length);
			frame[0] = (frame[0] & 0xfc) | version;
			frame[1] = 0x40; /* Protected must survive even rejection. */
			snprintf(label, sizeof(label), "%s/version %u", fixture->name, version);
			check_call(label, hdd_validate_frame_header, frame, fixture->length, expected);
			check_call(label, hdd_sanitize_frame_content, frame, fixture->length, expected);
			check_call(label, hdd_validate_80211_frame, frame, fixture->length, expected);
		}
	}
	puts("PASS: versions 0/1/2/3: header, sanitizer, public validator; rejection is non-mutating");
}

static void test_fc_flags(void)
{
	size_t index;
	unsigned int flags;

	for (index = 0; index < sizeof(flag_layouts) / sizeof(flag_layouts[0]); ++index) {
		const struct wire_fixture *fixture = &flag_layouts[index];

		for (flags = 0; flags < 256; ++flags) {
			uint8_t frame[sizeof(qos_data)];
			char label[96];

			memcpy(frame, fixture->bytes, fixture->length);
			frame[1] = (uint8_t)flags;
			snprintf(label, sizeof(label), "%s/FC1 0x%02x", fixture->name, flags);
			check_call(label, hdd_sanitize_frame_content, frame, fixture->length,
				   QDF_STATUS_SUCCESS);
			check_call(label, hdd_validate_80211_frame, frame, fixture->length,
				   QDF_STATUS_SUCCESS);
		}
	}
	puts("PASS: all 256 FC1 values on 4 layouts; all bytes preserved (128 Protected values/layout)");
}

static void test_association_boundaries(void)
{
	const struct wire_fixture fixtures[] = {
		{ "assoc 27/28", assoc_request, sizeof(assoc_request) },
		{ "reassoc 33/34", reassoc_request, sizeof(reassoc_request) }
	};
	size_t index;
	unsigned int truncated;

	CHECK(sizeof(assoc_request) == 28, "assoc fixture length");
	CHECK(sizeof(reassoc_request) == 34, "reassoc fixture length");
	for (index = 0; index < sizeof(fixtures) / sizeof(fixtures[0]); ++index) {
		for (truncated = 0; truncated < 2; ++truncated) {
			const struct wire_fixture *fixture = &fixtures[index];
			QDF_STATUS expected = truncated ? QDF_STATUS_E_INVAL : QDF_STATUS_SUCCESS;
			size_t length = fixture->length - truncated;

			check_call(fixture->name, hdd_validate_mgmt_frame, fixture->bytes, length, expected);
			check_call(fixture->name, hdd_validate_80211_frame, fixture->bytes, length, expected);
		}
	}
	puts("PASS: association 27 rejected / 28 accepted; reassociation 33 rejected / 34 accepted");
}

static void test_control_boundaries(void)
{
	static const uint8_t rts[] = {
		0xb4, 0x00, 0x2c, 0x00,
		0x02, 0, 0, 0, 0, 1, 0x02, 0, 0, 0, 0, 2
	};
	static const uint8_t cts[] = {
		0xc4, 0x00, 0x2c, 0x00, 0x02, 0, 0, 0, 0, 1
	};
	static const uint8_t ps_poll[] = {
		0xa4, 0x00, 0x01, 0xc0,
		0x02, 0, 0, 0, 0, 1, 0x02, 0, 0, 0, 0, 2
	};
	static const uint8_t cf_end[] = {
		0xe4, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02, 0, 0, 0, 0, 2
	};
	static const uint8_t cf_end_ack[] = {
		0xf4, 0x00, 0x00, 0x00,
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0x02, 0, 0, 0, 0, 2
	};
	/* BAR/BA Control 0x5004: single-TID compressed, TID 5. SSC 0x1230. */
	static const uint8_t compressed_bar[] = {
		0x84, 0x00, 0x2c, 0x00,
		0x02, 0, 0, 0, 0, 1, 0x02, 0, 0, 0, 0, 2,
		0x04, 0x50, 0x30, 0x12
	};
	static const uint8_t compressed_ba[] = {
		0x94, 0x00, 0x2c, 0x00,
		0x02, 0, 0, 0, 0, 1, 0x02, 0, 0, 0, 0, 2,
		0x04, 0x50, 0x30, 0x12, 0x01, 0, 0, 0, 0, 0, 0, 0x80
	};
	const struct wire_fixture fixtures[] = {
		{ "RTS 15/16", rts, sizeof(rts) },
		{ "CTS 9/10", cts, sizeof(cts) },
		{ "ACK 9/10", ack, sizeof(ack) },
		{ "PS-Poll 15/16", ps_poll, sizeof(ps_poll) },
		{ "CF-End 15/16", cf_end, sizeof(cf_end) },
		{ "CF-End+CF-Ack 15/16", cf_end_ack, sizeof(cf_end_ack) },
		{ "compressed BAR 19/20", compressed_bar, sizeof(compressed_bar) },
		{ "compressed BA 27/28", compressed_ba, sizeof(compressed_ba) }
	};
	size_t index;
	unsigned int truncated;

	for (index = 0; index < sizeof(fixtures) / sizeof(fixtures[0]); ++index) {
		for (truncated = 0; truncated < 2; ++truncated) {
			const struct wire_fixture *fixture = &fixtures[index];
			QDF_STATUS expected = truncated ? QDF_STATUS_E_INVAL : QDF_STATUS_SUCCESS;
			size_t length = fixture->length - truncated;

			check_call(fixture->name, hdd_validate_ctrl_frame, fixture->bytes, length, expected);
			check_call(fixture->name, hdd_validate_80211_frame, fixture->bytes, length, expected);
		}
	}
	puts("PASS: 8 control wire fixtures: RTS, CTS, ACK, PS-Poll, CF-End, CF-End+CF-Ack, BAR, BA boundaries");
}

static void test_ba_limitation(void)
{
	/* Basic (not compressed) BA: 20-byte prefix plus 128-byte bitmap.
	 * The current validator does NOT dispatch on BA Control. Its acceptance of
	 * the truncated 28-byte prefix is deliberately characterized as a limitation,
	 * NOT claimed to be correct or complete BA validation. Tightening production
	 * BA validation should change this expectation to rejection.
	 */
	static const uint8_t basic_ba[148] = {
		0x94, 0x00, 0x2c, 0x00,
		0x02, 0, 0, 0, 0, 1, 0x02, 0, 0, 0, 0, 2,
		0x00, 0x50, 0x30, 0x12, [20] = 0x01, [147] = 0x80
	};

	check_call("full basic BA", hdd_validate_ctrl_frame, basic_ba,
		   sizeof(basic_ba), QDF_STATUS_SUCCESS);
	check_call("full basic BA", hdd_validate_80211_frame, basic_ba,
		   sizeof(basic_ba), QDF_STATUS_SUCCESS);
	check_call("LIMITATION: truncated basic BA", hdd_validate_ctrl_frame,
		   basic_ba, 28, QDF_STATUS_SUCCESS);
	check_call("LIMITATION: truncated basic BA", hdd_validate_80211_frame,
		   basic_ba, 28, QDF_STATUS_SUCCESS);
	puts("KNOWN LIMITATION: basic BA accepts 28 as well as 148 bytes; BA variants are not fully validated");
}

int main(void)
{
	test_versions();
	test_fc_flags();
	test_association_boundaries();
	test_control_boundaries();
	test_ba_limitation();
	printf("PASS: %u checks; real production functions; UBSan enabled\n", checks);
	return EXIT_SUCCESS;
}
