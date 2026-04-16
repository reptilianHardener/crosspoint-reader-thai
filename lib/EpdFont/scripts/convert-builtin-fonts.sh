#!/bin/bash

set -e

cd "$(dirname "$0")"

NOTOSANSTHAI_FONT_SIZES=(12 14 16 18)

for size in ${NOTOSANSTHAI_FONT_SIZES[@]}; do
  for style in regular bold; do
    ttf_style=$([ "$style" = "bold" ] && echo "Bold" || echo "Regular")
    font_name="notosansthai_${size}_${style}"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size \
      ../builtinFonts/source/NotoSansThai/NotoSansThai-${ttf_style}.ttf \
      ../builtinFonts/source/NotoSans/NotoSans-${ttf_style}.ttf \
      --2bit --compress \
      --additional-intervals 0x0E00,0x0E7F \
      > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/Ubuntu/Ubuntu-${style}.ttf"
    output_path="../builtinFonts/${font_name}.h"
    python fontconvert.py $font_name $size $font_path > $output_path
    echo "Generated $output_path"
  done
done

python fontconvert.py notosans_8_regular 8 ../builtinFonts/source/NotoSans/NotoSans-Regular.ttf > ../builtinFonts/notosans_8_regular.h

echo ""
echo "Running compression verification..."
python verify_compression.py ../builtinFonts/
