#!/bin/bash

set -e

cd "$(dirname "$0")"

PY=python3
if [ -x "./.venv/bin/python3" ]; then
  PY="./.venv/bin/python3"
fi

READER_FONT_STYLES=("Regular" "Italic" "Bold" "BoldItalic")
NOTOSANS_FONT_SIZES=(12 14 16 18)
OPENDYSLEXIC_FONT_SIZES=(8 10 12 14)

NOTO_LATIN_DIR="../builtinFonts/source/NotoSans"
NOTO_THAI_DIR="../builtinFonts/source/NotoSansThai"
NOTO_SYMBOLS2="../builtinFonts/source/NotoSansSymbols2/NotoSansSymbols2-Regular.ttf"

NOTO_SERIF_LATIN_DIR="../builtinFonts/source/NotoSerif"
NOTO_SERIF_THAI_DIR="../builtinFonts/source/NotoSerifThai"
NOTOSERIFTHAI_FONT_SIZES=(12 14 16 18)

notosans_stack() {
  local style="$1"
  case "$style" in
    Regular)
      echo "${NOTO_LATIN_DIR}/NotoSans-Regular.ttf" "${NOTO_THAI_DIR}/NotoSansThai-Regular.ttf" "${NOTO_SYMBOLS2}"
      ;;
    Italic)
      echo "${NOTO_LATIN_DIR}/NotoSans-Italic.ttf" "${NOTO_THAI_DIR}/NotoSansThai-Regular.ttf" "${NOTO_SYMBOLS2}"
      ;;
    Bold)
      echo "${NOTO_LATIN_DIR}/NotoSans-Bold.ttf" "${NOTO_THAI_DIR}/NotoSansThai-Bold.ttf" "${NOTO_SYMBOLS2}"
      ;;
    BoldItalic)
      echo "${NOTO_LATIN_DIR}/NotoSans-BoldItalic.ttf" "${NOTO_THAI_DIR}/NotoSansThai-Bold.ttf" "${NOTO_SYMBOLS2}"
      ;;
  esac
}

notoserifthai_stack() {
  local style="$1"
  case "$style" in
    Regular)
      echo "${NOTO_SERIF_LATIN_DIR}/NotoSerif-Regular.ttf" "${NOTO_SERIF_THAI_DIR}/NotoSerifThai-Regular.ttf" "${NOTO_SYMBOLS2}"
      ;;
    Italic)
      echo "${NOTO_SERIF_LATIN_DIR}/NotoSerif-Italic.ttf" "${NOTO_SERIF_THAI_DIR}/NotoSerifThai-Regular.ttf" "${NOTO_SYMBOLS2}"
      ;;
    Bold)
      echo "${NOTO_SERIF_LATIN_DIR}/NotoSerif-Bold.ttf" "${NOTO_SERIF_THAI_DIR}/NotoSerifThai-Bold.ttf" "${NOTO_SYMBOLS2}"
      ;;
    BoldItalic)
      echo "${NOTO_SERIF_LATIN_DIR}/NotoSerif-BoldItalic.ttf" "${NOTO_SERIF_THAI_DIR}/NotoSerifThai-Bold.ttf" "${NOTO_SYMBOLS2}"
      ;;
  esac
}

for size in ${NOTOSANS_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notosans_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    output_path="../builtinFonts/${font_name}.h"
    read -r -a STACK <<< "$(notosans_stack "$style")"
    $PY fontconvert.py $font_name $size "${STACK[@]}" --2bit --compress > $output_path
    echo "Generated $output_path"
  done
done

for size in ${NOTOSERIFTHAI_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="notoserifthai_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    output_path="../builtinFonts/${font_name}.h"
    read -r -a STACK <<< "$(notoserifthai_stack "$style")"
    $PY fontconvert.py $font_name $size "${STACK[@]}" --2bit --compress > $output_path
    echo "Generated $output_path"
  done
done

for size in ${OPENDYSLEXIC_FONT_SIZES[@]}; do
  for style in ${READER_FONT_STYLES[@]}; do
    font_name="opendyslexic_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    font_path="../builtinFonts/source/OpenDyslexic/OpenDyslexic-${style}.otf"
    output_path="../builtinFonts/${font_name}.h"
    $PY fontconvert.py $font_name $size $font_path --2bit --compress > $output_path
    echo "Generated $output_path"
  done
done

UI_FONT_SIZES=(10 12)
UI_FONT_STYLES=("Regular" "Bold")

for size in ${UI_FONT_SIZES[@]}; do
  for style in ${UI_FONT_STYLES[@]}; do
    font_name="ubuntu_${size}_$(echo $style | tr '[:upper:]' '[:lower:]')"
    output_path="../builtinFonts/${font_name}.h"
    if [ "$style" = "Regular" ]; then
      $PY fontconvert.py $font_name $size \
        "../builtinFonts/source/Ubuntu/Ubuntu-Regular.ttf" \
        "${NOTO_THAI_DIR}/NotoSansThai-Regular.ttf" \
        "${NOTO_SYMBOLS2}" > $output_path
    else
      $PY fontconvert.py $font_name $size \
        "../builtinFonts/source/Ubuntu/Ubuntu-Bold.ttf" \
        "${NOTO_THAI_DIR}/NotoSansThai-Bold.ttf" \
        "${NOTO_SYMBOLS2}" > $output_path
    fi
    echo "Generated $output_path"
  done
done

$PY fontconvert.py notosans_8_regular 8 \
  "${NOTO_LATIN_DIR}/NotoSans-Regular.ttf" \
  "${NOTO_THAI_DIR}/NotoSansThai-Regular.ttf" \
  "${NOTO_SYMBOLS2}" > ../builtinFonts/notosans_8_regular.h

echo ""
echo "Running compression verification..."
$PY verify_compression.py ../builtinFonts/
