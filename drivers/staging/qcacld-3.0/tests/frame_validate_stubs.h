/*
 * TEST-ONLY USERSPACE STUBS -- NOT KERNEL HEADERS OR A KERNEL BUILD.
 *
 * Supply only the QDF/platform surface needed to compile the actual validator.
 * QDF status numbers here are arbitrary test-local values: assertions compare
 * symbolic outcomes, not a claim about the kernel's numeric QDF ABI. The actual
 * IEEE wire layout/constants and injection size limit are imported by Python.
 * No frame validation or sanitization is implemented in this file.
 */
#ifndef FRAME_VALIDATE_TEST_STUBS_H
#define FRAME_VALIDATE_TEST_STUBS_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define __packed __attribute__((packed))
#define QDF_MAC_ADDR_SIZE 6
#define QDF_MODULE_ID_HDD 0

typedef enum {
	QDF_STATUS_SUCCESS,
	QDF_STATUS_E_INVAL,
	QDF_STATUS_E_NULL_VALUE
} QDF_STATUS;

#define QDF_IS_STATUS_ERROR(status) ((status) != QDF_STATUS_SUCCESS)

struct qdf_mac_addr {
	uint8_t bytes[QDF_MAC_ADDR_SIZE];
};

static inline bool qdf_is_macaddr_zero(const struct qdf_mac_addr *address)
{
	size_t index;

	for (index = 0; index < QDF_MAC_ADDR_SIZE; ++index)
		if (address->bytes[index] != 0)
			return false;
	return true;
}

static inline bool qdf_is_macaddr_broadcast(const struct qdf_mac_addr *address)
{
	size_t index;

	for (index = 0; index < QDF_MAC_ADDR_SIZE; ++index)
		if (address->bytes[index] != 0xff)
			return false;
	return true;
}

/* Consume/evaluate diagnostic arguments, without emulating kernel logging. */
static inline void frame_test_trace(int module, const char *format, ...)
{
	(void)module;
	(void)format;
}

#define QDF_TRACE_DEBUG(...) frame_test_trace(__VA_ARGS__)
#define QDF_TRACE_INFO(...) frame_test_trace(__VA_ARGS__)
#define QDF_TRACE_WARN(...) frame_test_trace(__VA_ARGS__)
#define QDF_TRACE_ERROR(...) frame_test_trace(__VA_ARGS__)

#endif /* FRAME_VALIDATE_TEST_STUBS_H */
