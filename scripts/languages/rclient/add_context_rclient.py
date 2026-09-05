#!/usr/bin/env python3
import os
import re
import sys

CONTEXT = "RClient"

RE_LOCALIZE = re.compile(r'RCLocalize\("((?:\\.|[^"\\])*)"\s*\)')
RE_LOCALIZABLE = re.compile(r'RCLocalizable\("((?:\\.|[^"\\])*)"\s*\)')


def main(argv):
	os.chdir(os.path.dirname(__file__) + "/../..")

	total = 0
	for path2, dirs, files in os.walk("../src"):
		dirs.sort()
		for f in sorted(files):
			if not any(f.endswith(x) for x in [".cpp", ".c", ".h"]):
				continue
			path = os.path.join(path2, f)
			with open(path, "rb") as fh:
				raw = fh.read()
			bom = raw.startswith(b"\xef\xbb\xbf")
			content = raw.decode("utf-8-sig")
			new = RE_LOCALIZE.sub(r'RCLocalize("\1", "' + CONTEXT + r'")', content)
			if f == "bindchat.cpp":
				new = RE_LOCALIZABLE.sub(r'RCLocalizable("\1", "' + CONTEXT + r'")', new)
			if new != content:
				out = new.encode("utf-8")
				if bom:
					out = b"\xef\xbb\xbf" + out
				with open(path, "wb") as fh:
					fh.write(out)
				print(path)
				total += 1
	print(f"Successfully updated {total} files.")


if __name__ == "__main__":
	main(sys.argv)
