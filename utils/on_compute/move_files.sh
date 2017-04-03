#! /bin/bash

# Run this script as a cronjob every night @8 am (for night)
# to move files from buffer to a separate folder

YEST_DATE=$(date '+%Y-%m-%d' -d "yesterday")
TODAY_DATE=$(date '+%Y-%m-%d')
START_TIME="21:00"
END_TIME="06:00"

SOURCE=${AUDUBON_DATA}"audubon_imgs_live/"
DESTINATION=${AUDUBON_DATA}$YEST_DATE"_night"

echo "Finding *.raw and *.hdr for $DESTINATION"

mkdir -p $DESTINATION
find $SOURCE -type f \( -name "*.raw" -o -name "*.hdr" \) -newermt "$YEST_DATE $START_TIME" ! -newermt "$TODAY_DATE $END_TIME" -printf %P\\0 | rsync -av -P --remove-source-files --files-from=- --from0 $SOURCE $\
DESTINATION | python rsyncprogress.py

echo "Done" 
