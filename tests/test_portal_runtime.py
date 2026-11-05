#!/usr/bin/env python3
"""使用真实 cJSON 执行生产门户容量与超时回调，无需热点关联。"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]


def main(source="test_portal_runtime.c"):
    candidates = [Path(os.environ.get("IDF_PATH", "")),
                  Path.home() / "esp/esp-idf-v5.5.4"]
    json_source = next((base / "components/json/cJSON/cJSON.c" for base in candidates
                        if (base / "components/json/cJSON/cJSON.c").is_file()), None)
    if json_source is None:
        raise RuntimeError("未找到本地 ESP-IDF cJSON 源码")
    with tempfile.TemporaryDirectory(prefix="bikepower-portal-runtime-") as temp:
        folder = Path(temp)
        for name in ("esp_err.h", "esp_http_server.h", "esp_log.h", "esp_system.h", "esp_timer.h", "freertos/FreeRTOS.h"):
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text('#include "portal_stubs.h"\n')
        (folder / "web_index_html.h").write_text('static const char *index_html = ""; static const int index_html_len = 0;\n')
        binary = folder / "portal"
        dead_strip = ["-Wl,-dead_strip"] if sys.platform == "darwin" else ["-Wl,--gc-sections"]
        sources = sorted((ROOT / "components/bikepower_domain").glob("*.c"))
        subprocess.run([
            "cc", "-std=c11", "-D_POSIX_C_SOURCE=200809L", "-Wall", "-Wextra", "-Werror", "-Wno-unused-variable", "-Wno-deprecated-declarations",
            "-fsanitize=address,undefined", "-ffunction-sections", "-fdata-sections",
            "-I", str(folder), "-I", str(ROOT / "test"), "-I", str(json_source.parent),
            "-I", str(ROOT / "components/bikepower_domain/include"),
            str(ROOT / "test" / source), str(json_source), *map(str, sources),
            "-lm", *dead_strip, "-o", str(binary),
        ], check=True)
        subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    main()
