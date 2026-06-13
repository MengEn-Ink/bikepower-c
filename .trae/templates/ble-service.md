# BLE 特征值模板

```c
// UUID 定义（bikepower_config.h）
#define NEW_SERVICE_UUID  0xXXXX
#define NEW_CHAR_UUID     0xXXXX

// GATT 访问回调
static int new_char_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                              struct ble_gatt_access_ctxt *ctxt, void *arg) {
    if (ctxt->op == BLE_GATT_ACCESS_OP_READ_CHR) {
        return chr_read_buf(ctxt, buf, len);
    }
    if (ctxt->op == BLE_GATT_ACCESS_OP_WRITE_CHR) {
        // 处理写入
    }
    return BLE_ATT_ERR_UNLIKELY;
}

// GATT 服务表注册（ble_bikepower.c gatt_svcs[]）
{
    .type = BLE_GATT_SVC_TYPE_PRIMARY,
    .uuid = BLE_UUID16_DECLARE(NEW_SERVICE_UUID),
    .characteristics = (struct ble_gatt_chr_def[]){
        { .uuid = BLE_UUID16_DECLARE(NEW_CHAR_UUID),
          .access_cb = new_char_access_cb,
          .flags = BLE_GATT_CHR_F_READ | BLE_GATT_CHR_F_NOTIFY,
          .val_handle = &g_new_val_handle },
        { 0 }
    }
},
```
