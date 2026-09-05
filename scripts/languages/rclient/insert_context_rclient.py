#!/usr/bin/env python3
import os
import sys

import twlang_rclient as twlang

CONTEXT = "RClient"


def main(argv):
	os.chdir(os.path.dirname(__file__) + "/../..")

	for lang in twlang.languages():
		with open(lang, encoding="utf-8", newline="") as f:
			lines = f.readlines()
		eol = "\r\n" if lines and lines[0].endswith("\r\n") else "\n"

		out = []
		pending = False
		inserted = 0
		for line in lines:
			if not line.strip() or line.startswith("#"):
				out.append(line)
				pending = False
				continue
			if line.startswith("["):
				out.append(line)
				pending = True
				continue
			if line.startswith("== "):
				out.append(line)
				continue
			if not pending:
				out.append("[" + CONTEXT + "]" + eol)
				inserted += 1
			out.append(line)
			pending = False

		if inserted:
			with open(lang, "w", encoding="utf-8", newline="") as f:
				f.writelines(out)
			print(f"{lang}: inserted [{CONTEXT}] x{inserted}")


if __name__ == "__main__":
	main(sys.argv)
