#!/usr/bin/env python3
"""Compares src/json_parser.cpp with Python's json on random objects: python3 test/fuzz/json_fuzz.py [count] [seed]"""
import json
import os
import random
import subprocess
import sys
import tempfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..")
ALPHABET = 'abcXYZ019 .-_/\\"\n\tä€\U0001F600'
TYPES = {str: 0, float: 1, int: 1, bool: 2, type(None): 3}


def text(r, n):
    return "".join(r.choice(ALPHABET) for _ in range(r.randint(0, n)))


def key(r):
    return "".join(r.choice("abcXYZ019_-") for _ in range(r.randint(1, 8)))


def value(r, depth):
    kind = r.randint(0, 5 if depth < 3 else 4)
    if kind == 0:
        return r.randint(-100000, 100000)
    if kind == 1:
        return round(r.uniform(-1000, 1000), 3)
    if kind == 2:
        return r.choice([True, False])
    if kind == 3:
        return None
    if kind == 4:
        return text(r, 20)
    return obj(r, depth + 1)


def obj(r, depth=0):
    return {key(r): value(r, depth) for _ in range(r.randint(0 if depth else 1, 5))}


def flatten(o, prefix=""):
    for k, v in o.items():
        name = prefix + k
        if isinstance(v, dict):
            yield from flatten(v, name + ".")
        else:
            yield name, v


def expected(v):
    if v is None:
        return ""
    if v is True:
        return "true"
    if v is False:
        return "false"
    return str(v) if isinstance(v, str) else None  # numbers: compared by value below


def main():
    count = int(sys.argv[1]) if len(sys.argv) > 1 else 2000
    seed = int(sys.argv[2]) if len(sys.argv) > 2 else 1
    r = random.Random(seed)
    objects = [obj(r) for _ in range(count)]
    texts = [json.dumps(o, ensure_ascii=r.choice([True, False])) for o in objects]

    with tempfile.TemporaryDirectory() as tmp:
        exe = os.path.join(tmp, "json_fuzz")
        subprocess.check_call(["c++", "-std=gnu++11", "-I" + os.path.join(ROOT, "include"),
                               os.path.join(ROOT, "test", "fuzz", "json_fuzz_main.cpp"),
                               os.path.join(ROOT, "src", "json_parser.cpp"), "-o", exe])
        out = subprocess.run([exe], input=("\n".join(texts) + "\n").encode(), capture_output=True, check=True).stdout

    lines = iter(out.decode().split("\n"))
    for o, t in zip(objects, texts):
        got = []
        for line in lines:
            if line in ("OK", "FAIL"):
                break
            kind, k, v = line.split("\t")
            got.append((int(kind), bytes.fromhex(k).decode(), bytes.fromhex(v).decode()))
        want = list(flatten(o))
        if line != "OK" or len(got) != len(want):
            sys.exit("mismatch (%s, %d values): %s" % (line, len(got), t))
        for (kind, k, v), (wk, wv) in zip(got, want):
            same = kind == TYPES[type(wv)] and k == wk
            same = same and (float(v) == wv if isinstance(wv, (int, float)) and not isinstance(wv, bool) else v == expected(wv))
            if not same:
                sys.exit("mismatch at %s: %s" % (wk, t))
    print("%d objects match (seed %d)" % (count, seed))


if __name__ == "__main__":
    main()
