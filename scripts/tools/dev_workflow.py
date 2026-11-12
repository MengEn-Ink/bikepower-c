#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""BikePower 本机开发验证统一入口（dev_workflow）。

目标：
  - doctor / build / test / status / clean 统一子命令。
  - 自动识别并按进程环境（绝不改系统 PATH、不污染调用进程）装配
    IDF Python / toolchain / cmake / ninja / WSL。
  - production / diagnostic / maintenance 三种模式各自独立增量缓存，
    互不混用；维护与诊断产物绝不写回生产缓存。
  - fence 基于「内容 hash + git 身份 + 配置」，不依赖文件 mtime，
    输入源变化可靠触发重配，防止旧 mtime/拷贝时间戳误判复用。
  - 每模式一把本机文件锁保护缓存目录；分阶段状态 JSON + 可复用日志。
  - 超时与真实退出码：命令失败只记 FAIL，绝不冒称门禁 PASS。

边界：
  - 本工具不烧录、不碰串口/BLE/设备；生产固件构建由生产执行者发起。
  - clean 只删本工具自己管理的 build/devcache/<mode> 目录，绝不递归清用户文件。
  - 机器本地配置放用户目录（~/.bikepower/dev_workflow.json），仓库只放示例。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import platform
import shutil
import signal
import subprocess
import sys
import time
from dataclasses import dataclass, field
from pathlib import Path
from typing import Dict, List, Optional, Sequence, Tuple

# ---------------------------------------------------------------------------
# 常量
# ---------------------------------------------------------------------------

TOOL_VERSION = "1.0.0"
MODES = ("production", "diagnostic", "maintenance")
BOARDS = ("generic", "m5stamp")
CACHE_ROOT_NAME = "devcache"
LOCK_WAIT_DEFAULT = 60
TIMEOUT_DEFAULTS = {"doctor": 90, "build": 2400, "test": 1200, "lock_wait": LOCK_WAIT_DEFAULT}

# fence 扫描的源码输入（相对仓库根），与固件构建相关的核心输入集。
# 覆盖嵌套 CMakeLists、Kconfig.projbuild、web 内联资源、诊断 defaults；
# 不存在的文件自动跳过；managed_components 等下载依赖不 hash。
SOURCE_GLOBS = (
    "main/*.c",
    "main/*.h",
    "main/web/**/*",
    "components/bikepower_domain/*.c",
    "components/bikepower_domain/include/*.h",
)
SOURCE_TOP_FILES = (
    "CMakeLists.txt",
    "main/CMakeLists.txt",
    "main/Kconfig.projbuild",
    "components/bikepower_domain/CMakeLists.txt",
    "sdkconfig.defaults",
    "sdkconfig.diag.defaults",
    "partitions.csv",
    # 构建期由 main/CMakeLists 调用的生成脚本（CMAKE_SOURCE_DIR 相对路径）
    "scripts/inline_static.py",
    "scripts/vendor/rjsmin.py",
    "scripts/vendor/rjsmin.LICENSE",
    # managed 组件只 hash 清单/锁（不 hash 整个 managed_components tree）
    "main/idf_component.yml",
    "components/bikepower_domain/idf_component.yml",
    "dependencies.lock",
)

# production 模式拒绝的诊断标记（合并 defaults 中出现即拒）
import re as _re  # noqa: E402
DIAG_MARKER_RE = _re.compile(r"^CONFIG_\w*DIAG\w*\s*=\s*y\b", _re.IGNORECASE | _re.MULTILINE)

# 缓存目录内 fence 变化时必须清空（防旧 object/mtime 污染），但保留这些本工具元数据。
CACHE_KEEP_FILES = {"state.json", "build.lock", "mode.tag", "defaults.audit.txt",
                    "sdkconfig.merged.defaults", "snapshot.manifest.json",
                    "source-snapshot"}

BOARD_KCONFIG = {
    "generic": "CONFIG_BOARD_GENERIC_C3=y",
    "m5stamp": "CONFIG_BOARD_M5STAMP_C3=y",
}

EXIT_OK = 0
EXIT_ERROR = 1       # 用法/运行时错误
EXIT_BUILD_FAIL = 2  # 构建命令非零退出
EXIT_TEST_FAIL = 3   # 测试命令非零退出
EXIT_TIMEOUT = 4     # 超时
EXIT_LOCK_BUSY = 5   # 锁被占用且等待超时
EXIT_ENV_MISSING = 6  # 环境缺少必需组件


class DevWorkflowError(Exception):
    """带退出码的可预期错误。"""

    def __init__(self, message: str, code: int = EXIT_ERROR):
        super().__init__(message)
        self.code = code


class LockBusy(DevWorkflowError):
    def __init__(self, message: str):
        super().__init__(message, EXIT_LOCK_BUSY)


# ---------------------------------------------------------------------------
# 上下文与配置
# ---------------------------------------------------------------------------

@dataclass
class Context:
    repo: Path
    config_path: Optional[Path] = None
    config: dict = field(default_factory=dict)

    @property
    def cache_root(self) -> Path:
        override = self.config.get("cache_root")
        if override:
            return Path(override)
        return self.repo / "build" / CACHE_ROOT_NAME

    def mode_dir(self, mode: str) -> Path:
        return self.cache_root / mode

    def log_dir(self) -> Path:
        return self.cache_root / "logs"

    def timeout(self, key: str) -> int:
        return int(self.config.get("timeouts", {}).get(key, TIMEOUT_DEFAULTS[key]))


def default_repo_root() -> Path:
    # scripts/tools/dev_workflow.py -> parents[2] == 仓库根
    return Path(__file__).resolve().parents[2]


def user_config_path() -> Path:
    return Path(os.path.expanduser("~")) / ".bikepower" / "dev_workflow.json"


