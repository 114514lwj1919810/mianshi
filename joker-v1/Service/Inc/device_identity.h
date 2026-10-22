#ifndef DEVICE_IDENTITY_H
#define DEVICE_IDENTITY_H

/* The factory tool writes the seven-character machine number into Sector 11.
 * Every machine can therefore run the same App A/B images.  An invalid or
 * missing identity is shown as 000-000 and cannot unwrap an update key. */
const char *DeviceIdentity_GetDisplayNumber(void);

#endif
