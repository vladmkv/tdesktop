#!/usr/bin/env python3

import argparse
import dataclasses
import pathlib
import re
import subprocess
import sys
from typing import Iterable


BEGIN_RE = re.compile(r"^\s*(?://|#|<!--)\s*TG_CHANGE_BEGIN:\s*([a-z0-9-]+)\s*(?:-->)?\s*$")
END_RE = re.compile(r"^\s*(?://|#|<!--)\s*TG_CHANGE_END:\s*([a-z0-9-]+)\s*(?:-->)?\s*$")
HUNK_RE = re.compile(r"^@@\s*-(\d+)(?:,(\d+))?\s*\+(\d+)(?:,(\d+))?\s*@@")


@dataclasses.dataclass
class Violation:
	file_path: str
	message: str
	hunk_header: str | None = None
	line: int | None = None
	marker_id: str | None = None


@dataclasses.dataclass
class MarkerBlock:
	marker_id: str
	begin_line: int
	end_line: int

	@property
	def content_start(self) -> int:
		return self.begin_line + 1

	@property
	def content_end(self) -> int:
		return self.end_line - 1


def run_git(repo: pathlib.Path, args: list[str]) -> str:
	result = subprocess.run(
		["git", "-C", str(repo), *args],
		capture_output=True,
		text=True,
		check=False,
	)
	if result.returncode != 0:
		raise RuntimeError((result.stderr or result.stdout).strip())
	return result.stdout


def is_tg_owned_new_file(path: str) -> bool:
	normalized = path.replace("\\", "/")
	if normalized == "TG_CHANGE_POLICY.md":
		return True
	return (
		normalized.startswith("Telegram/tg_cli/")
		or normalized.startswith("TG_PROBES/")
	)


def parse_markers(file_path: str, text: str) -> tuple[list[MarkerBlock], list[Violation], set[int]]:
	blocks: list[MarkerBlock] = []
	violations: list[Violation] = []
	marker_lines: set[int] = set()
	stack: list[tuple[str, int]] = []
	ids_in_file: set[str] = set()

	lines = text.splitlines()
	for line_number, line in enumerate(lines, start=1):
		begin = BEGIN_RE.match(line)
		if begin:
			marker_id = begin.group(1)
			marker_lines.add(line_number)
			if stack:
				violations.append(Violation(
					file_path=file_path,
					line=line_number,
					marker_id=stack[-1][0],
					message=f"nested marker block inside '{stack[-1][0]}'",
				))
			if marker_id in ids_in_file:
				violations.append(Violation(
					file_path=file_path,
					line=line_number,
					marker_id=marker_id,
					message=f"duplicate marker id '{marker_id}'",
				))
			ids_in_file.add(marker_id)
			stack.append((marker_id, line_number))
			continue

		end = END_RE.match(line)
		if end:
			marker_id = end.group(1)
			marker_lines.add(line_number)
			if not stack:
				violations.append(Violation(
					file_path=file_path,
					line=line_number,
					marker_id=marker_id,
					message=f"unmatched TG_CHANGE_END for '{marker_id}'",
				))
				continue
			begin_id, begin_line = stack.pop()
			if marker_id != begin_id:
				violations.append(Violation(
					file_path=file_path,
					line=line_number,
					marker_id=marker_id,
					message=(
						f"marker mismatch: begin '{begin_id}' at line {begin_line}, "
						f"end '{marker_id}' at line {line_number}"
					),
				))
				continue
			if line_number <= begin_line + 1:
				violations.append(Violation(
					file_path=file_path,
					line=begin_line,
					marker_id=marker_id,
					message=f"empty marker block '{marker_id}'",
				))
				continue
			blocks.append(MarkerBlock(marker_id=marker_id, begin_line=begin_line, end_line=line_number))

	for marker_id, begin_line in stack:
		violations.append(Violation(
			file_path=file_path,
			line=begin_line,
			marker_id=marker_id,
			message=f"unmatched TG_CHANGE_BEGIN for '{marker_id}'",
		))

	return blocks, violations, marker_lines


