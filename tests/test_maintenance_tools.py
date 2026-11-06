#!/usr/bin/env python3
"""维护工具功能回归，不启动 ESP-IDF 或真实设备。"""
import ast
import importlib.util
from pathlib import Path
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts/tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class MaintenanceToolsTest(unittest.TestCase):
    def test_quality_options_preserve_default_full_gate(self):
        # 旧入口在导入时执行构建；仅载入声明以验证新 CLI 参数的纯解析契约。
        prefix = (ROOT / "scripts/tools/quality_gate.py").read_text().split("# 1. 编译检查")[0]
        source = ast.parse(prefix)
        declarations = [node for node in source.body
                        if isinstance(node, (ast.Import, ast.ImportFrom, ast.FunctionDef, ast.Assign))]
        namespace = {"__file__": str(ROOT / "scripts/tools/quality_gate.py")}
        exec(compile(ast.Module(body=declarations, type_ignores=[]), "quality_gate.py", "exec"), namespace)
        self.assertTrue("parse_options" in namespace, "缺少分层门禁参数解析")
        parse = namespace["parse_options"]
        self.assertEqual(parse([]).level, "full")
        self.assertFalse(parse([]).no_build)
        self.assertTrue(parse(["--no-build"]).no_build)
        self.assertEqual(parse(["--level", "docs"]).level, "docs")
        self.assertFalse(parse([]).strict)
        self.assertTrue(parse(["--strict"]).strict)
        self.assertEqual(parse(["--build-dir", "build-review"]).build_dir, "build-review")
        self.assertEqual(namespace["find_python"](), __import__("sys").executable)
        with self.assertRaises(SystemExit):
            parse(["--level", "unknown"])

    def test_markdown_links_check_fragments_and_ignore_code(self):
        tool = load("check_markdown_links")
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            (root / "target.md").write_text(
                "# 中文 标题\n## Same\n## Same\n```\n# fake\n<a id=\"fakeid\"></a>\n```\n"
                "Setext title\n============\n")
            doc = root / "index.md"
            doc.write_text(
                "[中文](target.md#%E4%B8%AD%E6%96%87-%E6%A0%87%E9%A2%98)\n"
                "[重复](target.md#same-1)\n[x][ref]\n[ref]: target.md#same\n"
                "[下划线标题](target.md#setext-title)\n"
                "```\n[示例](missing.md)\n```\n`[inline](missing.md)`\n"
                "[网络](https://example.com/#unknown)\n")
            self.assertEqual(tool.check_document(doc, root), [])
            doc.write_text("[错标题](target.md#fake)\n[错路径](missing.md)\n[代码ID](target.md#fakeid)\n")
            errors = tool.check_document(doc, root)
            self.assertEqual(len(errors), 3)

    def test_facts_use_current_source_and_detect_drift(self):
        tool = load("doc_facts")
        text = tool.render(ROOT)
        config = (ROOT / "main/bikepower_config.h").read_text()
        domain = (ROOT / "components/bikepower_domain/include/domain_config.h").read_text()
        self.assertIn("v" + tool.define(config, "FIRMWARE_VERSION"), text)
        self.assertIn("schema：" + tool.define(domain, "CONFIG_VERSION"), text)
        self.assertIn("POST /api/bridge/control", text)
        self.assertIn("main/ble_bikepower.c", text)
        with tempfile.TemporaryDirectory() as temp:
            target = Path(temp) / "facts.md"
            target.write_text(text)
            self.assertTrue(tool.matches(ROOT, target))
            target.write_text(text + "\n过期内容\n")
            self.assertFalse(tool.matches(ROOT, target))


if __name__ == "__main__":
    unittest.main()
