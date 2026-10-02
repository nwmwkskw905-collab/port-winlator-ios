#!/usr/bin/env python3
"""openstep_plist.py — strict parser for the OpenStep/NeXTSTEP ASCII property list
dialect that Xcode uses for `project.pbxproj`.

PHASE_02_RECONSTRUCTED_POC. Written after the first real Apple CI run rejected a
generated project with:

    xcodebuild: error: Unable to read project 'WinlatorPhase02.xcodeproj'
    The project 'WinlatorPhase02' is damaged and cannot be opened due to a parse error
    NSCocoaErrorDomain Code=3840

That is the error Xcode reports when the pbxproj is not a valid old-style plist.
This module exists so the same class of defect is caught *without* a Mac, instead of
being discovered by the CI. It is a syntax checker, not a replacement for
`xcodebuild -list`: only the real parser is authoritative.

Grammar implemented (as accepted by CoreFoundation for old-style ASCII plists):

    value      := dict | array | data | string
    dict       := '{' ( string '=' value ';' )* '}'
    array      := '(' ( value ',' )* ')'
    data       := '<' [0-9A-Fa-f whitespace]* '>'
    string     := quoted_string | unquoted_string
    unquoted   := [A-Za-z0-9_$+/:.-]+
    quoted     := '"' ... '"'   with C escapes
    comments   := '//' to end of line, '/* ... */'

Anything else is a syntax error — in particular a raw '<' or '>' that is not a data
token (e.g. the unquoted `sourceTree = <group>;` that broke the first CI run).
"""

from __future__ import annotations

import re

__all__ = ["PlistSyntaxError", "parse", "parse_file"]

# Characters that may appear in an unquoted string, matching CoreFoundation.
UNQUOTED_CHARS = set(
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_$+/:.-"
)
HEX_CHARS = set("0123456789abcdefABCDEF")
C_ESCAPES = {"n": "\n", "r": "\r", "t": "\t", "a": "\a", "b": "\b", "f": "\f", "v": "\v"}


class PlistSyntaxError(Exception):
    """A structural error, reported with line/column so it maps to a real file."""

    def __init__(self, message: str, line: int, column: int, snippet: str = "", hint: str = ""):
        self.message = message
        self.line = line
        self.column = column
        self.snippet = snippet
        self.hint = hint
        text = f"line {line}, column {column}: {message}"
        if snippet:
            text += f"\n    {snippet}"
        if hint:
            text += f"\n    hint: {hint}"
        super().__init__(text)


