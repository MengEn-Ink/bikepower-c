# 配置持久化模板

```c
// config_store.c — 添加新字段
static const config_field_t schema[] = {
    // ...
    { "new_field", offsetof(profile_t, new_field), FIELD_TYPE_INT, 0, 100, 50 },
};

// config_store_validate — 添加校验
if (p->new_field < 0 || p->new_field > 100) {
    p->new_field = 50;
    return false;
}

// nvs_store.c — 读写
// 使用现有 bikepower namespace，现有 save/load 循环自动覆盖
```

## 模板引用

- NVS namespace: `bikepower`
- key 长度: 最多 15 字符
- 支持类型: int8/uint8/int16/uint16/int32/uint32/string/blob
