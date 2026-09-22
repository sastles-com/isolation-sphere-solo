#!/usr/bin/env python3
"""docs/convert.html を src/ConvertPage.h (PROGMEM 文字列) に埋め込む。

変換ページの正本は docs/convert.html 側 (単体でもブラウザで開ける)。
ファームに焼くときはこのスクリプトで生成したヘッダを使う。
docs/convert.html を編集したら必ず再実行すること:

  python3 tools/embed_convert.py
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parent.parent
SRC = ROOT / "docs" / "convert.html"
OUT = ROOT / "src" / "ConvertPage.h"
DELIM = "CONVERTPAGE"


def main() -> int:
    html = SRC.read_text(encoding="utf-8")
    if f'){DELIM}"' in html:
        sys.exit(f"raw string の終端 ){DELIM}\" が HTML 中に現れています")
    OUT.write_text(
        "/**\n"
        " * @file ConvertPage.h\n"
        " * @brief 動画変換ページ (自動生成。編集は docs/convert.html 側で行う)\n"
        " *\n"
        " * 生成: python3 tools/embed_convert.py\n"
        " * 正本: docs/convert.html\n"
        " *\n"
        " * 球体が自分でこのページを配信する理由: AP に接続した iPhone は\n"
        " * インターネットに出られないため、外部サイトの変換ページを開けない。\n"
        " * 同一オリジンなら変換 → そのまま POST /api/video まで完結する。\n"
        " */\n"
        "\n"
        "#ifndef __CONVERT_PAGE_H__\n"
        "#define __CONVERT_PAGE_H__\n"
        "\n"
        "#include <Arduino.h>\n"
        "\n"
        "namespace sastle {\n"
        "\n"
        f"const char kConvertHtml[] PROGMEM = R\"{DELIM}({html}){DELIM}\";\n"
        "\n"
        "}  // namespace sastle\n"
        "\n"
        "#endif  // __CONVERT_PAGE_H__\n",
        encoding="utf-8",
    )
    print(f"{OUT} を生成 ({len(html)} bytes の HTML)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