def load_config(cli_config: Optional[str], repo: Path) -> Tuple[Optional[Path], dict]:
    """按优先级合并配置：用户目录配置 < 仓库示例不读 < 显式 --config。"""
    cfg: dict = {}
    path: Optional[Path] = None
    user_path = user_config_path()
    if user_path.is_file():
        try:
            cfg = json.loads(user_path.read_text(encoding="utf-8"))
            path = user_path
        except Exception as exc:  # noqa: BLE001
            raise DevWorkflowError(f"用户配置解析失败 {user_path}: {exc}")
    if cli_config:
        p = Path(cli_config).expanduser().resolve()
        if not p.is_file():
            raise DevWorkflowError(f"--config 指定的文件不存在: {p}")
        try:
            cfg = json.loads(p.read_text(encoding="utf-8"))
        except Exception as exc:  # noqa: BLE001
            raise DevWorkflowError(f"--config 解析失败 {p}: {exc}")
        path = p
    return path, cfg


# ---------------------------------------------------------------------------
# 环境探测（只读检测；装配只产出进程 env dict）
# ---------------------------------------------------------------------------

def _hash_file(p: Path) -> str:
    h = hashlib.sha256()
    with p.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def _first_existing(paths: Sequence[Path]) -> Optional[Path]:
    for p in paths:
        if p.exists():
            return p
    return None


def detect_idf(ctx: Context) -> Tuple[Optional[Path], List[str]]:
    notes: List[str] = []
    override = ctx.config.get("idf_path")
    candidates: List[Path] = []
    if override:
        candidates.append(Path(override))
    if os.environ.get("IDF_PATH"):
        candidates.append(Path(os.environ["IDF_PATH"]))
    # 仓库内置 toolchain（本机常见布局，历史保留）
    toolchain_root = ctx.repo / "build" / "toolchain"
    if toolchain_root.is_dir():
        candidates.extend(sorted(toolchain_root.glob("esp-idf-*"), reverse=True))
    # 常见安装位置
    home = Path(os.path.expanduser("~"))
    for root in (
        home / ".espressif" / "frameworks",
        Path("C:/Espressif/frameworks"),
        Path("D:/Espressif/frameworks"),
        home / "esp",
    ):
        if root.is_dir():
            candidates.extend(sorted(root.glob("esp-idf*"), reverse=True))
    for c in candidates:
        if (c / "tools" / "idf.py").is_file():
            return c.resolve(), notes
        notes.append(f"跳过无效候选(无 tools/idf.py): {c}")
    return None, notes


def detect_idf_python(ctx: Context, idf_path: Optional[Path]) -> Tuple[Optional[Path], Optional[Path]]:
    """返回 (python.exe, python_env 根目录)。"""
    override = ctx.config.get("idf_python")
    if override:
        p = Path(override).expanduser().absolute()
        if p.is_file():
            return p, _python_env_root(p)
    env_override = os.environ.get("IDF_PYTHON_ENV_PATH")
    home = Path(os.path.expanduser("~"))
    roots = []
    if env_override:
        roots.append(Path(env_override))
    roots += [home / ".espressif" / "python_env", Path("C:/Espressif/python_env")]
    for root in roots:
        if not root.is_dir():
            continue
        direct = root / ("Scripts/python.exe" if os.name == "nt" else "bin/python")
        if direct.is_file():
            return direct.absolute(), root.absolute()
        for d in sorted(root.glob("idf*_env"), reverse=True):
            py = d / "Scripts" / "python.exe" if os.name == "nt" else d / "bin" / "python"
            if py.is_file():
                return py.absolute(), d.absolute()
    # 回退：PATH 中的 python（不校验 idf 包，doctor 会单独报告）
    found = shutil.which("python") or shutil.which("python3")
    if found:
        return Path(found), None
    return None, None


def _python_env_root(p: Path) -> Optional[Path]:
    return p.parent.parent if p.parent.name.lower() in ("scripts", "bin") else None


def detect_tools_root(ctx: Context) -> Optional[Path]:
    override = ctx.config.get("tools_root")
    if override:
        p = Path(override)
        if p.is_dir():
            return p
    home = Path(os.path.expanduser("~"))
    for p in (home / ".espressif" / "tools", Path("C:/Espressif/tools")):
        if p.is_dir():
            return p
    return None


def _bin_dirs_for(tools_root: Path, tool: str) -> List[Path]:
    """在 tools_root/<tool>/<version>/ 下找 bin 目录。

    兼容两种布局：
      <tool>/<version>/bin/<exe>
      <tool>/<version>/<tool-name>/bin/<exe>   （riscv32-esp-elf 实际布局）
    """
    base = tools_root / tool
    out: List[Path] = []
    if not base.is_dir():
        return out
    for version_dir in sorted(base.iterdir(), reverse=True):
        if not version_dir.is_dir():
            continue
        for candidate in (version_dir / "bin", version_dir):
            if candidate.is_dir():
                out.append(candidate)
        for nested in version_dir.glob("*/bin"):
            if nested.is_dir():
                out.append(nested)
    return out


def load_idf_toolchain(idf_path: Optional[Path], tools_root: Optional[Path],
                       target: str = "esp32c3") -> Tuple[Dict[str, dict], str]:
    """按 IDF tools.json 的 recommended 版本精确选定工具链。

    只选 target 兼容且 status=recommended 的版本，避免 ~/.espressif/tools 下
    新老版本目录混排导致实际编译器漂移。返回 (selected, source)。
    selected: {tool_name: {"version": str, "bins": [abs dir...]}}
    tools.json 缺失/解析失败时返回空 dict + 说明，build_env 回退旧扫描。
    """
    if not idf_path or not tools_root:
        return {}, "no-idf-or-tools-root"
    tools_json = idf_path / "tools" / "tools.json"
    if not tools_json.is_file():
        return {}, "tools.json-missing"
    try:
        data = json.loads(tools_json.read_text(encoding="utf-8"))
    except Exception as exc:  # noqa: BLE001
        return {}, f"tools.json-parse-error:{exc}"
    selected: Dict[str, dict] = {}
    for tool in data.get("tools", []):
        targets = tool.get("supported_targets", [])
        if targets and "all" not in targets and target not in targets:
            continue
        rec = next((v for v in tool.get("versions", []) if v.get("status") == "recommended"), None)
        if not rec:
            continue
        version = rec["name"]
        base = tools_root / tool["name"] / version
        bins: List[Path] = []
        for parts in tool.get("export_paths", []):
            d = base.joinpath(*[p for p in parts if p])
            if d.is_dir():
                bins.append(d)
        if not bins and base.is_dir():
            bins.append(base)
        if bins:
            selected[tool["name"]] = {"version": version, "bins": [str(b) for b in bins]}
    return selected, "tools.json-recommended"


