#include "device_identity.h"

#include <stdint.h>
#include <string.h>

#define DEVICE_IDENTITY_ADDR  0x080E0000UL
#define DEVICE_IDENTITY_MAGIC 0x3144494AUL
#define DEVICE_IDENTITY_MODEL 0x4A563031UL
#define DEVICE_UID_ADDR       0x1FFF7A10UL

typedef struct
{
    uint32_t magic;
    uint32_t version;
    uint32_t model;
    uint8_t uid[12];
    char label[8];
    uint8_t device_key[32];
    uint32_t flags;
} DeviceIdentityPrefix;

static int label_valid(const char label[8])
{
    uint32_t i;
    for (i = 0U; i < 3U; ++i) {
        if (label[i] < '0' || label[i] > '9') return 0;
    }
    if (label[3] != '-') return 0;
    for (i = 4U; i < 7U; ++i) {
        if (label[i] < '0' || label[i] > '9') return 0;
    }
    return label[7] == '\0';
}

/* Called by the NRF discovery response.  The UID check prevents a copied
 * Sector-11 record from displaying another board's machine number. */
const char *DeviceIdentity_GetDisplayNumber(void)
{
    const DeviceIdentityPrefix *identity =
        (const DeviceIdentityPrefix *)DEVICE_IDENTITY_ADDR;
    static const char fallback[] = "000-000";

    if (identity->magic != DEVICE_IDENTITY_MAGIC ||
        identity->version != 1U ||
        identity->model != DEVICE_IDENTITY_MODEL ||
        identity->flags != 1U ||
        memcmp(identity->uid, (const void *)DEVICE_UID_ADDR, 12U) != 0 ||
        !label_valid(identity->label)) {
        return fallback;
    }
    return identity->label;
}
