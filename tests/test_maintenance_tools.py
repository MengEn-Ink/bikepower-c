#!/usr/bin/env python3
"""维护工具功能回归，不启动 ESP-IDF 或真实设备。"""
import ast
import importlib.util
import os
from pathlib import Path
import tempfile
import subprocess
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]


def load(name):
    spec = importlib.util.spec_from_file_location(name, ROOT / "scripts/tools" / f"{name}.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def symlink_supported():
    """Windows 非开发者模式无符号链接权限，探测一次供子用例跳过。"""
    with tempfile.TemporaryDirectory() as temp:
        target = Path(temp) / "target.txt"
        target.write_text("x", encoding="utf-8")
        try:
            (Path(temp) / "alias.txt").symlink_to(target)
        except (OSError, NotImplementedError):
            return False
    return True


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

    def test_connection_recovery_has_formal_gate_entries(self):
        script = "tests/test_ble_connection_recovery.py"
        quality = ast.parse((ROOT / "scripts/tools/quality_gate.py").read_text())
        lists = {target.id: ast.literal_eval(node.value) for node in quality.body
                 if isinstance(node, ast.Assign) and isinstance(node.value, ast.List)
                 for target in node.targets if isinstance(target, ast.Name)
                 and target.id in {"STATIC_CHECKS"}}
        self.assertIn(script, [row[0] for row in lists["STATIC_CHECKS"]])
        operation = ast.parse((ROOT / "tests/operation_gate.py").read_text())
        quick = next(node for node in operation.body
                     if isinstance(node, ast.FunctionDef) and node.name == "quick_steps")
        commands = [node.args[2].value for node in ast.walk(quick)
                    if isinstance(node, ast.Call) and isinstance(node.func, ast.Name)
                    and node.func.id == "run_step" and len(node.args) > 2
                    and isinstance(node.args[2], ast.Constant)]
        self.assertIn("python3 " + script, commands)
        self.assertIn("python3 " + script, (ROOT / "scripts/build_all.sh").read_text())
        self.assertIn("tests/operation_gate.py", (ROOT / ".github/workflows/ci.yml").read_text())

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

    def test_inline_generator_rejects_source_aliases_before_writing(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            web = root / 'web'
            web.mkdir()
            originals = {'index.html': '<!-- INLINE_CSS --><!-- INLINE_JS -->',
                         'app.css': 'body { color: black; }',
                         'app.js': 'const x = 1;'}
            for name, content in originals.items():
                (web / name).write_text(content, encoding='utf-8')
            alias = root / 'alias.h'
            if symlink_supported():
                alias.symlink_to(web / 'index.html')
            hardlink = root / 'hardlink.h'
            os.link(web / 'app.css', hardlink)
            outputs = [web / name for name in originals] + [alias, hardlink, root / 'wrong.html']
            for output in outputs:
                with self.subTest(output=output.name):
                    if output == alias and not alias.is_symlink():
                        self.skipTest("当前系统不允许创建符号链接（Windows 需开发者模式或管理员）")
                    for name, content in originals.items():
                        (web / name).write_text(content, encoding='utf-8')
                    result = subprocess.run([sys.executable, str(ROOT / 'scripts/inline_static.py'),
                                             '--web-dir', str(web), '--output', str(output)],
                                            capture_output=True, text=True)
                    self.assertNotEqual(result.returncode, 0)
                    self.assertIn('FAIL:', result.stderr)
                    for name, content in originals.items():
                        self.assertEqual((web / name).read_text(encoding='utf-8'), content)
            output = root / 'generated' / 'web_index_html.h'
            result = subprocess.run([sys.executable, str(ROOT / 'scripts/inline_static.py'),
                                     '--web-dir', str(web), '--output', str(output)],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn('static const char index_html[]', output.read_text(encoding='utf-8'))

    def test_minifier_preserves_asi_regex_strings_and_operators(self):
        spec = importlib.util.spec_from_file_location('inline_static_test', ROOT / 'scripts/inline_static.py')
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        cases = [
            'function f(){return\n42;} console.log(JSON.stringify(f()));',
            'let x=1,y=2; console.log(x + +y, x - -y);',
            'const url="https://example.invalid/a/*keep*/"; console.log(url);',
            'function f(x){return /[/*]+/.test(x);} console.log(f("//"));',
            'const x=`line one\\nline two`; console.log(x);',
            'let x=1\nx++\nconsole.log(x);',
        ]
        for source in cases:
            with self.subTest(source=source):
                before = subprocess.run(['node', '-e', source], capture_output=True, text=True, check=True)
                after = subprocess.run(['node', '-e', module.minify_js(source)], capture_output=True, text=True, check=True)
                self.assertEqual(after.stdout, before.stdout)
        self.assertIn('/*! license */', module.minify_js('/*! license */ const x = 1;'))

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