def build_env(ctx: Context, idf_path: Optional[Path], python_exe: Optional[Path],
              tools_root: Optional[Path], selected: Optional[Dict[str, dict]] = None) -> Dict[str, str]:
    """只产出子进程 env dict；绝不修改调用进程 os.environ。"""
    env = dict(os.environ)
    if idf_path:
        env["IDF_PATH"] = str(idf_path)
    if python_exe:
        env_dir = _python_env_root(python_exe)
        if env_dir is not None:
            env["IDF_PYTHON_ENV_PATH"] = str(env_dir)
    prepend: List[str] = []
    if python_exe:
        prepend.append(str(python_exe.parent))
    if selected:
        for tool in sorted(selected):
            prepend.extend(selected[tool]["bins"])
    elif tools_root:
        # 回退：无 tools.json 时按旧扫描（仅诊断用，真实 IDF 一定走上方精确路径）
        for tool in ("cmake", "ninja", "riscv32-esp-elf", "riscv32-esp-elf-gdb",
                     "openocd-esp32", "ccache", "esp-rom-elfs", "idf-exe"):
            prepend.extend(str(d) for d in _bin_dirs_for(tools_root, tool))
    if prepend:
        sep = ";" if os.name == "nt" else ":"
        env["PATH"] = sep.join(dict.fromkeys(prepend)) + sep + env.get("PATH", "")
    env["PYTHONDONTWRITEBYTECODE"] = "1"
    return env


def run_subprocess(argv: Sequence[str], env: Dict[str, str], cwd: Path,
                   timeout_s: float, log_path: Path, stream: bool = True) -> Tuple[int, float, str]:
    """同步运行子进程；stdout/stderr 合并写入 log_path；返回 (rc, 秒, 尾行摘要)。

    超时必须真实生效：读取在独立线程进行，主循环按 deadline 轮询，
    避免「阻塞在 readline 上、超时永远不触发」。
    """
    import queue
    import threading

    log_path.parent.mkdir(parents=True, exist_ok=True)
    started = time.monotonic()
    log_fh = log_path.open("a", encoding="utf-8", errors="replace")
    log_fh.write(f"\n$ {' '.join(str(a) for a in argv)}\n")
    log_fh.flush()
    popen = None
    reader = None
    try:
        popen = subprocess.Popen(
            [str(a) for a in argv],
            cwd=str(cwd), env=env,
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
            bufsize=1, text=True, encoding="utf-8", errors="replace",
            start_new_session=os.name != "nt",
        )
        assert popen.stdout is not None
        line_q: "queue.Queue[Optional[str]]" = queue.Queue()

        def _reader() -> None:
            assert popen.stdout is not None
            for line in popen.stdout:
                line_q.put(line)
            line_q.put(None)

        reader = threading.Thread(target=_reader, daemon=True)
        reader.start()

        tail: List[str] = []
        timed_out = False
        while True:
            if time.monotonic() - started >= timeout_s:
                timed_out = True
                _kill_tree(popen)
                popen.wait(timeout=5)
                break
            try:
                line = line_q.get(timeout=0.2)
            except queue.Empty:
                line = None
            if line is not None:
                log_fh.write(line)
                log_fh.flush()
                tail.append(line.rstrip())
                if len(tail) > 20:
                    tail = tail[-20:]
                if stream:
                    print(line, end="")
                continue
            rc = popen.poll()
            if rc is not None:
                break
        if timed_out:
            elapsed = time.monotonic() - started
            log_fh.write(f"\n!! TIMEOUT after {elapsed:.1f}s (limit {timeout_s}s)\n")
            log_fh.flush()
            return -1, elapsed, "TIMEOUT"
        # 进程已退出，收掉 reader 线程残余
        reader.join(timeout=2)
        elapsed = time.monotonic() - started
        log_fh.write(f"\n-- exit={rc} duration={elapsed:.1f}s\n")
        return rc, elapsed, "\n".join(tail[-8:])
    finally:
        try:
            if popen is not None and popen.stdout is not None:
                popen.stdout.close()
        except Exception:  # noqa: BLE001
            pass
        log_fh.close()


def _kill_tree(popen: subprocess.Popen) -> None:
    if popen.poll() is not None:
        return
    if os.name == "nt":
        subprocess.run(["taskkill", "/PID", str(popen.pid), "/T", "/F"],
                       capture_output=True)
    else:
        try:
            os.killpg(popen.pid, signal.SIGKILL)
        except Exception:  # noqa: BLE001
            popen.kill()


# ---------------------------------------------------------------------------
# 文件锁（每模式一把）
# ---------------------------------------------------------------------------

