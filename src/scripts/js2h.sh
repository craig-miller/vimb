#!/bin/sh
#
# Defined a constant for given JavaScript or CSS file. The file name is used
# to get the constant name and the contents are minifed and escaped as the
# value. ./js.h do_fancy_stuff.js creates somethings like
# #define JS_DO_FANCY_STUFF "Escaped JavaScriptCode"

FILE="$1"

# Check if the file exists.
if [ ! -r "$FILE" ]; then
    echo "File $FILE does not exist or is not readable" >&2
    exit 1
fi

# Put file extension and _ before file name, turn all to upper case to get the
# constant name. Convert hyphens to underscores so filenames like
# pass-detector.js yield a valid C identifier JS_PASS_DETECTOR.
CONSTANT=$(echo "$FILE" | sed -e 's:.*/::g' -e 's/.*\.css$/CSS_&/g' -e 's/.*\.js$/JS_&/g' -e 's/\.css$//' -e 's/\.js$//' -e 's/-/_/g' | tr a-z A-Z)

# minify the script
cat $FILE | \
# strip standalone // comment lines (indented or column-0)
sed -e 's|^[[:space:]]*//.*$||g' | \
# strip inline // comments — protects URL-like :// and path-like //
sed -e 's|\([^/:]\)//[^"]*$|\1|g' | \
# remove linebreaks
tr '\n\r' ' ' | \
# remove unneeded whitespace
sed -e 's:/\*[^*]*\*/::g' \
    -e 's|[ ]\{2,\}| |g' \
    -e 's|^ ||g' \
    -e "s|[ ]\{0,\}\([-*[%/!?<>:=(){};+\&\"',\|]\)[ ]\{0,\}|\1|g" \
    -e 's|"+"||g' | \
# ecaspe
sed -e 's|\\x20| |g' \
    -e 's|\\|\\\\|g' \
    -e 's|"|\\"|g' | \
# write opener with the starting and ending quote char
sed -e "1s/^/#define $CONSTANT \"/" \
    -e '$s/$/"/'
echo ""
