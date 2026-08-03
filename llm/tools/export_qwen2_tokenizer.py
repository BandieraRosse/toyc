#!/usr/bin/env python3
"""Export Qwen2 tokenizer.json vocabulary as raw token byte pieces."""

import argparse
import json
import struct
from pathlib import Path


def bytes_to_unicode():
    values = list(range(ord("!"), ord("~") + 1))
    values += list(range(ord("¡"), ord("¬") + 1))
    values += list(range(ord("®"), ord("ÿ") + 1))
    encoded = values[:]
    extra = 0
    for byte in range(256):
        if byte not in values:
            values.append(byte)
            encoded.append(256 + extra)
            extra += 1
    return dict(zip(values, map(chr, encoded)))


def token_bytes(token, decoder):
    output = bytearray()
    for character in token:
        if character in decoder:
            output.append(decoder[character])
        else:
            output.extend(character.encode("utf-8"))
    return bytes(output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model_dir", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    document = json.loads((args.model_dir / "tokenizer.json").read_text("utf-8"))
    vocab = dict(document["model"]["vocab"])
    for added in document.get("added_tokens", []):
        vocab[added["content"]] = added["id"]
    vocab_size = max(vocab.values()) + 1
    pieces = [b""] * vocab_size
    decoder = {character: byte for byte, character in bytes_to_unicode().items()}
    for token, token_id in vocab.items():
        pieces[token_id] = token_bytes(token, decoder)
    eos_id = 151645
    with args.output.open("wb") as output:
        output.write(struct.pack("<8sIII", b"TOYTOK1\0", 1, vocab_size, eos_id))
        for piece in pieces:
            output.write(struct.pack("<I", len(piece)))
            output.write(piece)
    print(f"wrote {vocab_size} token pieces to {args.output}")


if __name__ == "__main__":
    main()
