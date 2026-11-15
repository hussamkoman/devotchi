#!/bin/sh
# Renders every mockup to renders/*.png at 2x (640x480) with headless Firefox,
# then the pet sheet and the device-context picture. Uses throwaway profiles under /tmp.
cd "$(dirname "$0")/.." || exit 1
mkdir -p renders
rm -f renders/*.png
shot() { # profile width height out url
  mkdir -p "$1"
  firefox --headless --no-remote --profile "$1" --window-size="$2,$3" --screenshot "$PWD/$4" "$5" >/dev/null 2>&1
}
i=0
for f in mockups/*.html; do
  n=$(basename "$f" .html)
  shot "/tmp/devotchi-design-ff-$((i % 6))" 640 480 "renders/$n.png" "file://$PWD/$f#2x" &
  i=$((i + 1))
  [ $((i % 6)) -eq 0 ] && wait
done
wait
shot /tmp/devotchi-design-ff-0 1200 4600 renders/pet-moods.png "file://$PWD/pet-moods.html"
magick renders/pet-moods.png -trim +repage -bordercolor "#0C0E14" -border 24 renders/pet-moods.png
shot /tmp/devotchi-design-ff-0 551 551 renders/device-context.png "file://$PWD/device-context.html"
