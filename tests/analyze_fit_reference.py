#!/usr/bin/env python3
"""从真实 FIT 骑行记录提取 BikePower 模型使用的 1Hz 统计基线。"""

import argparse
import hashlib
import json
import math
from pathlib import Path


def percentile(values, fraction):
    ordered = sorted(values)
    position = (len(ordered) - 1) * fraction
    lower = int(position)
    upper = min(lower + 1, len(ordered) - 1)
    return ordered[lower] + (ordered[upper] - ordered[lower]) * (position - lower)


def mean(values):
    return sum(values) / len(values)


def stddev(values):
    center = mean(values)
    return math.sqrt(sum((value - center) ** 2 for value in values) / len(values))


def correlation(left, right, lag=0):
    count = min(len(left), len(right)) - lag
    left_values = left[:count]
    right_values = right[lag:lag + count]
    left_mean = mean(left_values)
    right_mean = mean(right_values)
    covariance = sum(
        (lhs - left_mean) * (rhs - right_mean)
        for lhs, rhs in zip(left_values, right_values)
    )
    left_variance = sum((value - left_mean) ** 2 for value in left_values)
    right_variance = sum((value - right_mean) ** 2 for value in right_values)
    if left_variance <= 0 or right_variance <= 0:
        return 0.0
    return covariance / math.sqrt(left_variance * right_variance)


def series_metrics(values):
    deltas = [abs(current - previous) for previous, current in zip(values, values[1:])]
    return {
        "mean": mean(values),
        "stddev": stddev(values),
        "min": min(values),
        "max": max(values),
        "p01": percentile(values, 0.01),
        "p05": percentile(values, 0.05),
        "p50": percentile(values, 0.50),
        "p95": percentile(values, 0.95),
        "p99": percentile(values, 0.99),
        "delta_p50": percentile(deltas, 0.50),
        "delta_p95": percentile(deltas, 0.95),
    }


def load_records(path):
    try:
        from fitparse import FitFile
    except ImportError as exc:
        raise RuntimeError(
            "缺少 fitparse，请先运行 python3 -m pip install -r requirements-test.txt"
        ) from exc

    records = []
    for message in FitFile(str(path)).get_messages("record"):
        records.append({field.name: field.value for field in message})
    if not records:
        raise RuntimeError("FIT 文件没有 record 消息")
    return records


def analyze(path, start_minute, end_minute, max_hr_lag):
    records = load_records(path)
    first_timestamp = records[0].get("timestamp")
    if first_timestamp is None:
        raise RuntimeError("FIT record 缺少 timestamp")

    start_second = start_minute * 60
    end_second = end_minute * 60
    selected = []
    for record in records:
        timestamp = record.get("timestamp")
        if timestamp is None:
            continue
        elapsed = (timestamp - first_timestamp).total_seconds()
        if start_second <= elapsed < end_second:
            selected.append(record)

    fields = ("power", "cadence", "heart_rate")
    aligned = [
        record for record in selected
        if all(isinstance(record.get(field), (int, float)) for field in fields)
    ]
    if len(aligned) < 2:
        raise RuntimeError("截取区间内没有足够的功率、踏频、心率对齐记录")

    power = [float(record["power"]) for record in aligned]
    cadence = [float(record["cadence"]) for record in aligned]
    heartrate = [float(record["heart_rate"]) for record in aligned]
    best_lag = max(range(max_hr_lag + 1), key=lambda lag: correlation(power, heartrate, lag))

    return {
        "source": {
            "filename": path.name,
            "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
            "record_count": len(records),
            "selected_count": len(selected),
            "aligned_count": len(aligned),
            "start_minute": start_minute,
            "end_minute": end_minute,
        },
        "power": {
            **series_metrics(power),
            "ac1": correlation(power, power, 1),
            "ac5": correlation(power, power, 5),
        },
        "cadence": series_metrics(cadence),
        "heartrate": series_metrics(heartrate),
        "relationships": {
            "power_cadence": correlation(power, cadence),
            "power_heartrate_now": correlation(power, heartrate),
            "power_heartrate_lag30": correlation(power, heartrate, 30),
            "power_heartrate_best_lag_seconds": best_lag,
            "power_heartrate_best_lag_correlation": correlation(power, heartrate, best_lag),
        },
    }


def verify(actual, expected, path=""):
    failures = []
    for key, expected_value in expected.items():
        location = f"{path}.{key}" if path else key
        if key not in actual:
            failures.append(f"缺少 {location}")
            continue
        actual_value = actual[key]
        if isinstance(expected_value, dict):
            if not isinstance(actual_value, dict):
                failures.append(f"{location} 类型不匹配")
            else:
                failures.extend(verify(actual_value, expected_value, location))
        elif isinstance(expected_value, float):
            if not math.isclose(float(actual_value), expected_value, rel_tol=1e-9, abs_tol=1e-9):
                failures.append(f"{location}: {actual_value} != {expected_value}")
        elif actual_value != expected_value:
            failures.append(f"{location}: {actual_value} != {expected_value}")
    return failures


def main():
    parser = argparse.ArgumentParser(description="提取真实 FIT 骑行数据的 1Hz 统计基线")
    parser.add_argument("fit_file", type=Path)
    parser.add_argument("--start-minute", type=int, default=5)
    parser.add_argument("--end-minute", type=int, default=85)
    parser.add_argument("--max-hr-lag", type=int, default=180)
    parser.add_argument("--verify", type=Path, help="与仓库中的参考 JSON 对比")
    args = parser.parse_args()

    if args.start_minute < 0 or args.end_minute <= args.start_minute:
        parser.error("时间区间必须满足 0 <= start < end")
    if args.max_hr_lag < 30:
        parser.error("max-hr-lag 必须至少为 30 秒")

    result = analyze(args.fit_file, args.start_minute, args.end_minute, args.max_hr_lag)
    print(json.dumps(result, ensure_ascii=False, indent=2, sort_keys=True))
    if args.verify:
        expected = json.loads(args.verify.read_text(encoding="utf-8"))
        failures = verify(result, expected)
        if failures:
            for failure in failures:
                print(f"[fit-reference] FAIL {failure}")
            return 1
        print("[fit-reference] PASS 参考指标与附件一致")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
