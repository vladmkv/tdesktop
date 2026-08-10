#!/usr/bin/env python3

import argparse
import copy
import dataclasses
import datetime
import hashlib
import json
import os
import pathlib
import re
import shutil
import stat
import subprocess
import tempfile
import textwrap
import uuid
import sys
from typing import Any, Iterable


BEGIN_RE = re.compile(r"^\s*(?://|#|<!--)\s*TG_CHANGE_BEGIN:\s*([a-z0-9-]+)\s*(?:-->)?\s*$")
END_RE = re.compile(r"^\s*(?://|#|<!--)\s*TG_CHANGE_END:\s*([a-z0-9-]+)\s*(?:-->)?\s*$")
HUNK_RE = re.compile(r"^@@\s*-(\d+)(?:,(\d+))?\s*\+(\d+)(?:,(\d+))?\s*@@")
SHA40_RE = re.compile(r"^[0-9a-f]{40}$")
ZERO_SHA = "0" * 40

REHEARSAL_REQUIRED_SUBMODULES = [
	"cmake",
	"Telegram/lib_rpl",
	"Telegram/lib_crl",
	"Telegram/lib_base",
	"Telegram/lib_ui",
	"Telegram/lib_tl",
	"Telegram/lib_spellcheck",
	"Telegram/lib_storage",
	"Telegram/lib_lottie",
	"Telegram/lib_qr",
	"Telegram/lib_translate",
	"Telegram/lib_webrtc",
	"Telegram/lib_webview",
	"Telegram/codegen",
]

EXIT_OK = 0
EXIT_VALIDATE = 1
EXIT_INPUT = 2
EXIT_QUALITY = 3
EXIT_BASE = 4
EXIT_INVENTORY = 5
EXIT_REHEARSAL_GIT = 6
EXIT_REHEARSAL_COMMAND = 7
EXIT_CLEANUP = 8
EXIT_INTERNAL = 9

MODE_VALIDATE = "validate"
MODE_QUALITY = "quality"
MODE_INVENTORY = "inventory"
MODE_REHEARSAL = "rehearsal"
MODE_REPORT = "report"
KNOWN_MODES = {
	MODE_VALIDATE,
	MODE_QUALITY,
	MODE_INVENTORY,
	MODE_REHEARSAL,
	MODE_REPORT,
}

DEFAULT_MANIFEST = "TG_PROBES/tg_change_fence_manifest.json"
DEFAULT_CONFIG = "TG_PROBES/tg_change_rehearsal_config.json"
ALLOWED_PLACEHOLDERS = {
	"{repo}",
	"{repo_parent}",
	"{worktree}",
	"{build}",
	"{python}",
	"{configuration}",
	"{generator}",
	"{platform}",
	"{toolset}",
}
REHEARSAL_FINGERPRINT_EXCLUDES = {
	"TG_PROBES/NOTE_tg_probe_31_acceptance_report.md",
	"TG_PROBES/PLAN_tg_probe_31_fence_quality_and_merge_rehearsal.md",
}


@dataclasses.dataclass
class CheckerResult:
	exit_code: int
	mode: str
	summary: dict[str, Any]


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


def run_git(repo: pathlib.Path, args: list[str], extra_env: dict[str, str] | None = None) -> str:
	env = os.environ.copy()
	if extra_env:
		env.update(extra_env)
	result = subprocess.run(
		["git", "-C", str(repo), *args],
		capture_output=True,
		text=True,
		check=False,
		env=env,
	)
	if result.returncode != 0:
		raise RuntimeError((result.stderr or result.stdout).strip())
	return result.stdout


def run_command(
		args: list[str],
		cwd: pathlib.Path,
		extra_env: dict[str, str] | None = None,
) -> subprocess.CompletedProcess[str]:
	env = os.environ.copy()
	if extra_env:
		env.update(extra_env)
	try:
		return subprocess.run(
			args,
			cwd=str(cwd),
			capture_output=True,
			text=True,
			check=False,
			env=env,
		)
	except FileNotFoundError as exc:
		command_text = " ".join(args)
		return subprocess.CompletedProcess(
			args=args,
			returncode=127,
			stdout="",
			stderr=f"command not found: {command_text}; {exc}",
		)


def utc_now_iso() -> str:
	return datetime.datetime.now(datetime.timezone.utc).replace(microsecond=0).isoformat().replace("+00:00", "Z")


def normalize_path(path: str) -> str:
	return path.replace("\\", "/")


def windows_extended_path(path: pathlib.Path) -> str:
	resolved = str(path.resolve())
	if os.name != "nt" or resolved.startswith("\\\\?\\"):
		return resolved
	if resolved.startswith("\\\\"):
		return "\\\\?\\UNC\\" + resolved[2:]
	return "\\\\?\\" + resolved


def remove_directory_tree(path: pathlib.Path) -> str:
	target = windows_extended_path(path)
	if not os.path.lexists(target):
		return ""

	errors: list[str] = []

	def retry_read_only(function: Any, failed_path: str, exc_info: Any) -> None:
		try:
			os.chmod(failed_path, stat.S_IWRITE)
			function(failed_path)
		except OSError as exc:
			errors.append(str(exc))

	try:
		shutil.rmtree(target, onerror=retry_read_only)
	except OSError as exc:
		errors.append(str(exc))
	if os.path.lexists(target):
		return "; ".join(errors) or f"directory still exists: {path}"
	return ""


def worktree_is_registered(repo: pathlib.Path, worktree: pathlib.Path) -> tuple[bool, str]:
	result = run_command(["git", "-C", str(repo), "worktree", "list", "--porcelain"], cwd=repo)
	if result.returncode != 0:
		return True, (result.stderr or result.stdout).strip()
	wanted = os.path.normcase(os.path.abspath(worktree))
	registered = any(
		line.startswith("worktree ")
		and os.path.normcase(os.path.abspath(line[len("worktree "):])) == wanted
		for line in result.stdout.splitlines()
	)
	return registered, ""


def worktree_cleanup_needed(added: bool, registered: bool, directory_exists: bool, probe_error: str) -> bool:
	return added or registered or directory_exists or bool(probe_error)


def path_for_report(path: pathlib.Path, repo: pathlib.Path) -> str:
	try:
		return normalize_path(str(path.resolve().relative_to(repo.resolve())))
	except ValueError:
		return normalize_path(str(path))


def deterministic_write_json(path: pathlib.Path, payload: dict[str, Any]) -> None:
	path.parent.mkdir(parents=True, exist_ok=True)
	text = json.dumps(payload, sort_keys=True, indent=2)
	path.write_text(text + "\n", encoding="utf-8")


def resolve_full_sha(repo: pathlib.Path, rev: str) -> str:
	return run_git(repo, ["rev-parse", rev]).strip()


def git_object_exists(repo: pathlib.Path, object_name: str) -> bool:
	probe = run_command(["git", "-C", str(repo), "cat-file", "-e", object_name], cwd=repo)
	return probe.returncode == 0


def snapshot_repo_integrity(repo: pathlib.Path) -> dict[str, str]:
	config_result = run_command(["git", "-C", str(repo), "config", "--local", "--null", "--list"], cwd=repo)
	config_text = config_result.stdout if config_result.returncode == 0 else config_result.stderr
	return {
		"head": resolve_full_sha(repo, "HEAD"),
		"indexTree": run_git(repo, ["write-tree"]).strip(),
		"status": run_git(repo, ["status", "--porcelain=v1", "--untracked-files=all"]),
		"localConfigHash": hashlib.sha256(config_text.encode("utf-8")).hexdigest(),
	}


def gitlink_fingerprint_bytes(staged_entry: str, checkout_head: str) -> bytes:
	return f"gitlink\0{staged_entry}\0checkout\0{checkout_head}".encode("utf-8")