@dataclasses.dataclass
class Hunk:
	header: str
	new_start: int
	added_lines: list[int]
	has_additions: bool
	has_deletions: bool


def parse_hunks(diff_text: str) -> list[Hunk]:
	hunks: list[Hunk] = []
	lines = diff_text.splitlines()
	i = 0
	while i < len(lines):
		line = lines[i]
		match = HUNK_RE.match(line)
		if not match:
			i += 1
			continue

		new_line = int(match.group(3))
		i += 1
		added_lines: list[int] = []
		has_additions = False
		has_deletions = False

		while i < len(lines) and not lines[i].startswith("@@"):
			current = lines[i]
			if current.startswith("+++") or current.startswith("---"):
				i += 1
				continue
			if current.startswith("+"):
				has_additions = True
				added_lines.append(new_line)
				new_line += 1
			elif current.startswith("-"):
				has_deletions = True
			elif current.startswith(" "):
				new_line += 1
			i += 1

		hunks.append(Hunk(
			header=line,
			new_start=int(match.group(3)),
			added_lines=added_lines,
			has_additions=has_additions,
			has_deletions=has_deletions,
		))

	return hunks


def line_in_blocks(line_number: int, blocks: Iterable[MarkerBlock], marker_lines: set[int]) -> bool:
	if line_number in marker_lines:
		return True
	for block in blocks:
		if block.content_start <= line_number <= block.content_end:
			return True
	return False


def nearest_marker_id(line_number: int, blocks: list[MarkerBlock]) -> str | None:
	if not blocks:
		return None
	marker = min(
		blocks,
		key=lambda block: min(abs(line_number - block.begin_line), abs(line_number - block.end_line)),
	)
	return marker.marker_id


def deletion_hunk_overlaps_block(new_start: int, blocks: Iterable[MarkerBlock]) -> bool:
	for block in blocks:
		if block.begin_line - 1 <= new_start <= block.end_line + 1:
			return True
	return False


def existing_in_base(repo: pathlib.Path, base: str, file_path: str) -> bool:
	probe = subprocess.run(
		["git", "-C", str(repo), "cat-file", "-e", f"{base}:{file_path}"],
		capture_output=True,
		text=True,
		check=False,
	)
	return probe.returncode == 0


def changed_files(repo: pathlib.Path, base: str) -> list[tuple[str, str]]:
	output = run_git(repo, ["diff", "--name-status", "--find-renames", base, "--"])
	result: list[tuple[str, str]] = []
	for raw in output.splitlines():
		parts = raw.split("\t")
		if not parts:
			continue
		status = parts[0]
		if status.startswith("R") or status.startswith("C"):
			if len(parts) >= 3:
				result.append((status, parts[2]))
		elif len(parts) >= 2:
			result.append((status, parts[1]))
	return result


