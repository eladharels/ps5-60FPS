#!/usr/bin/env python3
"""Tests for tools/build_patchdb.py: value encoding and patch filtering."""

import os
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PS4 = """<?xml version="1.0" encoding="utf-8"?>
<Patch>
  <TitleID><ID>CUSA00001</ID><ID>CUSA00002</ID></TitleID>
  <Metadata Title="Game" Name="60 FPS" Note="line one\\nline two" Author="a"
            PatchVer="1.0" AppVer="01.00" AppElf="eboot.bin">
    <PatchList>
      <Line Type="byte" Address="0x00400010" Value="0xeb"/>
      <Line Type="bytes16" Address="0x00400020" Value="0x1234"/>
      <Line Type="bytes32" Address="0x00400030" Value="1280"/>
      <Line Type="bytes64" Address="0x00400040" Value="0x0102030405060708"/>
      <Line Type="bytes" Address="0x00400050" Value="90 90"/>
      <Line Type="float32" Address="0x00400060" Value="1.0"/>
      <Line Type="float64" Address="0x00400070" Value="2.0"/>
      <Line Type="utf8" Address="0x00400080" Value="hi\\x00"/>
      <Line Type="utf16" Address="0x00400090" Value="hi"/>
      <Line Type="mask" Address="de ad ? EF" Value="b0" Offset="+4"/>
    </PatchList>
  </Metadata>
  <Metadata Title="Game" Name="Debug Menu" Author="a" AppVer="01.00" AppElf="eboot.bin">
    <PatchList><Line Type="bytes" Address="0x00400000" Value="00"/></PatchList>
  </Metadata>
  <Metadata Title="Game" Name="Unlock FPS" Author="a" AppVer="01.00" AppElf="eboot.bin">
    <PatchList><Line Type="bytes" Address="0x00000010" Value="00"/></PatchList>
  </Metadata>
</Patch>
"""

PS5 = """<?xml version="1.0" encoding="utf-8"?>
<Patch>
  <TitleID><ID>PPSA00001</ID></TitleID>
  <Metadata Title="Game5" Name="60 FPS Unlock" Author="b" AppVer="01.000.000"
            ImageBase="0x35b4c000" AppElf="eboot.bin">
    <PatchList><Line Type="bytes" Address="0x35b4c0bf" Value="cc"/></PatchList>
  </Metadata>
  <Metadata Title="Game5" Name="60 FPS" Author="b" AppVer="01.001.000" AppElf="eboot.bin">
    <PatchList><Line Type="bytes" Address="0x35b4c0bf" Value="cc"/></PatchList>
  </Metadata>
</Patch>
"""


def main():
    with tempfile.TemporaryDirectory() as tmp:
        os.makedirs(os.path.join(tmp, "patches/xml"))
        os.makedirs(os.path.join(tmp, "patches/xml_prospero"))
        with open(os.path.join(tmp, "patches/xml/Game.xml"), "w") as f:
            f.write(PS4)
        with open(os.path.join(tmp, "patches/xml_prospero/Game5.xml"), "w") as f:
            f.write(PS5)
        out = os.path.join(tmp, "patches.db")
        subprocess.run([sys.executable, os.path.join(ROOT, "tools/build_patchdb.py"),
                        tmp, out], check=True, capture_output=True)
        with open(out) as f:
            got = f.read().splitlines()

    expected = [
        "# fps60 patch database v1",
        "G\tGame\t60 FPS\ta\tline one line two\t01.00\teboot.bin\t0x400000\tCUSA00001,CUSA00002\t1",
        "A\t0x400010\teb",
        "A\t0x400020\t3412",
        "A\t0x400030\t00050000",
        "A\t0x400040\t0807060504030201",
        "A\t0x400050\t9090",
        "A\t0x400060\t0000803f",
        "A\t0x400070\t0000000000000040",
        "A\t0x400080\t686900",
        "A\t0x400090\t68006900",
        "M\tde ad ?? ef\t4\tb0",
        # Debug Menu is not a frame-rate patch and is dropped.
        # An address below the image base cannot be placed safely.
        "G\tGame\tUnlock FPS\ta\t\t01.00\teboot.bin\t0x0\tCUSA00001,CUSA00002\t0",
        "G\tGame5\t60 FPS Unlock\tb\t\t01.000.000\teboot.bin\t0x35b4c000\tPPSA00001\t1",
        "A\t0x35b4c0bf\tcc",
        # PS5 patches need an explicit image base.
        "G\tGame5\t60 FPS\tb\t\t01.001.000\teboot.bin\t0x0\tPPSA00001\t0",
    ]
    if got != expected:
        print("FAIL build_patchdb output differs")
        for i, (g, e) in enumerate(zip(got + [""] * 20, expected + [""] * 20)):
            if g != e:
                print("  line %d\n    got      %r\n    expected %r" % (i + 1, g, e))
        sys.exit(1)
    print("build_patchdb tests passed")


if __name__ == "__main__":
    main()
