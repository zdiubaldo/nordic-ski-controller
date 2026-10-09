#include "relay_idf.h"
#include "esp_err.h"

static bool write_register(void *context, uint8_t reg, uint8_t value)
{
    const uint8_t bytes[] = {reg, value};
    return i2c_master_transmit((i2c_master_dev_handle_t)context,
                              bytes, sizeof(bytes), 50) == ESP_OK;
}
static bool read_register(void *context, uint8_t reg, uint8_t *value)
{
    return i2c_master_transmit_receive((i2c_master_dev_handle_t)context,
                                     &reg, 1, value, 1, 50) == ESP_OK;
}
void relay_prepare_idf(relay_t *r, i2c_master_dev_handle_t device,
                       uint8_t inactive_levels)
{
    relay_prepare(r, device, device ? write_register : 0,
                  device ? read_register : 0, inactive_levels);
}