class _Parser:
    def __init__(self, text: str):
        self.text = text
        self.length = len(text)
        self.pos = 0

    # ---- diagnostics -----------------------------------------------------
    def _line_column(self, pos: int) -> tuple[int, int]:
        line = self.text.count("\n", 0, pos) + 1
        line_start = self.text.rfind("\n", 0, pos) + 1
        return line, pos - line_start + 1

    def _snippet(self, pos: int) -> str:
        line_start = self.text.rfind("\n", 0, pos) + 1
        line_end = self.text.find("\n", pos)
        if line_end == -1:
            line_end = self.length
        return self.text[line_start:line_end].strip()[:120]

    def fail(self, message: str, pos: int | None = None, hint: str = "") -> None:
        where = self.pos if pos is None else pos
        line, column = self._line_column(where)
        raise PlistSyntaxError(message, line, column, self._snippet(where), hint)

    # ---- lexing helpers --------------------------------------------------
    def skip_whitespace_and_comments(self) -> None:
        while self.pos < self.length:
            char = self.text[self.pos]
            if char in " \t\r\n":
                self.pos += 1
                continue
            if char == "/" and self.pos + 1 < self.length:
                following = self.text[self.pos + 1]
                if following == "/":
                    end = self.text.find("\n", self.pos + 2)
                    self.pos = self.length if end == -1 else end + 1
                    continue
                if following == "*":
                    end = self.text.find("*/", self.pos + 2)
                    if end == -1:
                        self.fail("unterminated block comment '/*'")
                    self.pos = end + 2
                    continue
            return

    def peek(self) -> str:
        return self.text[self.pos] if self.pos < self.length else ""

    # ---- productions -----------------------------------------------------
    def parse_value(self):
        self.skip_whitespace_and_comments()
        char = self.peek()
        if char == "":
            self.fail("unexpected end of file where a value was expected")
        if char == "{":
            return self.parse_dict()
        if char == "(":
            return self.parse_array()
        if char == "<":
            return self.parse_data()
        if char == '"':
            return self.parse_quoted_string()
        return self.parse_unquoted_string()

    def parse_dict(self) -> dict:
        self.pos += 1  # '{'
        result: dict = {}
        while True:
            self.skip_whitespace_and_comments()
            char = self.peek()
            if char == "":
                self.fail("unexpected end of file: dictionary opened with '{' was never closed")
            if char == "}":
                self.pos += 1
                return result
            key_pos = self.pos
            key = self.parse_quoted_string() if char == '"' else self.parse_unquoted_string()
            self.skip_whitespace_and_comments()
            if self.peek() != "=":
                self.fail(
                    f"expected '=' after the key '{key}'",
                    hint="OpenStep plists write dictionary entries as: key = value;",
                )
            self.pos += 1  # '='
            value = self.parse_value()
            self.skip_whitespace_and_comments()
            if self.peek() != ";":
                self.fail(
                    f"expected ';' after the value of '{key}'",
                    hint="every dictionary entry ends with a semicolon",
                )
            self.pos += 1  # ';'
            result[key] = value
            if key in ("sourceTree", "isa") and value == b"":
                self.fail(f"'{key}' parsed as an empty data token", key_pos)

    def parse_array(self) -> list:
        self.pos += 1  # '('
        result: list = []
        while True:
            self.skip_whitespace_and_comments()
            char = self.peek()
            if char == "":
                self.fail("unexpected end of file: array opened with '(' was never closed")
            if char == ")":
                self.pos += 1
                return result
            result.append(self.parse_value())
            self.skip_whitespace_and_comments()
            char = self.peek()
            if char == ",":
                self.pos += 1
                continue
            if char == ")":
                self.pos += 1
                return result
            self.fail("expected ',' or ')' in array", hint="array elements are separated by commas")

    def parse_data(self) -> bytes:
        start = self.pos
        self.pos += 1  # '<'
        content_start = self.pos
        while self.pos < self.length and self.text[self.pos] != ">":
            char = self.text[self.pos]
            if char not in HEX_CHARS and char not in " \t\r\n":
                bad = char
                self.fail(
                    f"invalid character '{bad}' inside a '<...>' data token",
                    pos=self.pos,
                    hint=("'<...>' means a data (bytes) literal in OpenStep plists. "
                          "A string that merely *contains* angle brackets must be quoted, "
                          'e.g. sourceTree = "<group>";'),
                )
            self.pos += 1
        if self.pos >= self.length:
            self.fail("unterminated data token: '<' without '>'", pos=start)
        raw = self.text[content_start:self.pos]
        self.pos += 1  # '>'
        digits = re.sub(r"\s+", "", raw)
        try:
            return bytes.fromhex(digits)
        except ValueError:
            self.fail("data token is not valid hexadecimal", pos=start)

    def parse_quoted_string(self) -> str:
        start = self.pos
        self.pos += 1  # '"'
        out: list[str] = []
        while True:
            if self.pos >= self.length:
                self.fail("unterminated string", pos=start)
            char = self.text[self.pos]
            if char == '"':
                self.pos += 1
                return "".join(out)
            if char == "\\":
                self.pos += 1
                if self.pos >= self.length:
                    self.fail("unterminated escape sequence", pos=start)
                escape = self.text[self.pos]
                if escape in C_ESCAPES:
                    out.append(C_ESCAPES[escape])
                    self.pos += 1
                elif escape in "Uu":
                    digits = self.text[self.pos + 1 : self.pos + 5]
                    if len(digits) != 4 or any(d not in HEX_CHARS for d in digits):
                        self.fail(f"'\\{escape}' must be followed by four hex digits")
                    out.append(chr(int(digits, 16)))
                    self.pos += 5
                elif escape in "01234567":
                    digits = ""
                    while self.pos < self.length and self.text[self.pos] in "01234567" and len(digits) < 3:
                        digits += self.text[self.pos]
                        self.pos += 1
                    out.append(chr(int(digits, 8)))
                else:
                    out.append(escape)
                    self.pos += 1
                continue
            out.append(char)
            self.pos += 1

    def parse_unquoted_string(self) -> str:
        start = self.pos
        while self.pos < self.length and self.text[self.pos] in UNQUOTED_CHARS:
            self.pos += 1
        if self.pos == start:
            char = self.peek()
            hint = ""
            if char in "<>":
                hint = ('angle brackets are not allowed in unquoted strings; write "<group>" '
                        "(with quotes) instead of <group>")
            elif char == "@":
                hint = "the '@' prefix belongs to the newer XML/JSON formats, not to pbxproj"
            elif char == "{":
                hint = "a dictionary was expected here; check the surrounding punctuation"
            self.fail(f"unexpected character '{char}' where a value was expected", hint=hint)
        return self.text[start:self.pos]


def parse(text: str):
    """Parse a complete OpenStep plist and return the Python value.

    Raises PlistSyntaxError with line/column on any structural problem.
    """
    parser = _Parser(text)
    parser.skip_whitespace_and_comments()
    value = parser.parse_value()
    parser.skip_whitespace_and_comments()
    if parser.pos != parser.length:
        parser.fail("trailing content after the end of the top-level value")
    return value


def parse_file(path) -> object:
    with open(path, "r", encoding="utf-8") as handle:
        return parse(handle.read())


if __name__ == "__main__":
    import sys

    if len(sys.argv) != 2:
        print("usage: openstep_plist.py FILE", file=sys.stderr)
        raise SystemExit(2)
    try:
        parsed = parse_file(sys.argv[1])
    except PlistSyntaxError as error:
        print(f"PLIST_SYNTAX=FAIL\n{error}", file=sys.stderr)
        raise SystemExit(1)
    top = list(parsed) if isinstance(parsed, dict) else type(parsed).__name__
    objects = parsed.get("objects", {}) if isinstance(parsed, dict) else {}
    print(f"PLIST_SYNTAX=PASS keys={top} objects={len(objects)}")
