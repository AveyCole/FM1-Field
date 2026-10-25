# SPDX-License-Identifier: MPL-2.0
"""The flash saving must preserve the visible pixels of FIELD's text faces."""
import importlib.util
from pathlib import Path
spec = importlib.util.spec_from_file_location('font', Path(__file__).resolve().parents[1]/'base/tools/gen_font.py')
font = importlib.util.module_from_spec(spec)
spec.loader.exec_module(font)
for name, path, size, scale, pixel, first, last in font.SIZES:
    if name not in {'XS', 'S', 'B'}:
        continue
    font.PAD = 2
    height, original = font.render(path, size, scale, pixel, first, last)
    font.PAD = 1
    _, compact = font.render(path, size, scale, pixel, first, last)
    for before, after in zip(original, compact):
        advance, width, data = before
        new_advance, new_width, new_data = after
        rows=font.ink_rows(new_data,new_width,height)
        assert new_data[:rows*new_width]+[0]*((height-rows)*new_width)==new_data
        assert advance == new_advance
        for y in range(height):
            assert data[y*width] == data[y*width+width-1] == 0
            assert data[y*width+1:y*width+width-1] == new_data[y*new_width:(y+1)*new_width]
    print(f'{name}: every printable glyph preserves its visible pixels and advance')
