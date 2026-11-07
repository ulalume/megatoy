# Replaces DESTINATION with a copy of the preset library at SOURCE, leaving out
# its .git folder.
# Usage: cmake -DSOURCE=<dir> -DDESTINATION=<dir> -P copy_presets.cmake
file(REMOVE_RECURSE "${DESTINATION}")
file(COPY "${SOURCE}/" DESTINATION "${DESTINATION}" PATTERN ".git" EXCLUDE)