def compute_rehearsal_input_fingerprint(repo: pathlib.Path) -> str:
	result = run_command(
		["git", "-C", str(repo), "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
		cwd=repo,
	)
	if result.returncode != 0:
		raise RuntimeError((result.stderr or result.stdout).strip())
	paths = sorted(path for path in result.stdout.split("\0") if path)
	digest = hashlib.sha256()
	for rel_path in paths:
		normalized = normalize_path(rel_path)
		if normalized.startswith("TG_PROBES/reports/") or normalized in REHEARSAL_FINGERPRINT_EXCLUDES:
			continue
		path = repo / rel_path
		digest.update(normalized.encode("utf-8"))
		digest.update(b"\0")
		if path.is_symlink():
			digest.update(b"symlink\0")
			digest.update(os.readlink(path).encode("utf-8"))
		elif path.is_dir():
			staged_entry = run_git(repo, ["ls-files", "--stage", "--", rel_path]).strip()
			checkout = run_command(["git", "-C", str(path), "rev-parse", "HEAD"], cwd=repo)
			checkout_head = checkout.stdout.strip() if checkout.returncode == 0 else ""
			digest.update(gitlink_fingerprint_bytes(staged_entry, checkout_head))
		elif path.exists():
			digest.update(b"file\0")
			digest.update(path.read_bytes())
		else:
			digest.update(b"missing\0")
		digest.update(b"\0")
	return digest.hexdigest()


def create_synthetic_snapshot_commit(repo: pathlib.Path, parent_head: str) -> tuple[str, str]:
	fd, temp_index = tempfile.mkstemp(prefix="tg-rehearsal-index-", suffix=".idx")
	os.close(fd)
	index_env = {"GIT_INDEX_FILE": temp_index}
	try:
		run_git(repo, ["read-tree", parent_head], extra_env=index_env)
		run_git(repo, ["add", "-A", "--", "."], extra_env=index_env)
		tree = run_git(repo, ["write-tree"], extra_env=index_env).strip()
		message = f"tg rehearsal synthetic snapshot {utc_now_iso()}"
		commit = run_git(
			repo,
			["commit-tree", tree, "-p", parent_head, "-m", message],
			extra_env=index_env,
		).strip()
		return commit, temp_index
	except Exception:
		try:
			os.remove(temp_index)
		except OSError:
			pass
		raise


def parse_iso_utc(value: str) -> datetime.datetime | None:
	try:
		if value.endswith("Z"):
			value = value[:-1] + "+00:00"
		dt = datetime.datetime.fromisoformat(value)
		if dt.tzinfo is None:
			return None
		return dt.astimezone(datetime.timezone.utc)
	except ValueError:
		return None


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
	deletion_anchors: list[int]
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
		deletion_anchors: list[int] = []
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
				deletion_anchors.append(new_line)
			elif current.startswith(" "):
				new_line += 1
			i += 1

		hunks.append(Hunk(
			header=line,
			new_start=int(match.group(3)),
			added_lines=added_lines,
			deletion_anchors=deletion_anchors,
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


def deletion_anchor_overlaps_block(anchor: int, blocks: Iterable[MarkerBlock]) -> bool:
	for block in blocks:
		if block.begin_line - 1 <= anchor <= block.end_line + 1:
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


def parse_json_file(path: pathlib.Path) -> dict[str, Any]:
	try:
		loaded = json.loads(path.read_text(encoding="utf-8"))
	except FileNotFoundError as exc:
		raise ValueError(f"missing JSON file: {path}") from exc
	except json.JSONDecodeError as exc:
		raise ValueError(f"invalid JSON in {path}: {exc}") from exc
	if not isinstance(loaded, dict):
		raise ValueError(f"top-level JSON must be an object: {path}")
	return loaded


def validate_manifest_schema(manifest: dict[str, Any]) -> list[str]:
	errors: list[str] = []
	required_top = [
		"schemaVersion",
		"repoPolicy",
		"generatedAtUtc",
		"baseRevision",
		"activePacket",
		"fences",
		"groups",
	]
	for key in required_top:
		if key not in manifest:
			errors.append(f"manifest missing top-level key: {key}")

	if "schemaVersion" in manifest and not isinstance(manifest["schemaVersion"], int):
		errors.append("manifest.schemaVersion must be int")
	if "repoPolicy" in manifest and not isinstance(manifest["repoPolicy"], str):
		errors.append("manifest.repoPolicy must be string")
	if "generatedAtUtc" in manifest:
		if not isinstance(manifest["generatedAtUtc"], str) or parse_iso_utc(manifest["generatedAtUtc"]) is None:
			errors.append("manifest.generatedAtUtc must be ISO-8601 UTC string")
	if "baseRevision" in manifest:
		base = manifest["baseRevision"]
		if not isinstance(base, str) or not SHA40_RE.match(base):
			errors.append("manifest.baseRevision must be 40-char lowercase SHA")
		elif base == ZERO_SHA:
			errors.append("manifest.baseRevision must not be zero SHA")
	if "activePacket" in manifest and not isinstance(manifest["activePacket"], str):
		errors.append("manifest.activePacket must be string")

	fences = manifest.get("fences")
	if not isinstance(fences, list):
		errors.append("manifest.fences must be array")
		fences = []
	for index, fence in enumerate(fences):
		if not isinstance(fence, dict):
			errors.append(f"fence[{index}] must be object")
			continue
		for key in [
			"id",
			"file",
			"beginLine",
			"endLine",
			"ownerPacket",
			"intent",
			"tests",
			"removalCondition",
			"status",
			"exceptionRationale",
			"rationaleReviewedAtUtc",
			"groupIds",
			"lastTouchedCommit",
		]:
			if key not in fence:
				errors.append(f"fence[{index}] missing key: {key}")
		if "id" in fence and not isinstance(fence["id"], str):
			errors.append(f"fence[{index}].id must be string")
		if "file" in fence and not isinstance(fence["file"], str):
			errors.append(f"fence[{index}].file must be string")
		for int_key in ["beginLine", "endLine"]:
			if int_key in fence and not isinstance(fence[int_key], int):
				errors.append(f"fence[{index}].{int_key} must be int")
		for text_key in ["ownerPacket", "intent", "removalCondition", "exceptionRationale", "rationaleReviewedAtUtc", "status", "lastTouchedCommit"]:
			if text_key in fence and not isinstance(fence[text_key], str):
				errors.append(f"fence[{index}].{text_key} must be string")
		if "lastTouchedCommit" in fence:
			last_touched = str(fence["lastTouchedCommit"]).strip()
			if last_touched and not SHA40_RE.match(last_touched):
				errors.append(f"fence[{index}].lastTouchedCommit must be empty or 40-char lowercase SHA")
		if "tests" in fence and (not isinstance(fence["tests"], list) or not all(isinstance(x, str) for x in fence["tests"])):
			errors.append(f"fence[{index}].tests must be string array")
		if "groupIds" in fence and (not isinstance(fence["groupIds"], list) or not all(isinstance(x, str) for x in fence["groupIds"])):
			errors.append(f"fence[{index}].groupIds must be string array")
		if "status" in fence and fence["status"] not in {"active", "candidate-remove", "exception-approved"}:
			errors.append(f"fence[{index}].status invalid")
		if "status" in fence and fence.get("status") == "exception-approved":
			if not str(fence.get("exceptionRationale", "")).strip():
				errors.append(f"fence[{index}] exception-approved requires exceptionRationale")
			if parse_iso_utc(str(fence.get("rationaleReviewedAtUtc", ""))) is None:
				errors.append(f"fence[{index}] exception-approved requires rationaleReviewedAtUtc")

	groups = manifest.get("groups")
	if not isinstance(groups, list):
		errors.append("manifest.groups must be array")
		groups = []
	for index, group in enumerate(groups):
		if not isinstance(group, dict):
			errors.append(f"group[{index}] must be object")
			continue
		for key in ["groupId", "kind", "members", "cohesionRationale", "splitPolicy"]:
			if key not in group:
				errors.append(f"group[{index}] missing key: {key}")
		if "groupId" in group and not isinstance(group["groupId"], str):
			errors.append(f"group[{index}].groupId must be string")
		if "kind" in group and group["kind"] not in {"header-cpp-pair", "multi-file-feature", "single-file-cluster"}:
			errors.append(f"group[{index}].kind invalid")
		if "members" in group and (not isinstance(group["members"], list) or not all(isinstance(x, str) and ":" in x for x in group["members"])):
			errors.append(f"group[{index}].members must be file:id string array")
		if "splitPolicy" in group and group["splitPolicy"] not in {"must-split", "allowed-broad-exception"}:
			errors.append(f"group[{index}].splitPolicy invalid")

	return errors


def normalize_manifest_commits_and_validate(repo: pathlib.Path, manifest: dict[str, Any]) -> list[str]:
	errors: list[str] = []
	for index, fence in enumerate(manifest.get("fences", [])):
		if not isinstance(fence, dict):
			continue
		raw_commit = str(fence.get("lastTouchedCommit", "")).strip()
		if raw_commit == ZERO_SHA:
			fence["lastTouchedCommit"] = ""
			continue
		if not raw_commit:
			fence["lastTouchedCommit"] = ""
			continue
		if not SHA40_RE.match(raw_commit):
			errors.append(f"fence[{index}].lastTouchedCommit invalid SHA")
			continue
		if not git_object_exists(repo, f"{raw_commit}^{{commit}}"):
			errors.append(f"fence[{index}].lastTouchedCommit missing commit object: {raw_commit}")
	return errors


def load_manifest(path: pathlib.Path, repo: pathlib.Path | None = None) -> dict[str, Any]:
	manifest = parse_json_file(path)
	errors = validate_manifest_schema(manifest)
	if repo is not None and not errors:
		errors.extend(normalize_manifest_commits_and_validate(repo, manifest))
	if errors:
		raise ValueError("; ".join(errors))
	return manifest


def enforce_base_authority(
		repo: pathlib.Path,
		manifest: dict[str, Any],
		cli_base: str,
		allow_inventory_base_mismatch: bool = False,
) -> tuple[int, str, list[str]]:
	errors: list[str] = []
	manifest_base = str(manifest["baseRevision"])
	if cli_base and cli_base != manifest_base and not allow_inventory_base_mismatch:
		errors.append(f"CLI base '{cli_base}' does not match manifest base '{manifest_base}'")
	if not SHA40_RE.match(manifest_base):
		errors.append("manifest baseRevision must be 40-char SHA")
	if errors:
		return EXIT_BASE, manifest_base, errors

	ancestor = subprocess.run(
		["git", "-C", str(repo), "merge-base", "--is-ancestor", manifest_base, "HEAD"],
		capture_output=True,
		text=True,
		check=False,
	)
	if ancestor.returncode != 0:
		return EXIT_BASE, manifest_base, [f"manifest base {manifest_base} is not an ancestor of HEAD"]

	merge_base = run_git(repo, ["merge-base", "HEAD", manifest_base]).strip()
	if merge_base != manifest_base:
		return EXIT_BASE, manifest_base, [
			f"merge-base(HEAD, {manifest_base}) = {merge_base}; expected {manifest_base}",
		]

	return EXIT_OK, manifest_base, []


def find_all_marker_blocks(repo: pathlib.Path) -> tuple[list[dict[str, Any]], list[Violation]]:
	fences: list[dict[str, Any]] = []
	violations: list[Violation] = []
	try:
		candidates_raw = run_git(repo, ["grep", "-l", "TG_CHANGE_BEGIN:", "--", ":(glob)**/*"]).splitlines()
		candidates = [normalize_path(line.strip()) for line in candidates_raw if line.strip()]
	except RuntimeError:
		candidates = []

	if not candidates:
		for root, _, files in os.walk(repo):
			for file_name in files:
				absolute = pathlib.Path(root) / file_name
				relative = normalize_path(str(absolute.relative_to(repo)))
				if is_tg_owned_new_file(relative):
					continue
				try:
					text = absolute.read_text(encoding="utf-8")
				except (UnicodeDecodeError, OSError):
					continue
				if "TG_CHANGE_BEGIN:" not in text:
					continue
				candidates.append(relative)

	for relative in sorted(set(candidates)):
		if is_tg_owned_new_file(relative):
			continue
		absolute = repo / relative
		if not absolute.exists() or not absolute.is_file():
			continue
		try:
			text = absolute.read_text(encoding="utf-8")
		except (UnicodeDecodeError, OSError):
			continue
		blocks, marker_violations, _ = parse_markers(relative, text)
		violations.extend(marker_violations)
		for block in blocks:
			fences.append({
				"id": block.marker_id,
				"file": relative,
				"beginLine": block.begin_line,
				"endLine": block.end_line,
			})

	fences.sort(key=lambda x: (x["file"], x["beginLine"], x["id"]))
	return fences, violations


def newest_line_commit(repo: pathlib.Path, file_path: str, begin_line: int, end_line: int) -> tuple[str, int]:
	content_begin = begin_line + 1
	content_end = max(content_begin, end_line - 1)
	result = run_command(
		[
			"git",
			"-C",
			str(repo),
			"blame",
			"--line-porcelain",
			"-L",
			f"{content_begin},{content_end}",
			"--",
			file_path,
		],
		cwd=repo,
	)
	if result.returncode != 0:
		return "", 0

	commit = ""
	committer_time = 0
	current_commit = ""
	for raw in result.stdout.splitlines():
		parts = raw.split()
		if parts and len(parts[0]) == 40 and all(c in "0123456789abcdef" for c in parts[0]):
			current_commit = "" if parts[0] == ZERO_SHA else parts[0]
		elif raw.startswith("committer-time "):
			value = int(raw.split(" ", 1)[1])
			if value >= committer_time:
				committer_time = value
				commit = current_commit

	return commit, committer_time


def build_manifest_entry_from_block(
		repo: pathlib.Path,
		block: dict[str, Any],
		owner_packet: str,
		active_packet: str,
) -> dict[str, Any]:
	commit, _ = newest_line_commit(repo, block["file"], int(block["beginLine"]), int(block["endLine"]))
	return {
		"id": block["id"],
		"file": block["file"],
		"beginLine": int(block["beginLine"]),
		"endLine": int(block["endLine"]),
		"ownerPacket": owner_packet or active_packet,
		"intent": f"TG change block {block['id']} ownership and merge-audit intent.",
		"tests": [
			"fence-self-test",
			"fence-validate",
		],
		"removalCondition": "Remove when upstream provides equivalent seam and all packet regressions pass.",
		"status": "active",
		"exceptionRationale": "",
		"rationaleReviewedAtUtc": "",
		"groupIds": [],
		"lastTouchedCommit": commit,
		"_activePacket": active_packet,
	}


def infer_group_kind(members: list[str]) -> str:
	if len(members) == 2:
		left_file = members[0].split(":", 1)[0]
		right_file = members[1].split(":", 1)[0]
		left_stem, left_ext = os.path.splitext(left_file)
		right_stem, right_ext = os.path.splitext(right_file)
		if left_stem == right_stem and {left_ext, right_ext} == {".h", ".cpp"}:
			return "header-cpp-pair"
	if len({member.split(":", 1)[0] for member in members}) == 1:
		return "single-file-cluster"
	return "multi-file-feature"


def infer_split_policy_for_id(marker_id: str) -> str:
	if marker_id in {
		"storage-domain-owner-account-factory",
		"launcher-console-profile-snapshot-gates",
	}:
		return "must-split"
	return "allowed-broad-exception"


def compute_auto_groups(fences: list[dict[str, Any]], existing_groups: list[dict[str, Any]]) -> list[dict[str, Any]]:
	by_id: dict[str, list[str]] = {}
	for fence in fences:
		key = f"{normalize_path(str(fence['file']))}:{fence['id']}"
		by_id.setdefault(str(fence["id"]), []).append(key)

	groups_by_id: dict[str, dict[str, Any]] = {}
	for group in existing_groups:
		group_id = str(group.get("groupId", ""))
		if group_id.startswith("auto-group-"):
			marker_id = group_id[len("auto-group-"):]
			groups_by_id[marker_id] = dict(group)

	for marker_id, members in sorted(by_id.items()):
		if len(members) <= 1:
			continue
		group_id = f"auto-group-{marker_id}"
		group = groups_by_id.get(marker_id)
		if group is None:
			groups_by_id[marker_id] = {
				"groupId": group_id,
				"kind": infer_group_kind(members),
				"members": sorted(members),
				"cohesionRationale": f"Auto-group for repeated fence id {marker_id} across declared occurrences.",
				"splitPolicy": infer_split_policy_for_id(marker_id),
			}
		else:
			group["members"] = sorted(members)

	result = [group for group in existing_groups if not str(group.get("groupId", "")).startswith("auto-group-")]
	result.extend(groups_by_id[key] for key in sorted(groups_by_id.keys()))
	result.sort(key=lambda item: str(item["groupId"]))
	return result


def sync_group_ids_in_fences(fences: list[dict[str, Any]], groups: list[dict[str, Any]]) -> None:
	member_to_groups: dict[str, list[str]] = {}
	for group in groups:
		group_id = str(group["groupId"])
		for member in group["members"]:
			member_to_groups.setdefault(str(member), []).append(group_id)

	for fence in fences:
		key = f"{normalize_path(str(fence['file']))}:{fence['id']}"
		auto_group_ids = sorted(g for g in member_to_groups.get(key, []) if g.startswith("auto-group-"))
		manual_group_ids = sorted(g for g in fence.get("groupIds", []) if not str(g).startswith("auto-group-"))
		fence["groupIds"] = manual_group_ids + auto_group_ids


def find_markers_by_file(text: str) -> dict[str, MarkerBlock]:
	markers: dict[str, MarkerBlock] = {}
	blocks, _, _ = parse_markers("", text)
	for block in blocks:
		markers[block.marker_id] = block
	return markers


def manifest_index(manifest: dict[str, Any]) -> dict[str, dict[str, Any]]:
	index: dict[str, dict[str, Any]] = {}
	for fence in manifest["fences"]:
		key = f"{normalize_path(str(fence['file']))}:{fence['id']}"
		index[key] = fence
	return index


def build_rehearsal_manifest_overlay(
		repo: pathlib.Path,
		manifest: dict[str, Any],
) -> tuple[dict[str, Any], dict[str, Any]]:
	found_fences, parse_violations = find_all_marker_blocks(repo)
	if parse_violations:
		raise ValueError("marker parsing violations while building rehearsal overlay")

	found_by_key = {
		f"{normalize_path(str(item['file']))}:{item['id']}": item
		for item in found_fences
	}
	manifest_by_key = manifest_index(manifest)

	overlay = copy.deepcopy(manifest)
	moved_fences: list[dict[str, Any]] = []
	moved_span_line_delta = 0
	for fence in overlay["fences"]:
		key = f"{normalize_path(str(fence['file']))}:{fence['id']}"
		current = found_by_key.get(key)
		if current is None:
			continue
		old_begin = int(fence["beginLine"])
		old_end = int(fence["endLine"])
		new_begin = int(current["beginLine"])
		new_end = int(current["endLine"])
		if old_begin != new_begin or old_end != new_end:
			moved_fences.append({
				"key": key,
				"manifestRange": [old_begin, old_end],
				"overlayRange": [new_begin, new_end],
			})
			moved_span_line_delta += abs(new_begin - old_begin) + abs(new_end - old_end)
		fence["beginLine"] = new_begin
		fence["endLine"] = new_end

	unknown_keys = sorted(set(found_by_key.keys()) - set(manifest_by_key.keys()))
	missing_keys = sorted(set(manifest_by_key.keys()) - set(found_by_key.keys()))

	return overlay, {
		"movedFenceCount": len(moved_fences),
		"movedSpanLineDelta": moved_span_line_delta,
		"movedFences": moved_fences,
		"unknownFenceCount": len(unknown_keys),
		"missingFenceCount": len(missing_keys),
	}


def inventory_operation(
		repo: pathlib.Path,
		base: str,
		manifest_path: pathlib.Path,
		generate: bool,
		update: bool,
		check: bool,
		allow_inventory_base_mismatch: bool,
		manifest_override: dict[str, Any] | None = None,
) -> CheckerResult:
	if sum([1 if generate else 0, 1 if update else 0, 1 if check else 0]) != 1:
		return CheckerResult(
			exit_code=EXIT_INPUT,
			mode=MODE_INVENTORY,
			summary={"error": "inventory mode requires exactly one of --generate, --update, --check"},
		)

	if manifest_override is not None and (generate or update):
		return CheckerResult(
			exit_code=EXIT_INPUT,
			mode=MODE_INVENTORY,
			summary={"error": "manifest_override is only supported for inventory --check"},
		)

	found_fences, parse_violations = find_all_marker_blocks(repo)
	if parse_violations:
		return CheckerResult(
			exit_code=EXIT_VALIDATE,
			mode=MODE_INVENTORY,
			summary={
				"error": "marker parsing violations while scanning repository",
				"violations": [dataclasses.asdict(v) for v in parse_violations],
			},
		)

	resolved_base = resolve_full_sha(repo, base)

	if generate:
		existing_manifest: dict[str, Any] | None = None
		if manifest_path.exists():
			try:
				existing_manifest = load_manifest(manifest_path, repo=repo)
			except ValueError:
				existing_manifest = None

		existing_index = manifest_index(existing_manifest) if existing_manifest else {}
		preserve_fields = [
			"ownerPacket",
			"intent",
			"tests",
			"removalCondition",
			"status",
			"exceptionRationale",
			"rationaleReviewedAtUtc",
			"groupIds",
		]

		entries = [
			build_manifest_entry_from_block(
				repo=repo,
				block=block,
				owner_packet="",
				active_packet="PLAN_tg_probe_31_fence_quality_and_merge_rehearsal.md",
			)
			for block in found_fences
		]
		for entry in entries:
			entry_key = f"{normalize_path(str(entry['file']))}:{entry['id']}"
			existing = existing_index.get(entry_key)
			if existing is None:
				continue
			for field in preserve_fields:
				if field in existing:
					entry[field] = existing[field]

		for entry in entries:
			entry.pop("_activePacket", None)
		auto_groups = compute_auto_groups(entries, list(existing_manifest.get("groups", [])) if existing_manifest else [])
		sync_group_ids_in_fences(entries, auto_groups)
		manifest = {
			"schemaVersion": 1,
			"repoPolicy": "TG_CHANGE_POLICY.md",
			"generatedAtUtc": utc_now_iso(),
			"baseRevision": resolved_base,
			"activePacket": str(existing_manifest.get("activePacket", "PLAN_tg_probe_31_fence_quality_and_merge_rehearsal.md")) if existing_manifest else "PLAN_tg_probe_31_fence_quality_and_merge_rehearsal.md",
			"fences": entries,
			"groups": auto_groups,
		}
		deterministic_write_json(manifest_path, manifest)
		return CheckerResult(
			exit_code=EXIT_OK,
			mode=MODE_INVENTORY,
			summary={
				"operation": "generate",
				"manifest": path_for_report(manifest_path, repo),
				"fenceCount": len(entries),
			},
		)

	if manifest_override is not None:
		manifest = copy.deepcopy(manifest_override)
		errors = validate_manifest_schema(manifest)
		errors.extend(normalize_manifest_commits_and_validate(repo, manifest))
		if errors:
			return CheckerResult(EXIT_INPUT, MODE_INVENTORY, {"error": "; ".join(errors)})
	else:
		try:
			manifest = load_manifest(manifest_path, repo=repo)
		except ValueError as exc:
			return CheckerResult(EXIT_INPUT, MODE_INVENTORY, {"error": str(exc)})

	base_code, _, base_errors = enforce_base_authority(
		repo=repo,
		manifest=manifest,
		cli_base=resolved_base,
		allow_inventory_base_mismatch=allow_inventory_base_mismatch,
	)
	if base_code != EXIT_OK:
		return CheckerResult(base_code, MODE_INVENTORY, {"errors": base_errors})

	manifest_by_key = manifest_index(manifest)
	found_by_key = {
		f"{normalize_path(str(item['file']))}:{item['id']}": item
		for item in found_fences
	}

	if update:
		updated_entries: list[dict[str, Any]] = []
		for key in sorted(found_by_key.keys()):
			fence = found_by_key[key]
			existing = manifest_by_key.get(key)
			if existing is None:
				entry = build_manifest_entry_from_block(
					repo=repo,
					block=fence,
					owner_packet="",
					active_packet=str(manifest.get("activePacket", "")),
				)
				entry.pop("_activePacket", None)
			else:
				entry = dict(existing)
				entry["beginLine"] = int(fence["beginLine"])
				entry["endLine"] = int(fence["endLine"])
				if not str(entry.get("ownerPacket", "")).strip():
					entry["ownerPacket"] = str(manifest.get("activePacket", "PLAN_tg_probe_31_fence_quality_and_merge_rehearsal.md"))
				commit, _ = newest_line_commit(repo, str(entry["file"]), int(entry["beginLine"]), int(entry["endLine"]))
				entry["lastTouchedCommit"] = commit
			updated_entries.append(entry)

		auto_groups = compute_auto_groups(updated_entries, list(manifest.get("groups", [])))
		sync_group_ids_in_fences(updated_entries, auto_groups)

		manifest["generatedAtUtc"] = utc_now_iso()
		manifest["baseRevision"] = resolved_base
		manifest["fences"] = updated_entries
		manifest["groups"] = auto_groups
		deterministic_write_json(manifest_path, manifest)
		return CheckerResult(
			exit_code=EXIT_OK,
			mode=MODE_INVENTORY,
			summary={
				"operation": "update",
				"manifest": path_for_report(manifest_path, repo),
				"fenceCount": len(updated_entries),
			},
		)

	drifts: list[dict[str, Any]] = []

	for key, fence in manifest_by_key.items():
		if key not in found_by_key:
			drifts.append({"type": "missing-fence-in-code", "key": key})

	for key, fence in found_by_key.items():
		if key not in manifest_by_key:
			drifts.append({"type": "unknown-fence-not-in-manifest", "key": key})

	for key in sorted(set(manifest_by_key.keys()).intersection(found_by_key.keys())):
		mf = manifest_by_key[key]
		ff = found_by_key[key]
		if int(mf["beginLine"]) != int(ff["beginLine"]) or int(mf["endLine"]) != int(ff["endLine"]):
			drifts.append({
				"type": "moved-lines",
				"key": key,
				"manifestRange": [mf["beginLine"], mf["endLine"]],
				"codeRange": [ff["beginLine"], ff["endLine"]],
			})

	for fence in manifest["fences"]:
		if not str(fence.get("ownerPacket", "")).strip():
			drifts.append({"type": "invalid-owner-packet-reference", "key": f"{fence['file']}:{fence['id']}", "reason": "ownerPacket empty"})
		else:
			owner_path = repo / "TG_PROBES" / str(fence["ownerPacket"])
			if not owner_path.exists():
				drifts.append({"type": "invalid-owner-packet-reference", "key": f"{fence['file']}:{fence['id']}", "reason": f"missing {owner_path.name}"})

		if not str(fence.get("intent", "")).strip():
			drifts.append({"type": "metadata-incomplete", "key": f"{fence['file']}:{fence['id']}", "field": "intent"})
		if not isinstance(fence.get("tests"), list) or len(fence["tests"]) == 0:
			drifts.append({"type": "metadata-incomplete", "key": f"{fence['file']}:{fence['id']}", "field": "tests"})
		if not str(fence.get("removalCondition", "")).strip():
			drifts.append({"type": "metadata-incomplete", "key": f"{fence['file']}:{fence['id']}", "field": "removalCondition"})

		if fence.get("status") == "exception-approved":
			if not str(fence.get("exceptionRationale", "")).strip() or parse_iso_utc(str(fence.get("rationaleReviewedAtUtc", ""))) is None:
				drifts.append({"type": "stale-exception-without-rationale", "key": f"{fence['file']}:{fence['id']}"})

	declared_group_members: dict[str, set[str]] = {}
	for group in manifest["groups"]:
		group_id = str(group["groupId"])
		members = set(str(x) for x in group["members"])
		declared_group_members[group_id] = members
		for member in members:
			if member not in manifest_by_key:
				drifts.append({"type": "group-member-mismatch", "groupId": group_id, "member": member, "reason": "member missing from fences"})

	for fence in manifest["fences"]:
		fence_key = f"{fence['file']}:{fence['id']}"
		for group_id in fence.get("groupIds", []):
			members = declared_group_members.get(group_id)
			if members is None or fence_key not in members:
				drifts.append({"type": "group-member-mismatch", "groupId": group_id, "member": fence_key, "reason": "groupIds/member mismatch"})

	exit_code = EXIT_OK if not drifts else EXIT_INVENTORY
	return CheckerResult(
		exit_code=exit_code,
		mode=MODE_INVENTORY,
		summary={
			"operation": "check",
			"manifest": path_for_report(manifest_path, repo),
			"driftCount": len(drifts),
			"drifts": drifts,
		},
	)


def validate(repo: pathlib.Path, base: str, verbose: bool, scope_files: set[str] | None = None) -> int:
	violations: list[Violation] = []
	files = changed_files(repo, base)
	if scope_files is not None:
		files = [
			(status, file_path)
			for status, file_path in files
			if normalize_path(file_path) in scope_files
		]

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
			if hunk.has_deletions:
				for anchor in hunk.deletion_anchors:
					if deletion_anchor_overlaps_block(anchor, blocks):
						continue
					violations.append(Violation(
						file_path=file_path,
						hunk_header=hunk.header,
						line=anchor,
						marker_id=nearest_marker_id(anchor, blocks),
						message="deleted line anchor does not overlap a fenced replacement block",
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


def validate_with_summary(
		repo: pathlib.Path,
		base: str,
		verbose: bool,
		scope_files: set[str] | None = None,
) -> tuple[int, dict[str, Any]]:
	violations: list[Violation] = []
	files = changed_files(repo, base)
	if scope_files is not None:
		files = [
			(status, file_path)
			for status, file_path in files
			if normalize_path(file_path) in scope_files
		]

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
			violations.append(Violation(file_path=file_path, message="modified existing file is missing in working tree"))
			continue

		try:
			text = absolute.read_text(encoding="utf-8")
		except UnicodeDecodeError:
			violations.append(Violation(file_path=file_path, message="file is not UTF-8 text; checker only supports text files"))
			continue

		blocks, marker_violations, marker_lines = parse_markers(file_path, text)
		violations.extend(marker_violations)

		diff_text = run_git(repo, ["diff", "--unified=0", "--find-renames", base, "--", file_path])
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
			if hunk.has_deletions:
				for anchor in hunk.deletion_anchors:
					if deletion_anchor_overlaps_block(anchor, blocks):
						continue
					violations.append(Violation(
						file_path=file_path,
						hunk_header=hunk.header,
						line=anchor,
						marker_id=nearest_marker_id(anchor, blocks),
						message="deleted line anchor does not overlap a fenced replacement block",
					))

	for issue in violations:
		marker = issue.marker_id if issue.marker_id else "none"
		hunk = f" {issue.hunk_header}" if issue.hunk_header else ""
		line = f" line {issue.line}" if issue.line else ""
		print(f"VIOLATION {issue.file_path}:{line}{hunk} near-marker={marker}: {issue.message}")

	exit_code = EXIT_VALIDATE if violations else EXIT_OK
	if exit_code == EXIT_OK:
		print("PASS: no fence violations found.")
	else:
		print(f"FAIL: {len(violations)} violation(s) found.")

	return exit_code, {
		"base": base,
		"checkedFileCount": len(files),
		"violationCount": len(violations),
		"violations": [dataclasses.asdict(v) for v in violations],
	}


def build_diff_hunk_index(repo: pathlib.Path, base: str, file_path: str) -> list[Hunk]:
	diff_text = run_git(repo, ["diff", "--unified=0", "--find-renames", base, "--", file_path])
	return parse_hunks(diff_text)


def compute_changed_density_for_fence(
		fence: dict[str, Any],
		hunks: list[Hunk],
) -> tuple[float, int, int]:
	content_start = int(fence["beginLine"]) + 1
	content_end = int(fence["endLine"]) - 1
	denominator = max(1, content_end - content_start + 1)
	positions: set[int] = set()

	for hunk in hunks:
		for added in hunk.added_lines:
			if content_start <= added <= content_end:
				positions.add(added)
		for anchor in hunk.deletion_anchors:
			if content_start <= anchor <= content_end:
				positions.add(anchor)

	numerator = len(positions)
	density = min(1.0, float(numerator) / float(denominator))
	return density, numerator, denominator


def quality_mode(
		repo: pathlib.Path,
		base: str,
		manifest_path: pathlib.Path,
		args: argparse.Namespace,
		allow_base_mismatch: bool = False,
		manifest_override: dict[str, Any] | None = None,
) -> CheckerResult:
	if manifest_override is not None:
		manifest = copy.deepcopy(manifest_override)
		errors = validate_manifest_schema(manifest)
		errors.extend(normalize_manifest_commits_and_validate(repo, manifest))
		if errors:
			return CheckerResult(EXIT_INPUT, MODE_QUALITY, {"error": "; ".join(errors)})
	else:
		try:
			manifest = load_manifest(manifest_path, repo=repo)
		except ValueError as exc:
			return CheckerResult(EXIT_INPUT, MODE_QUALITY, {"error": str(exc)})

	resolved_base = resolve_full_sha(repo, base)
	base_code, manifest_base, base_errors = enforce_base_authority(
		repo,
		manifest,
		resolved_base,
		allow_inventory_base_mismatch=allow_base_mismatch,
	)
	if base_code != EXIT_OK:
		return CheckerResult(base_code, MODE_QUALITY, {"errors": base_errors})
	effective_base = resolved_base if allow_base_mismatch else manifest_base

	now = datetime.datetime.now(datetime.timezone.utc)
	violations: list[dict[str, Any]] = []
	warnings: list[dict[str, Any]] = []
	file_to_hunks: dict[str, list[Hunk]] = {}

	manifest_fence_keys = set()
	for fence in manifest["fences"]:
		if is_tg_owned_new_file(str(fence["file"])):
			violations.append({
				"type": "upstream-file-scope",
				"key": f"{fence['file']}:{fence['id']}",
				"message": "manifest fence points to TG-owned path",
			})
		manifest_fence_keys.add(f"{normalize_path(str(fence['file']))}:{fence['id']}")

	for fence in manifest["fences"]:
		fence_key = f"{fence['file']}:{fence['id']}"
		file_path = normalize_path(str(fence["file"]))
		absolute = repo / file_path
		if not absolute.exists():
			violations.append({"type": "missing-file", "key": fence_key, "message": "manifest fence file missing"})
			continue

		content_lines = max(1, int(fence["endLine"]) - int(fence["beginLine"]) - 1)
		status = str(fence["status"])
		is_exception = status == "exception-approved"

		if content_lines > int(args.max_fence_lines):
			if is_exception:
				if content_lines > 120:
					violations.append({"type": "fence-size", "severity": "fail", "key": fence_key, "contentLines": content_lines, "limit": 120})
				if not str(fence.get("exceptionRationale", "")).strip() or parse_iso_utc(str(fence.get("rationaleReviewedAtUtc", ""))) is None:
					violations.append({"type": "exception-metadata", "severity": "fail", "key": fence_key, "message": "exception-approved missing rationale metadata"})
			else:
				violations.append({"type": "fence-size", "severity": "fail", "key": fence_key, "contentLines": content_lines, "limit": int(args.max_fence_lines)})
		elif content_lines > int(args.warn_fence_lines):
			warnings.append({"type": "fence-size", "severity": "warn", "key": fence_key, "contentLines": content_lines, "limit": int(args.warn_fence_lines)})

		if file_path not in file_to_hunks:
			file_to_hunks[file_path] = build_diff_hunk_index(repo, effective_base, file_path)
		density, numerator, denominator = compute_changed_density_for_fence(fence, file_to_hunks[file_path])
		if density < float(args.min_change_density):
			violations.append({
				"type": "change-density",
				"severity": "fail",
				"key": fence_key,
				"density": round(density, 4),
				"numerator": numerator,
				"denominator": denominator,
				"limit": float(args.min_change_density),
			})
		elif density < float(args.warn_min_change_density):
			warnings.append({
				"type": "change-density",
				"severity": "warn",
				"key": fence_key,
				"density": round(density, 4),
				"numerator": numerator,
				"denominator": denominator,
				"limit": float(args.warn_min_change_density),
			})

		commit, author_time = newest_line_commit(repo, file_path, int(fence["beginLine"]), int(fence["endLine"]))
		if author_time == 0:
			violations.append({"type": "staleness", "severity": "fail", "key": fence_key, "message": "unable to compute line-age metadata"})
		else:
			age_days = int((now - datetime.datetime.fromtimestamp(author_time, datetime.timezone.utc)).days)
			if status == "candidate-remove" and age_days > 30:
				violations.append({"type": "candidate-remove-stale", "severity": "fail", "key": fence_key, "ageDays": age_days, "limit": 30})
			elif age_days > int(args.max_stale_days):
				if is_exception and str(fence.get("exceptionRationale", "")).strip():
					warnings.append({"type": "staleness", "severity": "warn", "key": fence_key, "ageDays": age_days, "limit": int(args.max_stale_days), "message": "exception-approved override in effect"})
				else:
					violations.append({"type": "staleness", "severity": "fail", "key": fence_key, "ageDays": age_days, "limit": int(args.max_stale_days)})
			elif age_days > int(args.warn_stale_days):
				warnings.append({"type": "staleness", "severity": "warn", "key": fence_key, "ageDays": age_days, "limit": int(args.warn_stale_days)})
			fence["_lineAgeDays"] = age_days
			fence["_newestLineCommit"] = commit

	id_occurrences: dict[str, list[str]] = {}
	for fence in manifest["fences"]:
		key = f"{fence['file']}:{fence['id']}"
		id_occurrences.setdefault(str(fence["id"]), []).append(key)

	group_members = {str(group["groupId"]): set(str(member) for member in group["members"]) for group in manifest["groups"]}
	for marker_id, occurrences in id_occurrences.items():
		if len(occurrences) <= 1:
			continue
		matched = False
		occurrence_set = set(occurrences)
		for members in group_members.values():
			if occurrence_set.issubset(members):
				matched = True
				break
		if not matched:
			violations.append({
				"type": "global-id-group-consistency",
				"severity": "fail",
				"id": marker_id,
				"members": sorted(occurrences),
				"message": "reused ID must be fully represented by one group",
			})

	for fence in manifest["fences"]:
		fence_key = f"{fence['file']}:{fence['id']}"
		for group_id in fence.get("groupIds", []):
			members = group_members.get(str(group_id))
			if members is None:
				violations.append({"type": "group-reference", "severity": "fail", "key": fence_key, "groupId": group_id, "message": "groupId missing from groups"})
			elif fence_key not in members:
				violations.append({"type": "group-reference", "severity": "fail", "key": fence_key, "groupId": group_id, "message": "fence references group but not listed as member"})

	exit_code = EXIT_OK if not violations else EXIT_QUALITY
	if exit_code == EXIT_OK:
		print(f"PASS: quality checks passed with {len(warnings)} warning(s).")
	else:
		print(f"FAIL: quality checks reported {len(violations)} violation(s) and {len(warnings)} warning(s).")

	return CheckerResult(
		exit_code=exit_code,
		mode=MODE_QUALITY,
		summary={
			"manifest": path_for_report(manifest_path, repo),
			"base": effective_base,
			"committedBase": manifest_base,
			"warningCount": len(warnings),
			"violationCount": len(violations),
			"warnings": warnings,
			"violations": violations,
		},
	)


def apply_path_tokens(value: str, repo: pathlib.Path, worktree: pathlib.Path, build: pathlib.Path) -> str:
	text = value
	sources = [
		(str(build), "${BUILD}"),
		(str(worktree), "${WORKTREE}"),
		(str(repo), "${REPO}"),
		(str(repo.parent), "${REPO}/.."),
	]
	for source, token in sorted(sources, key=lambda item: len(item[0]), reverse=True):
		for spelling in {source, normalize_path(source)}:
			text = re.sub(re.escape(spelling), lambda _: token, text, flags=re.IGNORECASE)
	return normalize_path(text)


def redact_sensitive(text: str, sensitive_values: list[str]) -> str:
	result = text
	for value in sensitive_values:
		if value:
			result = result.replace(value, "<REDACTED>")
	return result


def redact_absolute_paths(text: str) -> str:
	result = re.sub(r"(?i)(?<![a-z0-9_])[a-z]:[\\/][^\r\n\t\"'<>|]*", "${ABS_PATH}", text)
	result = re.sub(r"(?i)(file://)[^\r\n\t\"'<>|]*", r"\1${ABS_PATH}", result)
	return result


def sanitize_report_value(
		value: Any,
		repo: pathlib.Path,
		worktree: pathlib.Path,
		build: pathlib.Path,
		sensitive_values: list[str],
) -> Any:
	if isinstance(value, str):
		sanitized = apply_path_tokens(redact_sensitive(value, sensitive_values), repo, worktree, build)
		return redact_absolute_paths(sanitized)
	if isinstance(value, list):
		return [sanitize_report_value(item, repo, worktree, build, sensitive_values) for item in value]
	if isinstance(value, dict):
		return {
			key: sanitize_report_value(item, repo, worktree, build, sensitive_values)
			for key, item in value.items()
		}
	return value


def load_cache_values(cache_path: pathlib.Path, keys: list[str]) -> dict[str, str]:
	values: dict[str, str] = {}
	if not cache_path.exists():
		return values
	for raw in cache_path.read_text(encoding="utf-8", errors="ignore").splitlines():
		if "=" not in raw or ":" not in raw:
			continue
		left, value = raw.split("=", 1)
		key = left.split(":", 1)[0]
		if key in keys:
			values[key] = value
	return values


def parse_rehearsal_config(path: pathlib.Path) -> dict[str, Any]:
	config = parse_json_file(path)
	required = {
		"schemaVersion",
		"defaultTarget",
		"worktreeNamePrefix",
		"buildDirectory",
		"primaryBuildDirectory",
		"cacheImportKeys",
		"sensitiveKeys",
		"configureCommand",
		"commands",
		"optionalPrerequisites",
	}
	missing = sorted(required.difference(config.keys()))
	if missing:
		raise ValueError(f"rehearsal config missing keys: {', '.join(missing)}")
	if config.get("schemaVersion") != 1:
		raise ValueError("rehearsal config schemaVersion must be 1")
	if config.get("cacheImportKeys") != [
		"QT_DIR",
		"TDESKTOP_API_ID",
		"TDESKTOP_API_HASH",
		"CMAKE_GENERATOR",
		"CMAKE_GENERATOR_PLATFORM",
		"CMAKE_GENERATOR_TOOLSET",
	]:
		raise ValueError("rehearsal config cacheImportKeys must match locked packet list")
	if config.get("sensitiveKeys") != ["TDESKTOP_API_ID", "TDESKTOP_API_HASH"]:
		raise ValueError("rehearsal config sensitiveKeys must match locked packet list")
	if not isinstance(config["configureCommand"], list) or not all(isinstance(x, str) for x in config["configureCommand"]):
		raise ValueError("configureCommand must be token array")
	placeholder_context = {placeholder[1:-1]: "value" for placeholder in ALLOWED_PLACEHOLDERS}
	resolve_command_tokens(config["configureCommand"], placeholder_context)
	if not isinstance(config["commands"], list):
		raise ValueError("commands must be array")
	environment = config.get("environment", {})
	if environment is None:
		environment = {}
	if not isinstance(environment, dict):
		raise ValueError("rehearsal config environment must be object when provided")
	for key, value in environment.items():
		if not isinstance(key, str) or not isinstance(value, str):
			raise ValueError("rehearsal config environment keys/values must be strings")
	config["environment"] = environment
	for index, command in enumerate(config["commands"]):
		if not isinstance(command, dict):
			raise ValueError(f"commands[{index}] must be object")
		tokens = command.get("tokens")
		if not isinstance(tokens, list) or not all(isinstance(token, str) for token in tokens):
			raise ValueError(f"commands[{index}].tokens must be token array")
		resolve_command_tokens(tokens, placeholder_context)
		command_env = command.get("environment", {})
		if command_env is None:
			command_env = {}
		if not isinstance(command_env, dict):
			raise ValueError(f"commands[{index}].environment must be object when provided")
		for key, value in command_env.items():
			if not isinstance(key, str) or not isinstance(value, str):
				raise ValueError(f"commands[{index}].environment keys/values must be strings")
		resolve_environment_map(command_env, placeholder_context)
		command["environment"] = command_env
	resolve_environment_map(environment, placeholder_context)
	return config


def classify_submodule_init_failure(stderr: str, stdout: str) -> str:
	combined = f"{stderr}\n{stdout}".lower()
	markers = [
		"did not contain",
		"not our ref",
		"unable to checkout",
		"unable to find",
		"reference is not a tree",
		"server does not allow request for unadvertised object",
	]
	if any(marker in combined for marker in markers):
		return "required-objects-unavailable-local-no-fetch"
	return "submodule-init-failed"


def declared_submodule_paths(worktree: pathlib.Path) -> list[str]:
	result = run_command(
		["git", "-C", str(worktree), "config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$"],
		cwd=worktree,
	)
	if result.returncode != 0:
		raise ValueError("unable to read top-level submodule paths from .gitmodules")
	paths = []
	for line in result.stdout.splitlines():
		_, separator, value = line.partition(" ")
		if separator and value.strip():
			paths.append(normalize_path(value.strip()))
	return sorted(set(paths))


def prepare_local_submodule_sources(
		repo: pathlib.Path,
		worktree: pathlib.Path,
		required_paths: list[str],
) -> tuple[list[dict[str, str]], list[dict[str, str]]]:
	modules_result = run_command(
		["git", "-C", str(worktree), "config", "-f", ".gitmodules", "--get-regexp", r"^submodule\..*\.path$"],
		cwd=worktree,
	)
	if modules_result.returncode != 0:
		return [], [{"path": ".gitmodules", "reason": "submodule-map-unavailable"}]
	path_to_name: dict[str, str] = {}
	for line in modules_result.stdout.splitlines():
		key, separator, value = line.partition(" ")
		match = re.fullmatch(r"submodule\.(.+)\.path", key)
		if separator and match:
			path_to_name[normalize_path(value.strip())] = match.group(1)

	sources: list[dict[str, str]] = []
	blockers: list[dict[str, str]] = []
	for rel_path in required_paths:
		name = path_to_name.get(normalize_path(rel_path))
		primary_path = repo / rel_path
		if not name:
			blockers.append({"path": rel_path, "reason": "submodule-name-missing"})
			continue
		probe = run_command(["git", "-C", str(primary_path), "rev-parse", "--git-dir"], cwd=repo)
		if probe.returncode != 0:
			blockers.append({"path": rel_path, "reason": "primary-submodule-not-initialized"})
			continue
		expected_result = run_command(["git", "-C", str(worktree), "rev-parse", f"HEAD:{rel_path}"], cwd=worktree)
		if expected_result.returncode != 0:
			blockers.append({"path": rel_path, "reason": "required-gitlink-missing"})
			continue
		expected_commit = expected_result.stdout.strip()
		object_probe = run_command(["git", "-C", str(primary_path), "cat-file", "-e", f"{expected_commit}^{{commit}}"], cwd=repo)
		if object_probe.returncode != 0:
			blockers.append({"path": rel_path, "reason": "required-object-missing-locally"})
			continue
		sources.append({"path": rel_path, "name": name, "source": str(primary_path), "commit": expected_commit})
	return sources, blockers


def verify_required_submodules_populated(worktree: pathlib.Path, required_paths: list[str]) -> list[dict[str, str]]:
	missing: list[dict[str, str]] = []
	for rel_path in required_paths:
		status = run_command(
			["git", "-C", str(worktree), "submodule", "status", "--", rel_path],
			cwd=worktree,
		)
		if status.returncode != 0:
			missing.append({"path": rel_path, "reason": "submodule-status-query-failed"})
			continue
		lines = [line for line in status.stdout.splitlines() if line.strip()]
		if not lines:
			missing.append({"path": rel_path, "reason": "submodule-status-empty"})
			continue
		prefix = lines[0][0]
		if prefix == "-":
			missing.append({"path": rel_path, "reason": "not-initialized"})
			continue
		if prefix == "U":
			missing.append({"path": rel_path, "reason": "merge-conflict"})
			continue
		submodule_path = worktree / rel_path
		if not submodule_path.exists() or not submodule_path.is_dir():
			missing.append({"path": rel_path, "reason": "missing-directory"})
			continue
		if not any(submodule_path.iterdir()):
			missing.append({"path": rel_path, "reason": "empty-directory"})
			continue
	return missing


def resolve_command_tokens(
		tokens: list[str],
		context: dict[str, str],
	) -> list[str]:
	resolved: list[str] = []
	for token in tokens:
		for placeholder in re.findall(r"\{[^{}]+\}", token):
			if placeholder not in ALLOWED_PLACEHOLDERS:
				raise ValueError(f"unknown placeholder in command token: {placeholder}")
		value = token
		for key, replacement in context.items():
			value = value.replace("{" + key + "}", replacement)
		resolved.append(value)
	return resolved


def resolve_environment_map(environment: dict[str, str], context: dict[str, str]) -> dict[str, str]:
	resolved: dict[str, str] = {}
	for key, raw_value in environment.items():
		value = raw_value
		for placeholder in re.findall(r"\{[^{}]+\}", value):
			if placeholder not in ALLOWED_PLACEHOLDERS:
				raise ValueError(f"unknown placeholder in environment value for {key}: {placeholder}")
		for context_key, replacement in context.items():
			value = value.replace("{" + context_key + "}", replacement)
		resolved[key] = value
	return resolved


def should_trigger_rebase_detach_fallback(stderr: str, stdout: str) -> bool:
	error_text = f"{stderr}\n{stdout}".lower()
	return "could not detach head" in error_text


def plan_linear_rebase_fallback(
		target_sha: str,
		synthetic_commit: str,
		fallback_base: str,
		ordered_commits: list[str],
		merge_commits: list[str],
) -> dict[str, Any]:
	if not SHA40_RE.match(target_sha):
		raise ValueError("fallback target_sha must be a full 40-char commit SHA")
	if not SHA40_RE.match(synthetic_commit):
		raise ValueError("fallback synthetic_commit must be a full 40-char commit SHA")
	if not SHA40_RE.match(fallback_base):
		raise ValueError("fallback merge-base must be a full 40-char commit SHA")
	if merge_commits:
		raise ValueError("fallback aborted: branch-only range contains merge commits; merge flattening is forbidden")
	return {
		"semanticMode": "reset-cherry-pick-linear-rebase",
		"targetSha": target_sha,
		"sourceSyntheticCommit": synthetic_commit,
		"base": fallback_base,
		"range": f"{fallback_base}..{synthetic_commit}",
		"orderedCommits": list(ordered_commits),
		"commitCount": len(ordered_commits),
	}


def run_rehearsal(repo: pathlib.Path, args: argparse.Namespace) -> CheckerResult:
	manifest_path = repo / args.manifest
	config_path = repo / args.config

	try:
		manifest = load_manifest(manifest_path, repo=repo)
		config = parse_rehearsal_config(config_path)
	except ValueError as exc:
		return CheckerResult(EXIT_INPUT, MODE_REHEARSAL, {"error": str(exc)})

	base_code, manifest_base, base_errors = enforce_base_authority(repo, manifest, manifest["baseRevision"])
	if base_code != EXIT_OK:
		return CheckerResult(base_code, MODE_REHEARSAL, {"errors": base_errors})

	target = args.target or str(config["defaultTarget"])
	phases: list[dict[str, Any]] = []
	sensitive_values: list[str] = []
	token = uuid.uuid4().hex[:10]
	worktree = repo.parent / f"{config['worktreeNamePrefix']}{token}"
	build_dir = worktree / str(config["buildDirectory"])
	rebase_branch_name = ""
	attempted_rebase_branches: list[str] = []
	retained_failure_refs: list[str] = []
	semantic_mode = args.mode
	pre_integrity = snapshot_repo_integrity(repo)
	input_fingerprint = compute_rehearsal_input_fingerprint(repo)
	synthetic_commit = ""
	temp_index_file = ""
	cleanup_error = ""
	failure_code = EXIT_OK
	worktree_added = False

	try:
		synthetic_commit, temp_index_file = create_synthetic_snapshot_commit(repo, pre_integrity["head"])
		if not git_object_exists(repo, synthetic_commit):
			failure_code = EXIT_REHEARSAL_GIT
			raise RuntimeError(f"synthetic commit object missing: {synthetic_commit}")
		phases.append({
			"name": "snapshot-synthetic-commit",
			"status": "pass",
			"parentHead": pre_integrity["head"],
			"syntheticCommit": synthetic_commit,
		})

		if args.fetch:
			remote, refspec = args.fetch
			fetch_result = run_command(["git", "-C", str(repo), "fetch", remote, refspec], cwd=repo)
			phases.append({
				"name": "fetch",
				"status": "pass" if fetch_result.returncode == 0 else "fail",
				"command": ["git", "fetch", remote, refspec],
				"stdout": fetch_result.stdout,
				"stderr": fetch_result.stderr,
			})
			if fetch_result.returncode != 0:
				failure_code = EXIT_REHEARSAL_GIT
				raise RuntimeError("fetch failed")
		else:
			phases.append({"name": "fetch", "status": "skipped", "reason": "--no-fetch default"})

		target_sha = resolve_full_sha(repo, target)
		phases.append({"name": "resolve-target", "status": "pass", "target": target, "targetSha": target_sha})

		if args.mode == "rebase":
			add_result = subprocess.CompletedProcess(args=[], returncode=1, stdout="", stderr="")
			for attempt in range(1, 4):
				candidate = f"tg-rehearsal-{token}" if attempt == 1 else f"tg-rehearsal-{token}-{attempt}"
				attempted_rebase_branches.append(candidate)
				add_command = [
					"git",
					"-C",
					str(repo),
					"worktree",
					"add",
					"-b",
					candidate,
					str(worktree),
					synthetic_commit,
				]
				add_result = run_command(add_command, cwd=repo)
				phases.append({
					"name": "worktree-add",
					"attempt": attempt,
					"status": "pass" if add_result.returncode == 0 else "fail",
					"command": add_command,
					"stdout": add_result.stdout,
					"stderr": add_result.stderr,
				})
				if add_result.returncode == 0:
					rebase_branch_name = candidate
					worktree_added = True
					break
				error_text = f"{add_result.stderr}\n{add_result.stdout}".lower()
				if "unable to write symref for head" in error_text or "permission denied" in error_text:
					prune_result = run_command(["git", "-C", str(repo), "worktree", "prune", "--verbose"], cwd=repo)
					phases.append({
						"name": "worktree-prune",
						"attempt": attempt,
						"status": "pass" if prune_result.returncode == 0 else "fail",
						"command": ["git", "worktree", "prune", "--verbose"],
						"stdout": prune_result.stdout,
						"stderr": prune_result.stderr,
					})
					if prune_result.returncode != 0:
						break
					continue
				break
			if not worktree_added:
				failure_code = EXIT_REHEARSAL_GIT
				raise RuntimeError("worktree add failed")
		else:
			add_command = ["git", "-C", str(repo), "worktree", "add", "--detach", str(worktree), synthetic_commit]
			add_result = run_command(add_command, cwd=repo)
			phases.append({
				"name": "worktree-add",
				"status": "pass" if add_result.returncode == 0 else "fail",
				"command": add_command,
				"stdout": add_result.stdout,
				"stderr": add_result.stderr,
			})
			if add_result.returncode != 0:
				failure_code = EXIT_REHEARSAL_GIT
				raise RuntimeError("worktree add failed")
			worktree_added = True

		for rel_path in [args.manifest, args.config]:
			if not (worktree / rel_path).exists():
				failure_code = EXIT_INPUT
				raise RuntimeError(f"required rehearsal file missing in synthetic snapshot: {rel_path}")

		if args.mode == "merge":
			git_action = ["git", "-C", str(worktree), "merge", "--no-commit", "--no-ff", target_sha]
		else:
			phases.append({
				"name": "rebase-branch-create",
				"status": "pass",
				"command": ["git", "worktree", "add", "-b", rebase_branch_name],
				"stdout": "created during worktree-add",
				"stderr": "",
			})
			git_action = ["git", "-C", str(worktree), "rebase", target_sha]

		action_result = run_command(git_action, cwd=worktree)
		native_rebase_result = action_result
		if args.mode == "rebase" and action_result.returncode != 0 and should_trigger_rebase_detach_fallback(action_result.stderr, action_result.stdout):
			fallback_head = synthetic_commit
			if not git_object_exists(worktree, fallback_head):
				phases.append({
					"name": "git-rebase-fallback-verify-synthetic",
					"status": "fail",
					"sourceSyntheticCommit": fallback_head,
					"message": "synthetic commit object not found in worktree object store",
				})
			else:
				phases.append({
					"name": "git-rebase-fallback-verify-synthetic",
					"status": "pass",
					"sourceSyntheticCommit": fallback_head,
				})
				fallback_base = run_git(worktree, ["merge-base", target_sha, fallback_head]).strip()
				fallback_range = f"{fallback_base}..{fallback_head}"
				merge_list_result = run_command(
					["git", "-C", str(worktree), "rev-list", "--reverse", "--merges", fallback_range],
					cwd=worktree,
				)
				if merge_list_result.returncode != 0:
					phases.append({
						"name": "git-rebase-fallback-merge-guard",
						"status": "fail",
						"command": ["git", "rev-list", "--reverse", "--merges", fallback_range],
						"stdout": merge_list_result.stdout,
						"stderr": merge_list_result.stderr,
					})
				else:
					merge_commits = [line.strip() for line in merge_list_result.stdout.splitlines() if line.strip()]
					list_result = run_command(
						["git", "-C", str(worktree), "rev-list", "--reverse", "--no-merges", fallback_range],
						cwd=worktree,
					)
					if list_result.returncode != 0:
						phases.append({
							"name": "git-rebase-fallback-plan",
							"status": "fail",
							"sourceSyntheticCommit": fallback_head,
							"base": fallback_base,
							"range": fallback_range,
							"command": ["git", "rev-list", "--reverse", "--no-merges", fallback_range],
							"stdout": list_result.stdout,
							"stderr": list_result.stderr,
						})
					else:
						fallback_commits = [line.strip() for line in list_result.stdout.splitlines() if line.strip()]
						try:
							fallback_plan = plan_linear_rebase_fallback(
								target_sha=target_sha,
								synthetic_commit=fallback_head,
								fallback_base=fallback_base,
								ordered_commits=fallback_commits,
								merge_commits=merge_commits,
							)
						except ValueError as exc:
							phases.append({
								"name": "git-rebase-fallback-plan",
								"status": "fail",
								"sourceSyntheticCommit": fallback_head,
								"base": fallback_base,
								"range": fallback_range,
								"mergeCommitCount": len(merge_commits),
								"mergeCommits": merge_commits,
								"message": str(exc),
							})
						else:
							phases.append({
								"name": "git-rebase-fallback-plan",
								"status": "pass",
								"semanticMode": str(fallback_plan["semanticMode"]),
								"sourceSyntheticCommit": str(fallback_plan["sourceSyntheticCommit"]),
								"base": str(fallback_plan["base"]),
								"range": str(fallback_plan["range"]),
								"commitCount": int(fallback_plan["commitCount"]),
								"commits": list(fallback_plan["orderedCommits"]),
							})

							abort_result = run_command(["git", "-C", str(worktree), "rebase", "--abort"], cwd=worktree)
							phases.append({
								"name": "git-rebase-fallback-abort-native",
								"status": "pass" if abort_result.returncode == 0 else "fail",
								"bestEffort": True,
								"command": ["git", "rebase", "--abort"],
								"stdout": abort_result.stdout,
								"stderr": abort_result.stderr,
							})

							reset_result = run_command(["git", "-C", str(worktree), "reset", "--hard", target_sha], cwd=worktree)
							phases.append({
								"name": "git-rebase-fallback-reset-target",
								"status": "pass" if reset_result.returncode == 0 else "fail",
								"command": ["git", "reset", "--hard", target_sha],
								"sourceSyntheticCommit": str(fallback_plan["sourceSyntheticCommit"]),
								"base": str(fallback_plan["base"]),
								"range": str(fallback_plan["range"]),
								"stdout": reset_result.stdout,
								"stderr": reset_result.stderr,
							})
							if reset_result.returncode == 0:
								fallback_commit_records: list[dict[str, Any]] = []
								fallback_ok = True
								failed_commit = ""
								for commit in list(fallback_plan["orderedCommits"]):
									pick_result = run_command(
										["git", "-C", str(worktree), "cherry-pick", "--allow-empty", commit],
										cwd=worktree,
									)
									fallback_commit_records.append({
										"commit": commit,
										"exitCode": pick_result.returncode,
									})
									if pick_result.returncode != 0:
										fallback_ok = False
										failed_commit = commit
										abort_pick_result = run_command(["git", "-C", str(worktree), "cherry-pick", "--abort"], cwd=worktree)
										phases.append({
											"name": "git-rebase-fallback-abort-cherry-pick",
											"status": "pass" if abort_pick_result.returncode == 0 else "fail",
											"command": ["git", "cherry-pick", "--abort"],
											"stdout": abort_pick_result.stdout,
											"stderr": abort_pick_result.stderr,
										})
										break
								if fallback_ok:
									action_result = subprocess.CompletedProcess(
										args=git_action,
										returncode=0,
										stdout=(native_rebase_result.stdout or "") + "\nrebase fallback applied via reset/cherry-pick",
										stderr=native_rebase_result.stderr,
									)
									semantic_mode = str(fallback_plan["semanticMode"])
									phases.append({
										"name": "git-rebase-fallback-replay",
										"status": "pass",
										"semanticMode": semantic_mode,
										"sourceSyntheticCommit": str(fallback_plan["sourceSyntheticCommit"]),
										"base": str(fallback_plan["base"]),
										"range": str(fallback_plan["range"]),
										"commitCount": int(fallback_plan["commitCount"]),
										"commits": fallback_commit_records,
									})
								else:
									phases.append({
										"name": "git-rebase-fallback-replay",
										"status": "fail",
										"semanticMode": str(fallback_plan["semanticMode"]),
										"sourceSyntheticCommit": str(fallback_plan["sourceSyntheticCommit"]),
										"base": str(fallback_plan["base"]),
										"range": str(fallback_plan["range"]),
										"failedCommit": failed_commit,
										"commitCount": int(fallback_plan["commitCount"]),
										"commits": fallback_commit_records,
									})
		if args.mode == "rebase":
			phases.append({
				"name": "git-rebase-native-attempt",
				"status": "pass" if native_rebase_result.returncode == 0 else "fail",
				"command": git_action,
				"stdout": native_rebase_result.stdout,
				"stderr": native_rebase_result.stderr,
			})
		phases.append({
			"name": f"git-{args.mode}",
			"status": "pass" if action_result.returncode == 0 else "fail",
			"command": git_action,
			"stdout": action_result.stdout,
			"stderr": action_result.stderr,
		})
		if action_result.returncode != 0:
			failure_code = EXIT_REHEARSAL_GIT
			raise RuntimeError("merge/rebase rehearsal failed")

		temp_base = target_sha
		phases.append({
			"name": "rehearsal-base",
			"status": "pass",
			"committedBase": manifest_base,
			"temporaryBase": temp_base,
		})

		overlay_manifest, overlay_meta = build_rehearsal_manifest_overlay(worktree, manifest)
		phases.append({
			"name": "manifest-overlay-refresh",
			"status": "pass",
			"key": "file:id",
			"movedFenceCount": int(overlay_meta["movedFenceCount"]),
			"movedSpanLineDelta": int(overlay_meta["movedSpanLineDelta"]),
			"missingFenceCount": int(overlay_meta["missingFenceCount"]),
			"unknownFenceCount": int(overlay_meta["unknownFenceCount"]),
			"movedFences": overlay_meta["movedFences"],
		})

		manifest_scope_files = {
			normalize_path(str(fence["file"]))
			for fence in overlay_manifest["fences"]
		}
		validate_code, validate_summary = validate_with_summary(
			worktree,
			target_sha,
			bool(args.verbose),
			scope_files=manifest_scope_files,
		)
		validate_summary["scopeMode"] = "manifest-files-vs-target"
		phases.append({"name": "fence-validate", "status": "pass" if validate_code == 0 else "fail", "summary": validate_summary})
		if validate_code != 0:
			if failure_code == EXIT_OK:
				failure_code = EXIT_VALIDATE

		quality_args = argparse.Namespace(
			warn_fence_lines=args.warn_fence_lines,
			max_fence_lines=args.max_fence_lines,
			warn_min_change_density=args.warn_min_change_density,
			min_change_density=args.min_change_density,
			warn_stale_days=args.warn_stale_days,
			max_stale_days=args.max_stale_days,
		)
		quality_result = quality_mode(
			worktree,
			temp_base,
			worktree / args.manifest,
			quality_args,
			allow_base_mismatch=True,
			manifest_override=overlay_manifest,
		)
		phases.append({"name": "fence-quality", "status": "pass" if quality_result.exit_code == 0 else "fail", "summary": quality_result.summary})
		if quality_result.exit_code != 0:
			if failure_code == EXIT_OK:
				failure_code = quality_result.exit_code

		inventory_result = inventory_operation(
			repo=worktree,
			base=temp_base,
			manifest_path=worktree / args.manifest,
			generate=False,
			update=False,
			check=True,
			allow_inventory_base_mismatch=True,
			manifest_override=overlay_manifest,
		)
		phases.append({"name": "fence-inventory-check", "status": "pass" if inventory_result.exit_code == 0 else "fail", "summary": inventory_result.summary})
		if inventory_result.exit_code != 0:
			if failure_code == EXIT_OK:
				failure_code = inventory_result.exit_code

		primary_cache = repo / str(config["primaryBuildDirectory"]) / "CMakeCache.txt"
		cache_values = load_cache_values(primary_cache, list(config["cacheImportKeys"]))
		sensitive_values = [cache_values.get(key, "") for key in config["sensitiveKeys"]]

		disk = shutil.disk_usage(worktree)
		phases.append({
			"name": "disk-space",
			"status": "pass",
			"freeBytes": int(disk.free),
			"freeGiB": round(float(disk.free) / float(1024 ** 3), 2),
			"strategy": "single-rehearsal-build-per-invocation",
		})

		required_submodules = list(REHEARSAL_REQUIRED_SUBMODULES)
		declared_submodules = declared_submodule_paths(worktree)
		submodule_blockers: list[str] = []
		local_sources, local_source_blockers = prepare_local_submodule_sources(repo, worktree, declared_submodules)
		phases.append({
			"name": "submodule-local-sources",
			"status": "pass" if not local_source_blockers else "skipped-prerequisite",
			"protocolPolicy": "file-only",
			"declaredTopLevelCount": len(declared_submodules),
			"sources": local_sources,
			"missing": local_source_blockers,
		})
		if local_source_blockers:
			submodule_blockers.append("required-submodules-unavailable-primary-worktree")
			submodule_update = subprocess.CompletedProcess(args=[], returncode=1, stdout="", stderr="local submodule prerequisites unavailable")
		else:
			submodule_command = ["git", "-C", str(worktree), "-c", "protocol.file.allow=always"]
			for source in local_sources:
				submodule_command.extend(["-c", f"submodule.{source['name']}.url={source['source']}"])
			submodule_command.extend([
				"submodule", "update", "--init", "--no-fetch", "--",
				*[str(source["path"]) for source in local_sources],
			])
			submodule_update = run_command(
				submodule_command,
				cwd=worktree,
				extra_env={"GIT_ALLOW_PROTOCOL": "file"},
			)
		if submodule_update.returncode != 0:
			reason = classify_submodule_init_failure(submodule_update.stderr, submodule_update.stdout)
			if local_source_blockers:
				reason = "required-submodules-unavailable-primary-worktree"
			status = "skipped-prerequisite" if local_source_blockers or reason == "required-objects-unavailable-local-no-fetch" else "fail"
			phases.append({
				"name": "submodule-init",
				"status": status,
				"reason": reason,
				"command": submodule_command if not local_source_blockers else ["git", "submodule", "update"],
				"protocolPolicy": "GIT_ALLOW_PROTOCOL=file",
				"stdout": submodule_update.stdout,
				"stderr": submodule_update.stderr,
			})
			submodule_blockers.append(reason)
		else:
			phases.append({
				"name": "submodule-init",
				"status": "pass",
				"command": submodule_command,
				"protocolPolicy": "GIT_ALLOW_PROTOCOL=file",
				"stdout": submodule_update.stdout,
				"stderr": submodule_update.stderr,
			})
			missing_submodules = verify_required_submodules_populated(worktree, required_submodules)
			if missing_submodules:
				phases.append({
					"name": "submodule-verify",
					"status": "skipped-prerequisite",
					"required": required_submodules,
					"missing": missing_submodules,
				})
				submodule_blockers.append("required-submodules-missing")
			else:
				phases.append({
					"name": "submodule-verify",
					"status": "pass",
					"requiredCount": len(required_submodules),
					"required": required_submodules,
				})

		required_for_configure = ["QT_DIR", "CMAKE_GENERATOR", "CMAKE_GENERATOR_PLATFORM", "CMAKE_GENERATOR_TOOLSET"]
		missing_required = [key for key in required_for_configure if not cache_values.get(key)]
		if missing_required or submodule_blockers:
			missing_payload: dict[str, Any] = {}
			if missing_required:
				missing_payload["cacheKeys"] = missing_required
			if submodule_blockers:
				missing_payload["submodules"] = submodule_blockers
			phases.append({"name": "configure", "status": "skipped-prerequisite", "missing": missing_payload})
			for command_obj in config["commands"]:
				phases.append({"name": str(command_obj.get("name", "unnamed")), "status": "skipped-prerequisite", "missing": missing_payload})
			if failure_code == EXIT_OK:
				failure_code = EXIT_REHEARSAL_COMMAND
		else:
			build_dir.mkdir(parents=True, exist_ok=True)
			if worktree not in build_dir.parents and build_dir != worktree:
				failure_code = EXIT_INPUT
				raise RuntimeError("build directory must be inside disposable worktree")

			context = {
				"repo": str(repo),
				"repo_parent": str(repo.parent),
				"worktree": str(worktree),
				"build": str(build_dir),
				"python": sys.executable,
				"configuration": "Debug",
				"generator": cache_values.get("CMAKE_GENERATOR", ""),
				"platform": cache_values.get("CMAKE_GENERATOR_PLATFORM", ""),
				"toolset": cache_values.get("CMAKE_GENERATOR_TOOLSET", ""),
			}

			configure_tokens = resolve_command_tokens(list(config["configureCommand"]), context)
			configure_tokens.extend(["-DBUILD_TG_CLI=ON"])
			configure_tokens.extend(["-DQT_DIR=" + cache_values.get("QT_DIR", "")])
			for key in ["TDESKTOP_API_ID", "TDESKTOP_API_HASH"]:
				if cache_values.get(key):
					configure_tokens.append(f"-D{key}=" + cache_values[key])

			command_env: dict[str, str] = {}
			qt_dir_value = str(cache_values.get("QT_DIR", "")).strip()
			qt_match = re.search(r"Qt[-_]?([0-9]+(?:\.[0-9]+)*)", qt_dir_value, re.IGNORECASE)
			if qt_match:
				command_env["QT"] = qt_match.group(1)
			command_env.update(resolve_environment_map(config.get("environment", {}), context))
			sensitive_key_set = {str(key) for key in config.get("sensitiveKeys", [])}
			for key, value in command_env.items():
				if key in sensitive_key_set:
					sensitive_values.append(value)

			configure_result = run_command(configure_tokens, cwd=worktree, extra_env=command_env)
			phases.append({
				"name": "configure",
				"status": "pass" if configure_result.returncode == 0 else "fail",
				"command": configure_tokens,
				"env": {"QT": command_env.get("QT", "")},
				"stdout": configure_result.stdout,
				"stderr": configure_result.stderr,
			})
			if configure_result.returncode != 0:
				failure_code = EXIT_REHEARSAL_COMMAND
				raise RuntimeError("configure failed")

			optional_prereq = config.get("optionalPrerequisites", {})
			already_executed = {"fence-validate", "fence-quality", "inventory-check"}
			for command_obj in config["commands"]:
				name = str(command_obj.get("name", "unnamed"))
				if name in already_executed:
					phases.append({"name": name, "status": "skipped", "reason": "already-executed-by-rehearsal-core"})
					continue
				tokens = command_obj.get("tokens")
				if not isinstance(tokens, list) or not all(isinstance(x, str) for x in tokens):
					failure_code = EXIT_INPUT
					raise RuntimeError(f"command '{name}' tokens must be string array")

				prereq_path = optional_prereq.get(name)
				if isinstance(prereq_path, str):
					resolved_prereq = resolve_command_tokens([prereq_path], context)[0]
					if not pathlib.Path(resolved_prereq).exists():
						phases.append({"name": name, "status": "skipped-prerequisite", "missing": resolved_prereq})
						continue

				resolved_tokens = resolve_command_tokens(tokens, context)
				per_command_env = dict(command_env)
				per_command_env.update(resolve_environment_map(command_obj.get("environment", {}), context))
				for key, value in per_command_env.items():
					if key in sensitive_key_set:
						sensitive_values.append(value)
				result = run_command(resolved_tokens, cwd=worktree, extra_env=per_command_env)
				phases.append({
					"name": name,
					"status": "pass" if result.returncode == 0 else "fail",
					"command": resolved_tokens,
					"env": per_command_env,
					"stdout": result.stdout,
					"stderr": result.stderr,
					"exitCode": result.returncode,
				})
				if result.returncode != 0:
					failure_code = EXIT_REHEARSAL_COMMAND
					raise RuntimeError(f"configured command failed: {name}")

	except RuntimeError as exc:
		if failure_code == EXIT_OK:
			failure_code = EXIT_INTERNAL
		phases.append({"name": "error", "status": "fail", "message": str(exc)})
	finally:
		if temp_index_file:
			try:
				os.remove(temp_index_file)
			except OSError:
				pass

		registered_before, registration_probe_error = worktree_is_registered(repo, worktree)
		directory_before = os.path.lexists(windows_extended_path(worktree))
		cleanup_needed = worktree_cleanup_needed(
			worktree_added,
			registered_before,
			directory_before,
			registration_probe_error,
		)
		if cleanup_needed and not (args.keep_failure_worktree and failure_code != EXIT_OK):
			remove_result = run_command(["git", "-C", str(repo), "worktree", "remove", "--force", str(worktree)], cwd=repo)
			fallback_error = ""
			if remove_result.returncode != 0 and os.path.lexists(windows_extended_path(worktree)):
				fallback_error = remove_directory_tree(worktree)
				phases.append({
					"name": "worktree-filesystem-remove",
					"status": "pass" if not fallback_error else "fail",
					"error": fallback_error,
				})
			if remove_result.returncode != 0:
				prune_result = run_command(["git", "-C", str(repo), "worktree", "prune", "--verbose"], cwd=repo)
				phases.append({
					"name": "worktree-prune",
					"status": "pass" if prune_result.returncode == 0 else "fail",
					"command": ["git", "worktree", "prune", "--verbose"],
					"stdout": prune_result.stdout,
					"stderr": prune_result.stderr,
				})
			registered, registration_error = worktree_is_registered(repo, worktree)
			directory_exists = os.path.lexists(windows_extended_path(worktree))
			cleanup_ok = not fallback_error and not registered and not directory_exists and not registration_error
			phases.append({
				"name": "worktree-remove",
				"status": "pass" if cleanup_ok else "fail",
				"command": ["git", "worktree", "remove", "--force", str(worktree)],
				"stdout": remove_result.stdout,
				"stderr": remove_result.stderr,
				"gitExitCode": remove_result.returncode,
				"registered": registered,
				"directoryExists": directory_exists,
				"registrationError": registration_error,
			})
			if not cleanup_ok:
				cleanup_error = fallback_error or registration_error or (remove_result.stderr or remove_result.stdout).strip()
				if failure_code == EXIT_OK:
					failure_code = EXIT_CLEANUP

		for branch_name in dict.fromkeys(attempted_rebase_branches):
			branch_probe = run_command(
				["git", "-C", str(repo), "show-ref", "--verify", "--quiet", f"refs/heads/{branch_name}"],
				cwd=repo,
			)
			if branch_probe.returncode != 0:
				continue
			if args.keep_failure_worktree and failure_code != EXIT_OK:
				retained_failure_refs.append(branch_name)
				phases.append({
					"name": "rebase-branch-retained",
					"status": "kept",
					"ref": branch_name,
				})
			else:
				delete_branch = run_command(["git", "-C", str(repo), "branch", "-D", branch_name], cwd=repo)
				phases.append({
					"name": "rebase-branch-delete",
					"status": "pass" if delete_branch.returncode == 0 else "fail",
					"command": ["git", "branch", "-D", branch_name],
					"stdout": delete_branch.stdout,
					"stderr": delete_branch.stderr,
				})
				if delete_branch.returncode != 0:
					cleanup_error = (delete_branch.stderr or delete_branch.stdout).strip()
					if failure_code == EXIT_OK:
						failure_code = EXIT_CLEANUP

	post_integrity = snapshot_repo_integrity(repo)
	integrity_ok = pre_integrity == post_integrity
	if not integrity_ok:
		if failure_code == EXIT_OK:
			failure_code = EXIT_CLEANUP
		phases.append({
			"name": "primary-integrity",
			"status": "fail",
			"expected": pre_integrity,
			"actual": post_integrity,
		})
	else:
		phases.append({
			"name": "primary-integrity",
			"status": "pass",
			"head": post_integrity["head"],
			"indexTree": post_integrity["indexTree"],
		})

	phases = sanitize_report_value(phases, repo, worktree, build_dir, sensitive_values)

	summary: dict[str, Any] = {
		"target": target,
		"targetSha": target_sha if 'target_sha' in locals() else "",
		"mode": args.mode,
		"semanticMode": semantic_mode,
		"manifest": normalize_path(str(args.manifest)),
		"config": normalize_path(str(args.config)),
		"syntheticCommit": synthetic_commit,
		"inputFingerprint": input_fingerprint,
		"worktree": apply_path_tokens(str(worktree), repo, worktree, build_dir),
		"buildRoot": apply_path_tokens(str(build_dir), repo, worktree, build_dir),
		"committedBase": manifest_base,
		"temporaryBase": target_sha if 'target_sha' in locals() else "",
		"primaryIntegrity": {
			"headUnchanged": pre_integrity["head"] == post_integrity["head"],
			"indexTreeUnchanged": pre_integrity["indexTree"] == post_integrity["indexTree"],
			"statusUnchanged": pre_integrity["status"] == post_integrity["status"],
			"localConfigUnchanged": pre_integrity["localConfigHash"] == post_integrity["localConfigHash"],
		},
		"phases": phases,
	}
	if retained_failure_refs:
		summary["retainedFailureRefs"] = retained_failure_refs
	if cleanup_error:
		summary["cleanupError"] = apply_path_tokens(redact_sensitive(cleanup_error, sensitive_values), repo, worktree, build_dir)
	summary = sanitize_report_value(summary, repo, worktree, build_dir, sensitive_values)

	if failure_code == EXIT_OK:
		print("PASS: rehearsal completed without mutating the current branch workspace.")
	else:
		print(f"FAIL: rehearsal failed with exit code {failure_code}.")

	return CheckerResult(exit_code=failure_code, mode=MODE_REHEARSAL, summary=summary)


def render_report_summary(input_path: pathlib.Path) -> CheckerResult:
	loaded = parse_json_file(input_path)
	mode = str(loaded.get("mode", "unknown"))
	exit_code = int(loaded.get("exitCode", 0))
	summary = loaded.get("summary", {})

	print(f"report-mode={mode}")
	print(f"report-exit-code={exit_code}")
	if isinstance(summary, dict):
		for key in sorted(summary.keys()):
			value = summary[key]
			if isinstance(value, (str, int, float, bool)):
				print(f"{key}={value}")

	return CheckerResult(exit_code=EXIT_OK, mode=MODE_REPORT, summary={
		"input": path_for_report(input_path, input_path.parent),
		"mode": mode,
		"sourceExitCode": exit_code,
	})


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
	if hunks[0].deletion_anchors != [10]:
		print("SELFTEST FAIL: expected deletion anchor for mixed hunk")
		return 1

	valid_replacement_block = [MarkerBlock(marker_id="replace", begin_line=9, end_line=12)]
	if not deletion_anchor_overlaps_block(10, valid_replacement_block):
		print("SELFTEST FAIL: expected valid fenced replacement overlap")
		return 1

	mixed_outside_block = [MarkerBlock(marker_id="replace", begin_line=20, end_line=24)]
	if deletion_anchor_overlaps_block(10, mixed_outside_block):
		print("SELFTEST FAIL: expected mixed-hunk deletion outside fence to fail")
		return 1

	adjacent = """
# TG_CHANGE_BEGIN: first
one = 1
# TG_CHANGE_END: first
# TG_CHANGE_BEGIN: second
two = 2
# TG_CHANGE_END: second
""".strip("\n")
	adjacent_blocks, adjacent_violations, adjacent_marker_lines = parse_markers("adjacent.py", adjacent)
	if adjacent_violations:
		print("SELFTEST FAIL: adjacent blocks should parse cleanly")
		return 1
	if not line_in_blocks(2, adjacent_blocks, adjacent_marker_lines) or not line_in_blocks(5, adjacent_blocks, adjacent_marker_lines):
		print("SELFTEST FAIL: adjacent block contents should be inside fences")
		return 1

	duplicate = """
# TG_CHANGE_BEGIN: dup
one = 1
# TG_CHANGE_END: dup
# TG_CHANGE_BEGIN: dup
two = 2
# TG_CHANGE_END: dup
""".strip("\n")
	_, duplicate_violations, _ = parse_markers("duplicate.py", duplicate)
	if not any("duplicate marker id" in issue.message for issue in duplicate_violations):
		print("SELFTEST FAIL: expected duplicate marker ID violation")
		return 1

	manifest = {
		"schemaVersion": 1,
		"repoPolicy": "TG_CHANGE_POLICY.md",
		"generatedAtUtc": "2026-08-07T00:00:00Z",
		"baseRevision": "0" * 40,
		"activePacket": "PLAN_packet.md",
		"fences": [{
			"id": "sample",
			"file": "Telegram/SourceFiles/core/launcher.cpp",
			"beginLine": 10,
			"endLine": 20,
			"ownerPacket": "PLAN_packet.md",
			"intent": "sample",
			"tests": ["x"],
			"removalCondition": "none",
			"status": "active",
			"exceptionRationale": "",
			"rationaleReviewedAtUtc": "",
			"groupIds": [],
			"lastTouchedCommit": "",
		}],
		"groups": [],
	}
	if not validate_manifest_schema(manifest):
		print("SELFTEST FAIL: zero baseRevision should be rejected")
		return 1

	manifest["baseRevision"] = "1" * 40
	if validate_manifest_schema(manifest):
		print("SELFTEST FAIL: valid manifest schema was rejected")
		return 1

	invalid_manifest = dict(manifest)
	invalid_manifest["baseRevision"] = "bad"
	if not validate_manifest_schema(invalid_manifest):
		print("SELFTEST FAIL: invalid manifest schema should fail")
		return 1

	hunks_for_density = parse_hunks(textwrap.dedent("""
	@@ -1,2 +1,3 @@
	-a
	+b
	 c
	+d
	""").strip("\n"))
	density, numerator, denominator = compute_changed_density_for_fence({"beginLine": 1, "endLine": 12}, hunks_for_density)
	if denominator != 10 or numerator <= 0 or density <= 0:
		print("SELFTEST FAIL: changed-line density computation invalid")
		return 1

	marker_only_hunks = parse_hunks(textwrap.dedent("""
	@@ -1 +1 @@
	-# TG_CHANGE_BEGIN: old
	+# TG_CHANGE_BEGIN: new
	@@ -12 +12 @@
	-# TG_CHANGE_END: old
	+# TG_CHANGE_END: new
	""").strip("\n"))
	_, marker_numerator, _ = compute_changed_density_for_fence({"beginLine": 1, "endLine": 12}, marker_only_hunks)
	if marker_numerator != 0:
		print("SELFTEST FAIL: marker-only edits must not contribute changed-line density")
		return 1

	test_repo = pathlib.Path(r"C:\Users\Example User\repo")
	test_worktree = pathlib.Path(r"C:\Users\Example User\.tg-rehearsal-test")
	test_build = test_worktree / "out-rehearsal"
	tokenized = apply_path_tokens(
		r"C:\Users\Example User\repo\file.cpp C:/Users/Example User/.tg-rehearsal-test/out-rehearsal/x.obj",
		test_repo,
		test_worktree,
		test_build,
	)
	if "${REPO}/file.cpp" not in tokenized or "${BUILD}/x.obj" not in tokenized or "${REPO}/../repo" in tokenized:
		print("SELFTEST FAIL: path token replacement order is incorrect")
		return 1
	redacted = redact_absolute_paths('error: "C:\\Users\\Example User\\Secret Folder\\file.txt"')
	if "Example User" in redacted or "Secret Folder" in redacted or "${ABS_PATH}" not in redacted:
		print("SELFTEST FAIL: spaced Windows path was not fully redacted")
		return 1

	try:
		resolve_command_tokens(["{unknown}"], {"repo": "x"})
		print("SELFTEST FAIL: unknown placeholder should fail")
		return 1
	except ValueError:
		pass

	with tempfile.TemporaryDirectory(prefix="tg-fence-cleanup-selftest-") as cleanup_root:
		cleanup_path = pathlib.Path(cleanup_root) / ("nested-" + "x" * 80) / ("leaf-" + "y" * 80)
		cleanup_path.mkdir(parents=True)
		readonly_file = cleanup_path / "readonly.txt"
		readonly_file.write_text("cleanup", encoding="utf-8")
		readonly_file.chmod(stat.S_IREAD)
		cleanup_error = remove_directory_tree(pathlib.Path(cleanup_root) / ("nested-" + "x" * 80))
		if cleanup_error or cleanup_path.exists():
			print(f"SELFTEST FAIL: directory cleanup fallback failed: {cleanup_error}")
			return 1
	if worktree_cleanup_needed(False, False, False, ""):
		print("SELFTEST FAIL: absent worktree should not require cleanup")
		return 1
	if not worktree_cleanup_needed(False, True, False, "") or not worktree_cleanup_needed(False, False, True, ""):
		print("SELFTEST FAIL: partial worktree state must require cleanup")
		return 1
	gitlink_staged = "160000 " + "1" * 40 + " 0\tTelegram/lib_base"
	if gitlink_fingerprint_bytes(gitlink_staged, "2" * 40) == gitlink_fingerprint_bytes(gitlink_staged, "3" * 40):
		print("SELFTEST FAIL: gitlink fingerprint must include checked-out HEAD")
		return 1

	stderr_sample = "fatal: update_ref failed for ref 'HEAD': couldn't set 'HEAD'\nerror: could not detach HEAD\n"
	if not should_trigger_rebase_detach_fallback(stderr_sample, ""):
		print("SELFTEST FAIL: expected fallback trigger for current detach-head stderr")
		return 1

	stderr_detach_only = "fatal: random rebase failure\nerror: Could Not Detach Head\n"
	if not should_trigger_rebase_detach_fallback(stderr_detach_only, ""):
		print("SELFTEST FAIL: fallback trigger should only require could not detach head text")
		return 1

	fallback_plan = plan_linear_rebase_fallback(
		target_sha="1" * 40,
		synthetic_commit="2" * 40,
		fallback_base="3" * 40,
		ordered_commits=["4" * 40, "5" * 40],
		merge_commits=[],
	)
	if fallback_plan["sourceSyntheticCommit"] != "2" * 40:
		print("SELFTEST FAIL: fallback source must remain synthetic commit")
		return 1
	if fallback_plan["semanticMode"] != "reset-cherry-pick-linear-rebase":
		print("SELFTEST FAIL: fallback semanticMode mismatch")
		return 1

	try:
		plan_linear_rebase_fallback(
			target_sha="1" * 40,
			synthetic_commit="2" * 40,
			fallback_base="3" * 40,
			ordered_commits=["4" * 40],
			merge_commits=["6" * 40],
		)
		print("SELFTEST FAIL: merge-commit guard should fail fallback planning")
		return 1
	except ValueError:
		pass

	print("SELFTEST PASS: valid and invalid fence cases behaved as expected.")
	return 0


def main() -> int:
	parser = argparse.ArgumentParser(description="TG_CHANGE fence checker and packet-31 quality/rehearsal toolchain.")
	parser.add_argument("mode", nargs="?", choices=sorted(KNOWN_MODES), help="Mode: validate (default), quality, inventory, rehearsal, report.")
	parser.add_argument("--repo", default=".", help="Repository root path.")
	parser.add_argument("--base", default="HEAD", help="Base revision for diff comparison. Legacy default remains HEAD.")
	parser.add_argument("--verbose", action="store_true", help="Print skipped files and extra details.")
	parser.add_argument("--self-test", action="store_true", help="Run built-in parser tests.")
	parser.add_argument("--json-report", default="", help="Write deterministic JSON report to path.")
	parser.add_argument("--manifest", default=DEFAULT_MANIFEST, help="Fence manifest path.")
	parser.add_argument("--config", default=DEFAULT_CONFIG, help="Rehearsal config path.")
	parser.add_argument("--warn-fence-lines", type=int, default=40)
	parser.add_argument("--max-fence-lines", type=int, default=80)
	parser.add_argument("--warn-min-change-density", type=float, default=0.50)
	parser.add_argument("--min-change-density", type=float, default=0.20)
	parser.add_argument("--warn-stale-days", type=int, default=180)
	parser.add_argument("--max-stale-days", type=int, default=365)
	parser.add_argument("--generate", action="store_true", help="inventory mode: generate manifest")
	parser.add_argument("--update", action="store_true", help="inventory mode: update manifest")
	parser.add_argument("--check", action="store_true", help="inventory mode: check manifest")
	parser.add_argument("--target", default="", help="rehearsal target ref")
	parser.add_argument("--mode", dest="rehearsal_mode", choices=["merge", "rebase"], default="merge", help="rehearsal git mode")
	parser.add_argument("--no-fetch", action="store_true", help="rehearsal: local refs only (default)")
	parser.add_argument("--fetch", nargs=2, metavar=("REMOTE", "REFSPEC"), help="rehearsal: explicit fetch remote refspec")
	parser.add_argument("--keep-failure-worktree", action="store_true", help="rehearsal: keep disposable worktree on failure")
	parser.add_argument("--input-report", default="", help="report mode: input JSON report path")
	parser.add_argument("--print-input-fingerprint", action="store_true", help="Print the current rehearsal input fingerprint and exit.")
	args = parser.parse_args()

	if args.self_test:
		return run_self_test()

	repo = pathlib.Path(args.repo).resolve()
	if args.print_input_fingerprint:
		print(compute_rehearsal_input_fingerprint(repo))
		return EXIT_OK
	mode = args.mode or MODE_VALIDATE

	result: CheckerResult
	if mode == MODE_VALIDATE:
		exit_code, summary = validate_with_summary(repo=repo, base=args.base, verbose=bool(args.verbose))
		result = CheckerResult(exit_code=exit_code, mode=MODE_VALIDATE, summary=summary)
	elif mode == MODE_QUALITY:
		result = quality_mode(repo=repo, base=args.base, manifest_path=repo / args.manifest, args=args)
	elif mode == MODE_INVENTORY:
		result = inventory_operation(
			repo=repo,
			base=args.base,
			manifest_path=repo / args.manifest,
			generate=bool(args.generate),
			update=bool(args.update),
			check=bool(args.check),
			allow_inventory_base_mismatch=False,
		)
	elif mode == MODE_REHEARSAL:
		args.mode = args.rehearsal_mode
		if args.fetch and args.no_fetch:
			message = "rehearsal fetch policy is mutually exclusive: use either --no-fetch or --fetch"
			print(f"ERROR: {message}.")
			result = CheckerResult(EXIT_INPUT, MODE_REHEARSAL, {"error": message})
		else:
			if not args.fetch:
				args.no_fetch = True
			result = run_rehearsal(repo=repo, args=args)
	elif mode == MODE_REPORT:
		if not args.input_report:
			print("ERROR: report mode requires --input-report <path>")
			return EXIT_INPUT
		result = render_report_summary(repo / args.input_report)
	else:
		print(f"ERROR: unknown mode '{mode}'")
		return EXIT_INPUT

	if args.json_report:
		report_worktree = repo.parent / ".tg-rehearsal-report"
		report_build = report_worktree / "out-rehearsal"
		report_payload = {
			"schemaVersion": 1,
			"generatedAtUtc": utc_now_iso(),
			"mode": result.mode,
			"exitCode": result.exit_code,
			"summary": sanitize_report_value(result.summary, repo, report_worktree, report_build, []),
		}
		deterministic_write_json(repo / args.json_report, report_payload)

	return result.exit_code


if __name__ == "__main__":
	sys.exit(main())
