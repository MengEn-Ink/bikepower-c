#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""dev_workflow 自测：全部使用临时 fixtures 与注入 runner，不真编固件、不碰设备。

运行：
  python tests/test_dev_workflow.py
覆盖：
  错误 Python / 路径空格 / 缺依赖 / hash 漂移 / 同内容不漂移 /
  模式隔离 / 锁竞争 / 真超时 / 失败不记 PASS / UTF-8 /
  clean 边界（不动旧 build 与用户文件）/ 进程 env 不泄漏。
"""

from __future__ import annotations

import json
import os
import platform
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from datetime import datetime
from pathlib import Path

TOOLS_DIR = Path(__file__).resolve().parents[1] / "scripts" / "tools"
sys.path.insert(0, str(TOOLS_DIR))

import dev_workflow as dw  # noqa: E402


def make_fake_repo(root: Path, name: str = "repo") -> Path:
    repo = root / name
    (repo / "main").mkdir(parents=True)
    (repo / "components" / "bikepower_domain" / "include").mkdir(parents=True)
    (repo / "CMakeLists.txt").write_text("project(bikepower)\n", encoding="utf-8")
    (repo / "main" / "CMakeLists.txt").write_text("idf_component_register(...)\n", encoding="utf-8")
    (repo / "main" / "Kconfig.projbuild").write_text('menu "BikePower"\nendmenu\n', encoding="utf-8")
    (repo / "components" / "bikepower_domain" / "CMakeLists.txt").write_text("idf_component_register()\n", encoding="utf-8")
    (repo / "sdkconfig.defaults").write_text("CONFIG_LOG_DEFAULT_LEVEL_WARN=y\n", encoding="utf-8")
    (repo / "sdkconfig.diag.defaults").write_text("CONFIG_DIAG_NOISE=y\n", encoding="utf-8")
    (repo / "partitions.csv").write_text("factory,app,0,0x400000,4M\n", encoding="utf-8")
    (repo / "main" / "main.c").write_text("int main(){return 0;}\n", encoding="utf-8")
    (repo / "main" / "web").mkdir(parents=True, exist_ok=True)
    (repo / "main" / "web" / "app.js").write_text("// app\n", encoding="utf-8")
    (repo / "components" / "bikepower_domain" / "power_engine.c").write_text(
        "/* 功率引擎 */\n", encoding="utf-8")
    (repo / "components" / "bikepower_domain" / "include" / "domain_config.h").write_text(
        "#define X 1\n", encoding="utf-8")
    # 旧 build 目录：clean/wipe 绝不能碰
    (repo / "build" / "old-build").mkdir(parents=True)
    (repo / "build" / "old-build" / "keep.txt").write_text("do not delete", encoding="utf-8")
    return repo


def make_fake_idf(root: Path) -> Path:
    idf = root / "esp-idf"
    (idf / "tools").mkdir(parents=True)
    (idf / "tools" / "idf.py").write_text("# fake\n", encoding="utf-8")
    return idf


class FakeRunner:
    """注入的子进程 runner：记录调用，按 argv 返回结果。"""

    def __init__(self, fail: bool = False):
        self.calls = []
        self.fail = fail
        self.last_env = None

    def __call__(self, argv, env, cwd, timeout_s, log_path):
        self.calls.append((list(argv), str(cwd), timeout_s))
        self.last_env = env
        (log_path).parent.mkdir(parents=True, exist_ok=True)
        with open(log_path, "a", encoding="utf-8") as fh:
            fh.write("$ fake argv\n")
        if self.fail:
            return 3, 0.1, "boom"
        return 0, 0.1, "fake ok"


def make_ctx(root: Path, repo: Path) -> dw.Context:
    idf = make_fake_idf(root)
    tools = root / "espressif-tools"
    tools.mkdir()
    cfg = {
        "idf_path": str(idf),
        "idf_python": sys.executable,
        "tools_root": str(tools),
    }
    return dw.Context(repo=repo, config=cfg)


class DevWorkflowTestBase(unittest.TestCase):
    def setUp(self):
        self.tmp = Path(tempfile.mkdtemp(prefix="bw-dw-"))
        self.addCleanup(lambda: shutil.rmtree(self.tmp, ignore_errors=True))

    def run_build(self, ctx, mode="production", board="generic", fail=False):
        fake = FakeRunner(fail=fail)
        old = dw.run_subprocess
        dw.run_subprocess = fake
        try:
            rc = dw.cmd_build(ctx, mode, board, dry_run=False)
            # 模拟构建产物存在，使后续运行走增量分支（fake runner 不真产 build.ninja）
            mode_dir = ctx.mode_dir(mode)
            if not fail:
                (mode_dir / "build.ninja").touch(exist_ok=True)
        finally:
            dw.run_subprocess = old
        return rc, fake


class TestEnvDetection(DevWorkflowTestBase):
    def test_wrong_python_missing(self):
        repo = make_fake_repo(self.tmp)
        bad_py = self.tmp / "badpy.txt"
        bad_py.write_text("not an executable", encoding="utf-8")
        ctx = dw.Context(repo=repo, config={
            "idf_path": str(make_fake_idf(self.tmp)),
            "idf_python": str(bad_py),
            "tools_root": str(self.tmp / "empty-tools"),
        })
        (self.tmp / "empty-tools").mkdir()
        # idf_python 指向不可执行文件 -> doctor 应报告 MISSING
        rc = dw.cmd_doctor(ctx, as_json=False)
        self.assertEqual(rc, dw.EXIT_ENV_MISSING)

    def test_missing_deps_reported(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        rc = dw.cmd_doctor(ctx, as_json=False)
        # tools_root 为空目录 -> cmake/ninja/gcc 全 MISSING
        self.assertEqual(rc, dw.EXIT_ENV_MISSING)

    def test_path_with_spaces(self):
        root = self.tmp / "path with space" / "nest"
        root.mkdir(parents=True)
        repo = make_fake_repo(root)
        ctx = make_ctx(root, repo)
        rc, _ = self.run_build(ctx)
        self.assertEqual(rc, 0)

    def test_utf8_paths_and_sources(self):
        root = self.tmp / "é测试目录"
        root.mkdir()
        repo = make_fake_repo(root, "仓库")
        (repo / "main" / "main.c").write_text(
            "/* 中文注释 */\nint main(){return 0;}\n", encoding="utf-8")
        ctx = make_ctx(root, repo)
        rc, fake = self.run_build(ctx)
        self.assertEqual(rc, 0)
        state = json.loads(dw.state_path(ctx, "production").read_text(encoding="utf-8"))
        self.assertEqual(state["status"], "PASS")


class TestFence(DevWorkflowTestBase):
    def test_hash_drift_triggers_reconfigure(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        rc, fake1 = self.run_build(ctx)
        self.assertEqual(rc, 0)
        self.assertEqual(len(fake1.calls), 2)  # reconfigure + build

        # 修改源码内容（hash 变），但保持 mtime 不变以证明不靠 mtime
        main_c = repo / "main" / "main.c"
        content = "int main(){return 42;}\n"
        old_mtime = main_c.stat().st_mtime
        main_c.write_text(content, encoding="utf-8")
        os.utime(main_c, (old_mtime, old_mtime))

        rc, fake2 = self.run_build(ctx)
        self.assertEqual(rc, 0)
        state = json.loads(dw.state_path(ctx, "production").read_text(encoding="utf-8"))
        self.assertFalse(state["full_rebuild"], "仅源码变化走增量，不 reconfigure")
        self.assertEqual(len(fake2.calls), 1, "增量分支只 build 一步")
        self.assertIn("main/main.c", state.get("snapshot_changed", []))

    def test_same_content_no_drift(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        rc, _ = self.run_build(ctx)
        self.assertEqual(rc, 0)
        snap = ctx.mode_dir("production") / "source-snapshot"
        t1 = (snap / "main" / "main.c").stat().st_mtime_ns
        rc, _ = self.run_build(ctx)
        self.assertEqual(rc, 0)
        state2 = json.loads(dw.state_path(ctx, "production").read_text(encoding="utf-8"))
        self.assertFalse(state2["full_rebuild"], "内容未变不应全量重编")
        self.assertEqual((snap / "main" / "main.c").stat().st_mtime_ns, t1,
                         "未变文件快照 mtime 保持旧值（快照边界）")


class TestModeIsolation(DevWorkflowTestBase):
    def test_modes_do_not_share_cache(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        rc, _ = self.run_build(ctx, mode="production")
        self.assertEqual(rc, 0)
        prod_state_path = dw.state_path(ctx, "production")
        prod_mtime_before = prod_state_path.stat().st_mtime_ns

        rc, _ = self.run_build(ctx, mode="maintenance")
        self.assertEqual(rc, 0)
        maint_state = json.loads(dw.state_path(ctx, "maintenance").read_text(encoding="utf-8"))
        self.assertEqual(maint_state["mode"], "maintenance")

        prod_state = json.loads(prod_state_path.read_text(encoding="utf-8"))
        self.assertEqual(prod_state["mode"], "production")
        # maintenance 运行绝不能改写 production 的 state 文件
        self.assertEqual(prod_state_path.stat().st_mtime_ns, prod_mtime_before)
        prod_dir = ctx.mode_dir("production")
        maint_dir = ctx.mode_dir("maintenance")
        self.assertNotEqual(prod_dir.resolve(), maint_dir.resolve())
        self.assertTrue((prod_dir / "state.json").is_file())
        self.assertTrue((maint_dir / "state.json").is_file())
        # 各自缓存目录只含自己的 state.json，互不串门
        self.assertFalse((maint_dir / "state.json").read_text(encoding="utf-8").find('"mode": "production"') >= 0)


class TestLockAndTimeout(DevWorkflowTestBase):
    def test_lock_contention(self):
        lock_path = self.tmp / "build.lock"
        lock1 = dw.ModeLock(lock_path)
        lock1.acquire(0.5)
        try:
            lock2 = dw.ModeLock(lock_path)
            with self.assertRaises(dw.LockBusy):
                lock2.acquire(1.0)
        finally:
            lock1.release()

    def test_real_timeout(self):
        log = self.tmp / "t.log"
        rc, dur, tail = dw.run_subprocess(
            [sys.executable, "-c", "import time;time.sleep(5)"],
            env=dict(os.environ), cwd=self.tmp, timeout_s=1.0, log_path=log, stream=False)
        self.assertEqual(rc, -1)
        self.assertEqual(tail, "TIMEOUT")
        self.assertIn("TIMEOUT", log.read_text(encoding="utf-8"))


class TestFailureHandling(DevWorkflowTestBase):
    def test_failure_never_records_pass(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        with self.assertRaises(dw.DevWorkflowError) as cm:
            self.run_build(ctx, fail=True)
        self.assertEqual(cm.exception.code, dw.EXIT_BUILD_FAIL)
        state = json.loads(dw.state_path(ctx, "production").read_text(encoding="utf-8"))
        self.assertEqual(state["status"], "FAIL")
        self.assertEqual(state["exit_code"], 3)

    def test_test_command_failure_code(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        fake = FakeRunner(fail=True)
        old = dw.run_subprocess
        dw.run_subprocess = fake
        try:
            rc = dw.cmd_test(ctx, "production", "unit", [sys.executable, "-c", "exit 3"])
        finally:
            dw.run_subprocess = old
        self.assertEqual(rc, dw.EXIT_TEST_FAIL)
        state = json.loads(dw.state_path(ctx, "production").read_text(encoding="utf-8"))
        self.assertEqual(state["status"], "FAIL")
        self.assertEqual(state["stage"], "test:unit")


class TestCleanBoundary(DevWorkflowTestBase):
    def test_clean_only_devcache(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        self.run_build(ctx, mode="diagnostic")
        # 旧 build 目录里的文件必须保留
        keep = repo / "build" / "old-build" / "keep.txt"
        self.assertTrue(keep.exists())
        dw.cmd_clean(ctx, "diagnostic")
        self.assertFalse(ctx.mode_dir("diagnostic").exists())
        self.assertTrue(keep.exists())  # 绝不动用户/旧产物


class TestEnvIsolation(DevWorkflowTestBase):
    def test_process_env_not_leaked(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        before = dict(os.environ)
        rc, fake = self.run_build(ctx)
        self.assertEqual(rc, 0)
        after = dict(os.environ)
        self.assertEqual(before, after)  # 调用进程环境不变
        self.assertEqual(fake.last_env["IDF_PATH"], ctx.config["idf_path"])


class TestFenceInputs(DevWorkflowTestBase):
    def test_kconfig_and_nested_cmakelists_havehed(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        fp1, _ = dw.source_fingerprint(ctx)
        (repo / "main" / "Kconfig.projbuild").write_text('menu "X"\nendmenu\n', encoding="utf-8")
        fp2, _ = dw.source_fingerprint(ctx)
        self.assertNotEqual(fp1, fp2)
        # web 资源同样在 fence 内
        (repo / "main" / "web" / "app.js").write_text("// changed\n", encoding="utf-8")
        fp3, _ = dw.source_fingerprint(ctx)
        self.assertNotEqual(fp2, fp3)

    def test_diag_defaults_changes_fence(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        fp1, _ = dw.source_fingerprint(ctx)
        (repo / "sdkconfig.diag.defaults").write_text("CONFIG_DIAG_NOISE=n\n", encoding="utf-8")
        fp2, _ = dw.source_fingerprint(ctx)
        self.assertNotEqual(fp1, fp2)


class TestCacheWipe(DevWorkflowTestBase):
    def test_wipe_removes_objects_keeps_metadata(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        mode_dir = ctx.mode_dir("production")
        mode_dir.mkdir(parents=True)
        # 模拟旧 object 目录与 CMake 状态
        (mode_dir / "CMakeFiles").mkdir()
        (mode_dir / "CMakeFiles" / "stale.dir").mkdir()
        (mode_dir / "CMakeFiles" / "stale.dir" / "old.obj").write_bytes(b"private-const")
        (mode_dir / "CMakeCache.txt").write_text("cache\n", encoding="utf-8")
        for keep in ("state.json", "build.lock", "mode.tag", "defaults.audit.txt",
                     "sdkconfig.merged.defaults"):
            (mode_dir / keep).write_text("keep", encoding="utf-8")
        removed = dw.wipe_cache_objects(mode_dir)
        self.assertNotIn("state.json", removed)
        self.assertNotIn("build.lock", removed)
        self.assertFalse((mode_dir / "CMakeFiles").exists())
        self.assertFalse((mode_dir / "CMakeCache.txt").exists())
        for keep in ("state.json", "build.lock", "mode.tag", "defaults.audit.txt"):
            self.assertTrue((mode_dir / keep).is_file())

    def test_source_change_incremental_snapshot(self):
        """内容变化但用户源码 mtime 保持旧值：仅快照刷新变化文件，不清缓存。"""
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        rc, _ = self.run_build(ctx)
        self.assertEqual(rc, 0)
        mode_dir = ctx.mode_dir("production")
        keep = mode_dir / "CMakeFiles" / "keep.dir"
        keep.mkdir(parents=True)
        (keep / "keep.obj").write_bytes(b"OLD_PRIVATE_CONST")

        # 用户改内容，但 mtime 故意保持旧值（拷贝旧 mtime 坑）
        (repo / "main" / "main.c").write_text("int main(){return 42;}\n", encoding="utf-8")
        old_t = datetime(2020, 1, 1).timestamp()
        os.utime(repo / "main" / "main.c", (old_t, old_t))

        rc, _ = self.run_build(ctx)
        self.assertEqual(rc, 0)
        state = json.loads(dw.state_path(ctx, "production").read_text(encoding="utf-8"))
        self.assertFalse(state["full_rebuild"], "仅源码变化应走增量")
        self.assertIn("main/main.c", state.get("snapshot_changed", []),
                      "变化文件必须以新 mtime 进入快照，驱动 Ninja 重编受影响 object")
        self.assertTrue(keep.is_dir(), "增量分支不清旧 object")

    def test_bucket_change_wipes_stale_object(self):
        """配置桶变化（切板型）：清空旧 object 全量重编。"""
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        rc, _ = self.run_build(ctx, board="generic")
        self.assertEqual(rc, 0)
        stale = ctx.mode_dir("production") / "CMakeFiles" / "stale.dir"
        stale.mkdir(parents=True)
        (stale / "old.obj").write_bytes(b"OLD_PRIVATE_CONST")
        rc, _ = self.run_build(ctx, board="m5stamp")
        self.assertEqual(rc, 0)
        state = json.loads(dw.state_path(ctx, "production").read_text(encoding="utf-8"))
        self.assertTrue(state["full_rebuild"])
        self.assertFalse(stale.exists(), "配置桶变化必须清掉旧 object")

    def test_production_rejects_diag_marker(self):
        """production 显式拒绝合并 defaults 中的 DIAG=y 标记。"""
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        (repo / "sdkconfig.defaults").write_text(
            "CONFIG_BOARD_GENERIC_C3=y\nCONFIG_DIAG_UART=y\n", encoding="utf-8")
        with self.assertRaises(dw.DevWorkflowError) as cm:
            self.run_build(ctx, mode="production")
        self.assertIn("DIAG", str(cm.exception))
        # diagnostic 模式同一份配置不拒
        rc, _ = self.run_build(ctx, mode="diagnostic")
        self.assertEqual(rc, 0)

    def test_mode_tag_rejects_cross_mode(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        mode_dir = ctx.mode_dir("production")
        mode_dir.mkdir(parents=True)
        (mode_dir / "mode.tag").write_text("maintenance", encoding="utf-8")
        with self.assertRaises(dw.DevWorkflowError) as cm:
            self.run_build(ctx, mode="production")
        self.assertIn("maintenance", str(cm.exception))

    @unittest.skipUnless(platform.system() == "Windows", "WSL 真编译 fixture 仅在 Windows 跑")
    def test_real_compile_wipe_forces_new_binary(self):
        """真实 gcc fixture：源码内容变化+按策略清缓存产物后，新二进制必须反映新常量。"""
        script = (
            "set -e; d=/tmp/bw-wipe-fixture; rm -rf $d; mkdir -p $d; cd $d; "
            "printf 'extern const char MSG[];\\n' > msg.h; "
            "printf 'const char MSG[] = \"OLD_PRIVATE\";\\n' > msg.c; "
            "printf '#include <stdio.h>\\n#include \"msg.h\"\\nint main(){puts(MSG);return 0;}\\n' > main.c; "
            "gcc main.c msg.c -o prog; ./prog; "
            "rm -f prog *.o; "
            "printf 'const char MSG[] = \"NEW_AFTER_WIPE\";\\n' > msg.c; "
            "gcc main.c msg.c -o prog; ./prog"
        )
        r = subprocess.run(["wsl", "-e", "bash", "-c", script], capture_output=True,
                           text=True, timeout=90, encoding="utf-8", errors="replace")
        self.assertEqual(r.returncode, 0, f"WSL fixture 失败: {r.stderr}")
        self.assertIn("OLD_PRIVATE", r.stdout)
        self.assertIn("NEW_AFTER_WIPE", r.stdout)

    @unittest.skipUnless(platform.system() == "Windows", "WSL make 增量 fixture 仅在 Windows 跑")
    def test_incremental_make_fixture(self):
        """make 真实增量：第二次无变化不重编；改内容但源码 mtime 旧→快照新 mtime 重编。"""
        # Makefile 配方行需要 tab，用 printf '\t' 生成
        script = (
            "set -e; d=/tmp/bw-incr-fixture; rm -rf $d; mkdir -p $d/src $d/snap; "
            "cd $d/src; "
            "printf 'extern const char MSG[];\\n' > msg.h; "
            "printf 'const char MSG[] = \"OLD\";\\n' > msg.c; "
            "printf '#include <stdio.h>\\n#include \"msg.h\"\\nint main(){puts(MSG);return 0;}\\n' > main.c; "
            "{ printf 'CC=gcc\\nall: prog\\n'; printf 'prog: main.o msg.o\\n\\t$(CC) main.o msg.o -o prog\\n'; "
            "printf 'main.o: main.c msg.h\\n\\t$(CC) -c main.c -o main.o\\n'; "
            "printf 'msg.o: msg.c msg.h\\n\\t$(CC) -c msg.c -o msg.o\\n'; } > Makefile; "
            "touch -d '2020-01-01' msg.c main.c msg.h; "
            "cp msg.c msg.h main.c Makefile ../snap/; "
            "cd ../snap; make >/dev/null; ./prog; "
            "msg_mtime_before=$(stat -c %Y msg.o); "
            "out2=$(make 2>&1); echo \"SECOND:$out2\"; "
            "msg_mtime_after=$(stat -c %Y msg.o); "
            "echo \"MTIME_DIFF:$((msg_mtime_after - msg_mtime_before))\"; "
            "cd ../src; printf 'const char MSG[] = \"NEW\";\\n' > msg.c; touch -d '2020-01-01' msg.c; "
            "cp msg.c ../snap/; cd ../snap; make; ./prog"        )
        r = subprocess.run(["wsl", "-e", "bash", "-c", script], capture_output=True,
                           text=True, timeout=90, encoding="utf-8", errors="replace")
        self.assertEqual(r.returncode, 0, f"WSL make fixture 失败: {r.stderr}")
        out = r.stdout
        self.assertIn("OLD", out)
        self.assertIn("Nothing to be done", out, "第二次无变化必须不重编")
        self.assertIn("MTIME_DIFF:0", out, "无变化时 msg.o 不重新生成")
        self.assertIn("NEW", out, "快照新 mtime 后必须重编并反映新常量")


class TestStatus(DevWorkflowTestBase):
    def test_status_lists_modes(self):
        repo = make_fake_repo(self.tmp)
        ctx = make_ctx(self.tmp, repo)
        self.run_build(ctx, mode="production")
        from io import StringIO
        import contextlib
        buf = StringIO()
        with contextlib.redirect_stdout(buf):
            dw.cmd_status(ctx)
        out = buf.getvalue()
        self.assertIn("production", out)
        self.assertIn("PASS", out)


if __name__ == "__main__":
    unittest.main(verbosity=2)
