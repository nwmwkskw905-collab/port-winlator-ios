#!/usr/bin/env python3
"""audit_platform_composition.py — platform composition, symbol policy and backend selection.

PHASE_02_RECONSTRUCTED_POC. Written after Apple CI run #5:

    "___clear_cache", referenced from:
      _linux_icache_flush in linux_platform.o
    ld: symbol(s) not found for architecture arm64
    clang: error: linker command failed with exit code 1

What happened, in order:

  1. tools/generate_xcodeproj.py globs `RuntimeCore/src/*.c` into the app target, so
     linux_platform.c is compiled for iphoneos — deliberately, because both backends are
     always compiled (runtime_platform.h: "the harness must be able to report what a
     platform would do").
  2. linux_icache_flush() called __builtin___clear_cache() outside any platform guard.
  3. On ARM that builtin is not inlined: clang emits an external call to __clear_cache
     (Mach-O `___clear_cache`). Measured with clang 19 for every target used here:
     aarch64-linux-gnu, armv7-linux, armv7-apple-ios, arm64-apple-macos, arm64-apple-ios
     and arm64_32-apple-watchos -> `bl __clear_cache`; x86-64/i386 -> no instruction.
  4. The Xcode target hands every object to the linker directly (no static archive), so
     the object's undefined symbols must resolve even though nothing in the Apple product
     ever calls it — rt_platform_current() returns &rt_platform_darwin under __APPLE__.
     The iPhoneOS SDK provides no ___clear_cache, hence the link failure.

What this audit enforces (all of it configuration-based, not filename-based)

  1. TARGET COMPOSITION   Every file the Xcode target compiles, and every file the CMake
                          targets compile, is the file that should be there; a source on
                          disk that no target compiles is reported too.
  2. APPLE FORBIDDEN      No symbol/header from the "forbidden in the Apple configuration"
                          table may be REACHABLE in the iphoneos/macos configurations.
                          Reachability is decided by evaluating the real preprocessor
                          conditionals of the file against the target's macro universe, so
                          a correct `#if defined(__linux__)` guard removes the reference
                          from the Apple build and an unguarded one does not.
  3. LINUX FORBIDDEN      The same in the other direction: Apple-only APIs reachable in the
                          Linux configuration. This is where the backend boundary is
                          actually proven, instead of being assumed from the file name.
  4. BACKEND SELECTION    rt_platform_current() must select rt_platform_darwin under
                          __APPLE__ and rt_platform_linux under __linux__; each backend
                          struct must be declared in the header and defined exactly once,
                          in its own file.
  5. UNDECIDED CONDITIONS Reported, never guessed: a condition whose value this tool cannot
                          establish for a configuration is printed with file and line and
                          its region counts as REACHABLE (conservative: no hidden violation
                          can slip through a guess).

The compiler and the Apple linker remain the authority; this is a static pre-check that
fails in milliseconds, with file and line, instead of costing a CI cycle.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
sys.dont_write_bytecode = True
sys.path.insert(0, str(Path(__file__).resolve().parent))

from openstep_plist import PlistSyntaxError, parse  # noqa: E402

# --------------------------------------------------------------------------- configurations
#
# A configuration is the macro universe a compiler would see for that target, restricted to
# the names this project's conditionals actually use. The facts are documented platform
# facts (SDK / CI evidence), not wishes: MAP_JIT is defined by both Apple SDKs;
# TARGET_OS_* come from TargetConditionals.h; RT_APPLE_HAS_JIT_WRITE_PROTECT is the value
# runtime_platform.h derives for that target (Fix 03 semantics, checked by
# audit_apple_apis.py and by the unit tests).
#
# A name that is not listed for a configuration is "not defined" (C semantics). A name in
# this project's own namespace (RT_*, PHASE02_*) that is neither listed here nor #defined
# by the file being scanned is reported as UNDECIDED rather than assumed to be 0, because
# it would come from a header this audit does not parse.

LINUX_MACROS = {
    "__linux__": "1",
    "__unix__": "1",
    "__LP64__": "1",
    "_SC_LEVEL1_DCACHE_LINESIZE": "1",
}
APPLE_MACROS = {
    "__APPLE__": "1",
    "__MACH__": "1",
    "__LP64__": "1",
    "__unix__": "1",
    "TARGET_OS_MAC": "1",                 # 1 on macOS *and* iOS
    "MAP_JIT": "1",                       # defined by both Apple SDKs (runtime-gated)
}

APPLE_HEADERS = {
    "sys/random.h": False, "sys/auxv.h": False, "sys/epoll.h": False,
    "sys/eventfd.h": False, "sys/prctl.h": False, "sys/statvfs.h": True,
    "sys/event.h": True, "libkern/OSCacheControl.h": True, "TargetConditionals.h": True,
}
LINUX_HEADERS = {
    "sys/random.h": True, "sys/auxv.h": True, "sys/epoll.h": True,
    "sys/eventfd.h": True, "sys/prctl.h": True, "sys/statvfs.h": True,
    "sys/event.h": False, "libkern/OSCacheControl.h": False, "TargetConditionals.h": False,
}

CONFIGS = [
    {
        "name": "linux-x86_64", "family": "linux", "arch": "x86",
        "macros": dict(LINUX_MACROS, __x86_64__="1"), "headers": LINUX_HEADERS,
    },
    {
        "name": "linux-aarch64", "family": "linux", "arch": "arm",
        "macros": dict(LINUX_MACROS, __aarch64__="1"), "headers": LINUX_HEADERS,
    },
    {
        "name": "macos-arm64", "family": "apple", "arch": "arm",
        "macros": dict(APPLE_MACROS, __aarch64__="1", TARGET_OS_OSX="1",
                       TARGET_OS_IPHONE="0", TARGET_OS_SIMULATOR="0",
                       RT_APPLE_HAS_JIT_WRITE_PROTECT="1"), "headers": APPLE_HEADERS,
    },
    {
        "name": "iphoneos-arm64", "family": "apple", "arch": "arm",
        "macros": dict(APPLE_MACROS, __aarch64__="1", TARGET_OS_OSX="0",
                       TARGET_OS_IPHONE="1", TARGET_OS_SIMULATOR="0",
                       RT_APPLE_HAS_JIT_WRITE_PROTECT="0"), "headers": APPLE_HEADERS,
    },
    {
        "name": "iphonesimulator-x86_64", "family": "apple", "arch": "x86",
        "macros": dict(APPLE_MACROS, __x86_64__="1", TARGET_OS_OSX="0",
                       TARGET_OS_IPHONE="1", TARGET_OS_SIMULATOR="1",
                       RT_APPLE_HAS_JIT_WRITE_PROTECT="0"), "headers": APPLE_HEADERS,
    },
]

# Names whose meaning this audit knows: platform macros, compiler built-ins and the
# normalised Apple target macros (whose value is the configuration's business, not a
# per-file fact).
KNOWN_UNIVERSE = set()
for _config in CONFIGS:
    KNOWN_UNIVERSE |= set(_config["macros"])
KNOWN_UNIVERSE |= {
    "__has_include", "__cplusplus", "__GNUC__", "__clang__",
    "__i386__", "__arm__", "__x86_64__", "__aarch64__",
    "TARGET_OS_OSX", "TARGET_OS_IPHONE", "TARGET_OS_SIMULATOR",
    "RT_APPLE_TARGET", "RT_APPLE_TARGET_NONE", "RT_APPLE_TARGET_MACOS",
    "RT_APPLE_TARGET_IPHONE_DEVICE", "RT_APPLE_TARGET_IPHONE_SIMULATOR",
    "_SC_LEVEL1_DCACHE_LINESIZE", "MAP_JIT",
}
# Platform tokens printed in the per-file classification (content, not file name).
PLATFORM_TOKENS = ("__APPLE__", "__linux__", "__aarch64__", "__x86_64__", "__i386__",
                   "__arm__", "TARGET_OS_OSX", "TARGET_OS_IPHONE", "TARGET_OS_SIMULATOR",
                   "TARGET_OS_MAC", "MAP_JIT", "RT_APPLE_HAS_JIT_WRITE_PROTECT")

# Macros the build system *may* define. None of this project's targets pass them, so every
# configuration sees the header's own default (runtime_cpu_abi.h defines it as 0).
GLOBAL_MACROS = {
    "RT_X18_RESERVED_BUILD": "0",
}

# Compiler built-ins that are always available in the toolchains this project uses
# (clang, and gcc for the host build). __cplusplus stays out: the C translation units are
# compiled without it, which is exactly what the headers' extern "C" guards test.
BUILTIN_MACROS = {
    "__has_include": "1",
    "__GNUC__": "1",
    "__clang__": "1",
}

PROJECT_MACRO = re.compile(r"^(RT_|PHASE02_)")
INCLUDE_GUARD = re.compile(r"_H$")

# ------------------------------------------------------------------------ forbidden symbols

# Scope: "arm" = forbidden only where the compiler turns the construct into a call to a
# symbol the Apple SDK does not provide; None = forbidden on every Apple target.
APPLE_FORBIDDEN = {
    "__builtin___clear_cache":
        ("arm",
         "on every ARM target clang lowers it to an external call to __clear_cache, and "
         "the iPhoneOS SDK provides no such symbol (CI run #5 link failure); Apple cache "
         "maintenance goes through sys_icache_invalidate() in darwin_platform.c. On x86 it "
         "expands to no instruction, so the x86 branches of darwin_platform.c are legal"),
    "__clear_cache":
        ("arm",
         "the symbol the builtin lowers to on ARM; only a Linux toolchain's compiler "
         "runtime provides it"),
    "getrandom":
        (None, "glibc; <sys/random.h> is not in the iphoneos SDK (CI run #2) — Apple uses "
               "arc4random_buf(3)"),
    "getauxval": (None, "glibc; Apple has no auxiliary vector"),
    "epoll_create": (None, "<sys/epoll.h> is Linux-only — Darwin multiplexes with kqueue/kevent"),
    "epoll_create1": (None, "<sys/epoll.h> is Linux-only"),
    "epoll_ctl": (None, "<sys/epoll.h> is Linux-only"),
    "epoll_wait": (None, "<sys/epoll.h> is Linux-only"),
    "eventfd": (None, "<sys/eventfd.h> is Linux-only"),
    "signalfd": (None, "<sys/signalfd.h> is Linux-only"),
    "prctl": (None, "<sys/prctl.h> is Linux-only"),
    "memfd_create": (None, "Linux-only"),
    "sched_setaffinity":
        (None, "Linux-only (Apple exposes affinity only through the Mach scheduler)"),
    "pthread_setname_np":
        (None, "Linux signature; Apple's takes (pthread_t, const char *) — different ABI"),
}
APPLE_FORBIDDEN_HEADERS = {
    "sys/random.h": "Linux/glibc (observed missing in CI run #2)",
    "sys/auxv.h": "Linux/glibc",
    "sys/epoll.h": "Linux only",
    "sys/eventfd.h": "Linux only",
    "sys/prctl.h": "Linux only",
    "sys/sendfile.h": "Linux only",
    "asm/hwcap.h": "Linux/AArch64 only",
    "linux/": "Linux only (whole subtree)",
}

LINUX_FORBIDDEN = {
    "sys_icache_invalidate":
        (None, "libkern/OSCacheControl.h — Apple-only; the Linux backend uses the compiler "
               "builtin"),
    "arc4random_buf":
        (None, "Apple's CSPRNG — the Linux backend uses getrandom(2), with /dev/urandom as "
               "the ENOSYS-only fallback"),
    "pthread_jit_write_protect_np":
        (None, "macOS-only API (CI run #3); must never be reachable on Linux or iOS"),
    "kqueue": (None, "sys/event.h — Darwin-only; the Linux backend multiplexes with epoll"),
    "kevent": (None, "sys/event.h — Darwin-only; the Linux backend multiplexes with epoll"),
    "MAP_JIT": (None, "Apple-only mapping flag — the Linux backend must not claim it"),
    "mach_absolute_time": (None, "Mach-only"),
}
LINUX_FORBIDDEN_HEADERS = {
    "libkern/OSCacheControl.h": "Apple-only",
    "TargetConditionals.h": "Apple-only",
    "sys/event.h": "Darwin-only",
}

# The cache-maintenance surface CI run #5 broke on: every call site is listed per
# configuration, so a future change to it is visible in the evidence.
ICACHE_SYMBOLS = ("icache_flush", "sys_icache_invalidate", "__builtin___clear_cache")

# ------------------------------------------------------------------------------ evaluation

CONDITION_PATTERN = re.compile(r"^\s*#\s*(if|ifdef|ifndef|elif)\b(.*)$")
DIRECTIVE_PATTERN = re.compile(r"^\s*#\s*(\w+)\b(.*)$")
INCLUDE_PATTERN = re.compile(r'^\s*#\s*include\s+[<"]([^>"]+)[>"]')
DEFINE_PATTERN = re.compile(r"^\s*#\s*define\s+(\w+)(.*)$")
UNDEF_PATTERN = re.compile(r"^\s*#\s*undef\s+(\w+)")
_TOKEN = re.compile(
    r"defined|__has_include|[A-Za-z_]\w*|0[xX][0-9a-fA-F]+|\d+|&&|\|\||[!()]|==|!=|[<>]"
)


class Undecided:
    """Sentinel for a condition this audit refuses to guess."""

    def __repr__(self) -> str:  # pragma: no cover - debugging aid
        return "<undecided>"


UNDECIDED = Undecided()


class Stripper:
    """Removes comments and string/char literals across *lines*.

    Multi-line comments matter here: both platform backends explain in their header
    comments which symbols they do NOT use, and naming a symbol in prose is not using it.
    """

    def __init__(self) -> None:
        self.in_block_comment = False

    def strip(self, line: str) -> str:
        out: list[str] = []
        index = 0
        length = len(line)
        while index < length:
            if self.in_block_comment:
                end = line.find("*/", index)
                if end == -1:
                    return "".join(out)
                self.in_block_comment = False
                index = end + 2
                continue
            char = line[index]
            if char == "/" and index + 1 < length and line[index + 1] == "/":
                break
            if char == "/" and index + 1 < length and line[index + 1] == "*":
                self.in_block_comment = True
                index += 2
                continue
            if char in "\"'":
                quote = char
                index += 1
                while index < length:
                    if line[index] == "\\":
                        index += 2
                        continue
                    if line[index] == quote:
                        index += 1
                        break
                    index += 1
                out.append(" ")
                continue
            out.append(char)
            index += 1
        return "".join(out)


class Evaluator:
    """Evaluates a preprocessor condition for one configuration.

    Returns True, False or UNDECIDED. Values come from the configuration's macro universe,
    from macros #define/#undef'd by the file itself along the active path, and from the
    configuration's documented header facts for __has_include().
    """

    def __init__(self, config: dict, defined_in_file: dict, undecided_log: list):
        self.config = config
        self.defined_in_file = defined_in_file
        self.undecided_log = undecided_log
        self.tokens: list[str] = []
        self.position = 0
        self.has_include: dict[str, str] = {}

    # -- macros ---------------------------------------------------------------------
    def macro(self, name: str):
        if name in GLOBAL_MACROS:
            return GLOBAL_MACROS[name]
        if name in BUILTIN_MACROS:
            return BUILTIN_MACROS[name]
        if name in self.defined_in_file:
            value = self.defined_in_file[name]
            return 1 if value == "" else value
        if name in self.config["macros"]:
            return self.config["macros"][name]
        if name in KNOWN_UNIVERSE:
            return "0"
        if PROJECT_MACRO.match(name) and not INCLUDE_GUARD.search(name):
            return UNDECIDED
        return "0"

    def is_defined(self, name: str):
        if name in GLOBAL_MACROS or name in BUILTIN_MACROS:
            return True
        if name in self.defined_in_file or name in self.config["macros"]:
            return True
        if name in KNOWN_UNIVERSE:
            return False
        if PROJECT_MACRO.match(name) and not INCLUDE_GUARD.search(name):
            return UNDECIDED
        return False

    # -- expression parser ----------------------------------------------------------
    def evaluate(self, expression: str):
        # __has_include() carries a header name, which the token grammar below would mangle
        # ("libkern/OSCacheControl.h" would lose its punctuation); it is lifted out first.
        def lift(match: "re.Match") -> str:
            header = match.group(1)[1:-1]
            token = f"__HASINCLUDE_{len(self.has_include)}"
            self.has_include[token] = header
            return token

        expression = re.sub('__has_include[ \t]*[(][ \t]*([<"][^>"]*[">])[ \t]*[)]', lift, expression)
        self.tokens = _TOKEN.findall(expression)
        self.position = 0
        if not self.tokens:
            self.undecided_log.append(expression.strip())
            return UNDECIDED
        value = self.parse_or()
        if self.position != len(self.tokens):
            self.undecided_log.append(expression.strip())
            return UNDECIDED
        return value

    def peek(self):
        return self.tokens[self.position] if self.position < len(self.tokens) else None

    def take(self):
        token = self.peek()
        self.position += 1
        return token

    def parse_or(self):
        value = self.parse_and()
        while self.peek() == "||":
            self.take()
            value = _or(value, self.parse_and())
        return value

    def parse_and(self):
        value = self.parse_unary()
        while self.peek() == "&&":
            self.take()
            value = _and(value, self.parse_unary())
        return value

    def parse_unary(self):
        if self.peek() == "!":
            self.take()
            return _not(self.parse_unary())
        return self.parse_primary()

    def parse_primary(self):
        token = self.take()
        if token == "(":
            value = self.parse_or()
            if self.peek() == ")":
                self.take()
            return value
        if token == "defined":
            parenthesised = self.peek() == "("
            if parenthesised:
                self.take()
            name = self.take() or ""
            if parenthesised and self.peek() == ")":
                self.take()
            result = self.is_defined(name)
            if result is UNDECIDED:
                self.undecided_log.append(f"defined({name})")
            return result
        if token in self.has_include:
            header = self.has_include[token]
            facts = self.config["headers"]
            if header in facts:
                return facts[header]
            self.undecided_log.append(f"__has_include(<{header}>)")
            return UNDECIDED
        if token is None:
            return UNDECIDED
        if re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+", token):
            return int(token, 0)
        if token.startswith("__HASINCLUDE_"):   # pragma: no cover - defensive
            self.undecided_log.append(token)
            return UNDECIDED
        # identifier, possibly with == / !=
        operator = self.peek()
        if operator in ("==", "!="):
            self.take()
            other = self.take() or "0"
            right = other if re.fullmatch(r"\d+", other) else self.macro(other)
            left = self.macro(token)
            if left is UNDECIDED or right is UNDECIDED:
                return UNDECIDED
            equal = int(left, 0) == int(right, 0)
            return equal if operator == "==" else not equal
        return truth(self.macro(token))


def truth(value):
    """C semantics: an integer expression is true when it is non-zero."""
    if value is UNDECIDED:
        return UNDECIDED
    return int(value, 0) != 0


def _and(left, right):
    if left is False or right is False:
        return False
    if left is True and right is True:
        return True
    return UNDECIDED


def _or(left, right):
    if left is True or right is True:
        return True
    if left is False and right is False:
        return False
    return UNDECIDED


def _not(value):
    if value is True:
        return False
    if value is False:
        return True
    return UNDECIDED


def _active(stack: list):
    """True (compiled), False (not compiled) or None (undecided: treat as reachable)."""
    for frame in stack:
        value = frame["current"]
        if value is False:
            return False
        if value is UNDECIDED:
            return None
    return True


def regions(path: Path, config: dict):
    """Yield (line_number, active, stripped_text) for every line of the file.

    active is True when the line is compiled for `config`, False when it is not, None when
    the audit cannot decide (callers treat None as reachable)."""
    macros: dict[str, str] = {}
    undecided: list[str] = []
    stack: list[dict] = []
    stripper = Stripper()
    for number, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        code = stripper.strip(raw)
        condition = CONDITION_PATTERN.match(code)
        if condition is not None:
            keyword, expression = condition.group(1), condition.group(2)
            if keyword in ("ifdef", "ifndef"):
                name = expression.strip()
                value = Evaluator(config, macros, undecided).is_defined(name)
                if value is UNDECIDED:
                    undecided.append(f"defined({name})")
                if keyword == "ifndef":
                    value = _not(value)
            elif keyword == "elif":
                value = Evaluator(config, macros, undecided).evaluate(expression)
            else:
                value = Evaluator(config, macros, undecided).evaluate(expression)
            if keyword == "elif" and stack:
                frame = stack[-1]
                value = False if frame["taken"] else value
                frame["current"] = value
                frame["taken"] = frame["taken"] or value is True
            else:
                stack.append({"taken": value is True, "current": value})
            if value is UNDECIDED and expression.strip() not in undecided:
                undecided.append(expression.strip())
            yield number, _active(stack), code
            continue

        directive = DIRECTIVE_PATTERN.match(code)
        if directive is not None:
            keyword = directive.group(1)
            if keyword == "else":
                if stack:
                    frame = stack[-1]
                    frame["current"] = _not(frame["current"]) if not frame["taken"] else False
                    frame["taken"] = True
                yield number, _active(stack), code
                continue
            if keyword == "endif":
                if stack:
                    stack.pop()
                yield number, _active(stack), code
                continue
            current = _active(stack)
            if current is True:
                definition = DEFINE_PATTERN.match(code)
                if definition is not None:
                    name, rest = definition.group(1), definition.group(2).strip()
                    macros[name] = rest.split()[0] if rest else ""
                    if rest and not re.fullmatch(r"0[xX][0-9a-fA-F]+|\d+|\w+", rest):
                        undecided.append(f"#define {name} {rest}")
                elif UNDEF_PATTERN.match(code) is not None:
                    macros.pop(UNDEF_PATTERN.match(code).group(1), None)
            yield number, current, code
            continue

        yield number, _active(stack), code


# ------------------------------------------------------------------------------- scanning

def scan_source(path: Path, config: dict, symbols: dict, headers: dict):
    """Symbols, headers and undecided lines REACHABLE for `config` (None counts as
    reachable)."""
    found_symbols: list[tuple[int, str]] = []
    found_headers: list[tuple[int, str]] = []
    undecided_lines: list[tuple[int, str]] = []
    for number, active, code in regions(path, config):
        if active is False:
            continue
        if active is None:
            undecided_lines.append((number, code.strip()))
        include = INCLUDE_PATTERN.match(code)
        if include is not None:
            header = include.group(1)
            for forbidden, why in headers.items():
                if header == forbidden or header.startswith(forbidden):
                    found_headers.append((number, f"{header} ({why})"))
            continue
        if code.lstrip().startswith("#"):
            continue
        for symbol in symbols:
            if re.search(r"\b" + re.escape(symbol) + r"\b", code) is not None:
                found_symbols.append((number, symbol))
    return found_symbols, found_headers, undecided_lines


def platform_tokens(path: Path) -> list[str]:
    """Which platform conditionals the file actually contains (content, not file name)."""
    stripper = Stripper()
    tokens: list[str] = []
    for raw in path.read_text(encoding="utf-8").splitlines():
        condition = CONDITION_PATTERN.match(stripper.strip(raw))
        if condition is None:
            continue
        for token in PLATFORM_TOKENS:
            if re.search(r"\b" + re.escape(token) + r"\b", condition.group(2)) \
                    and token not in tokens:
                tokens.append(token)
    return tokens


def has_active_code(path: Path, config: dict) -> bool:
    for _number, active, code in regions(path, config):
        if active is not False and code.strip() and not code.lstrip().startswith("#"):
            return True
    return False


# --------------------------------------------------------------- build-system composition

def xcode_target_sources() -> tuple[list[str], str]:
    """Read the real composition from the generated project (the authority), not from a
    name pattern."""
    project = ROOT / "WinlatorPhase02.xcodeproj" / "project.pbxproj"
    if not project.is_file():
        return [], "project.pbxproj missing (run tools/generate_xcodeproj.py)"
    try:
        parsed = parse(project.read_text(encoding="utf-8"))
    except PlistSyntaxError as error:
        return [], f"project.pbxproj is not a valid OpenStep plist: {error}"
    objects = parsed.get("objects", {})
    refs = {key: value for key, value in objects.items()
            if value.get("isa") == "PBXFileReference"}
    build_files = {key: value.get("fileRef") for key, value in objects.items()
                   if value.get("isa") == "PBXBuildFile"}
    phases = [value for value in objects.values()
              if value.get("isa") == "PBXSourcesBuildPhase"]
    if len(phases) != 1:
        return [], f"expected exactly one PBXSourcesBuildPhase, found {len(phases)}"
    names = []
    for entry in phases[0].get("files", []):
        reference = refs.get(build_files.get(entry, ""))
        if reference is None:
            return [], f"build file {entry} does not resolve to a file reference"
        names.append(Path(reference.get("path", "")).name)
    return sorted(names), ""


def cmake_target_sources() -> tuple[list[str], list[str]]:
    """Library and executable sources of the CMake build (host + AArch64 Linux)."""
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    library: list[str] = []
    executables: list[str] = []
    for match in re.finditer(r"add_(library|executable)\((\w+)([^)]*)\)", text, re.S):
        kind, body = match.group(1), match.group(3)
        names = [Path(p).name for p in re.findall(r"[\w/\.\-]+\.c(?:pp)?", body)]
        (library if kind == "library" else executables).extend(names)
    return sorted(library), sorted(executables)


def backend_selection(config: dict) -> list[tuple[int, str]]:
    """Which backend rt_platform_current() selects, decided by the reachability engine."""
    path = ROOT / "RuntimeCore" / "src" / "runtime_memory.c"
    selected: list[tuple[int, str]] = []
    for number, active, code in regions(path, config):
        if active is False:
            continue
        match = re.search(r"return\s+&(rt_platform_\w+)\s*;", code)
        if match is not None:
            selected.append((number, match.group(1)))
    return selected


# ----------------------------------------------------------------------------------- main

def main() -> int:
    print("== platform composition audit (configuration-based, not filename-based) ==")
    print(f"configurations : {', '.join(config['name'] for config in CONFIGS)}")
    print(f"project root   : {ROOT}")
    print()

    problems = 0
    undecided_report: list[tuple[str, int, str]] = []

    # ---- 1. target composition -------------------------------------------------
    core_sources = sorted(p.name for p in ROOT.glob("RuntimeCore/src/*.c"))
    diag_sources = sorted(p.name for p in ROOT.glob("Diagnostics/src/*.c"))
    objc_sources = sorted(p.name for p in ROOT.glob("RuntimePoC/*.m"))
    swift_sources = sorted(p.name for p in ROOT.glob("RuntimePoC/*.swift"))
    cli_sources = sorted(p.name for p in ROOT.glob("RuntimePoC/main.c"))
    test_sources = sorted(p.name for p in ROOT.glob("Tests/test_runtime_core.c"))

    apple_sources, apple_error = xcode_target_sources()
    cmake_library, cmake_executables = cmake_target_sources()

    expected_apple = sorted(core_sources + diag_sources + objc_sources + swift_sources)
    expected_library = sorted(core_sources + diag_sources)
    expected_executables = sorted(cli_sources + test_sources)

    findings: list[str] = []
    if apple_error:
        findings.append(apple_error)
    for name in expected_apple:
        if name not in apple_sources:
            findings.append(f"{name} is on disk but not compiled by the Xcode target")
    for name in apple_sources:
        if name not in expected_apple:
            findings.append(f"{name} is compiled by the Xcode target but is not a "
                            f"RuntimeCore/Diagnostics/RuntimePoC source")
    for name in expected_library:
        if name not in cmake_library:
            findings.append(f"{name} is missing from the CMake library")
    for name in cmake_library:
        if name not in expected_library:
            findings.append(f"{name} is in the CMake library but is not a "
                            f"RuntimeCore/Diagnostics source")
    for name in expected_executables:
        if name not in cmake_executables:
            findings.append(f"{name} is not an executable of the CMake build")
    for name in set(test_sources) | {"main.c"}:
        if name in apple_sources:
            findings.append(f"{name} must not be part of the Apple app target")
    problems += len(findings)
    for finding in findings:
        print(f"TARGET_COMPOSITION: {finding}")
    print(f"TARGET_COMPOSITION={len(findings)} "
          f"({len(apple_sources)} files in the Xcode target, {len(cmake_library)} in the "
          f"CMake library, {len(cmake_executables)} executables)")

    # ---- classification by content --------------------------------------------
    print()
    compiled_by: dict[str, set[str]] = {}
    for name in apple_sources:
        compiled_by.setdefault(name, set()).add("apple")
    for name in cmake_library + cmake_executables:
        compiled_by.setdefault(name, set()).add("linux")

    print("what each source really is (content-based, not filename-based):")
    for name in sorted(compiled_by):
        candidates = [p for p in ROOT.rglob(name) if p.is_file()]
        if not candidates:
            continue
        path = candidates[0]
        families = compiled_by[name]
        apple_active = "apple" in families and any(
            has_active_code(path, config) for config in CONFIGS if config["family"] == "apple")
        linux_active = "linux" in families and any(
            has_active_code(path, config) for config in CONFIGS if config["family"] == "linux")
        tokens = platform_tokens(path)
        verdict = []
        if "apple" in families:
            verdict.append(f"apple={'yes' if apple_active else 'NO '}")
        if "linux" in families:
            verdict.append(f"linux={'yes' if linux_active else 'NO '}")
        print(f"  {name:<26} {' '.join(verdict):<20} "
              f"platform conditionals: {', '.join(tokens) if tokens else '(none)'}")

    # ---- 2./3. symbol and header policy per configuration ----------------------
    print()
    apple_files = ([p for p in sorted(ROOT.glob("RuntimeCore/src/*.c"))
                    + sorted(ROOT.glob("Diagnostics/src/*.c"))
                    + sorted(ROOT.glob("RuntimePoC/*.m"))]
                   + sorted(ROOT.glob("RuntimeCore/include/*.h"))
                   + sorted(ROOT.glob("Diagnostics/include/*.h"))
                   + sorted(ROOT.glob("RuntimePoC/*.h")))
    linux_files = ([p for p in sorted(ROOT.glob("RuntimeCore/src/*.c"))
                    + sorted(ROOT.glob("Diagnostics/src/*.c"))
                    + sorted(ROOT.glob("RuntimePoC/main.c"))]
                   + sorted(ROOT.glob("RuntimeCore/include/*.h"))
                   + sorted(ROOT.glob("Diagnostics/include/*.h")))

    for family, files, symbols, headers in (
        ("apple", apple_files, APPLE_FORBIDDEN, APPLE_FORBIDDEN_HEADERS),
        ("linux", linux_files, LINUX_FORBIDDEN, LINUX_FORBIDDEN_HEADERS),
    ):
        violations: list[str] = []
        for config in [c for c in CONFIGS if c["family"] == family]:
            scoped = {name: reason for name, (scope, reason) in symbols.items()
                      if scope is None or scope == config["arch"]}
            for path in files:
                hits, header_hits, undecided = scan_source(path, config, scoped, headers)
                for number, symbol in hits:
                    violations.append(f"{path.relative_to(ROOT)}:{number} "
                                      f"[{config['name']}] {symbol} — {scoped[symbol]}")
                for number, header in header_hits:
                    violations.append(f"{path.relative_to(ROOT)}:{number} "
                                      f"[{config['name']}] #include <{header}>")
                for number, text in undecided:
                    undecided_report.append((str(path.relative_to(ROOT)), number,
                                             f"[{config['name']}] {text}"))
        key = "APPLE_FORBIDDEN_SYMBOLS" if family == "apple" else "LINUX_FORBIDDEN_SYMBOLS"
        problems += len(violations)
        for violation in violations:
            print(f"{key}: {violation}")
        print(f"{key}={len(violations)} " + ("(no forbidden reference is reachable in this "
              "configuration)" if not violations else "(REACHABLE — must be guarded)"))

    # ---- cache maintenance, per configuration ---------------------------------
    print()
    print("cache maintenance reached by each configuration:")
    for path in sorted(ROOT.glob("RuntimeCore/src/*platform.c")) \
            + sorted(ROOT.glob("RuntimeCore/src/runtime_jit.c")):
        for config in CONFIGS:
            hits = []
            for number, active, code in regions(path, config):
                if active is False:
                    continue
                for symbol in ICACHE_SYMBOLS:
                    if re.search(r"\b" + re.escape(symbol) + r"\b", code) is not None:
                        hits.append(f"{number}:{symbol}" + ("" if active else " (undecided)"))
            if hits:
                print(f"  {path.name:<20} [{config['name']:<24}] {', '.join(sorted(set(hits)))}")

    # ---- 4. backend selection --------------------------------------------------
    print()
    selection_findings: list[str] = []
    for config in CONFIGS:
        names = {name for _number, name in backend_selection(config)}
        expected = "rt_platform_darwin" if config["family"] == "apple" else "rt_platform_linux"
        if names != {expected}:
            selection_findings.append(f"{config['name']}: rt_platform_current() selects "
                                      f"{sorted(names)} instead of {expected}")
    header = (ROOT / "RuntimeCore" / "include" / "runtime_platform.h").read_text(encoding="utf-8")
    for backend, owner in (("rt_platform_linux", "linux_platform.c"),
                           ("rt_platform_darwin", "darwin_platform.c")):
        definitions = []
        for path in sorted(ROOT.glob("RuntimeCore/src/*.c")):
            for number, _active_value, code in regions(path, CONFIGS[0]):
                if re.search(r"const\s+rt_platform_t\s+" + backend + r"\s*=", code):
                    definitions.append(f"{path.name}:{number}")
        if len(definitions) != 1 or not definitions[0].startswith(owner + ":"):
            selection_findings.append(f"{backend} must be defined exactly once and in {owner}; "
                                      f"found {definitions}")
        if f"extern const rt_platform_t {backend};" not in header:
            selection_findings.append(f"{backend} is not declared in runtime_platform.h")
    problems += len(selection_findings)
    for finding in selection_findings:
        print(f"BACKEND_SELECTION: {finding}")
    print(f"BACKEND_SELECTION={'OK' if not selection_findings else 'FAILED'} "
          f"(Apple -> rt_platform_darwin, Linux -> rt_platform_linux, both declared, "
          f"no runtime picking)")

    # ---- 5. undecided conditions ----------------------------------------------
    print()
    if undecided_report:
        print("conditions this audit could not decide (region treated as reachable):")
        for path, number, text in undecided_report:
            print(f"  {path}:{number} {text}")
    print(f"UNDECIDED_CONDITIONS={len(undecided_report)} "
          f"({'every condition was decided for every configuration' if not undecided_report else 'add the fact to CONFIGS instead of guessing'})")

    print()
    print(f"PLATFORM_COMPOSITION_AUDIT={problems}")
    print("note: static pre-check — the Apple compiler and linker remain the authority for "
          "the iphoneos build.")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
