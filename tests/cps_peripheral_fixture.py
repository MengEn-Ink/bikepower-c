#!/usr/bin/env python3
"""macOS 独立 CPS 上游夹具；通过控制文件指定功率和暂停通知，有限时运行。"""
import argparse
import json
import struct
import time
from pathlib import Path

import CoreBluetooth as CB
from Foundation import NSObject, NSData, NSDate, NSRunLoop


def as_data(value):
    return NSData.dataWithBytes_length_(value, len(value))


class PeripheralDelegate(NSObject):
    def peripheralManagerDidUpdateState_(self, manager):
        if manager.state() != CB.CBManagerStatePoweredOn:
            print(f"[cps-fixture] 蓝牙状态 {manager.state()}", flush=True)
            return
        uuid = CB.CBUUID.UUIDWithString_
        self.measurement = CB.CBMutableCharacteristic.alloc().initWithType_properties_value_permissions_(
            uuid("2A63"), CB.CBCharacteristicPropertyNotify, None, CB.CBAttributePermissionsReadable)
        feature = CB.CBMutableCharacteristic.alloc().initWithType_properties_value_permissions_(
            uuid("2A65"), CB.CBCharacteristicPropertyRead, as_data(struct.pack("<I", 8)),
            CB.CBAttributePermissionsReadable)
        cps = CB.CBMutableService.alloc().initWithType_primary_(uuid("1818"), True)
        cps.setCharacteristics_([self.measurement, feature])
        self.added = 0
        manager.addService_(cps)
        # macOS 保留 DIS 服务，禁止自定义注册；夹具仅验证 CPS 数据链路。

    def peripheralManager_didAddService_error_(self, manager, service, error):
        if error:
            print(f"[cps-fixture] 注册失败 uuid={service.UUID()} {error}", flush=True)
            self.failed = True
            return
        self.added += 1
        if self.added == 1:
            manager.startAdvertising_({CB.CBAdvertisementDataLocalNameKey: "BikePower-CPS-Fixture",
                                      CB.CBAdvertisementDataServiceUUIDsKey: [CB.CBUUID.UUIDWithString_("1818")]})

    def peripheralManagerDidStartAdvertising_error_(self, manager, error):
        if error:
            self.failed = True
            print(f"[cps-fixture] 广播失败 {error}", flush=True)
        else:
            print("[cps-fixture] READY CPS 独立上游广播（DIS 由 macOS 保留）", flush=True)

    def peripheralManager_central_didSubscribeToCharacteristic_(self, manager, central, characteristic):
        self.subscribed = True
        print("[cps-fixture] SUBSCRIBED", flush=True)

    def peripheralManager_central_didUnsubscribeFromCharacteristic_(self, manager, central, characteristic):
        self.subscribed = False
        print("[cps-fixture] UNSUBSCRIBED", flush=True)

    def peripheralManagerIsReadyToUpdateSubscribers_(self, manager):
        pass

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--duration", type=float, default=600)
    parser.add_argument("--control", required=True, help="JSON 文件：power、pause")
    args = parser.parse_args()
    delegate = PeripheralDelegate.alloc().init()
    delegate.failed = False
    delegate.subscribed = False
    delegate.measurement = None
    manager = CB.CBPeripheralManager.alloc().initWithDelegate_queue_options_(delegate, None, None)
    control = Path(args.control)
    deadline = time.monotonic() + args.duration
    next_notify = 0
    crank = event = 0
    try:
        while time.monotonic() < deadline and not delegate.failed:
            NSRunLoop.currentRunLoop().runUntilDate_(NSDate.dateWithTimeIntervalSinceNow_(0.05))
            now = time.monotonic()
            if now < next_notify or not delegate.subscribed:
                continue
            next_notify = now + 1
            try:
                settings = json.loads(control.read_text()) if control.exists() else {}
            except (ValueError, OSError):
                settings = {}
            if settings.get("pause"):
                continue
            power = int(settings.get("power", 238))
            if power > 0:
                crank = (crank + 1) & 0xFFFF
                event = (event + 1024) & 0xFFFF
            payload = struct.pack("<HhHH", 0x20, power, crank, event)
            sent = manager.updateValue_forCharacteristic_onSubscribedCentrals_(
                as_data(payload), delegate.measurement, None)
            print(f"[cps-fixture] NOTIFY power={power} crank={crank} sent={int(sent)}", flush=True)
    finally:
        manager.stopAdvertising()
        manager.removeAllServices()
    return int(delegate.failed)


if __name__ == "__main__":
    raise SystemExit(main())