class ModeLock:
    """跨平台文件锁；非阻塞获取，忙等到超时。锁文件只放缓存目录内。"""

    def __init__(self, lock_path: Path):
        self.lock_path = lock_path
        self._fh = None

    def acquire(self, timeout_s: float) -> None:
        self.lock_path.parent.mkdir(parents=True, exist_ok=True)
        self._fh = self.lock_path.open("a+b")
        self._fh.seek(0)
        deadline = time.monotonic() + timeout_s
        while True:
            try:
                if os.name == "nt":
                    import msvcrt
                    try:
                        msvcrt.locking(self._fh.fileno(), msvcrt.LK_NBLCK, 1)
                        return
                    except OSError:
                        pass
                else:
                    import fcntl
                    try:
                        fcntl.flock(self._fh.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                        return
                    except OSError:
                        pass
            except LockBusy:
                raise
            if time.monotonic() >= deadline:
                self._close()
                raise LockBusy(
                    f"缓存锁被占用且等待 {timeout_s:.0f}s 仍未获得: {self.lock_path}\n"
                    f"  另一个 dev_workflow 进程正在使用同一模式；请等待该进程完成，保留锁文件。"
                )
            time.sleep(0.5)

    def release(self) -> None:
        if self._fh is None:
            return
        try:
            if os.name == "nt":
                import msvcrt
                self._fh.seek(0)
                try:
                    msvcrt.locking(self._fh.fileno(), msvcrt.LK_UNLCK, 1)
                except OSError:
                    pass
            else:
                import fcntl
                fcntl.flock(self._fh.fileno(), fcntl.LOCK_UN)
        finally:
            self._close()

    def _close(self) -> None:
        if self._fh is not None:
            try:
                self._fh.close()
            finally:
                self._fh = None

    def __enter__(self):
        return self

    def __exit__(self, *exc):
        self.release()


# ---------------------------------------------------------------------------
# fence：内容 hash + git 身份 + 配置
# ---------------------------------------------------------------------------

def git_identity(ctx: Context) -> Dict[str, str]:
    info = {"head": "nogit", "dirty": "unknown"}
    try:
        head = subprocess.run(
            ["git", "-C", str(ctx.repo), "rev-parse", "HEAD"],
            capture_output=True, text=True, timeout=15,
        )
        if head.returncode == 0:
            info["head"] = head.stdout.strip()
        st = subprocess.run(
            ["git", "-C", str(ctx.repo), "status", "--porcelain", "--"]
            + list(SOURCE_TOP_FILES) + ["main", "components/bikepower_domain"],
            capture_output=True, text=True, timeout=15,
        )
        if st.returncode == 0:
            info["dirty"] = "clean" if st.stdout.strip() == "" else "dirty"
    except Exception as exc:  # noqa: BLE001
        info["dirty"] = f"git-error:{exc}"
    return info


def source_fingerprint(ctx: Context) -> Tuple[str, int]:
    """对源码输入内容做 hash（不看 mtime）。返回 (指纹, 文件数)。"""
    h = hashlib.sha256()
    count = 0
    for name in SOURCE_TOP_FILES:
        p = ctx.repo / name
        if p.is_file():
            h.update(name.encode("utf-8"))
            h.update(_hash_file(p).encode("ascii"))
            count += 1
    for pattern in SOURCE_GLOBS:
        for p in sorted(ctx.repo.glob(pattern)):
            if p.is_file():
                h.update(p.relative_to(ctx.repo).as_posix().encode("utf-8"))
                h.update(_hash_file(p).encode("ascii"))
                count += 1
    return h.hexdigest(), count


def snapshot_files(ctx: Context) -> Dict[str, str]:
    """返回需要镜像进 source-snapshot 的全部输入：{相对路径: sha256}。"""
    out: Dict[str, str] = {}
    for name in SOURCE_TOP_FILES:
        p = ctx.repo / name
        if p.is_file():
            out[name] = _hash_file(p)
    for pattern in SOURCE_GLOBS:
        for p in sorted(ctx.repo.glob(pattern)):
            if p.is_file():
                out[p.relative_to(ctx.repo).as_posix()] = _hash_file(p)
    return out


def sync_source_snapshot(ctx: Context, mode: str, files: Dict[str, str]) -> Tuple[Path, List[str]]:
    """把内容变化的输入文件镜像到隔离 source-snapshot。

    - 变化/新增文件：copyfile（只写内容，快照文件自然获得当前 mtime），
      Ninja 凭新 mtime 重编受影响 object；不动用户源码 mtime。
    - 未变化文件：快照保持原样，绝不 touch（保住旧 mtime，Ninja 不重编）。
    - 已删除文件：从快照移除。
    返回 (快照根, 本次变化的相对路径列表)。
    """
    snap = ctx.mode_dir(mode) / "source-snapshot"
    snap.mkdir(parents=True, exist_ok=True)
    manifest_path = ctx.mode_dir(mode) / "snapshot.manifest.json"
    manifest: Dict[str, str] = {}
    if manifest_path.is_file():
        try:
            manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
        except Exception:  # noqa: BLE001
            manifest = {}
    changed: List[str] = []
    for rel, sha in files.items():
        dest = snap / rel
        if manifest.get(rel) != sha or not dest.is_file() or _hash_file(dest) != sha:
            dest.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ctx.repo / rel, dest)  # 内容拷贝，新 mtime
            changed.append(rel)
            manifest[rel] = sha
    for rel in list(manifest):
        if rel not in files:
            try:
                (snap / rel).unlink()
            except OSError:
                pass
            del manifest[rel]
    manifest_path.write_text(json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8")
    return snap, changed


def wipe_cache_objects(cache_dir: Path) -> List[str]:
    """fence 变化时清空缓存目录内的全部构建产物，只保留本工具元数据。

    目的：彻底消除「源码内容变了但旧 object 因 mtime/拷贝时间戳被 Ninja 复用」
    的历史污染路径。只操作 build/devcache/<mode> 自己的目录，绝不碰仓库源码与旧 build/。
    返回被删除的条目名列表。
    """
    removed: List[str] = []
    if not cache_dir.is_dir():
        return removed
    for entry in cache_dir.iterdir():
        if entry.name in CACHE_KEEP_FILES:
            continue
        try:
            if entry.is_dir() and not entry.is_symlink():
                shutil.rmtree(entry)
            else:
                entry.unlink()
            removed.append(entry.name)
        except OSError as exc:  # noqa: BLE001
            raise DevWorkflowError(f"清空缓存产物失败 {entry}: {exc}")
    return removed


def fence_digest(ctx: Context, mode: str, board: str, merged_defaults: str) -> str:
    fp, _ = source_fingerprint(ctx)
    gi = git_identity(ctx)
    material = json.dumps({
        "source": fp,
        "git": gi,
        "mode": mode,
        "board": board,
        "target": ctx.config.get("target", "esp32c3"),
        "defaults_sha": hashlib.sha256(merged_defaults.encode("utf-8")).hexdigest(),
        "tool_version": TOOL_VERSION,
    }, sort_keys=True)
    return hashlib.sha256(material.encode("utf-8")).hexdigest()


# ---------------------------------------------------------------------------
# 分阶段状态
# ---------------------------------------------------------------------------

def state_path(ctx: Context, mode: str) -> Path:
    return ctx.mode_dir(mode) / "state.json"


def write_state(ctx: Context, mode: str, **fields) -> None:
    p = state_path(ctx, mode)
    p.parent.mkdir(parents=True, exist_ok=True)
    data: dict = {}
    if p.is_file():
        try:
            data = json.loads(p.read_text(encoding="utf-8"))
        except Exception:  # noqa: BLE001
            data = {}
    data.update(fields)
    data["mode"] = mode
    data["updated_at"] = time.strftime("%Y-%m-%d %H:%M:%S")
    tmp = p.with_suffix(".json.tmp")
    tmp.write_text(json.dumps(data, indent=2, ensure_ascii=False), encoding="utf-8")
    tmp.replace(p)


def read_state(ctx: Context, mode: str) -> Optional[dict]:
    p = state_path(ctx, mode)
    if not p.is_file():
        return None
    try:
        return json.loads(p.read_text(encoding="utf-8"))
    except Exception:  # noqa: BLE001
        return None


# ---------------------------------------------------------------------------
# 子命令：doctor
# ---------------------------------------------------------------------------

def _run_version(argv: Sequence[str], timeout_s: int = 20) -> str:
    try:
        r = subprocess.run([str(a) for a in argv], capture_output=True, text=True,
                           timeout=timeout_s, encoding="utf-8", errors="replace")
        out = (r.stdout or "") + (r.stderr or "")
        first = next((ln.strip() for ln in out.splitlines() if ln.strip()), "")
        return first if r.returncode == 0 else f"exit={r.returncode} {first}"
    except FileNotFoundError:
        return "NOT_FOUND"
    except Exception as exc:  # noqa: BLE001
        return f"ERROR:{exc}"


def cmd_doctor(ctx: Context, as_json: bool) -> int:
    checks: List[Dict[str, str]] = []

    idf_path, notes = detect_idf(ctx)
    checks.append({
        "name": "ESP-IDF",
        "status": "OK" if idf_path and (idf_path / "tools" / "idf.py").is_file() else "MISSING",
        "detail": str(idf_path) if idf_path else "未找到 esp-idf（可在配置里写 idf_path）",
    })

    py_exe, py_env = detect_idf_python(ctx, idf_path)
    py_ver = _run_version([str(py_exe), "--version"]) if py_exe else "NOT_FOUND"
    py_broken = (not py_exe or "NOT_FOUND" in py_ver or py_ver.startswith(("ERROR", "exit=")))
    checks.append({
        "name": "IDF Python",
        "status": "MISSING" if py_broken else "OK",
        "detail": f"{py_exe}  ({py_ver})",
    })

    tools_root = detect_tools_root(ctx)
    selected, toolchain_src = load_idf_toolchain(idf_path, tools_root,
                                                  ctx.config.get("target", "esp32c3"))
    env = build_env(ctx, idf_path, py_exe, tools_root, selected)
    if tools_root:
        checks.append({
            "name": "tools_root",
            "status": "OK", "detail": str(tools_root),
        })
        checks.append({
            "name": "工具链选定来源",
            "status": "OK" if selected else "WARN",
            "detail": f"{toolchain_src}；选中 {len(selected)} 个 recommended 版本（不扫描混排）",
        })
        cmake_bin = shutil.which("cmake", path=env["PATH"])
        ninja_bin = shutil.which("ninja", path=env["PATH"])
        gcc_used = shutil.which("riscv32-esp-elf-gcc", path=env["PATH"])
        checks.append({
            "name": "cmake",
            "status": "OK" if cmake_bin else "MISSING",
            "detail": f"{cmake_bin}  ({_run_version([str(cmake_bin), '--version'])})"
                      if cmake_bin else "tools.json 选定版本缺失",
        })
        checks.append({
            "name": "ninja",
            "status": "OK" if ninja_bin else "MISSING",
            "detail": f"{ninja_bin}  ({_run_version([str(ninja_bin), '--version'])})"
                      if ninja_bin else "tools.json 选定版本缺失",
        })
        checks.append({
            "name": "riscv32-esp-elf gcc（build 实际使用）",
            "status": "OK" if gcc_used else "MISSING",
            "detail": f"{gcc_used}  ({_run_version([str(gcc_used), '--version'])})"
                      if gcc_used else "tools.json 选定版本缺失",
        })
    else:
        for name in ("tools_root", "cmake", "ninja", "riscv32-esp-elf gcc（build 实际使用）"):
            checks.append({"name": name, "status": "MISSING", "detail": "未找到 ~/.espressif/tools"})

    # WSL 仅用于 host 单测，缺失只 WARN
    wsl_ok = False
    if os.name == "nt":
        r = subprocess.run(["wsl", "-e", "gcc", "--version"], capture_output=True, text=True, timeout=20)
        wsl_ok = r.returncode == 0
        detail = (r.stdout or r.stderr or "").splitlines()[0] if (r.stdout or r.stderr) else "wsl 不可用"
    else:
        detail = platform.platform()
        wsl_ok = bool(shutil.which("gcc"))
    checks.append({
        "name": "host 单测 gcc (WSL/Unix)",
        "status": "OK" if wsl_ok else "WARN",
        "detail": detail,
    })

    gi = git_identity(ctx)
    checks.append({
        "name": "git 身份",
        "status": "OK" if gi["head"] != "nogit" else "WARN",
        "detail": f"head={gi['head']} working_tree={gi['dirty']}",
    })

    payload = {
        "tool_version": TOOL_VERSION,
        "repo": str(ctx.repo),
        "config_path": str(ctx.config_path) if ctx.config_path else "(无用户配置，使用自动探测)",
        "python_env": str(py_env) if py_env else "(python_env 根未定位)",
        "notes": notes,
        "checks": checks,
    }
    if as_json:
        print(json.dumps(payload, indent=2, ensure_ascii=False))
    else:
        print(f"dev_workflow {TOOL_VERSION}  repo={ctx.repo}")
        print(f"配置: {payload['config_path']}")
        for c in checks:
            mark = {"OK": "[OK]  ", "WARN": "[WARN]", "MISSING": "[MISS]"}[c["status"]]
            print(f"{mark} {c['name']}: {c['detail']}")
        if notes:
            print("候选笔记: " + "; ".join(notes))
    missing = [c for c in checks if c["status"] == "MISSING"]
    return EXIT_ENV_MISSING if missing else EXIT_OK


# ---------------------------------------------------------------------------
# 子命令：build
# ---------------------------------------------------------------------------

def _merged_defaults(ctx: Context, board: str) -> str:
    base = (ctx.repo / "sdkconfig.defaults").read_text(encoding="utf-8")
    return base.rstrip("\r\n") + "\n" + BOARD_KCONFIG[board] + "\n"


def _config_bucket(mode: str, board: str, merged: str, selected: Dict[str, dict],
                   target: str = "esp32c3") -> str:
    """配置桶：模式/板型/合并defaults/工具链版本/target 变化才全量 wipe。"""
    material = json.dumps({
        "mode": mode, "board": board, "target": target,
        "defaults_sha": hashlib.sha256(merged.encode("utf-8")).hexdigest(),
        "tools": {k: v["version"] for k, v in sorted(selected.items())},
        "tool_version": TOOL_VERSION,
    }, sort_keys=True)
    return hashlib.sha256(material.encode("utf-8")).hexdigest()


def cmd_build(ctx: Context, mode: str, board: str, dry_run: bool) -> int:
    if mode not in MODES or board not in BOARDS:
        raise DevWorkflowError(f"未知模式或板型: {mode}/{board}")
    lock = ModeLock(ctx.mode_dir(mode) / "build.lock")
    lock.acquire(ctx.timeout("lock_wait"))
    try:
        return _cmd_build_locked(ctx, mode, board, dry_run)
    finally:
        lock.release()


ARTIFACT_FILES = ("bikepower-c.bin", "bootloader/bootloader.bin",
                  "partition_table/partition-table.bin")


def artifact_hashes(cache_dir: Path) -> Dict[str, str]:
    missing = [rel for rel in ARTIFACT_FILES if not (cache_dir / rel).is_file()]
    if missing:
        raise DevWorkflowError(f"构建缺少实际产物: {missing}", EXIT_BUILD_FAIL)
    return {rel: _hash_file(cache_dir / rel) for rel in ARTIFACT_FILES}


def _cmd_build_locked(ctx: Context, mode: str, board: str, dry_run: bool) -> int:
    if mode not in MODES:
        raise DevWorkflowError(f"未知模式 {mode}，可选: {MODES}")
    if board not in BOARDS:
        raise DevWorkflowError(f"未知板型 {board}，可选: {BOARDS}")

    idf_path, _ = detect_idf(ctx)
    py_exe, _ = detect_idf_python(ctx, idf_path)
    tools_root = detect_tools_root(ctx)
    if not idf_path or not py_exe:
        raise DevWorkflowError(
            "环境不完整：先运行 doctor 检查。缺 IDF 或 IDF Python。", EXIT_ENV_MISSING)

    selected, toolchain_src = load_idf_toolchain(idf_path, tools_root,
                                                 ctx.config.get("target", "esp32c3"))

    cache_dir = ctx.mode_dir(mode)
    cache_dir.mkdir(parents=True, exist_ok=True)

    # 模式标记审计（第二道防线）：缓存目录已被其他模式占用时拒绝混用
    tag_file = cache_dir / "mode.tag"
    if tag_file.is_file():
        tagged = tag_file.read_text(encoding="utf-8").strip()
        if tagged and tagged != mode:
            raise DevWorkflowError(
                f"缓存目录模式不匹配：{cache_dir} 属于 [{tagged}]，拒绝以 [{mode}] 使用；"
                f"如确需复用，先执行 dev_workflow clean --mode {tagged}。")
    tag_file.write_text(mode, encoding="utf-8")

    merged = _merged_defaults(ctx, board)
    defaults_file = cache_dir / "sdkconfig.merged.defaults"
    defaults_file.write_text(merged, encoding="utf-8")
    (cache_dir / "defaults.audit.txt").write_text(merged, encoding="utf-8")

    # production 显式拒绝诊断标记（DIAG=y / maintenance 遗留 markers）
    if mode == "production":
        hit = DIAG_MARKER_RE.search(merged)
        if hit:
            raise DevWorkflowError(
                f"production 模式拒绝诊断标记：合并 defaults 中出现 {hit.group(0)}。"
                f"诊断构建请显式使用 --mode diagnostic。")

    files = snapshot_files(ctx)
    source_sha = source_fingerprint(ctx)[0]
    gi = git_identity(ctx)
    bucket = _config_bucket(mode, board, merged, selected, ctx.config.get("target", "esp32c3"))
    prev = read_state(ctx, mode) or {}
    prev_bucket = prev.get("config_bucket")
    has_build_state = (cache_dir / "build.ninja").is_file() or (cache_dir / "CMakeCache.txt").is_file()
    prev_failed = prev.get("status") in (None, "FAIL", "TIMEOUT", "ERROR")
    full_rebuild = (prev_bucket != bucket) or (not has_build_state) or prev_failed
    reconfigure = full_rebuild or prev.get("git_head") != gi["head"]

    print(f"模式={mode} board={board} cache={cache_dir}")
    print(f"git head={gi['head']} tree={gi['dirty']} 源码输入文件数={len(files)}")
    print(f"工具链来源={toolchain_src} 选定 {len(selected)} 个推荐版本")
    if full_rebuild:
        print("决策：配置桶变化/首次/上次失败 → 清空旧产物全量重编")
    else:
        print("决策：配置桶未变 → 只同步变化源码，Ninja 增量重编")

    if dry_run:
        env = build_env(ctx, idf_path, py_exe, tools_root, selected)
        print("[dry-run] 进程 env（示例键）:")
        for k in ("IDF_PATH", "IDF_PYTHON_ENV_PATH"):
            print(f"  {k}={env.get(k)}")
        for name in sorted(selected):
            print(f"  tool {name} {selected[name]['version']} -> {selected[name]['bins'][0]}")
        print(f"[dry-run] 构建项目根=source-snapshot，缓存目录={cache_dir}")
        return EXIT_OK

    log_path = ctx.log_dir() / f"{mode}-{time.strftime('%Y%m%d-%H%M%S')}.log"
    write_state(ctx, mode, board=board, stage="lock", status="RUNNING",
                config_bucket=bucket, full_rebuild=full_rebuild,
                log_path=str(log_path), git_head=gi["head"],
                toolchain=toolchain_src)

    try:
        # 全量重编：清空旧 object/CMake 状态；source-snapshot 与 manifest 保留
        if full_rebuild:
            removed = wipe_cache_objects(cache_dir)
            for rel in ("sdkconfig", "sdkconfig.old"):
                (cache_dir / "source-snapshot" / rel).unlink(missing_ok=True)
            if removed:
                shown = ", ".join(sorted(removed)[:8])
                print(f"已清空 {len(removed)} 项旧产物 [{shown}{'...' if len(removed) > 8 else ''}]")

        # 同步隔离 source snapshot：变化文件 copyfile 新 mtime，未变文件绝不 touch
        snap, changed = sync_source_snapshot(ctx, mode, files)
        if changed:
            print(f"source-snapshot 更新 {len(changed)} 个文件（Ninja 将凭新 mtime 重编受影响 object）")
        elif not full_rebuild:
            print("source-snapshot 无变化")

        idf_py = idf_path / "tools" / "idf.py"
        base_argv = [
            str(py_exe), str(idf_py),
            "-B", str(cache_dir),
            "-C", str(snap),
            "-D", f"SDKCONFIG_DEFAULTS={defaults_file}",
            "-D", f"IDF_TARGET={ctx.config.get('target', 'esp32c3')}",
            "-D", f"BIKEPOWER_SOURCE_GIT_SHA={gi['head']}",
        ]
        env = build_env(ctx, idf_path, py_exe, tools_root, selected)

        if reconfigure:
            write_state(ctx, mode, stage="reconfigure", status="RUNNING")
            rc, dur, tail = run_subprocess(
                base_argv + ["reconfigure"], env, snap, ctx.timeout("doctor") + 60, log_path)
            if rc != 0:
                if tail == "TIMEOUT":
                    write_state(ctx, mode, stage="reconfigure", status="TIMEOUT", exit_code=rc,
                                duration_s=round(dur, 1))
                    raise DevWorkflowError(f"reconfigure 超时，日志: {log_path}", EXIT_TIMEOUT)
                write_state(ctx, mode, stage="reconfigure", status="FAIL", exit_code=rc,
                            duration_s=round(dur, 1))
                raise DevWorkflowError(f"reconfigure 失败 exit={rc}，日志: {log_path}\n{tail}",
                                       EXIT_BUILD_FAIL)

        write_state(ctx, mode, stage="build", status="RUNNING")
        rc, dur, tail = run_subprocess(
            base_argv + ["build"], env, snap, ctx.timeout("build"), log_path)
        if rc != 0:
            if tail == "TIMEOUT":
                write_state(ctx, mode, stage="build", status="TIMEOUT", exit_code=rc,
                            duration_s=round(dur, 1))
                raise DevWorkflowError(f"build 超时，日志: {log_path}", EXIT_TIMEOUT)
            write_state(ctx, mode, stage="build", status="FAIL", exit_code=rc,
                        duration_s=round(dur, 1))
            raise DevWorkflowError(f"build 失败 exit={rc}，日志: {log_path}\n{tail}", EXIT_BUILD_FAIL)

        hashes = artifact_hashes(cache_dir)
        config_hash = _hash_file(snap / "sdkconfig")
        if source_fingerprint(ctx)[0] != source_sha or git_identity(ctx) != gi:
            write_state(ctx, mode, stage="done", status="FAIL")
            raise DevWorkflowError("构建期间源码或 Git 身份变化，拒绝记录通过", EXIT_BUILD_FAIL)
        write_state(ctx, mode, stage="done", status="PASS", exit_code=0,
                    duration_s=round(dur, 1), log_path=str(log_path),
                    snapshot_changed=changed,
                    source_fingerprint=source_sha,
                    artifact_hashes=hashes, sdkconfig_hash=config_hash)
        print(f"[OK] build 完成 {dur:.0f}s  日志: {log_path}")
        print("提示：这只是本工具的构建退出码记录，不等于项目门禁 PASS；正式门禁以 quality_gate/设备验收为准。")
        return EXIT_OK
    except OSError as exc:
        write_state(ctx, mode, status="ERROR")
        raise DevWorkflowError(f"构建文件操作失败: {exc}", EXIT_BUILD_FAIL) from exc


# ---------------------------------------------------------------------------
# 子命令：test（任意命令；只记退出码，不冒称门禁）
# ---------------------------------------------------------------------------

def cmd_test(ctx: Context, mode: str, name: str, argv: List[str]) -> int:
    if mode not in MODES:
        raise DevWorkflowError(f"未知模式 {mode}，可选: {MODES}")
    if not argv:
        raise DevWorkflowError("test 子命令需要命令：dev_workflow test -- <cmd...>")
    cache_dir = ctx.mode_dir(mode)
    cache_dir.mkdir(parents=True, exist_ok=True)
    log_path = ctx.log_dir() / f"{mode}-test-{name}-{time.strftime('%Y%m%d-%H%M%S')}.log"

    lock = ModeLock(cache_dir / "build.lock")
    lock.acquire(ctx.timeout("lock_wait"))
    try:
        write_state(ctx, mode, stage=f"test:{name}", status="RUNNING",
                    log_path=str(log_path))
        idf_path, _ = detect_idf(ctx)
        py_exe, _ = detect_idf_python(ctx, idf_path)
        tools_root = detect_tools_root(ctx)
        env = build_env(ctx, idf_path, py_exe, tools_root)
        rc, dur, tail = run_subprocess(argv, env, ctx.repo, ctx.timeout("test"), log_path)
        if rc == -1 and tail == "TIMEOUT":
            write_state(ctx, mode, stage=f"test:{name}", status="TIMEOUT",
                        exit_code=rc, duration_s=round(dur, 1))
            raise DevWorkflowError(f"测试命令超时，日志: {log_path}", EXIT_TIMEOUT)
        status = "PASS" if rc == 0 else "FAIL"
        write_state(ctx, mode, stage=f"test:{name}", status=status,
                    exit_code=rc, duration_s=round(dur, 1))
        print(f"[{'OK' if rc==0 else 'FAIL'}] test {name} exit={rc} {dur:.0f}s 日志: {log_path}")
        print("提示：dev_workflow 只记录该命令退出码，绝不代表项目门禁或设备验收 PASS。")
        return EXIT_OK if rc == 0 else EXIT_TEST_FAIL
    finally:
        lock.release()


# ---------------------------------------------------------------------------
# 子命令：status / clean
# ---------------------------------------------------------------------------

def cmd_status(ctx: Context) -> int:
    any_state = False
    for mode in MODES:
        st = read_state(ctx, mode)
        print(f"[{mode}] cache={ctx.mode_dir(mode)}")
        if not st:
            print("  (尚无记录)")
            continue
        any_state = True
        print(f"  状态={st.get('status')} 阶段={st.get('stage')} exit={st.get('exit_code')} "
              f"fence={str(st.get('fence'))[:12]} 源码变化={st.get('source_changed')}")
        if st.get("log_path"):
            print(f"  日志: {st['log_path']}")
    if not any_state:
        print("(本工具尚未在 build/devcache 下产生任何运行记录)")
    return EXIT_OK


def cmd_clean(ctx: Context, mode: Optional[str]) -> int:
    """同模式持锁清理产物，保留锁文件以防等待者锁到不同 inode。"""
    if mode and mode not in MODES:
        raise DevWorkflowError(f"未知模式 {mode}，可选: {MODES}")
    locks = []
    try:
        for name in (mode,) if mode else MODES:
            lock = ModeLock(ctx.mode_dir(name) / "build.lock")
            lock.acquire(ctx.timeout("lock_wait"))
            locks.append(lock)
        for lock in locks:
            for entry in lock.lock_path.parent.iterdir():
                if entry == lock.lock_path:
                    continue
                if entry.is_dir() and not entry.is_symlink():
                    shutil.rmtree(entry)
                else:
                    entry.unlink()
            print(f"已清理本工具缓存，保留锁: {lock.lock_path.parent}")
        return EXIT_OK
    finally:
        for lock in reversed(locks):
            lock.release()


def cmd_release(ctx: Context, argv: List[str]) -> int:
    """委托给 release_build，保持发行流水线单一实现。"""
    sys.path.insert(0, str(Path(__file__).parent))
    import release_build  # noqa: PLC0415
    return release_build.main(argv, ctx=ctx)


# ---------------------------------------------------------------------------
# 入口
# ---------------------------------------------------------------------------

def build_parser() -> argparse.ArgumentParser:
    p = argparse.ArgumentParser(
        prog="dev_workflow",
        description="BikePower 本机开发验证统一入口（只动 build/devcache，不碰设备/生产源码）")
    p.add_argument("--config", help="机器配置 JSON 路径（默认读 ~/.bikepower/dev_workflow.json）")
    p.add_argument("--repo", help="仓库根（默认按脚本位置推断）")
    sub = p.add_subparsers(dest="cmd", required=True)

    sp = sub.add_parser("doctor", help="检查本机环境并报告")
    sp.add_argument("--json", action="store_true")

    sp = sub.add_parser("build", help="在模式缓存中增量构建")
    sp.add_argument("--mode", default="production", choices=MODES)
    sp.add_argument("--board", default="generic", choices=BOARDS)
    sp.add_argument("--dry-run", action="store_true")

    sp = sub.add_parser("test", help="运行任意测试命令并记录退出码（不代表门禁 PASS）")
    sp.add_argument("--mode", default="production", choices=MODES)
    sp.add_argument("--name", default="cmd")
    sp.add_argument("argv", nargs=argparse.REMAINDER)

    sub.add_parser("status", help="显示各模式状态")
    sp = sub.add_parser("clean", help="只清理 build/devcache 本工具缓存")
    sp.add_argument("--mode", choices=MODES)

    sp = sub.add_parser("release", help="一条命令生成发行产物（不 tag / 不发布）")
    sp.add_argument("--version", required=True, help="发行版本，如 4.3.3")
    sp.add_argument("--board", default="generic", choices=BOARDS)
    sp.add_argument("--release-root", help="产物根目录（默认 build/release）")
    sp.add_argument("--evidence", help="真机验收证据 JSON 源路径")
    sp.add_argument("--endurance", help="耐久报告 md 源路径")
    sp.add_argument("--test-summary", help="测试摘要 md")
    sp.add_argument("--notes", help="Release 说明 md")
    sp.add_argument("--skip-build", action="store_true")
    sp.add_argument("--allow-dirty-source", action="store_true")
    sp.add_argument("--candidate", action="store_true")
    sp.add_argument("--dry-run", action="store_true")

    sub.add_parser("version", help="打印版本")
    return p


def main(argv: Optional[Sequence[str]] = None) -> int:
    args = build_parser().parse_args(argv)
    repo = Path(args.repo).resolve() if args.repo else default_repo_root()
    config_path, config = load_config(args.config, repo)
    ctx = Context(repo=repo, config_path=config_path, config=config)

    if args.cmd == "doctor":
        return cmd_doctor(ctx, args.json)
    if args.cmd == "build":
        return cmd_build(ctx, args.mode, args.board, args.dry_run)
    if args.cmd == "test":
        rest = list(args.argv or [])
        if rest and rest[0] == "--":
            rest = rest[1:]
        return cmd_test(ctx, args.mode, args.name, rest)
    if args.cmd == "status":
        return cmd_status(ctx)
    if args.cmd == "clean":
        return cmd_clean(ctx, args.mode)
    if args.cmd == "release":
        fwd = ["--version", args.version, "--board", args.board]
        for flag, value in (("--release-root", args.release_root),
                            ("--evidence", args.evidence),
                            ("--endurance", args.endurance),
                            ("--test-summary", args.test_summary),
                            ("--notes", args.notes)):
            if value:
                fwd += [flag, value]
        for flag, on in (("--skip-build", args.skip_build),
                         ("--allow-dirty-source", args.allow_dirty_source),
                         ("--candidate", args.candidate),
                         ("--dry-run", args.dry_run)):
            if on:
                fwd.append(flag)
        return cmd_release(ctx, fwd)
    if args.cmd == "version":
        print(f"dev_workflow {TOOL_VERSION}")
        return EXIT_OK
    return EXIT_ERROR


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except DevWorkflowError as err:
        print(f"[dev_workflow] FAIL {err}", file=sys.stderr)
        raise SystemExit(err.code)
    except KeyboardInterrupt:
        print("[dev_workflow] 被用户中断", file=sys.stderr)
        raise SystemExit(130)