def validate(repo: pathlib.Path, base: str, verbose: bool) -> int:
	violations: list[Violation] = []
	files = changed_files(repo, base)

	for status, file_path in files:
		if status.startswith("A") and is_tg_owned_new_file(file_path):
			if verbose:
				print(f"SKIP new TG-owned file: {file_path}")
			continue

		if not existing_in_base(repo, base, file_path):
			if verbose:
				print(f"SKIP file absent in base: {file_path}")
			continue

		absolute = repo / file_path
		if not absolute.exists():
			violations.append(Violation(
				file_path=file_path,
				message="modified existing file is missing in working tree",
			))
			continue

		try:
			text = absolute.read_text(encoding="utf-8")
		except UnicodeDecodeError:
			violations.append(Violation(
				file_path=file_path,
				message="file is not UTF-8 text; checker only supports text files",
			))
			continue

		blocks, marker_violations, marker_lines = parse_markers(file_path, text)
		violations.extend(marker_violations)

		diff_text = run_git(repo, ["diff", "--unified=0", base, "--", file_path])
		hunks = parse_hunks(diff_text)

		for hunk in hunks:
			if hunk.has_additions:
				for line_number in hunk.added_lines:
					if line_in_blocks(line_number, blocks, marker_lines):
						continue
					violations.append(Violation(
						file_path=file_path,
						hunk_header=hunk.header,
						line=line_number,
						marker_id=nearest_marker_id(line_number, blocks),
						message="changed line is outside any TG_CHANGE fence",
					))
			elif hunk.has_deletions:
				if not deletion_hunk_overlaps_block(hunk.new_start, blocks):
					violations.append(Violation(
						file_path=file_path,
						hunk_header=hunk.header,
						line=hunk.new_start,
						marker_id=nearest_marker_id(hunk.new_start, blocks),
						message="deletion-only hunk does not overlap a fenced replacement block",
					))

	if violations:
		for issue in violations:
			marker = issue.marker_id if issue.marker_id else "none"
			hunk = f" {issue.hunk_header}" if issue.hunk_header else ""
			line = f" line {issue.line}" if issue.line else ""
			print(f"VIOLATION {issue.file_path}:{line}{hunk} near-marker={marker}: {issue.message}")
		print(f"FAIL: {len(violations)} violation(s) found.")
		return 1

	print("PASS: no fence violations found.")
	return 0


def run_self_test() -> int:
	valid = """
# TG_CHANGE_BEGIN: alpha
value = 1
# TG_CHANGE_END: alpha
""".strip("\n")
	blocks, violations, marker_lines = parse_markers("valid.py", valid)
	if violations:
		print("SELFTEST FAIL: valid marker parsing reported violations")
		return 1
	if len(blocks) != 1:
		print("SELFTEST FAIL: expected one valid marker block")
		return 1
	if not line_in_blocks(2, blocks, marker_lines):
		print("SELFTEST FAIL: expected content line to be inside fence")
		return 1

	invalid_nested = """
# TG_CHANGE_BEGIN: a
# TG_CHANGE_BEGIN: b
v = 1
# TG_CHANGE_END: b
# TG_CHANGE_END: a
""".strip("\n")
	_, nested_violations, _ = parse_markers("nested.py", invalid_nested)
	if not any("nested" in issue.message for issue in nested_violations):
		print("SELFTEST FAIL: expected nested marker violation")
		return 1

	invalid_unmatched = """
# TG_CHANGE_BEGIN: a
v = 1
""".strip("\n")
	_, unmatched_violations, _ = parse_markers("unmatched.py", invalid_unmatched)
	if not any("unmatched TG_CHANGE_BEGIN" in issue.message for issue in unmatched_violations):
		print("SELFTEST FAIL: expected unmatched begin violation")
		return 1

	hunk_text = """
@@ -10,1 +10,2 @@
-old
+new
+newer
""".strip("\n")
	hunks = parse_hunks(hunk_text)
	if len(hunks) != 1 or not hunks[0].has_additions or hunks[0].added_lines != [10, 11]:
		print("SELFTEST FAIL: hunk parsing failed")
		return 1

	print("SELFTEST PASS: valid and invalid fence cases behaved as expected.")
	return 0


def main() -> int:
	parser = argparse.ArgumentParser(description="Enforce TG_CHANGE fences in modified upstream files.")
	parser.add_argument("--repo", default=".", help="Repository root path.")
	parser.add_argument("--base", default="HEAD", help="Base revision for diff comparison.")
	parser.add_argument("--verbose", action="store_true", help="Print skipped files and extra details.")
	parser.add_argument("--self-test", action="store_true", help="Run built-in parser tests.")
	args = parser.parse_args()

	if args.self_test:
		return run_self_test()

	repo = pathlib.Path(args.repo).resolve()
	return validate(repo=repo, base=args.base, verbose=args.verbose)


if __name__ == "__main__":
	sys.exit(main())
